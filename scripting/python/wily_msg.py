#! /usr/bin/env python3
import os
import pwd
import socket
import struct
import tempfile
from enum import IntEnum


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


def get_wily_fifo() -> str:
    if "WILYFIFO" in os.environ:
        return os.environ["WILYFIFO"]
    user = pwd.getpwuid(os.getuid()).pw_name
    disp = os.environ.get("DISPLAY")
    if not disp:
        raise RuntimeError("$DISPLAY not set and $WILYFIFO not set")
    tmpdir = os.environ.get("TMPDIR") or "/tmp"
    return f"{tmpdir}/wily{user}{disp}"


def connect_wily() -> socket.socket:
    fifo_path = get_wily_fifo()

    # 1. Generate a unique temporary socket path (matching mkstemp + unlink)
    tmp_fd, sock_path = tempfile.mkstemp(prefix="wily.", dir="/tmp")
    os.close(tmp_fd)
    os.unlink(sock_path)

    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        # 2. Bind and listen on our temporary Unix domain socket
        srv.bind(sock_path)
        srv.listen(1)

        # 3. Write socket path (NO trailing NUL byte) to Wily's named pipe
        path_bytes = sock_path.encode("utf-8")
        fifo_fd = os.open(fifo_path, os.O_WRONLY)
        try:
            written = os.write(fifo_fd, path_bytes)
            if written != len(path_bytes):
                raise IOError("Short write to Wily FIFO")
        finally:
            os.close(fifo_fd)

        # 4. Accept Wily's incoming connection
        conn, _ = srv.accept()
        return conn
    finally:
        srv.close()
        if os.path.exists(sock_path):
            os.unlink(sock_path)


def pack_msg(
    mtype: Mtype,
    msg_id: int = 0,
    win_id: int = 0,
    p0: int = 0,
    p1: int = 0,
    flag: int = 0,
    text: str = "",
) -> bytes:
    payload = text.encode("utf-8") + b"\x00"
    total_len = HEADERSIZE + len(payload)
    header = struct.pack(
        HEADER_FMT,
        COOKIE,
        int(mtype),
        total_len,
        msg_id,
        win_id,
        p0,
        p1,
        flag,
    )
    return header + payload


def recv_exact(sock: socket.socket, n: int) -> bytes:
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("Wily closed the IPC socket")
        buf.extend(chunk)
    return bytes(buf)


def recv_msg(sock: socket.socket) -> dict:
    header = recv_exact(sock, HEADERSIZE)
    cookie, mtype, total_len, msg_id, win_id, p0, p1, flag = struct.unpack(
        HEADER_FMT, header
    )
    if cookie != COOKIE or total_len < HEADERSIZE:
        raise ValueError(
            f"Invalid Wily message header: cookie={hex(cookie)}, len={total_len}"
        )

    payload_len = total_len - HEADERSIZE
    raw_text = recv_exact(sock, payload_len)
    text = raw_text[:-1].decode("utf-8", errors="replace")

    return {
        "type": Mtype(mtype),
        "m": msg_id,
        "w": win_id,
        "range": (p0, p1),
        "flag": flag,
        "s": text,
    }


if __name__ == "__main__":
    with connect_wily() as conn:
        # 1. List open windows
        conn.sendall(pack_msg(Mtype.WMlist, msg_id=1))
        reply = recv_msg(conn)
        print("Open windows:\n", reply["s"])

        # 2. Create a new window named "+Python"
        conn.sendall(pack_msg(Mtype.WMnew, msg_id=2, flag=0, text="+Python"))
        new_reply = recv_msg(conn)
        if new_reply["type"] == Mtype.WRerror:
            raise RuntimeError(new_reply["s"])

        win_id = new_reply["w"]

        # 3. Insert text at range (0, 0)
        conn.sendall(
            pack_msg(
                Mtype.WMreplace,
                msg_id=3,
                win_id=win_id,
                p0=0,
                p1=0,
                text="Hello from Python 3!\n",
            )
        )
        print("Replace reply:", recv_msg(conn))

