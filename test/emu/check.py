#!/usr/bin/env python3
"""Run the SM83 crypto ROM and compare it to Python.

The ROM is the same GBDK compile of the wallet crypto, with no UI.
Peanut-GB runs it with the LCD and audio compiled out, so a test takes
as long as the CPU math takes on this machine. gbc_sec in the harness
output is an estimate of Game Boy Color double-speed time.

Quick checks (seconds):

    python3 test/emu/check.py

The full address generation (the ~90 minute GBC workload):

    python3 test/emu/check.py address
"""

import hashlib
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ROM_DIR = ROOT / "test" / "rom"
EMU_DIR = ROOT / "test" / "emu"
RUNNER = EMU_DIR / "build" / "run_crypto_test"
ROM = ROM_DIR / "build" / "crypto_test.gb"
NOI = ROM_DIR / "build" / "crypto_test.noi"

MNEMONIC = (
    "abandon abandon abandon abandon abandon abandon abandon "
    "abandon abandon abandon abandon about"
)

# Vectors already pinned in src/states_testing.c.
KNOWN_ADDRESSES = {
    "doge": "DBus3bamQjgJULBJtYXpEzDWQRwF5iwxgC",
    "pepe": "PehYeRLFsRj5jboZXTC6rFHxmYdmV9RdfR",
    "bells": "BBDr846KrqMvPAUsmSsDpMraFueXWBWgih",
}

P = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F
GX = 0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798


def build():
    subprocess.check_call(["make", "-C", str(ROM_DIR)])
    subprocess.check_call(["make", "-C", str(EMU_DIR)])


def run_rom(test_id, timeout):
    proc = subprocess.run(
        [str(RUNNER), str(ROM), str(NOI), str(test_id), str(timeout)],
        text=True,
        stdout=subprocess.PIPE,
        stderr=None,
    )
    fields = {}
    for line in proc.stdout.splitlines():
        key, _, value = line.partition(" ")
        fields[key] = value
    fields["_returncode"] = proc.returncode
    return fields


def expect_hex(fields, expected, label):
    got = fields.get("hex", "")
    gbc = fields.get("gbc_sec", "?")
    host = fields.get("host_sec", "?")
    frames = fields.get("frames", "?")
    print(f"{label}: host {host}s, gbc est {gbc}s, frames {frames}")
    if fields.get("ok") != "1" or got != expected:
        print(f"  got  {got}")
        print(f"  want {expected}")
        print(f"  runner status {fields.get('status')} exit {fields.get('_returncode')}")
        return False
    print("  match")
    return True


def python_address(mnemonic, version):
    from mnemonic import Mnemonic
    from bip32utils import BIP32Key
    import base58

    seed = Mnemonic("english").to_seed(mnemonic, passphrase="")
    node = BIP32Key.fromEntropy(seed)
    for index in (44 + 0x80000000, 3 + 0x80000000, 0x80000000, 0, 0):
        node = node.ChildKey(index)
    pubkey = node.PublicKey()
    hash160 = hashlib.new("ripemd160", hashlib.sha256(pubkey).digest()).digest()
    versioned = bytes([version]) + hash160
    checksum = hashlib.sha256(hashlib.sha256(versioned).digest()).digest()[:4]
    return base58.b58encode(versioned + checksum).decode("ascii"), seed, pubkey


def check_sha512():
    expected = hashlib.sha512(b"abc").hexdigest()
    return expect_hex(run_rom(1, 60), expected, "sha512 abc")


def check_mul():
    expected = f"{pow(GX, 2, P):064x}"
    return expect_hex(run_rom(2, 60), expected, "field square")


def check_pbkdf2_once():
    expected = hashlib.pbkdf2_hmac("sha512", MNEMONIC.encode(), b"mnemonic", 1).hex()
    return expect_hex(run_rom(3, 120), expected, "pbkdf2 x1")


def check_address():
    doge, seed, pubkey = python_address(MNEMONIC, 0x1E)
    pepe, _, _ = python_address(MNEMONIC, 0x38)
    bells, _, _ = python_address(MNEMONIC, 0x19)
    if (doge, pepe, bells) != (
        KNOWN_ADDRESSES["doge"],
        KNOWN_ADDRESSES["pepe"],
        KNOWN_ADDRESSES["bells"],
    ):
        print("python reference does not match the pinned abandon addresses")
        print(doge, pepe, bells)
        return False

    fields = run_rom(4, 7200)
    raw = bytes.fromhex(fields.get("hex", ""))
    if fields.get("ok") != "1" or len(raw) < 202:
        print(f"address run failed status {fields.get('status')} len {len(raw)}")
        return False

    got = {
        "doge": raw[0:35].split(b"\x00", 1)[0].decode(),
        "pepe": raw[35:70].split(b"\x00", 1)[0].decode(),
        "bells": raw[70:105].split(b"\x00", 1)[0].decode(),
    }
    got_seed = raw[105:169]
    got_pub = raw[169:202]
    gbc = float(fields.get("gbc_sec", "nan"))
    host = fields.get("host_sec", "?")
    print(f"address: host {host}s, gbc est {gbc / 60:.1f} min, frames {fields.get('frames')}")
    ok = True
    for name in ("doge", "pepe", "bells"):
        if got[name] != KNOWN_ADDRESSES[name]:
            print(f"  {name} got  {got[name]}")
            print(f"  {name} want {KNOWN_ADDRESSES[name]}")
            ok = False
        else:
            print(f"  {name} {got[name]}")
    if got_seed != seed:
        print("  seed mismatch")
        ok = False
    if got_pub != pubkey:
        print("  pubkey mismatch")
        ok = False
    return ok


def main():
    which = sys.argv[1] if len(sys.argv) > 1 else "quick"
    build()
    if which == "address":
        tests = [check_address]
    elif which == "quick":
        tests = [check_sha512, check_mul, check_pbkdf2_once]
    elif which == "all":
        tests = [check_sha512, check_mul, check_pbkdf2_once, check_address]
    else:
        print("usage: check.py [quick|address|all]")
        return 1

    failed = 0
    for test in tests:
        if not test():
            failed += 1
    if failed:
        print(f"{failed} failed")
        return 1
    print("all matched")
    return 0


if __name__ == "__main__":
    sys.exit(main())
