#!/usr/bin/env python3
"""
pywin-v14.py - Interactive shell window ('win') for Wily using wily_rpc.py.
"""

import fcntl
import os
import pty
import select
import shutil
import signal
import sys
import termios
from typing import Optional

from wily_rpc import Mtype, Msg, WilyClient, WilyError


def resolve_command(args: list[str]) -> list[str]:
    if args:
        return args
    shell = os.environ.get("SHELL", "")
    if shell and os.path.basename(shell) == "bash" and os.path.isfile(shell):
        return [shell, "--norc", "-i"]
    bash = shutil.which("bash")
    if bash:
        return [bash, "--norc", "-i"]
    if shell and os.path.isfile(shell) and os.access(shell, os.X_OK):
        return [shell]
    for candidate in ("/bin/sh", "sh"):
        path = candidate if os.path.isabs(candidate) else shutil.which(candidate)
        if path and os.path.isfile(path) and os.access(path, os.X_OK):
            return [path]
    raise FileNotFoundError("No usable shell found ($SHELL, bash, or /bin/sh)")


class WilyShellWindow:
    def __init__(self, wily: WilyClient, cmd: list[str], debug: bool = False):
        self.wily = wily
        self.cmd = cmd
        self.debug = debug

        title = f"+{os.path.basename(cmd[0])}" if cmd else "+win"
        self.wid = self.wily.new(title, backup=False)
        self.wily.settools(self.wid, "Interrupt EOF Clear ")
        self.wily.attach(
            self.wid,
            Mtype.WEexec | Mtype.WEgoto | Mtype.WEdestroy | Mtype.WEreplace,
        )

        self.output_point = 0
        self.input_buf = ""
        self._pending_echoes: list[tuple[int, int, str]] = []

        self.master_fd: Optional[int] = None
        self.child_pid: Optional[int] = None

    def log(self, msg: str) -> None:
        if self.debug:
            print(f"[pywin] {msg}", file=sys.stderr, flush=True)

    def spawn_shell(self) -> None:
        pid, master_fd = pty.fork()
        if pid == 0:
            try:
                fcntl.ioctl(0, termios.TIOCSCTTY, 0)
            except OSError:
                pass

            try:
                attrs = termios.tcgetattr(0)
                attrs[0] |= termios.ICRNL
                attrs[3] &= ~termios.ECHO
                termios.tcsetattr(0, termios.TCSANOW, attrs)
            except termios.error:
                pass

            os.environ["TERM"] = "dumb"
            os.environ["PAGER"] = "cat"
            os.environ["PS1"] = ": "
            os.environ.pop("PROMPT_COMMAND", None)
            os.execvp(self.cmd[0], self.cmd)
            os._exit(1)

        self.child_pid = pid
        self.master_fd = master_fd
        self.log(f"Spawned {self.cmd} (pid={pid}, master_fd={master_fd})")

    def _consume_self_echo(self, ev: Msg) -> bool:
        for i, (p0, p1, text) in enumerate(self._pending_echoes):
            if ev.p0 == p0 and ev.p1 == p1 and ev.s == text:
                del self._pending_echoes[i]
                return True
        return False

    def append_output(self, text: str) -> None:
        clean = text.replace("\r\n", "\n").replace("\r", "")
        if not clean:
            return

        pt = self.output_point
        self.log(f"PTY -> Wily ({len(clean)} chars) at output_point={pt}: {clean!r}")
        self._pending_echoes.append((pt, pt, clean))
        try:
            self.wily.replace(self.wid, pt, pt, clean)
        except WilyError as exc:
            self.log(f"replace error ({exc}), resetting output_point=0")
            if self._pending_echoes:
                self._pending_echoes.pop()
            self.output_point = 0
            self.input_buf = ""
            self._pending_echoes.append((0, 0, clean))
            self.wily.replace(self.wid, 0, 0, clean)
            pt = 0

        self.output_point = pt + len(clean)

    def send_to_pty(self, text: str) -> None:
        if self.master_fd is None or not text:
            return
        wire = text.replace("\n", "\r")
        self.log(f"Wily -> PTY: {wire!r}")
        os.write(self.master_fd, wire.encode("utf-8"))

    def clear_window(self) -> None:
        end = self.output_point + len(self.input_buf)
        if end > 0:
            self._pending_echoes.append((0, end, ""))
            try:
                self.wily.replace(self.wid, 0, end, "")
            except WilyError:
                if self._pending_echoes:
                    self._pending_echoes.pop()
        self.output_point = 0
        self.input_buf = ""

    def handle_wily_event(self, ev: Msg) -> bool:
        self.log(
            f"Event: type={ev.t.name} w={ev.w} range=({ev.p0},{ev.p1}) flag={ev.flag} s={ev.s!r}"
        )

        if ev.t == Mtype.WEdestroy:
            return False

        if ev.t == Mtype.WEreplace:
            if self._consume_self_echo(ev):
                self.log("  -> ignored self-echo")
                return True

            if ev.p0 < self.output_point:
                delta = len(ev.s) - (ev.p1 - ev.p0)
                self.output_point = max(0, self.output_point + delta)
                self.log(f"  -> edit before output_point, new output_point={self.output_point}")
                return True

            rel_start = ev.p0 - self.output_point
            rel_end = ev.p1 - self.output_point
            self.input_buf = (
                self.input_buf[:rel_start] + ev.s + self.input_buf[rel_end:]
            )
            self.log(f"  -> input_buf={self.input_buf!r}")

            while "\n" in self.input_buf:
                nl_idx = self.input_buf.index("\n")
                line = self.input_buf[: nl_idx + 1]
                self.input_buf = self.input_buf[nl_idx + 1 :]
                self.output_point += len(line)
                self.send_to_pty(line)

            return True

        if ev.t == Mtype.WEexec:
            cmd = ev.s.strip()
            if cmd == "Interrupt" and self.master_fd is not None:
                try:
                    os.write(self.master_fd, b"\x03")
                except OSError:
                    pass
            elif cmd == "EOF" and self.master_fd is not None:
                self.send_to_pty("\x04")
            elif cmd == "Clear":
                self.clear_window()
            else:
                self.wily.bounce(ev)
            return True

        if ev.t == Mtype.WEgoto:
            self.wily.bounce(ev)
            return True

        return True

    def run(self) -> None:
        self.spawn_shell()
        assert self.master_fd is not None

        running = True
        while running:
            while not self.wily.would_block():
                if not self.handle_wily_event(self.wily.next_event()):
                    running = False
                    break
            if not running:
                break

            rlist, _, _ = select.select([self.wily.fileno(), self.master_fd], [], [])

            if self.master_fd in rlist:
                try:
                    data = os.read(self.master_fd, 4096)
                except OSError:
                    data = b""
                if not data:
                    self.append_output("\n[Process exited]\n")
                    break
                self.append_output(data.decode("utf-8", errors="replace"))

            if self.wily.fileno() in rlist:
                while not self.wily.would_block():
                    if not self.handle_wily_event(self.wily.next_event()):
                        running = False
                        break

    def cleanup(self) -> None:
        if self.master_fd is not None:
            try:
                os.close(self.master_fd)
            except OSError:
                pass
            self.master_fd = None
        if self.child_pid is not None:
            try:
                os.kill(self.child_pid, signal.SIGHUP)
                os.waitpid(self.child_pid, os.WNOHANG)
            except OSError:
                pass
            self.child_pid = None


def main() -> None:
    debug = False
    cmd_args: list[str] = []
    for arg in sys.argv[1:]:
        if arg == "--debug":
            debug = True
        else:
            cmd_args.append(arg)

    cmd = resolve_command(cmd_args)
    with WilyClient() as wily:
        win = WilyShellWindow(wily, cmd, debug=debug)
        try:
            win.run()
        finally:
            win.cleanup()


if __name__ == "__main__":
    main()
