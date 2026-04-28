#!/usr/bin/env python3
"""
patch_cfe_clkfreq.py -- patch the embedded NVRAM defaults inside an ASUS RT-N18U
                        CFE bootloader image so the default CPU/DDR clock is
                        permanently changed.

Reads:  mtd0.bin              (raw dump of the CFE partition, 524288 bytes)
Writes: mtd0.patched.bin      (same size, only the FLSH defaults block changed)

What it changes
---------------
The CFE partition contains a "FLSH" block at offset 0x400 that holds the factory
default NVRAM. CFE merges these defaults into live NVRAM at every boot, which is
why `nvram set clkfreq=...; nvram commit; reboot` does not stick: the FLSH block
overwrites it again.

This script:
  1. Locates the FLSH header at 0x400 (magic "FLSH", little-endian length at +4).
  2. Inside the body, locates the NUL-terminated entry  clkfreq=<old>
  3. Replaces it with  clkfreq=<new>  (size change is allowed: the trailing NUL
     padding inside the FLSH body absorbs/expands accordingly).
  4. Recomputes the Broadcom NVRAM CRC-8 over body bytes [hdr+9 .. hdr+length)
     and writes it to byte 0x408.
  5. Leaves all bytes outside the FLSH region byte-identical (ARM code, the
     LZMA-compressed CFE main, and the trailing 0xff erase fill are untouched).

Defaults: 800,533 -> 1000,800   (1 GHz CPU / 800 MHz DDR; sustained in this
project for >213k stress iterations without panic/throttle).

Usage:
    python3 patch_cfe_clkfreq.py mtd0.bin mtd0.patched.bin
    python3 patch_cfe_clkfreq.py mtd0.bin mtd0.patched.bin --new 1200,800

WARNING: a bad CFE image bricks the board. Take a verified backup first
(see backup_mtd.sh) and read docs/FREQ_TUNING.md before flashing.
"""

import argparse
import struct
import sys

FLSH_OFFSET = 0x400          # FLSH header location inside mtd0
FLSH_MAGIC  = b"FLSH"
CRC_OFFSET  = FLSH_OFFSET + 8  # one byte: CRC-8 over [hdr+9 .. hdr+length)
BODY_START  = FLSH_OFFSET + 0x14   # first key=value byte (after 20-byte hdr)


# Broadcom NVRAM CRC-8 (the same hndcrc8 routine CFE uses to validate FLSH).
# Reflected variant: reflected polynomial 0xab, init 0xff, no xor-out.
# Discovered empirically by brute-forcing all 8-bit CRC parameter combinations
# against the known-good (original) and known-good (patched) FLSH headers.
_CRC8_POLY_REFL = 0xab
_CRC8_INIT      = 0xff


def _build_crc8_table():
    table = []
    for b in range(256):
        c = b
        for _ in range(8):
            c = (c >> 1) ^ _CRC8_POLY_REFL if (c & 1) else (c >> 1)
        table.append(c & 0xff)
    return table


_CRC8_TABLE = _build_crc8_table()


def crc8(data: bytes, init: int = _CRC8_INIT) -> int:
    c = init
    for b in data:
        c = _CRC8_TABLE[(c ^ b) & 0xff]
    return c


def patch(image: bytes, old_value: bytes, new_value: bytes) -> bytes:
    """Return a patched copy of `image` with clkfreq=old_value -> clkfreq=new_value.

    Raises ValueError on any structural mismatch (wrong magic, key not found,
    insufficient slack to hold the new value, etc.).
    """
    if image[FLSH_OFFSET:FLSH_OFFSET + 4] != FLSH_MAGIC:
        raise ValueError(f"FLSH magic not found at 0x{FLSH_OFFSET:x}")

    flsh_len = struct.unpack("<I", image[FLSH_OFFSET + 4:FLSH_OFFSET + 8])[0]
    flsh_end = FLSH_OFFSET + flsh_len
    if flsh_end > len(image):
        raise ValueError(f"FLSH length 0x{flsh_len:x} runs past end of image")

    # Body = the key=value\0 region. Final bytes are zero padding (slack).
    body = bytearray(image[BODY_START:flsh_end])

    old_entry = b"clkfreq=" + old_value + b"\x00"
    new_entry = b"clkfreq=" + new_value + b"\x00"

    idx = body.find(old_entry)
    if idx < 0:
        raise ValueError(f"clkfreq={old_value.decode()} not found in FLSH body")

    delta = len(new_entry) - len(old_entry)
    if delta > 0:
        # Need to consume `delta` trailing NUL slack bytes.
        if body[-delta:] != b"\x00" * delta:
            raise ValueError(
                f"not enough trailing NUL slack to grow entry by {delta} bytes"
            )
        body = body[:idx] + new_entry + body[idx + len(old_entry):-delta]
    elif delta < 0:
        # Shorter -- pad the tail with NULs so the body length is preserved.
        body = (
            body[:idx] + new_entry + body[idx + len(old_entry):] + b"\x00" * (-delta)
        )
    else:
        body = body[:idx] + new_entry + body[idx + len(old_entry):]

    assert len(body) == flsh_end - BODY_START, "body length must be preserved"

    # Reassemble: header (with old CRC placeholder) + new body
    out = bytearray(image)
    out[BODY_START:flsh_end] = body

    # CRC is computed over [hdr+9 .. hdr+length), i.e. starting one byte AFTER
    # the CRC byte itself, all the way through the end of the FLSH region.
    crc_region = bytes(out[CRC_OFFSET + 1:flsh_end])
    out[CRC_OFFSET] = crc8(crc_region)

    return bytes(out)


def main(argv):
    ap = argparse.ArgumentParser(description="Patch CFE clkfreq default.")
    ap.add_argument("input", help="path to original mtd0.bin")
    ap.add_argument("output", help="path to write patched image")
    ap.add_argument("--old", default="800,533",
                    help="current clkfreq value to replace (default: 800,533)")
    ap.add_argument("--new", default="1000,800",
                    help="new clkfreq value to write (default: 1000,800)")
    args = ap.parse_args(argv)

    with open(args.input, "rb") as f:
        image = f.read()

    patched = patch(image, args.old.encode("ascii"), args.new.encode("ascii"))

    with open(args.output, "wb") as f:
        f.write(patched)

    # Quick sanity report
    diffs = sum(1 for a, b in zip(image, patched) if a != b)
    print(f"input  : {args.input}  ({len(image)} bytes)")
    print(f"output : {args.output} ({len(patched)} bytes)")
    print(f"clkfreq: {args.old} -> {args.new}")
    print(f"new CRC-8 at 0x{CRC_OFFSET:x}: 0x{patched[CRC_OFFSET]:02x}")
    print(f"bytes differing: {diffs}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
