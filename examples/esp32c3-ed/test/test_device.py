#!/usr/bin/env python3
"""Drives ed on the firmware as a terminal would, and checks every byte that
comes back: the echo of the input and ed's output.

  idf.py build && python test/test_device.py --qemu    # in QEMU
  python test/test_device.py --port /dev/ttyUSB0       # on a board (writes
                                                       # files test*.txt to it)

Run it in the ESP-IDF environment, which has pyserial and idf.py.
"""
import argparse
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time

READY = b"ed ready\r\n"
PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class Qemu:
    """QEMU on a copy of the flash image, kept across restart()."""

    def __init__(self):
        self.dir = tempfile.mkdtemp(prefix="ed-qemu-")
        self.flash = os.path.join(self.dir, "flash.bin")
        build = os.path.join(PROJECT, "build")
        subprocess.check_call(
            [sys.executable, "-m", "esptool", "--chip=esp32c3", "merge_bin",
             f"--output={self.flash}", "--fill-flash-size=2MB", "@flash_args"],
            cwd=build, stdout=subprocess.DEVNULL)
        self.proc = None
        self.start()

    def start(self):
        self.proc = subprocess.Popen(
            ["idf.py", "qemu", f"--flash-file={self.flash}"], cwd=PROJECT,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, start_new_session=True)
        self.buf = bytearray()
        self.lock = threading.Lock()
        threading.Thread(target=self._reader, args=(self.proc,),
                         daemon=True).start()

    def _reader(self, proc):
        while True:
            data = os.read(proc.stdout.fileno(), 4096)
            if not data:
                return
            with self.lock:
                self.buf += data

    def write(self, data):
        self.proc.stdin.write(data)
        self.proc.stdin.flush()

    def read(self):
        with self.lock:
            data = bytes(self.buf)
            self.buf.clear()
        return data

    def stop(self):
        try:
            os.killpg(self.proc.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        self.proc.wait()

    def restart(self):
        self.stop()
        self.start()

    def close(self):
        self.stop()
        shutil.rmtree(self.dir, ignore_errors=True)


class Board:
    """A board on a serial port, reset with the usual DTR/RTS wiring."""

    def __init__(self, port):
        import serial
        self.serial = serial.Serial(port, 115200, timeout=0)
        self.restart()

    def write(self, data):
        self.serial.write(data)

    def read(self):
        return self.serial.read(65536)

    def restart(self):
        self.serial.dtr = False
        self.serial.rts = True
        time.sleep(0.1)
        self.serial.rts = False

    def close(self):
        self.serial.close()


class Terminal:
    def __init__(self, link, timeout):
        self.link = link
        self.timeout = timeout
        self.seen = bytearray()

    def wait_for(self, what, timeout=None):
        """Reads until `what`; returns all up to and including it."""
        deadline = time.monotonic() + (timeout or self.timeout)
        while what not in self.seen:
            data = self.link.read()
            if data:
                self.seen += data
            elif time.monotonic() > deadline:
                raise TimeoutError(f"no {what!r} in {bytes(self.seen[-300:])!r}")
            else:
                time.sleep(0.002)
        end = self.seen.index(what) + len(what)
        out = bytes(self.seen[:end])
        del self.seen[:end]
        return out

    def boot(self):
        banner = self.wait_for(READY, 120)
        return banner[banner.rindex(b"V7 ed"):]

    def session(self, steps):
        """One ed session, to its exit: `steps` are (bytes to send, the exact
        bytes expected in return). Returns what the next ed's banner says."""
        for send, expect in steps:
            self.link.write(send)
            self.wait_exact(expect)
        return self.wait_for(READY)

    def wait_exact(self, expect):
        if not expect:
            return
        deadline = time.monotonic() + self.timeout
        while len(self.seen) < len(expect):
            data = self.link.read()
            if data:
                self.seen += data
            elif time.monotonic() > deadline:
                break
            else:
                time.sleep(0.002)
        got = bytes(self.seen[:len(expect)])
        if got != expect:
            raise AssertionError(f"expected {expect[:200]!r}\n     got {got[:200]!r}")
        del self.seen[:len(expect)]


def line(text, *output):
    """A line typed at the terminal: echoed, then the output lines of ed."""
    data = text.encode()
    expect = data + b"\r\n" + b"".join(o.encode() + b"\r\n" for o in output)
    return (data + b"\r", expect)


def text_block(*lines):
    return [line("a")] + [line(l) for l in lines] + [line(".")]


def test_commands(term):
    term.session(text_block("hello world", "foo foo foo") + [
        line("1,$p", "hello world", "foo foo foo"),
        line("=", "2"),
        line("1s/world/wasmc/p", "hello wasmc"),
        line("2s/foo/bar/gp", "bar bar bar"),
        line("1;+1p", "hello wasmc", "bar bar bar"),
        line(r"1s/\(hello\) \(wasmc\)/\2 \1/p", "wasmc hello"),
        line("g/bar/d"),
        line("1,$p", "wasmc hello"),
        line("Q"),
    ])


def test_errors(term):
    long_name = "n" * 80
    term.session(text_block("kept", "x1") + [
        line("zz", "?"),
        line("9p", "?"),
        line("1,$s/nothing/x/", "?"),
        line("g/x/zz", "?"),
        line("w " + long_name, "?"),
        line("1p", "kept"),
        line("q", "?"),      # unsaved: the first q is refused
        line("1p", "kept"),
        line("q"),
    ])
    term.session([
        line("e nosuchfile", "?nosuchfile"),
        line("q"),
    ])
    # the frame of global() stays on the C stack at every error
    term.session(text_block("x") + [line("g/x/zz", "?")] * 100 + [
        line("1p", "x"), line("Q")])


def test_line_editing(term):
    term.session([
        (b"a\r", b"a\r\n"),
        (b"hellx\x7fo\r", b"hellx\b \bo\r\n"),      # DEL
        (b"worlt\x08d\r", b"worlt\b \bd\r\n"),      # BS
        (b"bad\x15good\r", b"bad\b \b\b \b\b \bgood\r\n"),  # Ctrl-U
        (b"\x7f\r", b"\r\n"),                        # erase nothing: empty line
        (b"crlf\r\n", b"crlf\r\n"),                  # \r\n is one newline
        (b"lf\n", b"lf\r\n"),
        (b"caff\xc3\xa8\r", b"caff\a\a\r\n"),        # 7-bit only
        (b"tab\there\r", b"tab\there\r\n"),
        (b".\r", b".\r\n"),
        (b"1,$p\r", b"1,$p\r\nhello\r\nworld\r\ngood\r\n\r\ncrlf\r\nlf\r\n"
                    b"caff\r\ntab\there\r\n"),
        (b"Q\r", b"Q\r\n"),
    ])
    # the longest line that ed takes; beyond it the terminal rings the bell
    term.session([
        line("a"),
        (b"x" * 515 + b"\r", b"x" * 509 + b"\a" * 6 + b"\r\n"),
        line("."),
        line("s/x*/y/p", "y"),
        line("Q"),
    ])


def test_eof(term):
    term.session([(b"\x04", b"")])                   # Ctrl-D: ed exits
    term.session(text_block("unsaved") + [
        (b"\x04", b"?\r\n"),                         # refused, like q
        (b"\x04", b""),
    ])


def test_files(term):
    term.session(text_block("alpha", "beta") + [
        line("w test1.txt", "11"),
        line("q"),
    ])
    term.session([
        line("e test1.txt", "11"),
        line("1,$p", "alpha", "beta"),
        line("1s/alpha/gamma/"),
        line("w", "11"),
        line("q"),
    ])
    term.session([
        line("r test1.txt", "11"),
        line("1p", "gamma"),
        line("Q"),
    ])


def test_big_buffer(term):
    """8192 lines, through the temp file on LittleFS, fill most of the page of
    linear memory. Twice that needs memory.grow to find 128 KB while the old
    64 KB are alive: on this heap, an error from ed, and the text is safe."""
    start = time.monotonic()
    term.session(text_block("x") + [line("1,$t$")] * 13 + [
        line("=", "8192"),
        line("w test2.txt", "16384"),
        line("1,$t$", "?MEM?"),
        line("1p", "x"),
        line("$p", "x"),
        line("Q"),
    ])
    print(f"    {time.monotonic() - start:.1f} s")


def test_temp_limit(term):
    """V7's limit on the temp file, about 127 KB, is an error, not a crash."""
    text = "y" * 400
    banner = term.session(text_block(text) + [line("1,$t$")] * 8 + [
        line("1,$t$", "?TMP"),
        line("1p", text),
        # a new temp file makes room
        line("2,$d"),
        line("w test3.txt", "401"),
        line("e", "401"),
        line("a"),
        line(text),
        line("."),
        line("$p", text),
        line("Q"),
    ])
    print("    " + banner.decode(errors="replace").splitlines()[0])


def test_persistence(term):
    term.link.restart()
    banner = term.boot()
    print("    " + banner.decode(errors="replace").replace("\r\n", "\n    "))
    if b"test1.txt" not in banner:
        raise AssertionError(f"test1.txt not listed: {banner!r}")
    term.session([
        line("e test1.txt", "11"),
        line("1,$p", "gamma", "beta"),
        line("q"),
    ])


TESTS = [test_commands, test_errors, test_line_editing, test_eof, test_files,
         test_big_buffer, test_temp_limit, test_persistence]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument("--qemu", action="store_true")
    target.add_argument("--port")
    parser.add_argument("--timeout", type=float, default=60)
    args = parser.parse_args()

    link = Qemu() if args.qemu else Board(args.port)
    failed = 0
    try:
        term = Terminal(link, args.timeout)
        banner = term.boot()
        print(banner.decode(errors="replace").replace("\r\n", "\n"))
        for test in TESTS:
            print(test.__name__)
            try:
                test(term)
            except (AssertionError, TimeoutError) as err:
                failed += 1
                print(f"  FAIL: {err}")
                # back to a known state: a new boot
                term.seen.clear()
                link.restart()
                term.boot()
    finally:
        link.close()
    print(f"{len(TESTS)} tests, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
