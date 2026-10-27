#!/usr/bin/env python3
"""Build the final fuxi boot.img carrying a full XNUP payload as ramdisk.

Inputs:
  --input  boot-fuxi.img produced by the patched edk2-msm build
  --xnup   full XNUP payload (NO "usb" entry; must contain "kernel")
  --output xnu-usb.img

The input boot image supplies the 144-byte fuxi BootShim, the 32-MiB UEFI
FV and the appended DTB.  The output keeps those bytes unchanged and changes
only the Android ramdisk field to the XNUP payload, loaded at 0xF0000000.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import struct
import zlib
from pathlib import Path

PAGE = 2048
SHIM_SIZE = 144
FV_SIZE = 0x02000000
KERNEL_ADDR = 0x10000000
RAMDISK_ADDR = 0xF0000000
TAGS_ADDR = 0x10000000
OS_VERSION = 0x1A00016B
PAYLOAD_MAX = 0x04000000


def pad(data: bytes, page: int = PAGE) -> bytes:
    rem = len(data) % page
    return data if rem == 0 else data + b"\0" * (page - rem)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_xnup(data: bytes) -> list[str]:
    if len(data) < 32 or data[:4] != b"XNUP":
        raise SystemExit("XNUP: bad magic")
    body_size = struct.unpack_from("<I", data, 4)[0]
    if body_size != len(data) - 8:
        raise SystemExit(f"XNUP: size field {body_size} != {len(data) - 8}")
    if len(data) > PAYLOAD_MAX - 0x1000:
        raise SystemExit(f"XNUP: too large ({len(data)} bytes)")

    names: list[str] = []
    p = 8
    while p + 24 <= len(data):
        name = data[p:p + 16].split(b"\0", 1)[0]
        if not name:
            break
        esize = struct.unpack_from("<I", data, p + 16)[0]
        name_s = name.decode("ascii", "replace")
        names.append(name_s)
        p += 24 + ((esize + 15) & ~15)
    if "usb" in names:
        raise SystemExit("XNUP: refusing a payload with a 'usb' entry (QCOM USB device protocol is absent)")
    if "kernel" not in names:
        raise SystemExit(f"XNUP: no 'kernel' entry; entries={names}")
    return names


def extract_input_boot(data: bytes) -> tuple[bytes, bytes, bytes]:
    if data[:8] != b"ANDROID!":
        raise SystemExit("input: not an Android boot image")
    page = struct.unpack_from("<I", data, 36)[0]
    version = struct.unpack_from("<I", data, 40)[0]
    if page != PAGE or version != 1:
        raise SystemExit(f"input: expected boot v1/page 2048, got v{version}/page {page}")
    kernel_size = struct.unpack_from("<I", data, 8)[0]
    kernel_field = data[PAGE:PAGE + kernel_size]
    dec = zlib.decompressobj(31)
    raw = dec.decompress(kernel_field) + dec.flush()
    if not dec.eof:
        raise SystemExit("input: gzip stream did not end")
    unused = dec.unused_data
    if len(unused) < 8 or unused[:4] != b"\xd0\x0d\xfe\xed":
        raise SystemExit("input: appended DTB missing")
    dtb_size = struct.unpack_from(">I", unused, 4)[0]
    if dtb_size < 8 or dtb_size > len(unused):
        raise SystemExit("input: bad appended-DTB size")
    if unused[dtb_size:] and set(unused[dtb_size:]) != {0}:
        raise SystemExit("input: non-zero bytes after appended DTB")
    dtb = unused[:dtb_size]
    if len(raw) != SHIM_SIZE + FV_SIZE:
        raise SystemExit(f"input: raw size {len(raw)} != {SHIM_SIZE + FV_SIZE}")
    shim, fv = raw[:SHIM_SIZE], raw[SHIM_SIZE:]
    if fv[40:44] != b"_FVH":
        raise SystemExit("input: FV signature missing at +0x28")
    if struct.unpack_from("<Q", fv, 32)[0] != FV_SIZE:
        raise SystemExit("input: FV length is not 32 MiB")
    return shim, fv, dtb


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True, type=Path, help="boot-fuxi.img from edk2-msm build")
    ap.add_argument("--xnup", required=True, type=Path, help="full XNUP payload (no usb entry)")
    ap.add_argument("--output", required=True, type=Path)
    a = ap.parse_args()

    boot = a.input.read_bytes()
    xnup = a.xnup.read_bytes()
    entries = parse_xnup(xnup)
    shim, fv, dtb = extract_input_boot(boot)

    gz = gzip.compress(shim + fv, compresslevel=6, mtime=0)
    region = gz + dtb
    ident = hashlib.sha1(region + xnup).digest()
    hdr = struct.pack(
        "<8s10I16s512s32s1024s",
        b"ANDROID!",
        len(region),
        KERNEL_ADDR,
        len(xnup),
        RAMDISK_ADDR,
        0,
        0,
        TAGS_ADDR,
        PAGE,
        1,
        OS_VERSION,
        b"\0" * 16,
        b"\0" * 512,
        ident + b"\0" * 12,
        b"\0" * 1024,
    ).ljust(PAGE, b"\0")
    image = hdr + pad(region) + pad(xnup)
    a.output.write_bytes(image)

    print(f"input boot : {len(boot):>10,} bytes  sha256={sha256(boot)}")
    print(f"BootShim   : {len(shim):>10,} bytes  sha256={sha256(shim)}")
    print(f"FV         : {len(fv):>10,} bytes  sha256={sha256(fv)}")
    print(f"DTB        : {len(dtb):>10,} bytes  sha256={sha256(dtb)}")
    print(f"XNUP       : {len(xnup):>10,} bytes  sha256={sha256(xnup)}  entries={entries}")
    print(f"ramdisk_addr: 0x{RAMDISK_ADDR:08x}  ramdisk_size={len(xnup):,}")
    print(f"output     : {len(image):>10,} bytes -> {a.output}")
    print(f"output sha256: {sha256(image)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
