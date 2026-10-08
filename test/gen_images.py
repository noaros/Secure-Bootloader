#!/usr/bin/env python3
"""Generate keys and signed dummy images for the host tests."""
import os
import struct
import subprocess
import sys

out = sys.argv[1]
tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "imgtool.py")
os.makedirs(out, exist_ok=True)


def run(*args):
    subprocess.run([sys.executable, tool, *args], check=True, stdout=subprocess.DEVNULL)


for k in ("test_key", "other_key"):
    if not os.path.exists(f"{out}/{k}.pem"):
        run("keygen", f"{out}/{k}.pem")
run("pubkey-c", f"{out}/test_key.pem", f"{out}/pubkey.c")

SLOTS = {"A": 0x08020000, "B": 0x08120000}
for name, slot, ver, ctr, key in [
    ("A_v1", "A", "1.0.0", 1, "test_key"),
    ("B_v2", "B", "2.0.0", 2, "test_key"),
    ("A_v3", "A", "3.0.0", 3, "test_key"),
    ("B_badkey", "B", "9.0.0", 9, "other_key"),
]:
    base = SLOTS[slot]
    payload = struct.pack("<II", 0x20030000, base + 0x200 + 0x101) + os.urandom(4096 - 8 + 2)
    with open(f"{out}/{name}.bin", "wb") as f:
        f.write(payload)
    run("sign", "--key", f"{out}/{key}.pem", "--load-addr", hex(base), "--version", ver,
        "--security-counter", str(ctr), f"{out}/{name}.bin", f"{out}/{name}.signed.bin")
