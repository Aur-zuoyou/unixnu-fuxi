#!/usr/bin/env python3
"""打包 Android boot image（header v2），给 fuxi shim 用。

布局（与 mkbootimg v2 一致）：
  [header][kernel][ramdisk][second][dtb]，每段按 page_size 对齐。
地址字段：base=0，kernel_offset=0x8000，ramdisk_offset=0x1000000，
          tags_offset=0x100，dtb_offset=0x1f00000（和 ios-sweet 一致）

用法:
  mkshim_bootimg.py --kernel Image --ramdisk payload.bin --dtb fuxi_runtime.dtb \
                    --output boot-fuxi-shim.img [--cmdline "console=null"]
"""

import argparse
import struct
import sys

PAGE = 4096
KERNEL_OFFSET = 0x8000
RAMDISK_OFFSET = 0x1000000
TAGS_OFFSET = 0x100
DTB_OFFSET = 0x1F00000
BOARD_NAME = b"fuxi"


def pad(data: bytes, page: int = PAGE) -> bytes:
    rem = len(data) % page
    return data if rem == 0 else data + b"\0" * (page - rem)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--kernel", required=True)
    ap.add_argument("--ramdisk", required=True)
    ap.add_argument("--dtb", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--cmdline", default="console=null")
    ap.add_argument("--header-version", type=int, default=2)
    a = ap.parse_args()

    kernel = open(a.kernel, "rb").read()
    ramdisk = open(a.ramdisk, "rb").read()
    dtb = open(a.dtb, "rb").read()

    cmdline = a.cmdline.encode() + b"\0"
    extra = b"\0" * 1024
    name = BOARD_NAME.ljust(16, b"\0")
    ident = b"\0" * 32

    # v2 header：magic + 10 个 u32 + name(16) + cmdline(512) + id(32) + extra(1024)
    hdr = struct.pack(
        "<8s10I16s512s32s1024s",
        b"ANDROID!",
        len(kernel),            # kernel_size
        KERNEL_OFFSET,          # kernel_addr
        len(ramdisk),           # ramdisk_size
        RAMDISK_OFFSET,         # ramdisk_addr
        0,                      # second_size
        0,                      # second_addr
        TAGS_OFFSET,            # tags_addr
        PAGE,                   # page_size
        a.header_version,       # header_version
        0,                      # os_version
        name,
        cmdline.ljust(512, b"\0"),
        ident,
        extra,
    )
    hdr = hdr.ljust(PAGE, b"\0")

    out = hdr + pad(kernel) + pad(ramdisk) + pad(dtb)
    open(a.output, "wb").write(out)

    print(f"kernel   : {len(kernel):>10,} bytes")
    print(f"ramdisk  : {len(ramdisk):>10,} bytes")
    print(f"dtb      : {len(dtb):>10,} bytes")
    print(f"boot.img : {len(out):>10,} bytes -> {a.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
