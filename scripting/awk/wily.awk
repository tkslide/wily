# wily.awk — GNU Awk client library for Wily's libmsg / RPC protocol
# Usage: @include "wily"

@load "wily_sock"

BEGIN {
    # --- Message Types ---
    WEexec      = 1
    WEgoto      = 2
    WEdestroy   = 4
    WEreplace   = 8
    WEfencepost = 9

    WRerror     = 10
    WMlist      = 11; WRlist     = 12
    WMnew       = 13; WRnew      = 14
    WMattach    = 15; WRattach   = 16
    WMsetname   = 17; WRsetname  = 18
    WMgetname   = 19; WRgetname  = 20
    WMsettools  = 21; WRsettools = 22
    WMgettools  = 23; WRgettools = 24
    WMread      = 25; WRread     = 26
    WMreplace   = 27; WRreplace  = 28
    WMexec      = 29; WRexec     = 30
    WMgoto      = 31; WRgoto     = 32
    WMfencepost = 33

    WILY_FD      = -1
    WILY_NEXT_ID = 1
    WILY_EV_HEAD = 1
    WILY_EV_TAIL = 0
}

# --- Environment & Connection ---

function wily_get_fifo(    user, disp, tmpdir, cmd) {
    if ("WILYFIFO" in ENVIRON && ENVIRON["WILYFIFO"] != "") {
        return ENVIRON["WILYFIFO"]
    }
    user = ENVIRON["USER"]
    if (user == "") {
        cmd = "id -un"
        cmd | getline user
        close(cmd)
    }
    disp = ENVIRON["DISPLAY"]
    if (disp == "") {
        print "wily.awk: $DISPLAY not set and $WILYFIFO not set" > "/dev/stderr"
        exit 1
    }
    tmpdir = ("TMPDIR" in ENVIRON && ENVIRON["TMPDIR"] != "") ? ENVIRON["TMPDIR"] : "/tmp"
    return tmpdir "/wily" user disp
}

function wily_connect(fifo_path) {
    if (fifo_path == "") fifo_path = wily_get_fifo()
    WILY_FD = wily_dial(fifo_path)
    if (WILY_FD < 0) {
        print "wily.awk: failed to connect to Wily FIFO: " fifo_path > "/dev/stderr"
        exit 1
    }
    WILY_NEXT_ID = 1
    WILY_EV_HEAD = 1
    WILY_EV_TAIL = 0
    delete WILY_EV_Q
}

function wily_close() {
    if (WILY_FD >= 0) {
        wily_hangup(WILY_FD)
        WILY_FD = -1
    }
}

# --- Internal RPC & Framing ---

function _wily_alloc_id(    mid) {
    mid = WILY_NEXT_ID
    WILY_NEXT_ID = (WILY_NEXT_ID % 0xFFFF) + 1
    return mid
}

function _wily_recv_raw(msg,    rc) {
    rc = wily_read_pkt(WILY_FD, msg)
    if (rc == 0) {
        print "wily.awk: Wily closed the IPC connection" > "/dev/stderr"
        exit 1
    } else if (rc == -2) {
        print "wily.awk: corrupt Wily packet" > "/dev/stderr"
        exit 1
    } else if (rc < 0) {
        print "wily.awk: socket read error" > "/dev/stderr"
        exit 1
    }
}

function wily_rpc(req_type, expected_reply, win_id, p0, p1, flag, text, reply,    mid, msg, idx, k) {
    if (WILY_FD < 0) {
        print "wily.awk: not connected to Wily" > "/dev/stderr"
        exit 1
    }
    mid = _wily_alloc_id()
    if (wily_write_pkt(WILY_FD, req_type, mid, win_id, p0, p1, flag, text) < 0) {
        print "wily.awk: socket write error" > "/dev/stderr"
        exit 1
    }

    while (1) {
        _wily_recv_raw(msg)
        if (msg["t"] < WEfencepost) {
            idx = ++WILY_EV_TAIL
            for (k in msg) WILY_EV_Q[idx, k] = msg[k]
            continue
        }
        if (msg["m"] != mid) {
            printf "wily.awk: unexpected msg id %d (expected %d)\n", msg["m"], mid > "/dev/stderr"
            exit 1
        }
        if (msg["t"] == WRerror) {
            printf "WilyError: %s\n", msg["s"] > "/dev/stderr"
            exit 1
        }
        if (msg["t"] != expected_reply) {
            printf "wily.awk: unexpected reply %d (expected %d)\n", msg["t"], expected_reply > "/dev/stderr"
            exit 1
        }
        delete reply
        for (k in msg) reply[k] = msg[k]
        return
    }
}

