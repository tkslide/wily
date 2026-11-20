# Wily RPC Clients (`libmsg` Protocol)

Client libraries for communicating with the [Wily](https://yorku.ca) text editor over its Unix domain socket IPC protocol (`libmsg` / `rpc`).

Available in **five languages**, all sharing identical semantics, event queuing, and built-in command bouncing:

| Language | Library File(s) | Native Extension | External Dependencies |
| :--- | :--- | :--- | :--- |
| **Python 3** | `wily.py` | None (pure Python) | Standard library (`socket`, `struct`, `select`) |
| **Racket** | `wily.rkt` | None (pure Racket) | `racket/unix-socket`, `ffi/unsafe` |
| **Lua (5.3+)** | `wily.lua` | `wily_unix.c` (`wily_unix.so`) | `liblua` headers (or headerless ABI) |
| **Bash (4+)** | `wily.bash` | `wily_bash.c` (`wily_bash.so`) | None (headerless Bash loadable builtin) |
| **GNU Awk (5.3+)** | `wily.awk` | `wily_sock.c` (`wily_sock.so`) | `gawkapi.h` (`gawk` C extension API) |

---

## Connection Discovery

Every client locates Wily's named pipe (FIFO) in the following order:

1. Explicit `fifo_path` passed to `connect(...)`
2. `$WILYFIFO` environment variable
3. `${TMPDIR:-/tmp}/wily${USER}${DISPLAY}`

During `connect()`, the client creates a temporary Unix domain socket in `/tmp/wily.XXXXXX`, writes that socket's path to Wily's FIFO, and accepts Wily's incoming stream connection.

---

## Building & Installing (Lua, Bash, GNU Awk)

Python and Racket require no compilation. For Lua, Bash, and GNU Awk, use the top-level `Makefile`:

```sh
# Build all three C extensions (wily_unix.so, wily_bash.so, wily_sock.so)
make

# Or build a single language target
make lua
make bash
make awk

# Install libraries and shared objects system-wide (default PREFIX=/usr/local)
sudo make install

# Clean compiled .so artifacts
make clean
```

If your files are organized into subdirectories (`lua/`, `bash/`, `awk/`), pass their paths to `make`:

```sh
make LUA_DIR=lua BASH_DIR=bash AWK_DIR=awk
```

---

## Wire Protocol Overview

All packets begin with a **22-byte big-endian header** (`>HHIHHIIH`) followed by a NUL-terminated UTF-8 payload string:

| Offset | Size | Field | Description |
| :--- | :--- | :--- | :--- |
| `0` | 2 (`uint16`) | `cookie` | Magic constant `0xFEED` |
| `2` | 2 (`uint16`) | `t` | Message type (`WE*` event or `WM*`/`WR*` RPC) |
| `4` | 4 (`uint32`) | `len` | Total packet length in bytes (`22 + strlen(s) + 1`) |
| `8` | 2 (`uint16`) | `m` | Message ID (`0` for async events, `1..65535` for RPCs) |
| `10` | 2 (`uint16`) | `w` | Window ID |
| `12` | 4 (`uint32`) | `p0` | Start rune offset |
| `16` | 4 (`uint32`) | `p1` | End rune offset |
| `20` | 2 (`uint16`) | `flag` | Bitmask (`WE*` events) or boolean (`backup`, `set_dot`) |
| `22` | `len - 22` | `s` | UTF-8 string + trailing `\0` |

* **Interleaved Events:** If Wily sends an asynchronous event (`t < WEfencepost`) while a client is waiting for an RPC reply (`t >= WEfencepost`), the client automatically queues the event in FIFO order so `next_event()` delivers it without losing packets.
* **Event Bouncing:** Unhandled `WEexec` (B2) or `WEgoto` (B3) events can be returned to Wily via `bounce(event)` so built-in commands (`Cut`, `Paste`, `Snarf`, `Del`, file searches) behave normally.

---

## Side-by-Side API Reference

### Constants (`Mtype` / Event Bitmasks)

| Constant | Value | Meaning |
| :--- | :--- | :--- |
| `WEexec` | `1` | B2 (middle-click) command execution event / mask |
| `WEgoto` | `2` | B3 (right-click) search/open event / mask |
| `WEdestroy` | `4` | Window closed event / mask |
| `WEreplace` | `8` | Text modified event / mask |

### Operations

| Operation | Python | Racket | Lua | Bash | GNU Awk |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Connect** | `with WilyClient() as w:` | `(call-with-wily (λ (w) ...))` | `local w <close> = wily.connect()` | `wily_connect` | `wily_connect()` |
| **List windows** | `w.list_windows()` | `(wily-list-windows w)` | `w:list_windows()` | `wily_list_windows` (`WILY_WINDOWS`) | `wily_list_windows(wins)` |
| **Open/create window** | `wid = w.new(name, backup)` | `(wily-new w name #:backup #f)` | `wid = w:new(name, false)` | `wily_new name 0` (`$REPLY`) | `wid = wily_new(name, 0)` |
| **Subscribe events** | `w.attach(wid, mask)` | `(wily-attach w wid mask)` | `w:attach(wid, mask)` | `wily_attach "$wid" "$mask"` | `wily_attach(wid, mask)` |
| **Set/get tag tools** | `w.settools(wid, s)` / `w.gettools(wid)` | `(wily-settools w wid s)` / `(wily-gettools w wid)` | `w:settools(wid, s)` / `w:gettools(wid)` | `wily_settools` / `wily_gettools` | `wily_settools(wid, s)` / `wily_gettools(wid)` |
| **Set/get window name** | `w.setname(wid, s)` / `w.getname(wid)` | `(wily-setname w wid s)` / `(wily-getname w wid)` | `w:setname(wid, s)` / `w:getname(wid)` | `wily_setname` / `wily_getname` | `wily_setname(wid, s)` / `wily_getname(wid)` |
| **Read rune range** | `w.read(wid, p0, p1)` | `(wily-read w wid p0 p1)` | `w:read(wid, p0, p1)` | `wily_read "$wid" "$p0" "$p1"` | `wily_read(wid, p0, p1)` |
| **Replace rune range** | `w.replace(wid, p0, p1, s)` | `(wily-replace w wid p0 p1 s)` | `w:replace(wid, p0, p1, s)` | `wily_replace "$wid" "$p0" "$p1" "$s"` | `wily_replace(wid, p0, p1, s)` |
| **Execute B2 command** | `w.exec_cmd(wid, cmd)` | `(wily-exec-cmd w wid cmd)` | `w:exec_cmd(wid, cmd)` | `wily_exec_cmd "$wid" "$cmd"` | `wily_exec_cmd(wid, cmd)` |
| **Search / B3 jump** | `w.goto(wid, target, True)` | `(wily-goto w wid target)` | `w:go_to(wid, target, true)` | `wily_goto "$wid" "$target" 1` | `wily_goto(wid, target, 1, res)` |
| **Non-blocking check** | `w.would_block()` | `(wily-would-block? w)` | `w:would_block()` | `wily_would_block` (exit `0`/`1`) | `wily_would_block()` |
| **Next event** | `ev = w.next_event()` | `(wily-next-event w)` | `ev = w:next_event()` | `wily_next_event` (`WILY_EVENT`) | `wily_next_event(ev)` |
| **Bounce event** | `w.bounce(ev)` | `(wily-bounce w ev)` | `w:bounce(ev)` | `wily_bounce` | `wily_bounce(ev)` |
| **Close connection** | `w.close()` | `(wily-close! w)` | `w:close()` | `wily_close` | `wily_close()` |

---

## Quick Start Examples

### Python
```sh
python3 wily.py
```

### Racket
```sh
racket wily.rkt
```

### Lua
```sh
make lua
lua example.lua
```

### Bash
```sh
make bash
./example.sh
```

### GNU Awk
```sh
make awk
AWKPATH=. AWKLIBPATH=. gawk -f example.awk
```

