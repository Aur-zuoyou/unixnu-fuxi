#!/usr/bin/env python3
"""给 edk2-msm 的固件打两个实验性补丁，用来让 ExitBootServices 能成功。

补丁 1: RUNTIME_PAGE_ALLOCATION_GRANULARITY 0x10000 -> 1
        EDK2 在 AArch64 上把 runtime 内存粒度定为 64KB，而高通平台按 4KB
        分配 runtime 区，导致 DxeCore 的 CoreTerminateMemoryMap 判定
        "A RUNTIME memory entry is not on a proper alignment" 并返回
        EFI_INVALID_PARAMETER。改成 1 等于跳过这个健全性检查。

补丁 2: CoreTerminateMemoryMap 的 MapKey 比对改为恒真
        CoreExitBootServices 会先通知 EXIT_BOOT_SERVICES 事件，各驱动的
        收尾回调会分配/释放内存，使调用者手里的 MapKey 立即失效。
        实验阶段先跳过比对，确认下游能否跑通。

用法: python3 tools/patch_firmware.py <edk2-msm 目录>
"""

import re
import sys
from pathlib import Path

GRAN_HEADER = "Common/edk2/MdePkg/Include/AArch64/ProcessorBind.h"
DXE_PAGE = "Common/edk2/MdeModulePkg/Core/Dxe/Mem/Page.c"


def patch_granularity(root: Path) -> str:
    p = root / GRAN_HEADER
    if not p.exists():
        return f"补丁1 失败: 找不到 {p}"
    text = p.read_text(encoding="utf-8", errors="replace")
    new, n = re.subn(
        r"#define\s+RUNTIME_PAGE_ALLOCATION_GRANULARITY\s+\S+",
        "#define RUNTIME_PAGE_ALLOCATION_GRANULARITY 1",
        text,
    )
    if n == 0:
        return f"补丁1 失败: {p} 里没有匹配到宏定义"
    p.write_text(new, encoding="utf-8")
    return f"补丁1 成功: 替换 {n} 处 ({p.relative_to(root)})"


def patch_mapkey(root: Path) -> str:
    p = root / DXE_PAGE
    if not p.exists():
        return f"补丁2 失败: 找不到 {p}"
    text = p.read_text(encoding="utf-8", errors="replace")
    if "if (MapKey == mMemoryMapKey)" not in text:
        return f"补丁2 失败: {p} 里没有 MapKey 比对语句"
    new = text.replace("if (MapKey == mMemoryMapKey)", "if (1)")
    p.write_text(new, encoding="utf-8")
    return f"补丁2 成功: 跳过 MapKey 比对 ({p.relative_to(root)})"


def check(root: Path) -> str:
    gran = (root / GRAN_HEADER).read_text(encoding="utf-8", errors="replace")
    page = (root / DXE_PAGE).read_text(encoding="utf-8", errors="replace")
    g = "define RUNTIME_PAGE_ALLOCATION_GRANULARITY 1" in gran
    m = "if (1)" in page
    return f"校验: granularity={int(g)} mapkey={int(m)}"


def main() -> int:
    if len(sys.argv) != 2:
        print("用法: patch_firmware.py <edk2-msm 目录>")
        return 2
    root = Path(sys.argv[1])
    if not root.is_dir():
        print(f"目录不存在: {root}")
        return 2
    print(patch_granularity(root))
    print(patch_mapkey(root))
    print(check(root))
    return 0


if __name__ == "__main__":
    sys.exit(main())
