// xnu_jump.S —— 关 MMU / 清缓存 / 跳进 XNU
//
// 两个函数：
//   AsmXnuDcacheCleanRange(x0 = start, x1 = end)   按 VA 清 D-cache
//   AsmXnuJump(x0 = entry_pa, x1 = boot_args_pa)   关 MMU/缓存后跳转，x0 = boot_args

.text

// ---------------------------------------------------------------------------
// void AsmXnuDcacheCleanRange(UINTN start, UINTN end)
// ---------------------------------------------------------------------------
.global AsmXnuDcacheCleanRange
.type   AsmXnuDcacheCleanRange, %function
AsmXnuDcacheCleanRange:
    mrs     x2, ctr_el0
    ubfx    x3, x2, #16, #4         // DminLine = log2(行字节数) - 2
    mov     x4, #4
    lsl     x4, x4, x3              // 行字节数
    sub     x5, x4, #1
    bic     x0, x0, x5              // 对齐到行首
1:
    dc      cvac, x0
    add     x0, x0, x4
    cmp     x0, x1
    b.lo    1b
    dsb     sy
    ret
.size   AsmXnuDcacheCleanRange, . - AsmXnuDcacheCleanRange

// ---------------------------------------------------------------------------
// void AsmXnuJump(UINTN entry_pa, UINTN boot_args_pa)
// ---------------------------------------------------------------------------
.global AsmXnuJump
.type   AsmXnuJump, %function
AsmXnuJump:
    msr     daifset, #0xf           // 关全部中断
    mov     x3, x0                  // entry
    mov     x4, x1                  // boot_args

    // 清 I-cache（整条流水线），并同步
    ic      iallu
    dsb     sy
    isb

    // 关 MMU 与缓存：SCTLR_EL1.M/A/C/I = 0
    mrs     x2, sctlr_el1
    bic     x2, x2, #0x1            // M
    bic     x2, x2, #0x2            // A
    bic     x2, x2, #0x4            // C
    bic     x2, x2, #0x1000         // I
    msr     sctlr_el1, x2
    isb
    dsb     sy

    // x0 = boot_args 物理地址，跳到入口
    mov     x0, x4
    br      x3
.size   AsmXnuJump, . - AsmXnuJump