# --- Event Queue & Bouncing ---

function wily_would_block() {
    if (WILY_EV_HEAD <= WILY_EV_TAIL) return 0
    if (WILY_FD < 0) return 1
    return (wily_poll(WILY_FD, 0) > 0) ? 0 : 1
}

function wily_next_event(ev,    idx, fields, i, f) {
    delete ev
    if (WILY_EV_HEAD <= WILY_EV_TAIL) {
        idx = WILY_EV_HEAD++
        split("t m w p0 p1 flag s", fields, " ")
        for (i in fields) {
            f = fields[i]
            ev[f] = WILY_EV_Q[idx, f]
            delete WILY_EV_Q[idx, f]
        }
        return
    }
    while (1) {
        _wily_recv_raw(ev)
        if (ev["t"] < WEfencepost) return
    }
}

function wily_bounce(ev,    rep) {
    if (ev["t"] == WEexec) {
        wily_rpc(WMexec, WRexec, ev["w"], ev["p0"], ev["p1"], ev["flag"], ev["s"], rep)
    } else if (ev["t"] == WEgoto) {
        wily_rpc(WMgoto, WRgoto, ev["w"], ev["p0"], ev["p1"], ev["flag"], ev["s"], rep)
    }
}

# --- High-Level RPC Methods ---

function wily_list_windows(windows,    rep, lines, n, i, count, parts) {
    delete windows
    wily_rpc(WMlist, WRlist, 0, 0, 0, 0, "", rep)
    n = split(rep["s"], lines, "\n")
    count = 0
    for (i = 1; i <= n; i++) {
        if (lines[i] ~ /^[ \t\r]*$/) continue
        count++
        split(lines[i], parts, /[ \t]+/)
        windows[count, "id"]   = parts[2] + 0
        windows[count, "name"] = parts[1]
    }
    return count
}

function wily_new(name, backup,    rep) {
    wily_rpc(WMnew, WRnew, 0, 0, 0, backup ? 1 : 0, name, rep)
    return rep["w"]
}

function wily_attach(win_id, mask,    rep) {
    wily_rpc(WMattach, WRattach, win_id, 0, 0, mask, "", rep)
}

function wily_setname(win_id, name,    rep) {
    wily_rpc(WMsetname, WRsetname, win_id, 0, 0, 0, name, rep)
}

function wily_getname(win_id,    rep) {
    wily_rpc(WMgetname, WRgetname, win_id, 0, 0, 0, "", rep)
    return rep["s"]
}

function wily_settools(win_id, tools,    rep) {
    wily_rpc(WMsettools, WRsettools, win_id, 0, 0, 0, tools, rep)
}

function wily_gettools(win_id,    rep) {
    wily_rpc(WMgettools, WRgettools, win_id, 0, 0, 0, "", rep)
    return rep["s"]
}

function wily_read(win_id, p0, p1,    rep) {
    wily_rpc(WMread, WRread, win_id, p0, p1, 0, "", rep)
    return rep["s"]
}

function wily_replace(win_id, p0, p1, text,    rep) {
    wily_rpc(WMreplace, WRreplace, win_id, p0, p1, 0, text, rep)
}

function wily_exec_cmd(win_id, cmd,    rep) {
    wily_rpc(WMexec, WRexec, win_id, 0, 0, 0, cmd, rep)
}

function wily_goto(win_id, target, set_dot, res,    rep) {
    if (set_dot == "") set_dot = 1
    wily_rpc(WMgoto, WRgoto, win_id, 0, 0, set_dot ? 1 : 0, target, rep)
    res["w"]  = rep["w"]
    res["p0"] = rep["p0"]
    res["p1"] = rep["p1"]
}
