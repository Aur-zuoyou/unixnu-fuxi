#!/usr/bin/env python3
"""打包 fuxi 的 boot image（header v1，page 2048 —— 与 edk2-msm 产出的官方镜像一致）。

fuxi(小米13) 的 ABL 只认这套参数：
  page_size      = 2048
  header_version = 1
  kernel_addr    = 0x10000000
  ramdisk_addr   = 0x20000000   （payload 放这里，ABL 会写进 /chosen linux,initrd-*）
  tags_addr      = 0x10000000

布局：[header(page)][kernel][ramdisk]，每段按 page 对齐。
"""

import argparse
import hashlib
import struct
import sys

PAGE = 2048
KERNEL_ADDR = 0x10000000
RAMDISK_ADDR = 0x10000000
TAGS_ADDR = 0x10000000
OS_VERSION = 0x1A00016B   # 与 UEFI 镜像一致
BOARD_NAME = b"\0" * 16   # 空名字（与 UEFI 镜像一致）


def pad(data: bytes, page: int) -> bytes:
    rem = len(data) % page
    return data if rem == 0 else data + b"\0" * (page - rem)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--kernel", required=True)
    ap.add_argument("--ramdisk", default="")
    ap.add_argument("--output", required=True)
    ap.add_argument("--cmdline", default="")
    a = ap.parse_args()

    kernel = open(a.kernel, "rb").read()
    ramdisk = open(a.ramdisk, "rb").read() if a.ramdisk else b"\0"

    cmdline = a.cmdline.encode() + b"\0"
    name = BOARD_NAME.ljust(16, b"\0")

    sha = hashlib.sha1()
    sha.update(kernel)
    sha.update(ramdisk)
    ident = sha.digest() + b"\0" * (32 - 20)   # 32 字节，前 20 是 SHA1

    hdr = struct.pack(
        "<8s10I16s512s32s1024s",
        b"ANDROID!",
        len(kernel),          # kernel_size
        KERNEL_ADDR,          # kernel_addr
        len(ramdisk),         # ramdisk_size
        RAMDISK_ADDR,         # ramdisk_addr
        0,                    # second_size
        0,                    # second_addr（与 UEFI 镜像一致）
        TAGS_ADDR,            # tags_addr
        PAGE,                 # page_size
        1,                    # header_version（fuxi 用 v1）
        OS_VERSION,           # os_version
        name,
        cmdline.ljust(512, b"\0"),
        ident,                # id = SHA1(kernel+ramdisk)
        b"\0" * 1024,         # extra_cmdline
    )
    hdr = hdr.ljust(PAGE, b"\0")

    out = hdr + pad(kernel, PAGE) + pad(ramdisk, PAGE)
    open(a.output, "wb").write(out)

    print(f"kernel   : {len(kernel):>10,} bytes @ 0x{KERNEL_ADDR:x}")
    print(f"ramdisk  : {len(ramdisk):>10,} bytes @ 0x{RAMDISK_ADDR:x}")
    print(f"boot.img : {len(out):>10,} bytes -> {a.output}")
    print(f"header   : v1, page {PAGE}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
