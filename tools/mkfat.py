"""造一个最小的 FAT16 镜像，里面放 /EFI/UniXNU/ 下的文件。

不依赖 mkfs.fat / mtools：直接按 Microsoft FAT 规范写 BPB、FAT 表、
根目录区和数据区。FAT16 在 32MB 以内足够用，簇大小 2 扇区（1KB）。

用法：
    mkfat.py <out.img> <size_mb>  file1=path1 [file2=path2 ...]

文件路径用 '/' 分隔，目录会自动创建。
"""

import struct
import sys

SECTOR = 512


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 1
    out_path = sys.argv[1]
    size_mb = int(sys.argv[2])
    items = []
    for a in sys.argv[3:]:
        name, path = a.split("=", 1)
        items.append((name.strip("/"), path))

    total_sectors = size_mb * 1024 * 1024 // SECTOR

    # FAT16 布局：保留区 1 扇区，FAT 表 2 张，根目录 512 项（32 扇区）
    reserved = 1
    num_fats = 2
    root_entries = 512
    root_sectors = (root_entries * 32 + SECTOR - 1) // SECTOR
    spc = 2                      # sectors per cluster = 2 -> 1KB clusters
    media = 0xF8

    # 先用一个估计的 FAT 大小，再迭代修正（簇数决定 FAT 表大小）
    fatsz = 1
    for _ in range(8):
        data_start = reserved + num_fats * fatsz + root_sectors
        clusters = (total_sectors - data_start) // spc
        new_fatsz = ((clusters + 2) * 2 + SECTOR - 1) // SECTOR
        if new_fatsz == fatsz:
            break
        fatsz = new_fatsz
    data_start = reserved + num_fats * fatsz + root_sectors
    data_sectors = total_sectors - data_start
    clusters = data_sectors // spc
    assert 4085 < clusters < 65525, "簇数 %d 不落在 FAT16 范围" % clusters

    img = bytearray(total_sectors * SECTOR)

    # ── BPB ──
    bpb = bytearray(SECTOR)
    bpb[0:3] = b"\xEB\x3C\x90"
    bpb[3:11] = b"MSDOS5.0"
    struct.pack_into("<H", bpb, 11, SECTOR)         # bytes per sector
    bpb[13] = spc                                   # sectors per cluster
    struct.pack_into("<H", bpb, 14, reserved)       # reserved sectors
    bpb[16] = num_fats
    struct.pack_into("<H", bpb, 17, root_entries)   # root dir entries
    struct.pack_into("<H", bpb, 19, total_sectors if total_sectors < 65536 else 0)
    bpb[21] = media
    struct.pack_into("<H", bpb, 22, fatsz)
    struct.pack_into("<H", bpb, 24, 32)             # sectors per track
    struct.pack_into("<H", bpb, 26, 64)             # heads
    struct.pack_into("<I", bpb, 28, 0)              # hidden sectors
    struct.pack_into("<I", bpb, 32, total_sectors if total_sectors >= 65536 else 0)
    # Extended boot record
    bpb[36] = 0x80                                  # drive number
    bpb[38] = 0x29                                  # boot signature
    struct.pack_into("<I", bpb, 39, 0x12345678)     # volume id
    bpb[43:54] = b"UNIXNU     "
    bpb[54:62] = b"FAT16   "
    bpb[510:512] = b"\x55\xAA"
    img[0:SECTOR] = bpb

    # ── FAT 表 ──
    fat = bytearray(fatsz * SECTOR)
    struct.pack_into("<HH", fat, 0, 0xFFF8, 0xFFFF)  # 前两项 + 1 号簇（根目录用不到）
    next_free = 2

    def alloc_chain(nbytes):
        nonlocal next_free, fat
        need = max(1, (nbytes + spc * SECTOR - 1) // (spc * SECTOR))
        first = next_free
        for k in range(need):
            c = first + k
            nxt = 0xFFFF if k == need - 1 else c + 1
            struct.pack_into("<H", fat, c * 2, nxt)
        next_free += need
        return first, need

    def cluster_off(c):
        return (data_start + (c - 2) * spc) * SECTOR

    def write_data(first, data):
        off = cluster_off(first)
        img[off:off + len(data)] = data

    # ── 目录结构：先建目录树 ──
    # 收集需要的目录
    dirs = {""}
    for name, _ in items:
        parts = name.split("/")
        for k in range(1, len(parts)):
            dirs.add("/".join(parts[:k]))

    # 根目录在 root_sectors 区，子目录各占一个簇
    dir_clusters = {}
    for d in sorted(dirs):
        if d == "":
            continue
        c, _ = alloc_chain(spc * SECTOR)
        dir_clusters[d] = c

    def diritem(name, attr, cluster, size):
        e = bytearray(32)
        base = name.split(".")[0].upper()[:8].ljust(8)
        ext = (name.split(".")[1].upper()[:3] if "." in name else "").ljust(3)
        e[0:8] = base.encode("ascii")
        e[8:11] = ext.encode("ascii")
        e[11] = attr
        struct.pack_into("<H", e, 26, cluster)
        struct.pack_into("<I", e, 28, size)
        return bytes(e)

    def add_entry(dirpath, name, attr, cluster, size):
        if dirpath == "":
            base_off = reserved * SECTOR + num_fats * fatsz * SECTOR
            region = root_entries * 32
        else:
            base_off = cluster_off(dir_clusters[dirpath])
            region = spc * SECTOR
        entry = diritem(name, attr, cluster, size)
        for k in range(0, region, 32):
            if img[base_off + k] == 0:
                img[base_off + k:base_off + k + 32] = entry
                return True
        raise RuntimeError("目录 %r 满了" % dirpath)

    # 先写 ".." 和 "." 到子目录
    for d, c in dir_clusters.items():
        parent = d.rsplit("/", 1)[0] if "/" in d else ""
        pcluster = 0 if parent == "" else dir_clusters[parent]
        off = cluster_off(c)
        img[off:off + 32] = diritem(".", 0x10, c, 0)
        img[off + 32:off + 64] = diritem("..", 0x10, pcluster, 0)

    # 目录项
    for d in sorted(dirs):
        if d == "":
            continue
        parent = d.rsplit("/", 1)[0] if "/" in d else ""
        leaf = d.rsplit("/", 1)[-1]
        add_entry(parent, leaf, 0x10, dir_clusters[d], 0)

    # 文件
    for name, path in items:
        data = open(path, "rb").read()
        first, _ = alloc_chain(len(data))
        write_data(first, data)
        parent = name.rsplit("/", 1)[0] if "/" in name else ""
        leaf = name.rsplit("/", 1)[-1]
        add_entry(parent, leaf, 0x20, first, len(data))
        print("  %-40s %9d 字节  簇 %d" % (name, len(data), first))

    # FAT 表写到两个副本。切片长度必须和 fat_bytes 长度一致，
    # 否则 bytearray 会插入而不是覆盖，整个镜像会错位。
    fat_bytes = bytes(fat)
    assert len(fat_bytes) == fatsz * SECTOR, (len(fat_bytes), fatsz * SECTOR)
    img[reserved * SECTOR:(reserved + fatsz) * SECTOR] = fat_bytes
    img[(reserved + fatsz) * SECTOR:(reserved + 2 * fatsz) * SECTOR] = fat_bytes

    with open(out_path, "wb") as f:
        f.write(img)
    print("\n%s: %d 字节 (%.1f MB), FAT16, %d 簇, 已用 %d 簇"
          % (out_path, len(img), len(img) / 1048576, clusters, next_free - 2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
