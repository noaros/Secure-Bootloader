#!/usr/bin/env python3
"""Upload a signed image to the board over the ST-LINK virtual COM port.

Works against both the running application (which installs into the inactive
slot) and the bootloader's recovery mode. Pass the image signed for *each*
slot; the device announces which slot it will write and the matching file is
sent:

    tools/update.py --port /dev/ttyACM0 build/app-v1.1.0-sc2-A.signed.bin build/app-v1.1.0-sc2-B.signed.bin

Dependency-free (POSIX termios), so it runs anywhere Python 3 does on Linux/macOS.
"""
import argparse
import os
import select
import struct
import sys
import termios
import time
import tty
import zlib

ACK, NAK, ERR = 0x06, 0x15, 0x18
CHUNK = 1024
HDR_SIZE = 0x200

ERRORS = {1: "refused (confirm the running image first)", 2: "bad size", 3: "flash erase failed",
          4: "device timed out", 5: "flash program failed", 6: "image rejected by device"}
IMG_ERRORS = {1: "empty", 2: "bad header", 3: "linked for a different slot", 4: "bad size",
              5: "bad vector table", 6: "payload hash mismatch", 7: "SIGNATURE INVALID",
              8: "rollback blocked (security counter too low)"}


class Port:
    def __init__(self, path, baud=115200):
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
        attrs = termios.tcgetattr(self.fd)
        attrs[0] = 0                                        # iflag
        attrs[1] = 0                                        # oflag
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attrs[3] = 0                                        # lflag
        speed = getattr(termios, f"B{baud}")
        attrs[4] = attrs[5] = speed
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)

    def write(self, data):
        view = memoryview(data)
        while view:
            n = os.write(self.fd, view)
            view = view[n:]
        termios.tcdrain(self.fd)

    def read(self, n, timeout):
        out = bytearray()
        deadline = time.monotonic() + timeout
        while len(out) < n:
            left = deadline - time.monotonic()
            if left <= 0:
                break
            r, _, _ = select.select([self.fd], [], [], left)
            if r:
                out += os.read(self.fd, n - len(out))
        return bytes(out)

    def flush_input(self):
        termios.tcflush(self.fd, termios.TCIFLUSH)


def fail(msg):
    sys.exit(f"error: {msg}")


def expect(port, timeout, what):
    """Wait for ACK/NAK; skip console text. ERR raises with the device's reason."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        b = port.read(1, deadline - time.monotonic())
        if not b:
            break
        if b[0] in (ACK, NAK):
            return b[0]
        if b[0] == ERR:
            code, detail = (port.read(2, 2) + b"\0\0")[:2]
            reason = ERRORS.get(code, f"error {code}")
            if code == 6:
                reason += f": {IMG_ERRORS.get(detail, detail)}"
            fail(f"device reported {reason} ({what})")
    fail(f"timeout waiting for device ({what})")


def load_images(paths):
    images = {}
    for p in paths:
        with open(p, "rb") as f:
            data = f.read()
        magic, _, _, load = struct.unpack_from("<IHHI", data, 0)
        if magic != 0x31494253:
            fail(f"{p} is not a signed image (run tools/imgtool.py sign)")
        data += b"\xff" * (-len(data) % 4)
        images[load] = (p, data)
    return images


def monitor(port, seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        data = port.read(256, 0.1)
        if data:
            sys.stdout.write(data.decode("utf-8", "replace").replace("\r", ""))
            sys.stdout.flush()


def terminal(port):
    """Interactive console: show device output and send each keystroke as typed."""
    stdin = sys.stdin.fileno()
    saved = termios.tcgetattr(stdin) if os.isatty(stdin) else None
    if saved:
        tty.setcbreak(stdin)                    # unbuffered keys, Ctrl-C still works
    print("---- console (Ctrl-C to exit) ----")
    try:
        while True:
            r, _, _ = select.select([port.fd, stdin], [], [])
            if port.fd in r:
                data = os.read(port.fd, 256)
                sys.stdout.write(data.decode("utf-8", "replace").replace("\r", ""))
                sys.stdout.flush()
            if stdin in r:
                key = os.read(stdin, 64)
                if not key:                     # stdin closed: keep showing output
                    monitor(port, float("inf"))
                port.write(key)
    finally:
        if saved:
            termios.tcsetattr(stdin, termios.TCSADRAIN, saved)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True, help="serial device, e.g. /dev/ttyACM0")
    ap.add_argument("--monitor", type=float, default=20.0,
                    help="seconds to show console output after uploading (default 20)")
    ap.add_argument("images", nargs="*", help="signed images (one per slot)")
    args = ap.parse_args()

    port = Port(args.port)
    if not args.images:
        terminal(port)
        return

    images = load_images(args.images)

    port.flush_input()
    port.write(b"UPD1")
    expect(port, 10, "handshake")
    target, max_size = struct.unpack("<II", port.read(8, 2))
    if target not in images:
        fail(f"device wants an image for slot @0x{target:08x}; pass the build signed for it")
    path, data = images[target]
    if len(data) > max_size:
        fail(f"{path} is larger than the slot")
    print(f"device is installing into slot @0x{target:08x}; sending {path} ({len(data)} bytes)")

    port.write(struct.pack("<I", len(data)))
    print("erasing slot...")
    expect(port, 30, "erase")

    sent = 0
    t0 = time.monotonic()
    while sent < len(data):
        chunk = data[sent:sent + CHUNK]
        last = sent + len(chunk) == len(data)
        for _attempt in range(5):
            port.write(struct.pack("<H", len(chunk)) + chunk + struct.pack("<I", zlib.crc32(chunk)))
            if expect(port, 20 if last else 5, "verify" if last else f"chunk @{sent}") == ACK:
                break
        else:
            fail("too many CRC retries")
        sent += len(chunk)
        sys.stdout.write(f"\r  {sent * 100 // len(data):3d}%  {sent}/{len(data)} bytes")
        sys.stdout.flush()
    print(f"\nupload complete in {time.monotonic() - t0:.1f}s - device verified the signature")
    if args.monitor > 0:
        print("---- device console ----")
        monitor(port, args.monitor)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
