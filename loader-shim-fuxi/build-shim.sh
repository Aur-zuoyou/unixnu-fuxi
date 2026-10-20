#!/bin/bash
# 编译 fuxi shim 并打包成可 fastboot flash 的 boot.img
#
# 环境：Linux + aarch64-linux-gnu-gcc（GitHub ubuntu runner 自带或 apt 装）
set -euo pipefail

cd "$(dirname "$0")"

CC=${CC:-aarch64-linux-gnu-gcc}
OBJCOPY=${OBJCOPY:-aarch64-linux-gnu-objcopy}

CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align -fno-stack-protector -Wall -Wextra"

OUT=out-shim
rm -rf "$OUT"
mkdir -p "$OUT"

SRCS="start.S shim.c console.c mmu.c fdt.c loader.c ufs.c"

echo "=== 编译 ==="
for f in $SRCS; do
  $CC $CFLAGS -c "$f" -o "$OUT/${f%.*}.o"
  echo "  ${f%.*}.o"
done

echo "=== 链接 ==="
$CC $CFLAGS -static -T shim.ld -Wl,--build-id=none -Wl,--no-warn-rwx-segments -Wl,-N \
    "$OUT/start.o" "$OUT/shim.o" "$OUT/console.o" "$OUT/mmu.o" "$OUT/fdt.o" "$OUT/loader.o" "$OUT/ufs.o" \
    -o "$OUT/shim.elf"

$OBJCOPY -O binary "$OUT/shim.elf" "$OUT/Image"

# 用 ELF 段表算镜像总大小（.bss/栈也要在 RAM 里存在）
SIZE=$(python3 -c '
import struct, sys
data = open(sys.argv[1], "rb").read()
shoff, = struct.unpack_from("<Q", data, 0x28)
shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3A)
best = 0
for i in range(shnum):
    off = shoff + i * shentsize
    addr, foff, size = struct.unpack_from("<QQQ", data, off + 0x10)
    end = addr + size
    if end > best:
        best = end
print(best)
' "$OUT/shim.elf")

echo "  Image 补齐到 $SIZE 字节（让 .bss/栈有实体空间）"
truncate -s "$SIZE" "$OUT/Image"

ls -l "$OUT/Image"
echo "=== shim 编译完成 ==="
