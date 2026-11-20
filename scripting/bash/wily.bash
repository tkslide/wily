# wily.bash — Bash client library for Wily's libmsg / RPC protocol
# Usage: source /path/to/wily.bash

[[ -n "${_WILY_BASH_LOADED:-}" ]] && return 0
readonly _WILY_BASH_LOADED=1

_WILY_LIB_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE}")" && pwd)"
_WILY_SO="${WILY_BASH_SO:-$_WILY_LIB_DIR/wily_bash.so}"

if [[ ! -f "$_WILY_SO" ]]; then
    echo "wily.bash: shared library not found at $_WILY_SO (run 'make' first)" >&2
    return 1
fi

enable -f "$_WILY_SO" wily_dial wily_write_pkt wily_read_pkt wily_poll wily_hangup

# --- Message Types ---
readonly WEexec=1
readonly WEgoto=2
readonly WEdestroy=4
readonly WEreplace=8
readonly WEfencepost=9

readonly WRerror=10
readonly WMlist=11      WRlist=12
readonly WMnew=13       WRnew=14
readonly WMattach=15    WRattach=16
readonly WMsetname=17   WRsetname=18
readonly WMgetname=19   WRgetname=20
readonly WMsettools=21  WRsettools=22
readonly WMgettools=23  WRgettools=24
readonly WMread=25      WRread=26
readonly WMreplace=27   WRreplace=28
readonly WMexec=29      WRexec=30
readonly WMgoto=31      WRgoto=32
readonly WMfencepost=33

# --- Client State ---
WILY_FD=-1
WILY_NEXT_ID=1
WILY_EV_HEAD=0
WILY_EV_TAIL=0
declare -gA WILY_EV_Q=()
declare -gA WILY_REPLY=()
declare -gA WILY_EVENT=()
declare -gA WILY_WINDOWS=()

# --- Environment & Connection ---

wily_get_fifo() {
    if [[ -n "${WILYFIFO:-}" ]]; then
        REPLY="$WILYFIFO"
        return 0
    fi
    local user="${USER:-$(id -un)}"
    if [[ -z "${DISPLAY:-}" ]]; then
        echo "wily.bash: \$DISPLAY not set and \$WILYFIFO not set" >&2
        return 1
    fi
    local tmpdir="${TMPDIR:-/tmp}"
    REPLY="${tmpdir}/wily${user}${DISPLAY}"
}

wily_connect() {
    local fifo="${1:-}"
    if [[ -z "$fifo" ]]; then
        wily_get_fifo || return 1
        fifo="$REPLY"
    fi
    wily_dial "$fifo" || {
        echo "wily.bash: failed to connect to Wily FIFO: $fifo" >&2
        return 1
    }
    WILY_NEXT_ID=1
    WILY_EV_HEAD=0
    WILY_EV_TAIL=0
    WILY_EV_Q=()
}

wily_close() {
    if (( WILY_FD >= 0 )); then
        wily_hangup "$WILY_FD"
        WILY_FD=-1
    fi
}

# --- Internal RPC & Framing ---

_wily_alloc_id() {
    REPLY=$WILY_NEXT_ID
    WILY_NEXT_ID=$(( (WILY_NEXT_ID % 65535) + 1 ))
}

_wily_recv_raw() {
    local rc=0
    wily_read_pkt "$WILY_FD" || rc=$?
    if (( rc == 1 )); then
        echo "wily.bash: Wily closed the IPC connection" >&2
        return 1
    elif (( rc != 0 )); then
        echo "wily.bash: corrupt packet or socket read error" >&2
        return 1
    fi
}

wily_rpc() {
    local req_type="$1" expected_reply="$2" win_id="${3:-0}"
    local p0="${4:-0}" p1="${5:-0}" flag="${6:-0}" text="${7:-}"

    if (( WILY_FD < 0 )); then
        echo "wily.bash: not connected to Wily" >&2
        return 1
    fi

    _wily_alloc_id
    local mid=$REPLY

    wily_write_pkt "$WILY_FD" "$req_type" "$mid" "$win_id" "$p0" "$p1" "$flag" "$text" || {
        echo "wily.bash: socket write error" >&2
        return 1
    }

    while true; do
        _wily_recv_raw || return 1
        if (( WILY_PKT_T < WEfencepost )); then
            local idx=$WILY_EV_TAIL
            (( WILY_EV_TAIL++ ))
            WILY_EV_Q["$idx,t"]="$WILY_PKT_T"
            WILY_EV_Q["$idx,m"]="$WILY_PKT_M"
            WILY_EV_Q["$idx,w"]="$WILY_PKT_W"
            WILY_EV_Q["$idx,p0"]="$WILY_PKT_P0"
            WILY_EV_Q["$idx,p1"]="$WILY_PKT_P1"
            WILY_EV_Q["$idx,flag"]="$WILY_PKT_FLAG"
            WILY_EV_Q["$idx,s"]="$WILY_PKT_S"
            continue
        fi
        if (( WILY_PKT_M != mid )); then
            echo "wily.bash: unexpected msg id $WILY_PKT_M (expected $mid)" >&2
            return 1
        fi
        if (( WILY_PKT_T == WRerror )); then
            echo "WilyError: $WILY_PKT_S" >&2
            return 1
        fi
        if (( WILY_PKT_T != expected_reply )); then
            echo "wily.bash: unexpected reply $WILY_PKT_T (expected $expected_reply)" >&2
            return 1
        fi
        WILY_REPLY=(
            [t]="$WILY_PKT_T"
            [m]="$WILY_PKT_M"
            [w]="$WILY_PKT_W"
            [p0]="$WILY_PKT_P0"
            [p1]="$WILY_PKT_P1"
            [flag]="$WILY_PKT_FLAG"
            [s]="$WILY_PKT_S"
        )
        return 0
    done
}

