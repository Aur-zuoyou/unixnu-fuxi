#!/usr/bin/env python3
"""把 ios-sweet 的 kalama.adt 改成 fuxi 能用的版本。

做三件事：
  1. 节点名 / 属性值里的 sweet* -> fuxi*
     （内核补丁要求节点名必须是 "fuxi-fbcon"；UFS 节点同理）
  2. chosen 的 dram-base / dram-size 改成占位值 0x100000000 / 0xfcc00000
     这两个值是加载器运行时替换的锚点（见 loader/XnuMem.c 的 XnuPatchAdt）。
  3. gic 节点的 reg 从"相对 /soc 的偏移"改成绝对物理地址。
     kalama.adt 里是 0x16100000（= 0x17100000 - 0x01000000），而我们的
     平台专家补丁不做 ranges 转换，必须给绝对地址。

用法: adt_fix_fuxi.py <输入.adt> <输出.adt> [--dump]
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import iosadt as adt  # noqa: E402

OLD = b"sweet"
NEW = b"fuxi"

DRAM_BASE = 0x100000000
DRAM_SIZE = 0xfcc00000

GICD, GICD_SIZE = 0x17100000 - 0x01000000, 0x10000   # 相对 /soc（XNU 会加 soc_base_phys=0x01000000）
GICR, GICR_SIZE = 0x17180000 - 0x01000000, 0x200000


def node_name(node):
    """节点名 = 第一个属性的值（截到 NUL）"""
    if not node.props:
        return b""
    return node.props[0][2].split(b"\x00")[0]


def rename_in_tree(node):
    """把所有 prop 的值里的 sweet 换成 fuxi"""
    n = 0
    for i, (key, flags, val) in enumerate(node.props):
        if isinstance(val, (bytes, bytearray)) and OLD in val:
            node.props[i] = (key, flags, val.replace(OLD, NEW))
            n += 1
    for child in node.children:
        n += rename_in_tree(child)
    return n


def set_chosen_dram(node):
    """把 /chosen 的 dram-base / dram-size 改成加载器替换用的锚点值"""
    if node_name(node) == b"chosen":
        for i, (key, flags, val) in enumerate(node.props):
            if key == "dram-base":
                node.props[i] = (key, flags, struct.pack("<Q", DRAM_BASE))
            elif key == "dram-size":
                node.props[i] = (key, flags, struct.pack("<Q", DRAM_SIZE))
        return True
    for c in node.children:
        if set_chosen_dram(c):
            return True
    return False


def set_gic_absolute(node):
    """gic 节点的 reg 改成绝对物理地址"""
    if node_name(node) == b"gic":
        for i, (key, flags, val) in enumerate(node.props):
            if key == "reg":
                node.props[i] = (
                    key,
                    flags,
                    struct.pack("<4Q", GICD, GICD_SIZE, GICR, GICR_SIZE),
                )
                return True
        return False
    for c in node.children:
        if set_gic_absolute(c):
            return True
    return False


FB_BASE, FB_SIZE = 0xB8000000, 0x9E4000   # 裸机阶段真正显示的 splash 帧缓冲


def set_fbcon_reg(node):
    """把 fuxi-fbcon 节点的 reg 强制成裸机可见的帧缓冲"""
    if node_name(node) == b"fuxi-fbcon":
        for i, (key, flags, val) in enumerate(node.props):
            if key == "reg":
                node.props[i] = (key, flags, struct.pack("<2Q", FB_BASE, FB_SIZE))
                return True
        return False
    for c in node.children:
        if set_fbcon_reg(c):
            return True
    return False


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    src, dst = sys.argv[1], sys.argv[2]

    root, _ = adt.parse(open(src, "rb").read())
    renamed = rename_in_tree(root)
    dram_ok = set_chosen_dram(root)
    gic_ok = set_gic_absolute(root)
    fb_ok = set_fbcon_reg(root)
    blob = adt.serialize(root)
    open(dst, "wb").write(blob)

    print(f"改名属性数: {renamed}")
    print(f"chosen dram 已设置: {dram_ok}")
    print(f"gic 改成绝对地址: {gic_ok}")
    print(f"fbcon 帧缓冲: {fb_ok}")
    print(f"输出: {dst}  {len(blob):,} 字节")

    if "--dump" in sys.argv:
        root2, _ = adt.parse(blob)
        adt.dump(root2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
