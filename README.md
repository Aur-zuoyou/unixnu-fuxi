# ios-on-fuxi

在小米 13（`fuxi` / SM8550）上尝试引导 XNU 的个人项目骨架。

定位：**自己从头搭一遍**，不依赖 HackiOS 那套未公开的代码，复用社区已有的 fuxi UEFI
（Renegade / edk2-msm）作为底座。

## 目录

```
ios-on-fuxi/
├─ appledt/                 设备树工具链（纯 Python，可跑可测）
│  ├─ apple_dt.py           FDT 读写引擎 + Apple 风格 DT 合成 + CLI
│  ├─ profiles/fuxi.json    目标设备树 profile（参数化，未核实字段有标注）
│  └─ selftest.py           11 项自测
├─ tools/                   固件解析（纯 Python，可跑可测）
│  ├─ bootimg.py            boot / vendor_boot / dtbo 解析与抽取
│  ├─ overlay.py            FDT overlay 合并（fdt_overlay_apply 的等价实现）
│  ├─ inventory.py          设备树 -> 引导必需设备清单
│  └─ selftest.py           8 项自测
├─ .github/workflows/       build-loader.yml（没有本地工具链也能编 .efi）
├─ loader/                  EDK2 UEFI Application（代码就位，未编译）
│  ├─ Loader.c
│  ├─ FuxiLoader.inf
│  └─ README.md             构建步骤与已知问题
└─ docs/
   ├─ boot-chain.md         fuxi 引导链、三个交接点、上真机前的待确认清单
   ├─ hardware-notes.md     真机硬件实测数据（内存/GIC/UART/CPU/overlay）
   ├─ hardware-inventory.md 引导必需设备清单（自动生成）
   └─ roadmap.md            M0–M6 里程碑与验收标准
```

## 快速开始

```bash
cd appledt
python selftest.py                       # 应输出 11/11 通过
python apple_dt.py synth profiles/fuxi.json -o fuxi.dtb
python apple_dt.py dump fuxi.dtb
python apple_dt.py from-fdt real.dtb -o again.dtb   # 拿真机 dtb 做往返验证
```

## 现在到哪一步了

| 里程碑 | 状态 |
|---|---|
| M0 工具链 | ✅ 今天完成，自测全过 |
| M1 真机跑 EFI app | ⬜ 等机器（需先刷 Renegade UEFI 并能进 Shell） |
| M2 跳转 payload | ⬜ |
| M3 DT 校准 | ⬜ 需要 iPhone X 的参考 DT（该文件未加密，可合法提取） |
| M4 QEMU 跑 XNU | ⬜ 纯开源路径，不依赖真机 |
| M5 真机 XNU | ⬜ |
| M6 驱动/用户态 | ⬜ |

## 边界

本仓库只做**自己写的代码**：引导器、设备树合成、驱动。

不做：解析/解密/分发苹果加密二进制、绕过激活锁、提供刷机包。

## 下一步（按顺序）

1. 你的 fuxi 能不能进 UEFI Shell？能 → 走 M1；不能 → 先做刷机 + 救砖（确认 9008 可用）
2. 装 aarch64 交叉工具链 + edk2，把 `FuxiLoader.efi` 编出来（首次大概率要修头文件）
3. 真机跑起来，把 `CurrentEL` 和内存图贴回来，M2 的跳转参数才能定
