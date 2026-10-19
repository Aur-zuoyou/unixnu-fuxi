# 当前进度与架构（2026-10-19 晚）

## 里程碑

| # | 目标 | 状态 |
|---|---|---|
| M0 | 工具链与骨架 | ✅ |
| M0.5 | 固件解析 + 真机设备树（4852 节点） | ✅ |
| M1 | 真机运行我们的代码（EL1 / GOP / 帧缓冲） | ✅ |
| M2 | `ExitBootServices` 成功 | ✅ |
| M3 | 裸机自绘文字 | ✅ |
| M4 | 内核交接（x0 = 设备树） | ✅ |
| M5 | 独立内核运行（`KERNEL RUNNING`） | ✅ |
| M6 | 自建 XNU 编译 | ✅ `kernel.development.fuxi` 16.4MB |
| **M7** | **XNU 在真机上产生输出** | 🟡 **加载器全链路打通，内核已进入 `_start`** |
| M8 | 平台层移植（显示/中断/存储） | ⬜ |

## M7 具体到哪一步

```
✅ UEFI 启动 → 探针驱动执行
✅ 从固件卷（FV）RAW section 读到 XNU payload（15.8MB）
✅ EFI AllocatePages 拿内核内存，并向上对齐到 32MB
✅ 按段拷贝内核（phys_base + (vmaddr - virt_base)）
✅ 改写 ADT 的 dram-base/dram-size
✅ 填 boot_args（Revision/Version=2、virtBase、physBase、memSize、
   topOfKernelData、Video、deviceTreeP(VA)、CommandLine）
✅ 清缓存 → 关 MMU/缓存 → 跳 __start
✅ 内核第一条指令执行了（绿色标记验证）
❌ 卡在 start_first_cpu：建页表 / 开 MMU 那一段（蓝色标记未出现）
```

## 关键架构决定：改用 ios-sweet 路线

参考 [dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet)：
他们把 xnu-8796 移植到骁龙 732G（sweet）并跑起 iOS 16 用户态。

我们沿用的部分：

| 项 | 我们的做法 |
|---|---|
| 构建 | `blacktop/darwin-xnu-build` + 补丁序列（钉住 DXB_SHA） |
| 板级 | 补丁里的 `SWEET` 全部改名 `FUXI`，`config/MASTER.arm64.fuxi` |
| payload | `mkadt`（生成 Apple 设备树）+ `mkpayload`（打包段表 + ADT + 命令行） |
| 交接约定 | 段拷贝公式、boot_args 布局、关 MMU 后 `x0 = boot_args` 跳 `__start` |

我们和他们不同的部分（因为我们在 UEFI 里）：

| 项 | ios-sweet | 我们 |
|---|---|---|
| 引导者 | 裸机 shim（自己建 16K 页表） | edk2 UEFI（用固件的 4K 页表） |
| payload 来源 | ABL ramdisk | 固件卷 RAW section |
| 内核内存 | 固定 0x1_00000000 | `AllocatePages` + 32MB 对齐 |

## 硬件常量（fuxi / SM8550，实测）

| 项 | 值 |
|---|---|
| 帧缓冲 | `0xB0000000`，1080×2400，stride 4320 字节，BGRX，0x9E4000 |
| GICv3 distributor | `0x17100000`（0x10000） |
| GICv3 redistributor | `0x17180000`（0x200000） |
| UFS 主机 | `0x01d84000`（0x3000） |
| 定时器 | 19.2 MHz，虚拟定时器 INTID **19**（高通编号） |
| DRAM 基址 | `0x80000000` |
| SMP | 8 核：1×X3 + 4×A715 + 3×A510 |

## 已导入的 XNU 补丁（xnu/patches/）

| # | 内容 |
|---|---|
| 0001-0002 | 新 clang 兼容（去掉已删除的编译项、UIO_STACKBUF backport） |
| 0003 | 帧缓冲串口控制台（从 ADT 的 `fuxi-fbcon` 节点取地址） |
| 0004 | 机器配置：`pexpert/arm64/FUXI.h` + proc_reg/sleh/start.s/pe_fiq 适配 |
| 0005 | 非 PAC 构建修正 |
| 0006 | `config/MASTER.arm64.fuxi`（打开 `nos_arm_asm`/`nos_arm_pmap`，去掉 kpc） |
| 0007 | Apple 私有头替身（`ppl/sart.h`、`ppl/uat.h`、`tunables/tunables.s`） |
| 0008 | 闭源库缺口填补（kpc 替身、amcc_rorgn、caches_asm） |
| 0009 | 去掉 Apple SoC 专属导出 |
| 0010 | 早期帧缓冲调试（`fuxi_early_mark`，地址已改 0xB0000000） |
| 0011 | physmap 对齐 |
| 0012 | TTBR0 切换后保持调试控制台（地址已改 0xB0000000） |
| 0013-0015 | 无 corecrypto 的随机数、HMAC PRNG、trust cache |
| 0016 | 设备树平台专家 |
| 0017 | 设备树虚拟定时器 IRQ |
| 0018 | 启动 CPU 驱动 |
| 0020 | MMU 开启后的蓝色标记（我们自己加的调试探针） |

