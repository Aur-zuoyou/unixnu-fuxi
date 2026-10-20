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
import struct
import sys

PAGE = 2048
KERNEL_ADDR = 0x10000000
RAMDISK_ADDR = 0x20000000
TAGS_ADDR = 0x10000000
BOARD_NAME = b"fuxi"


def pad(data: bytes, page: int) -> bytes:
    rem = len(data) % page
    return data if rem == 0 else data + b"\0" * (page - rem)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--kernel", required=True)
    ap.add_argument("--ramdisk", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--cmdline", default="console=null")
    a = ap.parse_args()

    kernel = open(a.kernel, "rb").read()
    ramdisk = open(a.ramdisk, "rb").read()

    cmdline = a.cmdline.encode() + b"\0"
    name = BOARD_NAME.ljust(16, b"\0")

    hdr = struct.pack(
        "<8s10I16s512s32s1024s",
        b"ANDROID!",
        len(kernel),          # kernel_size
        KERNEL_ADDR,          # kernel_addr
        len(ramdisk),         # ramdisk_size
        RAMDISK_ADDR,         # ramdisk_addr
        0,                    # second_size
        KERNEL_ADDR,          # second_addr
        TAGS_ADDR,            # tags_addr
        PAGE,                 # page_size
        1,                    # header_version（fuxi 用 v1）
        0,                    # os_version
        name,
        cmdline.ljust(512, b"\0"),
        b"\0" * 32,           # id
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
