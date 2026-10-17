# 路线图

原则：每个里程碑都有**可验证的验收标准**，做不到就停在那一步，不往下堆假设。

| # | 目标 | 验收标准 | 估算 | 依赖 |
|---|---|---|---|---|
| **M0** | 工具链与骨架 | `appledt` 自测全过；能合成合法 DTB；loader 代码就位 | ✅ 已完成 | 无 |
| **M0.5** | 固件解析 + 真机设备树 | 从 vendor_boot/dtbo 抽出 DT 并合并成 4852 节点、校验通过 | ✅ 已完成 | 无 |
| **M1** | fuxi 上跑起我们的 EFI app | UEFI Shell 里执行后，屏幕打印出 `CurrentEL`、内存描述符数量、DTB 地址 | 1–3 天（**卡在 aarch64 工具链**） | 刷 Renegade UEFI + 能编译 |
| **M2** | 装载并跳转平坦 payload | 跳转后看到 payload 打印的一行字（UART 或 ramoops） | 1–2 周 | M1 |
| **M3** | 设备树校准 | 生成的 DT 与 iPhone X 参考 DT 的节点/属性差异清单收敛到"仅硬件相关" | 1–2 周 | 参考 DT |
| **M4** | QEMU virt 上跑通 XNU | 虚拟机里看到 XNU 早期启动日志（不是 panic） | 1–2 月 | 无（纯开源） |
| **M5** | 真机 XNU 早期启动 | fuxi 上 XNU 进入 `PE_init` 之后，panic 信息可读 | 不确定 | M2+M3+M4 |
| **M6** | 驱动与用户态 | 能显示、能输入、能跑第一个用户态进程 | 季度级 | M5 |

## M0（本仓库当前状态）

- [x] `appledt/apple_dt.py`：FDT 读写引擎（序列化/解析/合成/CLI）
- [x] `appledt/profiles/fuxi.json`：参数化 profile，已按实测更新
- [x] `tools/bootimg.py`：boot / vendor_boot / dtbo 解析（含 dtbo 大端这个坑）
- [x] `tools/overlay.py`：FDT overlay 合并（phandle 重编号 + local/external fixup）
- [x] `tools/inventory.py`：从合并树生成引导必需设备清单
- [x] `loader/`：EDK2 应用 + DSC + GitHub Actions 构建流程
- [x] `docs/boot-chain.md` / `hardware-notes.md` / `hardware-inventory.md`

## M0.5 的结果

`rom/out/fuxi_merged.dtb`：4852 节点，无重复 phandle，无残留 `0xffffffff`。
关键地址：DRAM `0x80000000`、GIC `0x17100000/0x17180000`、调试 UART `0xa9c000`、
PSCI `smc`、定时器 `/soc/timer`。

## 已知阻塞

本机没有 aarch64 工具链（clang/gcc/WSL 全无，网络下载被拦），所以 `.efi` 编不出来。
两条出路：
1. 推到 GitHub，用 `.github/workflows/build-loader.yml` 在 Actions 里编（推荐，零本地依赖）
2. 本地装 `aarch64-linux-gnu-` + edk2 BaseTools

## M1 的具体步骤

1. 装好 Renegade UEFI，能进 UEFI Shell（这一步本身就是独立工程：解锁 BL、刷 boot、
   处理 AVB、可能还要 `fastboot --disable-verity`）
2. 把 `fuxi.dtb` 和编译出来的 `FuxiLoader.efi` 放 ESP 根目录
3. `FuxiLoader.efi` → 记录打印内容
4. 若黑屏/重启：改用 ramoops 写日志，或先确认 ESP 路径规则

## M3 的参考数据

设备树（`DeviceTree.*.im4p`）在 IPSW 里**通常是未加密**的，只有签名，所以格式解析 + 提取
不涉及解密。用它校准 `profiles/fuxi.json` 里的 `_unverified` 字段是合法且必要的一步。
（内核缓存/文件系统是另一回事：加密，本仓库不碰。）

## 明确不做的事

- 不解析、不解密、不分发苹果的加密二进制（kernelcache、根文件系统）
- 不绕过激活锁、不做"解锁服务"
- 不提供"一键刷机包"
