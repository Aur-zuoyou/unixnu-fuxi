#!/bin/bash
# 编译 fuxi shim 并打包成可 fastboot flash 的 boot.img
#
# 环境：Linux + aarch64-linux-gnu-gcc（GitHub ubuntu runner 自带或 apt 装）
set -euo pipefail

cd "$(dirname "$0")"

CC=${CC:-aarch64-linux-gnu-gcc}
OBJCOPY=${OBJCOPY:-aarch64-linux-gnu-objcopy}
READELF=${READELF:-aarch64-linux-gnu-readelf}
NM=${NM:-aarch64-linux-gnu-nm}

CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align -fno-stack-protector -Wall -Wextra"

OUT=out-shim
rm -rf "$OUT"
mkdir -p "$OUT"

# ufs.c 必须编：shim.c 里引用了 test_ufs
SRCS="start.S shim.c console.c mmu.c fdt.c loader.c ufs.c"

echo "=== 编译 ==="
for f in $SRCS; do
  $CC $CFLAGS -c "$f" -o "$OUT/${f%.*}.o"
  echo "  ${f%.*}.o"
done

echo "=== 链接 ==="
$CC $CFLAGS -static -T shim.ld -Wl,--build-id=none -Wl,--no-warn-rwx-segments -Wl,-N \
    $OUT/start.o $OUT/shim.o $OUT/console.o $OUT/mmu.o $OUT/fdt.o $OUT/loader.o \
    -o "$OUT/shim.elf"

$OBJCOPY -O binary "$OUT/shim.elf" "$OUT/Image"

END_HEX=$($NM "$OUT/shim.elf" | awk '$3=="_end"{print $1}')
SIZE=$((16#$END_HEX))
echo "  Image 补齐到 $_end=$SIZE 字节（让 .bss/栈有实体空间）"
truncate -s "$SIZE" "$OUT/Image"

ls -l "$OUT/Image"
echo "=== shim 编译完成 ==="
