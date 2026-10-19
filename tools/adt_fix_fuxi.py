#!/usr/bin/env python3
"""把 ios-sweet 的 kalama.adt 改成 fuxi 能用的版本。

做两件事（用户指定）：
  1. 节点名 / 属性值里的 sweet* → fuxi*
     （内核补丁要求节点名必须是 "fuxi-fbcon"；UFS 节点同理）
  2. chosen 的 dram-base / dram-size 改成占位值 0x100000000 / 0xfcc00000
     这两个值是加载器运行时替换的锚点（见 loader/XnuMem.c 的 XnuPatchAdt），
     必须和那里一致；原来的 0xb0000000 会和帧缓冲地址冲突。

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


def rename_in_tree(node):
    """把所有 prop 的值和节点名里的 sweet 换成 fuxi（保持 NUL 分隔的字符串不变形）"""
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
    if (node.props and node.props[0][2].rstrip(b"\0") == b"chosen"):
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


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    src, dst = sys.argv[1], sys.argv[2]

    root, _ = adt.parse(open(src, "rb").read())
    renamed = rename_in_tree(root)
    ok = set_chosen_dram(root)
    blob = adt.serialize(root)
    open(dst, "wb").write(blob)

    print(f"改名属性数: {renamed}")
    print(f"chosen dram 已设置: {ok}")
    print(f"输出: {dst}  {len(blob):,} 字节")

    if "--dump" in sys.argv:
        root2, _ = adt.parse(blob)
        adt.dump(root2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
