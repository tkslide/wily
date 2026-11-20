#!/usr/bin/env bash
set -euo pipefail


# source "{BASH_LOADABLES_PATH}/wily.bash"
source "/usr/local/lib/bash/wily.bash"

wily_connect
trap wily_close EXIT

wily_list_windows
echo "Existing windows:"
for id in "${!WILY_WINDOWS[@]}"; do
    printf '  [%s] %s\n' "$id" "${WILY_WINDOWS[$id]}"
done

wily_new "+BashLib" 0
wid=$REPLY

wily_settools "$wid" " Hello Quit "
wily_replace "$wid" 0 0 $'Middle-click (B2) \'Hello\' or \'Quit\' in the tag.\n'
wily_attach "$wid" $(( WEexec | WEgoto | WEdestroy ))

while wily_next_event; do
    case "${WILY_EVENT[t]}" in
        "$WEdestroy")
            echo "Window closed, exiting."
            break
            ;;
        "$WEexec")
            cmd="${WILY_EVENT[s]}"
            cmd="${cmd#"${cmd%%[![:space:]]*}"}"
            cmd="${cmd%"${cmd##*[![:space:]]}"}"
            case "$cmd" in
                Hello) wily_replace "$wid" 0 0 $'Clicked Hello!\n' ;;
                Quit)  wily_exec_cmd "$wid" "Del"; break ;;
                *)     wily_bounce ;;
            esac
            ;;
        "$WEgoto")
            wily_bounce
            ;;
    esac
done

