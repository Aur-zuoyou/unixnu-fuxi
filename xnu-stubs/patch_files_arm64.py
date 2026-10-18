#!/usr/bin/env python3
"""让 osfmk/arm64/start.s 参与编译（只改构建清单，不动业务代码）。

问题链:
  链接报 `entry point (__start) undefined`
  → start.s 没被汇编
  → osfmk/conf/files.arm64 里这一行是:
        osfmk/arm64/start.s   optional   nos_arm_asm
    `nos_arm_asm`（"不要 ARM 汇编"）在 VMAPPLE 构建下被打开，于是被排除

处理:
  把 nos_arm_asm 换成 arm64（config 要求 optional 后面必须有条件）。
  但**只对在源码树里真实存在的文件这么做**——有些文件（如 kpc.c）是 Apple
  私有实现，开源树里没有，放开后会变成
        No rule to make target 'osfmk/arm64/kpc.c'
  所以逐个检查文件是否存在。

用法: python3 xnu-stubs/patch_files_arm64.py <xnu 目录>
"""

import re
import sys
from pathlib import Path

LIST_FILES = ["osfmk/conf/files.arm64", "osfmk/conf/files"]


def patch_one(root: Path, list_path: Path) -> int:
    if not list_path.exists():
        print(f"跳过（不存在）: {list_path}")
        return 0

    lines = list_path.read_text(encoding="utf-8", errors="replace").split("\n")
    changed = 0

    for i, line in enumerate(lines):
        if line.lstrip().startswith("#"):
            continue
        if "nos_arm_asm" not in line:
            continue

        parts = line.split()
        if not parts:
            continue
        src_rel = parts[0]

        # 文件不存在就跳过（Apple 私有实现，放开会报 No rule to make target）
        if not (root / src_rel).exists():
            print(f"跳过（源码不存在）: {src_rel}")
            continue

        new = line
        new = re.sub(r"(?<![A-Za-z_0-9])nos_arm_asm(?![A-Za-z_0-9])", "", new)
        new = re.sub(r"&&\s*$", "", new).rstrip()
        new = re.sub(r"[ \t]+", "\t", new)
        if re.search(r"\boptional\s*$", new):
            new = re.sub(r"\boptional\s*$", "optional\tarm64", new)

        if new != line:
            print(f"第 {i + 1} 行: {line}")
            print(f"     -> : {new}")
            lines[i] = new
            changed += 1

    if changed:
        list_path.write_text("\n".join(lines), encoding="utf-8")
        print(f"{list_path}: 修改 {changed} 行")
    return changed


def main() -> int:
    if len(sys.argv) != 2:
        print("用法: patch_files_arm64.py <xnu 目录>")
        return 2
    root = Path(sys.argv[1])
    if not root.is_dir():
        print(f"目录不存在: {root}")
        return 2

    total = 0
    for rel in LIST_FILES:
        total += patch_one(root, root / rel)

    print()
    print(f"共修改 {total} 行")
    if total == 0:
        print("警告: 没有修改任何行，需要人工确认 files.arm64")
    return 0


if __name__ == "__main__":
    sys.exit(main())
