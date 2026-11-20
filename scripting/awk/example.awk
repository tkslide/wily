#!/usr/bin/gawk -f

@include "wily"

BEGIN {
    wily_connect()

    n_win = wily_list_windows(wins)
    printf "Existing windows:\n"
    for (i = 1; i <= n_win; i++) {
        printf "  [%d] %s\n", wins[i, "id"], wins[i, "name"]
    }

    wid = wily_new("+GawkLib", 0)
    wily_settools(wid, " Hello Quit ")
    wily_replace(wid, 0, 0, "Middle-click (B2) 'Hello' or 'Quit' in the tag.\n")
    wily_attach(wid, or(WEexec, or(WEgoto, WEdestroy)))

    while (1) {
        wily_next_event(ev)
        if (ev["t"] == WEdestroy) {
            print "Window closed, exiting."
            break
        } else if (ev["t"] == WEexec) {
            cmd = ev["s"]
            gsub(/^[ \t\r\n]+|[ \t\r\n]+$/, "", cmd)
            if (cmd == "Hello") {
                wily_replace(wid, 0, 0, "Clicked Hello!\n")
            } else if (cmd == "Quit") {
                wily_exec_cmd(wid, "Del")
                break
            } else {
                wily_bounce(ev)
            }
        } else if (ev["t"] == WEgoto) {
            wily_bounce(ev)
        }
    }

    wily_close()
    exit 0
}
