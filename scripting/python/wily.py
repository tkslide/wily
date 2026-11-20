from collections import deque
from dataclasses import dataclass
from enum import IntEnum
import os
import pwd
import select
import socket
import struct
import tempfile
from typing import Optional


class WilyError(Exception):
    """Raised when Wily returns a WRerror packet."""


class Mtype(IntEnum):
    # Events (also used as bitmasks)
    WEexec = 1
    WEgoto = 2
    WEdestroy = 4
    WEreplace = 8

    WEfencepost = 9

    # Requests and Responses
    WRerror = 10
    WMlist = 11
    WRlist = 12
    WMnew = 13
    WRnew = 14
    WMattach = 15
    WRattach = 16
    WMsetname = 17
    WRsetname = 18
    WMgetname = 19
    WRgetname = 20
    WMsettools = 21
    WRsettools = 22
    WMgettools = 23
    WRgettools = 24
    WMread = 25
    WRread = 26
    WMreplace = 27
    WRreplace = 28
    WMexec = 29
    WRexec = 30
    WMgoto = 31
    WRgoto = 32

    WMfencepost = 33


COOKIE = 0xFEED
HEADER_FMT = ">HHIHHIIH"  # cookie(2), t(2), len(4), m(2), w(2), p0(4), p1(4), flag(2)
HEADERSIZE = struct.calcsize(HEADER_FMT)  # 22 bytes


@dataclass
class Msg:
    t: Mtype
    m: int = 0
    w: int = 0
    p0: int = 0
    p1: int = 0
    flag: int = 0
    s: str = ""

    def pack(self) -> bytes:
        payload = self.s.encode("utf-8") + b"\x00"
        total_len = HEADERSIZE + len(payload)
        header = struct.pack(
            HEADER_FMT,
            COOKIE,
            int(self.t),
            total_len,
            self.m,
            self.w,
            self.p0,
            self.p1,
            self.flag,
        )
        return header + payload


def get_wily_fifo() -> str:
    if "WILYFIFO" in os.environ:
        return os.environ["WILYFIFO"]
    user = pwd.getpwuid(os.getuid()).pw_name
    disp = os.environ.get("DISPLAY")
    if not disp:
        raise RuntimeError("$DISPLAY not set and $WILYFIFO not set")
    tmpdir = os.environ.get("TMPDIR") or "/tmp"
    return f"{tmpdir}/wily{user}{disp}"


