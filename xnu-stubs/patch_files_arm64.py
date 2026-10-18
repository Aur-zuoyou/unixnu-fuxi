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
        if line.lstrip().startswith("#"):
            continue
        # 处理所有带 nos_arm_asm 的行（不只 start.s），
        # 因为该标记会排除整个 arm64 汇编集合
        if "nos_arm_asm" not in line:
            continue

        print(f"原始第 {i + 1} 行: {line}")

        # 实测这一行是: osfmk/arm64/start.s  optional  nos_arm_asm
        # nos_arm_asm = "不要 ARM 汇编" 标记，VMAPPLE 构建时被打开，
        # 导致所有 arm64 汇编（含 start.s）被排除。
        # 处理办法：把这个标记从条件里删掉，只保留 optional（= 恒编译）。
        new = line
        for token in ("nos_arm_asm", "IVMAPPLE", "VMAPPLE"):
            new = re.sub(r"(?<![A-Za-z_0-9])" + token + r"(?![A-Za-z_0-9])", "", new)
        new = re.sub(r"\s*&&\s*$", "", new)          # 去掉行尾残留的 &&
        new = re.sub(r"&&\s+", "", new)              # 去掉中间残留的 &&
        new = re.sub(r"[ \t]+", "\t", new).rstrip()  # 规整空白（文件用 TAB 分隔）

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

    # 找出 nos_arm_asm 在哪里被定义（便于彻底关掉它）
    print()
    print("== nos_arm_asm 的定义位置 ==")
    import subprocess
    try:
        out = subprocess.run(
            ["grep", "-rn", "nos_arm_asm", str(root / "makedefs"), str(root / "osfmk" / "conf")],
            capture_output=True, text=True)
        for ln in (out.stdout or "").split("\n")[:20]:
            if ln.strip():
                print("  ", ln)
    except Exception as exc:      # noqa: BLE001
        print("  (grep 失败:", exc, ")")
    return 0


if __name__ == "__main__":
    sys.exit(main())
