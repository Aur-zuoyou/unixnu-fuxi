#!/usr/bin/env python3
"""让 osfmk/arm64/start.s 参与编译。

背景:
  链接时报 `entry point (__start) undefined`，根因是 start.s 没有被汇编，
  构建目录里没有 start.o，LDFILELIST 里也没有它。
  XNU 的文件清单 osfmk/conf/files.arm64 里，start.s 这一行带了平台条件
  （形如 `optional arm64 && IVMAPPLE`），当前平台下条件不成立，于是被排除。

本脚本做的事:
  1. 打印 start.s 那一行的原文（便于确认条件到底是什么）
  2. 去掉该行末尾的条件限定，只保留架构限定
  3. 再打印改后的行

用法: python3 xnu-stubs/patch_files_arm64.py <xnu 目录>
"""

import re
import sys
from pathlib import Path

TARGETS = [
    "osfmk/conf/files.arm64",
    "osfmk/conf/files",
]


def patch(path: Path) -> bool:
    if not path.exists():
        print(f"跳过（不存在）: {path}")
        return False

    lines = path.read_text(encoding="utf-8", errors="replace").split("\n")
    changed = False

    for i, line in enumerate(lines):
        if "start.s" not in line or line.lstrip().startswith("#"):
            continue
        if "arm64/start.s" not in line:
            continue

        print(f"原始第 {i + 1} 行: {line}")

        # 去掉 && IVMAPPLE / && VMAPPLE 之类的平台条件，只留 arm64
        new = re.sub(r"\s*&&\s*[!A-Z_0-9]*IVMAPPLE[A-Z_0-9]*", "", line)
        new = re.sub(r"\s*&&\s*[!A-Z_0-9]*VMAPPLE[A-Z_0-9]*", "", new)
        new = re.sub(r"\s*&&\s*[!A-Z_0-9]*XNU_TARGET_OS_[A-Z_0-9]*", "", new)

        if new == line:
            # 兜底：把 optional 之后的条件整段删掉，只保留 arch
            m = re.match(r"^(\s*\S+\s+optional\s+)(arm64)(\s.*)?$", line)
            if m:
                new = m.group(1) + m.group(2) + "\n"

        if new != line:
            lines[i] = new.rstrip("\n")
            print(f"改后第 {i + 1} 行: {lines[i]}")
            changed = True
        else:
            print(f"第 {i + 1} 行未改动（条件形式未识别，请查看上面的原文）")

    if changed:
        path.write_text("\n".join(lines), encoding="utf-8")
        print(f"已写入 {path}")
    return changed


def main() -> int:
    if len(sys.argv) != 2:
        print("用法: patch_files_arm64.py <xnu 目录>")
        return 2
    root = Path(sys.argv[1])
    if not root.is_dir():
        print(f"目录不存在: {root}")
        return 2

    any_change = False
    for rel in TARGETS:
        any_change |= patch(root / rel)

    if not any_change:
        print("警告: 没有匹配到 start.s 的条件行，需要人工确认 files.arm64 的内容")
    return 0


if __name__ == "__main__":
    sys.exit(main())