## 加载器构成（loader/）

| 文件 | 作用 |
|---|---|
| Probe.c | 探针主体（GOP、自绘字体、屏幕日志、EBS、跳转调度） |
| XnuBoot.c | payload 解析、段拷贝、boot_args、清缓存、跳转 |
| XnuLoad.c | EFI_BLOCK_IO 读 payload（本固件无块设备，未用上） |
| XnuFv.c | **固件卷 RAW section 读 payload（当前使用）** |
| XnuMem.c | AllocatePages 取内存（32MB 对齐）、改写 ADT 的 dram-base |
| XnuMmu.c | 自己建页表（试过，会搞乱 UEFI 属性，已停用） |
| XnuUfs.c | 直接读 UFS（MMIO 未映射，已停用） |
| xnu_jump.S | 清缓存 / 关 MMU / 跳转 / 自测桩 |

## 踩过的坑（按发生顺序）

| 现象 | 根因 | 解法 |
|---|---|---|
| UEFI 一加载固件就 `Asynchronous Exception` | 16MB 模块超出加载能力 | 内核改放 FV RAW section |
| `BLK HANDLES = 0` | 固件没编 UFS/块设备驱动 | 不走存储，走 FV |
| 直接读 UFS `0x1d84000` 崩溃 | 该 MMIO 区未映射 | 放弃这条路 |
| 拷贝段时 `external abort` | ADT 硬编码 dram-base=0x100000000 | AllocatePages 取 4GB 以下内存 |
| 接管 TTBR0 后立刻崩 | 改 MAIR/TCR 把 UEFI 属性语义搞乱 | 不接管，直接用 UEFI 内存 |
| 屏幕日志文字叠在一起 | 行距 24 < 字高 48 | 行距改 60，字号 6→4 |
| 少量像素不出现在屏幕上 | 帧缓冲 cacheable | 绘制后按缓存行回扫 |
| 跳转后屏幕无变化 | 内核早期日志写死 sweet 的 `0x9c000000` | 改成 `0xB0000000` |
| 绿带（MMU 关）出现、蓝带（MMU 开）不出现 | physBase 未 32MB 对齐 | AllocatePages 后向上对齐 32MB |

## 下一步

1. 验证 32MB 对齐后蓝带是否出现（MMU 能否打开）
2. 蓝带通过 → 把标记推进到 `arm_init` / `PE_init_platform`
3. 内核能自己打印（`fuxi_early_mark`）→ **M7 达成**

---

## 明天（2026-10-20）从这里开始

### 自动在跑的东西

`
xnu 补丁变 → build-xnu（macOS，约 8 分钟）
          → 完成后自动触发 build-fuxi-uefi（workflow_run）
          → 产出 fuxi-uefi-N.zip（含 boot-fuxi.img，内核从最新 xnu-kernel 产物取）
`

所以只需要：**明天打开 Actions，下载最新的 fuxi-uefi-N.zip**，然后刷。

### 昨晚最后一版改了什么（commit 2f30e94）

| 改动 | 原因 |
|---|---|
| 删除 0020（蓝色标记补丁） | 它一直 apply 失败，从来没生效过 |
| 0021 的黄色标记移到 start_first_cpu 第一条指令 | 原来插在 msr OSLAR_EL1, xzr 之后；OSLAR_EL1 是 Apple 专有寄存器，在骁龙上可能 UNDEF，标记永远执行不到 |

### 刷机后怎么判读（三种颜色 = 三个检查点）

| 屏幕 | 说明 | 下一步 |
|---|---|---|
| 黄带 y=500 | 进入 start_first_cpu（OSLAR 那条过去了） | 看品红 |
| 黄 + 品红 y=560 | 页表建完，即将进 common_start | 死在 MMU 使能（TCR/TTBR/SCTLR） |
| 再看有没有英文字 | 内核自己的 uxi_early_mark 开始打印 | **M7 达成** |

- 只有黄 → 死在 start_first_cpu 中段（VBAR/栈/读 boot_args/建页表）
- 黄+品红、无字 → 死在 common_start，重点查 sysreg_restore 里的 TCR 值是否适合高通（Apple 用 16K 页，TCR 可能带 Apple 专属位）

### 已知待办

1. 蓝带标记需要重新做一个（0020 失败了），等黄/品红确认后再加
2. ADT 用的 om/xnu/fuxi.adt（来自 kalama.adt）：节点名已改 fuxi-*，dram 锚点已改
3. ADT PATCHED 只报了 1（预期 2），说明 dram-size 没匹配上——需要在 XnuMem.c 里补上 kalama.adt 用的 size 值，或者改成按属性名定位而不是按值替换