# --- High-Level RPC API ---

# Populates associative array WILY_WINDOWS[id]=name
wily_list_windows() {
    wily_rpc "$WMlist" "$WRlist" || return 1
    WILY_WINDOWS=()
    local line id name
    while IFS= read -r line; do
        [[ -z "${line// }" ]] && continue
        id="${line##*[[:space:]]}"
        name="${line%[[:space:]]*}"
        WILY_WINDOWS["$id"]="$name"
    done <<< "${WILY_REPLY[s]}"
}

# Sets REPLY=<new_win_id>
wily_new() {
    local name="$1" backup="${2:-0}"
    wily_rpc "$WMnew" "$WRnew" 0 0 0 "$backup" "$name" || return 1
    REPLY="${WILY_REPLY[w]}"
}

wily_attach()   { wily_rpc "$WMattach"   "$WRattach"   "$1" 0    0    "$2" ""; }
wily_setname()  { wily_rpc "$WMsetname"  "$WRsetname"  "$1" 0    0    0    "$2"; }
wily_settools() { wily_rpc "$WMsettools" "$WRsettools" "$1" 0    0    0    "$2"; }
wily_replace()  { wily_rpc "$WMreplace"  "$WRreplace"  "$1" "$2" "$3" 0    "$4"; }
wily_exec_cmd() { wily_rpc "$WMexec"     "$WRexec"     "$1" 0    0    0    "$2"; }

# Sets REPLY=<name>
wily_getname() {
    wily_rpc "$WMgetname" "$WRgetname" "$1" || return 1
    REPLY="${WILY_REPLY[s]}"
}

# Sets REPLY=<tools>
wily_gettools() {
    wily_rpc "$WMgettools" "$WRgettools" "$1" || return 1
    REPLY="${WILY_REPLY[s]}"
}

# Sets REPLY=<text>
wily_read() {
    wily_rpc "$WMread" "$WRread" "$1" "$2" "$3" || return 1
    REPLY="${WILY_REPLY[s]}"
}

# Populates WILY_REPLY[w], WILY_REPLY[p0], WILY_REPLY[p1]
wily_goto() {
    local win_id="$1" target="$2" set_dot="${3:-1}"
    wily_rpc "$WMgoto" "$WRgoto" "$win_id" 0 0 "$set_dot" "$target"
}

# --- Event Queue & Bouncing ---

# Returns 0 if next wily_next_event will block, 1 if an event/packet is ready
wily_would_block() {
    (( WILY_EV_HEAD < WILY_EV_TAIL )) && return 1
    (( WILY_FD < 0 )) && return 0
    if wily_poll "$WILY_FD" 0; then
        return 1
    fi
    return 0
}

# Populates associative array WILY_EVENT
wily_next_event() {
    WILY_EVENT=()
    if (( WILY_EV_HEAD < WILY_EV_TAIL )); then
        local idx=$WILY_EV_HEAD
        (( WILY_EV_HEAD++ ))
        local k
        for k in t m w p0 p1 flag s; do
            WILY_EVENT["$k"]="${WILY_EV_Q[$idx,$k]}"
            unset "WILY_EV_Q[$idx,$k]"
        done
        return 0
    fi
    while true; do
        _wily_recv_raw || return 1
        if (( WILY_PKT_T < WEfencepost )); then
            WILY_EVENT=(
                [t]="$WILY_PKT_T"
                [m]="$WILY_PKT_M"
                [w]="$WILY_PKT_W"
                [p0]="$WILY_PKT_P0"
                [p1]="$WILY_PKT_P1"
                [flag]="$WILY_PKT_FLAG"
                [s]="$WILY_PKT_S"
            )
            return 0
        fi
    done
}

wily_bounce() {
    if (( WILY_EVENT[t] == WEexec )); then
        wily_rpc "$WMexec" "$WRexec" "${WILY_EVENT[w]}" "${WILY_EVENT[p0]}" \
                 "${WILY_EVENT[p1]}" "${WILY_EVENT[flag]}" "${WILY_EVENT[s]}"
    elif (( WILY_EVENT[t] == WEgoto )); then
        wily_rpc "$WMgoto" "$WRgoto" "${WILY_EVENT[w]}" "${WILY_EVENT[p0]}" \
                 "${WILY_EVENT[p1]}" "${WILY_EVENT[flag]}" "${WILY_EVENT[s]}"
    fi
}