class WilyClient:
    """Pure-Python 3 client for Wily's libmsg / rpc protocol."""

    def __init__(self, fifo_path: Optional[str] = None):
        self.fifo_path = fifo_path or get_wily_fifo()
        self.sock: Optional[socket.socket] = None
        self._next_id = 1
        self._events: deque[Msg] = deque()

    def connect(self) -> "WilyClient":
        tmp_fd, sock_path = tempfile.mkstemp(prefix="wily.", dir="/tmp")
        os.close(tmp_fd)
        os.unlink(sock_path)

        srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            srv.bind(sock_path)
            srv.listen(1)

            path_bytes = sock_path.encode("utf-8")
            fifo_fd = os.open(self.fifo_path, os.O_WRONLY)
            try:
                if os.write(fifo_fd, path_bytes) != len(path_bytes):
                    raise IOError("Short write to Wily FIFO")
            finally:
                os.close(fifo_fd)

            self.sock, _ = srv.accept()
            return self
        finally:
            srv.close()
            if os.path.exists(sock_path):
                os.unlink(sock_path)

    def close(self) -> None:
        if self.sock is not None:
            self.sock.close()
            self.sock = None

    def __enter__(self) -> "WilyClient":
        return self.connect()

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.close()

    def fileno(self) -> int:
        if self.sock is None:
            raise ConnectionError("Not connected to Wily")
        return self.sock.fileno()

    def _alloc_id(self) -> int:
        mid = self._next_id
        self._next_id = (self._next_id % 0xFFFF) + 1
        return mid

    def _recv_exact(self, n: int) -> bytes:
        if self.sock is None:
            raise ConnectionError("Not connected to Wily")
        buf = bytearray()
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                self.close()
                raise ConnectionError("Wily closed the IPC connection")
            buf.extend(chunk)
        return bytes(buf)

    def _recv_raw_msg(self) -> Msg:
        header = self._recv_exact(HEADERSIZE)
        cookie, mtype, total_len, msg_id, win_id, p0, p1, flag = struct.unpack(
            HEADER_FMT, header
        )
        if cookie != COOKIE or total_len < HEADERSIZE:
            raise ValueError(
                f"Corrupt Wily packet: cookie={hex(cookie)}, len={total_len}"
            )
        raw_text = self._recv_exact(total_len - HEADERSIZE)
        text = raw_text[:-1].decode("utf-8", errors="replace")
        return Msg(
            t=Mtype(mtype),
            m=msg_id,
            w=win_id,
            p0=p0,
            p1=p1,
            flag=flag,
            s=text,
        )

    def _rpc(
        self,
        req_type: Mtype,
        expected_reply: Mtype,
        win_id: int = 0,
        p0: int = 0,
        p1: int = 0,
        flag: int = 0,
        text: str = "",
    ) -> Msg:
        if self.sock is None:
            raise ConnectionError("Not connected to Wily")

        mid = self._alloc_id()
        req = Msg(t=req_type, m=mid, w=win_id, p0=p0, p1=p1, flag=flag, s=text)
        self.sock.sendall(req.pack())

        # Read until our matching RPC reply arrives; queue any interleaved events
        while True:
            msg = self._recv_raw_msg()
            if msg.t < Mtype.WEfencepost:
                self._events.append(msg)
                continue
            if msg.m != mid:
                raise RuntimeError(f"Unexpected message id {msg.m} (expected {mid})")
            if msg.t == Mtype.WRerror:
                raise WilyError(msg.s)
            if msg.t != expected_reply:
                raise RuntimeError(
                    f"Unexpected reply type {msg.t.name} (expected {expected_reply.name})"
                )
            return msg

    # --- High-level RPC methods (matching libmsg/rpc.c) ---

    def list_windows(self) -> list[tuple[int, str]]:
        """Return list of (win_id, name) tuples."""
        reply = self._rpc(Mtype.WMlist, Mtype.WRlist)
        windows = []
        for line in reply.s.splitlines():
            if not line.strip():
                continue
            parts = line.split(None, 1)
            wid = int(parts[1])
            name = parts[0] if len(parts) > 1 else ""
            windows.append((wid, name))
        return windows

    def new(self, name: str, backup: bool = False) -> int:
        """Create or open a window and return its window ID."""
        reply = self._rpc(
            Mtype.WMnew, Mtype.WRnew, flag=1 if backup else 0, text=name
        )
        return reply.w

    def attach(self, win_id: int, mask: int) -> None:
        """Subscribe to event bitmask (WEexec | WEgoto | WEdestroy | WEreplace)."""
        self._rpc(Mtype.WMattach, Mtype.WRattach, win_id=win_id, flag=mask)

    def setname(self, win_id: int, name: str) -> None:
        self._rpc(Mtype.WMsetname, Mtype.WRsetname, win_id=win_id, text=name)

    def getname(self, win_id: int) -> str:
        return self._rpc(Mtype.WMgetname, Mtype.WRgetname, win_id=win_id).s

    def settools(self, win_id: int, tools: str) -> None:
        self._rpc(Mtype.WMsettools, Mtype.WRsettools, win_id=win_id, text=tools)

    def gettools(self, win_id: int) -> str:
        return self._rpc(Mtype.WMgettools, Mtype.WRgettools, win_id=win_id).s

    def read(self, win_id: int, p0: int, p1: int) -> str:
        """Read rune range [p0, p1) from window."""
        return self._rpc(
            Mtype.WMread, Mtype.WRread, win_id=win_id, p0=p0, p1=p1
        ).s

    def replace(self, win_id: int, p0: int, p1: int, text: str) -> None:
        """Replace rune range [p0, p1) with text."""
        self._rpc(
            Mtype.WMreplace, Mtype.WRreplace, win_id=win_id, p0=p0, p1=p1, text=text
        )

    def exec_cmd(self, win_id: int, cmd: str) -> None:
        """Execute a command as if B2-clicked in win_id."""
        self._rpc(Mtype.WMexec, Mtype.WRexec, win_id=win_id, text=cmd)

    def goto(
        self, win_id: int, target: str, set_dot: bool = True
    ) -> tuple[int, tuple[int, int]]:
        """Search/jump as if B3-clicked; returns (target_win_id, (p0, p1))."""
        reply = self._rpc(
            Mtype.WMgoto,
            Mtype.WRgoto,
            win_id=win_id,
            flag=1 if set_dot else 0,
            text=target,
        )
        return reply.w, (reply.p0, reply.p1)

    # --- Event Queue & Bouncing ---

    def would_block(self) -> bool:
        """Return True if next_event() would block waiting for Wily."""
        if self._events:
            return False
        if self.sock is None:
            return True
        r, _, _ = select.select([self.sock], [], [], 0)
        return len(r) == 0

    def next_event(self) -> Msg:
        """Pop the next queued event or block until Wily sends one."""
        if self._events:
            return self._events.popleft()
        while True:
            msg = self._recv_raw_msg()
            if msg.t < Mtype.WEfencepost:
                return msg

    def bounce(self, event: Msg) -> None:
        """Return an unhandled WEexec or WEgoto event back to Wily."""
        if event.t == Mtype.WEexec:
            self._rpc(
                Mtype.WMexec,
                Mtype.WRexec,
                win_id=event.w,
                p0=event.p0,
                p1=event.p1,
                flag=event.flag,
                text=event.s,
            )
        elif event.t == Mtype.WEgoto:
            self._rpc(
                Mtype.WMgoto,
                Mtype.WRgoto,
                win_id=event.w,
                p0=event.p0,
                p1=event.p1,
                flag=event.flag,
                text=event.s,
            )


if __name__ == "__main__":
    with WilyClient() as wily:
        print("Existing windows:", wily.list_windows())

        wid = wily.new("+PyConsole", backup=False)
        wily.settools(wid, " Hello Quit ")
        wily.replace(wid, 0, 0, "Middle-click (B2) 'Hello' or 'Quit' in the tag.\n")
        wily.attach(wid, Mtype.WEexec | Mtype.WEgoto | Mtype.WEdestroy)

        while True:
            ev = wily.next_event()
            if ev.t == Mtype.WEdestroy:
                print("Window closed, exiting.")
                break
            elif ev.t == Mtype.WEexec:
                cmd = ev.s.strip()
                if cmd == "Hello":
                    wily.replace(wid, 0, 0, "Clicked Hello!\n")
                elif cmd == "Quit":
                    wily.exec_cmd(wid, "Del")
                    break
                else:
                    # Let Wily handle built-in commands like Cut, Paste, Snarf, Del
                    wily.bounce(ev)
            elif ev.t == Mtype.WEgoto:
                # Let Wily handle normal B3 searches/file opens
                wily.bounce(ev)
