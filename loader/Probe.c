/** @file  Probe.c

  fuxi 鎺㈤拡 v11 鈥斺€?M3: 瑁告満 payload 鑷繁鐢诲瓧

  宸茬煡:
    M1: EL1 / GOP 鍙敤 / 鑳芥墦鍗?    M2: ExitBootServices 涔嬪悗浠嶈兘杩愯(鏁村睆鍒疯摑)

  闂: 閫€鍑?Boot Services 鍚庢病鏈夋帶鍒跺彴, 鎯宠緭鍑哄彧鑳借嚜宸卞線鏄惧瓨鐢诲儚绱犮€?  v11 灏卞仛杩欎欢浜? 鍐呯疆 8x8 鐐归樀瀛椾綋, 鏀惧ぇ 6 鍊嶇敾鍒板抚缂撳啿銆?
  棰勬湡灞忓箷: 钃濆簳 + 鐧借壊鏂囧瓧
      M3
      EL1
      FDT=0X........
      SIZE0X........
**/

#include <Uefi.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Protocol/GraphicsOutput.h>

#define SCALE      6
#define LINE_H     (8 * SCALE + 24)

STATIC EFI_GUID  mFdtTableGuid = {
  0xb1b621d5, 0xf19c, 0x41a5, { 0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0 }
};

STATIC UINTN       mFdtAddr;
STATIC UINT32      mFdtSize;
STATIC UINT8      *mFb;
STATIC UINTN       mFbSize;
STATIC UINT32      mFbW;
STATIC UINT32      mFbPitch;
STATIC UINT32      mGopFmt;
STATIC UINT32      mFbH;
STATIC EFI_HANDLE  mImageHandle;
STATIC EFI_STATUS  mLocateStatus;
STATIC VOID       *mMapBuf;
STATIC UINTN       mKernelStack;
STATIC UINT32      mFdtFound;
STATIC UINTN       mCfgCount;
STATIC UINTN       mMapBufSize;

/* ---------------- 8x8 鐐归樀瀛椾綋 ---------------- */

STATIC CONST CHAR8  CHARSET[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ :-=.()x/";

STATIC CONST UINT8  FONT[][8] = {
  { 0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00 }, /* 0 */
  { 0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00 }, /* 1 */
  { 0x3C,0x66,0x06,0x0C,0x30,0x60,0x7E,0x00 }, /* 2 */
  { 0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00 }, /* 3 */
  { 0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00 }, /* 4 */
  { 0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00 }, /* 5 */
  { 0x3C,0x66,0x60,0x7C,0x66,0x66,0x3C,0x00 }, /* 6 */
  { 0x7E,0x66,0x0C,0x18,0x18,0x18,0x18,0x00 }, /* 7 */
  { 0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00 }, /* 8 */
  { 0x3C,0x66,0x66,0x3E,0x06,0x66,0x3C,0x00 }, /* 9 */
  { 0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00 }, /* A */
  { 0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00 }, /* B */
  { 0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00 }, /* C */
  { 0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00 }, /* D */
  { 0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00 }, /* E */
  { 0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00 }, /* F */
  { 0x3C,0x66,0x60,0x6E,0x66,0x66,0x3E,0x00 }, /* G */
  { 0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00 }, /* H */
  { 0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00 }, /* I */
  { 0x1E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0x00 }, /* J */
  { 0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00 }, /* K */
  { 0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00 }, /* L */
  { 0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00 }, /* M */
  { 0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00 }, /* N */
  { 0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00 }, /* O */
  { 0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00 }, /* P */
  { 0x3C,0x66,0x66,0x66,0x6E,0x3C,0x0E,0x00 }, /* Q */
  { 0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0x00 }, /* R */
  { 0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00 }, /* S */
  { 0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00 }, /* T */
  { 0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00 }, /* U */
  { 0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00 }, /* V */
  { 0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00 }, /* W */
  { 0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00 }, /* X */
  { 0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00 }, /* Y */
  { 0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00 }, /* Z */
  { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 }, /* sp */
  { 0x00,0x00,0x18,0x00,0x00,0x18,0x00,0x00 }, /* :  */
  { 0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00 }, /* =  */
  { 0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00 }, /* -  */
  { 0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00 }, /* .  */
  { 0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00 }, /* (  */
  { 0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00 }, /* )  */
  { 0x00,0x66,0x3C,0x18,0x3C,0x66,0x00,0x00 }, /* x  */
  { 0x06,0x0C,0x18,0x30,0x60,0x00,0x00,0x00 }, /* /  */
};

STATIC __attribute__((unused))
UINT64
CurrentEl (
  VOID
  )
{
  UINT64  Val;
  __asm__ __volatile__ ("mrs %0, CurrentEL" : "=r" (Val));
  return (Val >> 2) & 0x3;
}

STATIC __attribute__((unused))
VOID
Halt (
  VOID
  )
{
  for ( ; ; ) {
    __asm__ __volatile__ ("wfi");
  }
}

/** 重新读 GOP 参数。入口那一刻 FrameBufferBase 可能还是 0, 必须晚点再读。 */
STATIC
VOID
RefreshGop (
  VOID
  )
{
  EFI_STATUS                     Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop = NULL;

  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **) &Gop);
  if (EFI_ERROR (Status) || Gop == NULL || Gop->Mode == NULL) {
    return;
  }
  mFb     = (UINT8 *) (UINTN) Gop->Mode->FrameBufferBase;
  mFbSize = (UINTN) Gop->Mode->FrameBufferSize;
  mFbW    = (UINT32) Gop->Mode->Info->HorizontalResolution;
  mFbH    = (UINT32) Gop->Mode->Info->VerticalResolution;
  mGopFmt = (UINT32) Gop->Mode->Info->PixelFormat;

  mFbPitch = mFbW;
  if (mFbH != 0 && (mFbSize % ((UINTN) mFbH * 4)) == 0) {
    UINTN  derived = mFbSize / ((UINTN) mFbH * 4);
    if (derived >= mFbW && derived < 16384) {
      mFbPitch = (UINT32) derived;
    }
  }
}
STATIC __attribute__((unused))
UINTN
GlyphIndex (
  IN CHAR8  Ch
  )
{
  UINTN  i;

  for (i = 0; CHARSET[i] != 0; i++) {
    if (CHARSET[i] == Ch) {
      return i;
    }
  }
  return 36;
}

STATIC __attribute__((unused))
VOID
PutPixel (
  IN UINT32  X,
  IN UINT32  Y,
  IN UINT32  Color
  )
{
  volatile UINT32  *P;

  if (mFb == NULL || X >= mFbW || Y >= mFbH) {
    return;
  }
  P    = (volatile UINT32 *) mFb;
  P   += (UINTN) Y * mFbPitch + X;
  *P   = Color;
}

STATIC __attribute__((unused))
VOID
Fill (
  IN UINT32  Color
  )
{
  volatile UINT32  *Pix;
  UINTN             Count;
  UINTN             i;

  if (mFb == NULL || mFbSize < 4) {
    return;
  }
  Pix   = (volatile UINT32 *) mFb;
  Count = mFbSize / 4;
  for (i = 0; i < Count; i++) {
    Pix[i] = Color;
  }
}

STATIC __attribute__((unused))
VOID
DrawRect (
  IN UINT32  X,
  IN UINT32  Y,
  IN UINT32  W,
  IN UINT32  H,
  IN UINT32  Color
  )
{
  UINT32  x;
  UINT32  y;

  for (y = 0; y < H; y++) {
    for (x = 0; x < W; x++) {
      PutPixel (X + x, Y + y, Color);
    }
  }
}
/** 鎶?16 浣嶆暟鍊肩敾鎴愪竴鎺掕壊鍧? 浜?1 鏆?0銆傚嵆浣垮瓧浣撳潖浜嗕篃鑳借鏁板瓧銆?*/
STATIC __attribute__((unused))
VOID
DrawBits (
  IN UINT32  X,
  IN UINT32  Y,
  IN UINT32  Value,
  IN UINT32  Bits
  )
{
  UINT32  i;

  for (i = 0; i < Bits; i++) {
    UINT32  On = (Value >> (Bits - 1 - i)) & 1;
    DrawRect (X + i * 56, Y, 44, 80, On ? 0xFFFFFFFF : 0xFF202020);
  }
}
STATIC __attribute__((unused))
VOID
DrawChar (
  IN UINT32  X,
  IN UINT32  Y,
  IN CHAR8   Ch,
  IN UINT32  Color
  )
{
  CONST UINT8  *Glyph = FONT[GlyphIndex (Ch)];
  UINT32        Row;
  UINT32        Col;
  UINT32        dy;
  UINT32        dx;

  for (Row = 0; Row < 8; Row++) {
    for (Col = 0; Col < 8; Col++) {
      if ((Glyph[Row] & (0x80 >> Col)) == 0) {
        continue;
      }
      for (dy = 0; dy < SCALE; dy++) {
        for (dx = 0; dx < SCALE; dx++) {
          PutPixel (X + Col * SCALE + dx, Y + Row * SCALE + dy, Color);
        }
      }
    }
  }
}

STATIC __attribute__((unused))
VOID
DrawString (
  IN UINT32        X,
  IN UINT32        Y,
  IN CONST CHAR8  *Str,
  IN UINT32        Color
  )
{
  UINT32  x = X;

  while (*Str != 0) {
    DrawChar (x, Y, *Str, Color);
    x += 8 * SCALE;
    Str++;
  }
}

STATIC __attribute__((unused))
VOID
Hex (
  IN  UINT64  Value,
  OUT CHAR8  *Out
  )
{
  STATIC CONST CHAR8  Digits[] = "0123456789ABCDEF";
  UINTN               i;

  Out[0] = '0';
  Out[1] = 'X';
  for (i = 0; i < 16; i++) {
    Out[2 + i] = Digits[(Value >> ((15 - i) * 4)) & 0xF];
  }
  Out[18] = 0;
}


/* ---------------- M4: "内核"入口 ---------------- */

/**
  真实内核通过 x0 收到设备树指针。这里用同样的约定: x0 = 设备树物理地址。
  本函数只做一件事: 读设备树头部的 magic, 判断是不是合法 DTB, 并把结果画出来。
**/
STATIC __attribute__((unused)) VOID
PayloadEntry (
  IN UINT64  Dtb
  )
{
  CHAR8   Num[20];
  UINT32  Magic = 0;
  UINT32  yy;
  UINT8  *P = (UINT8 *) (UINTN) Dtb;

  if (P != NULL) {
    Magic = ((UINT32) P[0] << 24) | ((UINT32) P[1] << 16) |
            ((UINT32) P[2] << 8)  | (UINT32) P[3];
  }

  Fill (0xFF000000);

  yy = 100;

  /* 每个数字单独一行, 避免超出 1080 宽度 */
  DrawString (40, yy, "M4", 0xFFFFFFFF);
  yy += LINE_H;

  DrawString (40, yy, "DTB", 0xFFFFFFFF);
  yy += LINE_H;
  Hex (Dtb, Num);
  DrawString (40, yy, Num, 0xFFFFFFFF);
  yy += LINE_H;

  DrawString (40, yy, "MAGIC", 0xFFFFFFFF);
  yy += LINE_H;
  Hex ((UINT64) Magic, Num);
  DrawString (40, yy, Num, 0xFFFFFFFF);
  yy += LINE_H;

  DrawString (40, yy, Magic == 0xD00DFEEDUL ? "DT OK" : "DT BAD", 0xFFFFFFFF);
  yy += LINE_H;

  DrawString (40, yy, mFdtFound ? "FOUND 1" : "FOUND 0", 0xFFFFFFFF);
  yy += LINE_H;

  DrawString (40, yy, "CFG", 0xFFFFFFFF);
  yy += LINE_H;
  Hex ((UINT64) mCfgCount, Num);
  DrawString (40, yy, Num, 0xFFFFFFFF);
  yy += LINE_H;

  /* 缓存回扫: 把像素挤到显存 */
  {
    volatile UINT32  *Pix = (volatile UINT32 *) mFb;
    UINTN             Total = mFbSize / 4;
    UINTN             k;
    for (k = 0; k < Total; k += 16) {
      UINT32  tmp = Pix[k];
      Pix[k] = tmp;
    }
  }

  for ( ; ; ) {
    __asm__ __volatile__ ("wfi");
  }
}

/** 关中断、换栈、x0=设备树, 跳到入口 */
STATIC __attribute__((unused)) VOID
JumpToPayload (
  IN UINTN  Entry,
  IN UINTN  Dtb,
  IN UINTN  StackTop
  )
{
  __asm__ __volatile__ (
    "msr  daifset, #0xf\n"
    "mov  sp, %2\n"
    "mov  x0, %1\n"
    "br   %0\n"
    :
    : "r" (Entry), "r" (Dtb), "r" (StackTop)
    : "memory"
    );
}

STATIC __attribute__((unused))
EFI_STATUS
LeaveBootServices (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS              Status = EFI_SUCCESS;
  UINTN                   MapSize = 0;
  UINTN                   MapKey;
  UINTN                   DescSize;
  UINT32                  DescVer;
  EFI_MEMORY_DESCRIPTOR  *Map = NULL;
  UINTN                   Try;

  for (Try = 0; Try < 4; Try++) {
    MapSize = 0;
    Status  = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVer);
    if (EFI_ERROR (Status) && Status != EFI_BUFFER_TOO_SMALL) {
      return Status;
    }
    if (Map != NULL) {
      FreePool (Map);
    }
    MapSize += 2 * DescSize;
    Map      = AllocatePool (MapSize);
    if (Map == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
    Status = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVer);
    if (EFI_ERROR (Status)) {
      continue;
    }
    Status = gBS->ExitBootServices (ImageHandle, MapKey);
    if (!EFI_ERROR (Status)) {
      return EFI_SUCCESS;
    }
  }
  return Status;
}

STATIC __attribute__((unused))
VOID
EFIAPI
OnTextOut (
  IN EFI_EVENT  Event,
  IN VOID      *Context
  )
{
  CHAR16  Buf[160];
  CHAR8   HexBuf[20];
  UINTN   j;
  UINT32  y;

  (VOID) Event;
  (VOID) Context;

  if (gST == NULL || gST->ConOut == NULL) {
    return;
  }

  RefreshGop ();     /* 入口时 FrameBufferBase 可能是 0, 这里重读 */

  /* 鍙墦鍗板唴閮ㄥ彉閲? 涓嶇敾浠讳綍涓滆タ 鈥斺€?鎺у埗鍙版枃瀛楁槸鑳界湅鍒扮殑閭ｆ潯閫氶亾 */
  UnicodeSPrint (
    Buf, sizeof (Buf),
    L"v35 st=%r fb=%Lx sz=%Lx w=%u h=%u p=%u f=%u\r\n",
    mLocateStatus, (UINT64) mFb, (UINT64) mFbSize,
    mFbW, mFbH, mFbPitch, mGopFmt
    );
  for (j = 0; j < 8; j++) {
    gST->ConOut->OutputString (gST->ConOut, Buf);
    {
      UINTN  k;
      for (k = 0; k < 20000000UL; k++) {
        __asm__ __volatile__ ("");
      }
    }
  }

  /* ---- 鍐呭瓨鍥惧璁? 鎵惧嚭鏈榻愮殑 runtime 鏉＄洰 ---- */
  {
    EFI_STATUS              St;
  /* ---- 枚举 EFI 配置表: 设备树到底挂在哪个 GUID 下 ---- */
  {
    UINTN  ci;
    UINTN  shown = 0;

    UnicodeSPrint (Buf, sizeof (Buf), L"cfg count=%Lu\r\n",
                   (UINT64) gST->NumberOfTableEntries);
    gST->ConOut->OutputString (gST->ConOut, Buf);

    for (ci = 0; ci < gST->NumberOfTableEntries && shown < 12; ci++) {
      UnicodeSPrint (Buf, sizeof (Buf), L"[%Lu] %g ptr=0x%Lx\r\n",
                     (UINT64) ci,
                     &gST->ConfigurationTable[ci].VendorGuid,
                     (UINT64) (UINTN) gST->ConfigurationTable[ci].VendorTable);
      gST->ConOut->OutputString (gST->ConOut, Buf);
      shown++;
    }
  }

  /* ---- 内存图审计 + 正确顺序的 ExitBootServices ----
     规则: GetMemoryMap 之后到 ExitBootServices 之间不能有任何内存分配;
     控制台打印也会分配内存, 所以打印必须全部放在取图之前。            */
  {
    EFI_STATUS              St;
    UINTN                   MapSize = 0;
    UINTN                   MapKey = 0;
    UINTN                   DescSize = 0;
    UINT32                  DescVer = 0;
    EFI_MEMORY_DESCRIPTOR  *Map = NULL;
    UINTN                   Count = 0;
    UINTN                   Rt = 0;
    UINTN                   Bad = 0;
    UINTN                   idx;

    /* 第一遍: 只统计并打印 */
    St = gBS->GetMemoryMap (&MapSize, NULL, &MapKey, &DescSize, &DescVer);
    if (St == EFI_BUFFER_TOO_SMALL) {
      MapSize += 8 * DescSize;
      Map = (EFI_MEMORY_DESCRIPTOR *) AllocatePool (MapSize);
      if (Map != NULL) {
        St = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVer);
        if (!EFI_ERROR (St)) {
          Count = MapSize / DescSize;
          for (idx = 0; idx < Count; idx++) {
            EFI_MEMORY_DESCRIPTOR  *D = (EFI_MEMORY_DESCRIPTOR *) ((UINT8 *) Map + idx * DescSize);
            /* 关键: 固件看的是 Attribute 里的 EFI_MEMORY_RUNTIME 位, 不是类型 */
            if ((D->Attribute & EFI_MEMORY_RUNTIME) != 0) {
              Rt++;
              /* 全部打出来: 让我看固件在抱怨哪一条 */
              UnicodeSPrint (Buf, sizeof (Buf),
                L"RT t=%Lu s=0x%Lx p=%Lu a=%Lu\r\n",
                (UINT64) D->Type, (UINT64) D->PhysicalStart,
                (UINT64) D->NumberOfPages,
                (UINT64) (D->Attribute & 0xFFFFFFFF));
              gST->ConOut->OutputString (gST->ConOut, Buf);
              if ((D->PhysicalStart & (EFI_PAGE_SIZE - 1)) != 0) {
                Bad++;
              }
            }
          }
          UnicodeSPrint (Buf, sizeof (Buf), L"map: desc=%Lu rt=%Lu bad=%Lu descsize=%Lu\r\n",
            (UINT64) Count, (UINT64) Rt, (UINT64) Bad, (UINT64) DescSize);
          gST->ConOut->OutputString (gST->ConOut, Buf);
        }
        FreePool (Map);
        Map = NULL;
      }
    }

    /* 需要重试的原因(EDK2 的实现顺序):
         CoreExitBootServices 先 CoreNotifySignalList(EXIT_BOOT_SERVICES),
         各驱动在回调里分配/释放内存 -> 内存图变化 -> 之前取的 MapKey 失效,
         然后 CoreTerminateMemoryMap(MapKey) 校验失败, 返回 EFI_INVALID_PARAMETER。
       第二次调用时回调不会重复执行, 所以再取一次 key 就能成功。
       循环内不做任何打印/分配, 避免自己又把 key 弄失效。            */
    St = EFI_ABORTED;
    if (mMapBuf != NULL) {
      for (idx = 0; idx < 8; idx++) {
        MapSize = mMapBufSize;
        St      = gBS->GetMemoryMap (&MapSize, mMapBuf, &MapKey, &DescSize, &DescVer);
        if (EFI_ERROR (St)) {
          continue;
        }
        St = gBS->ExitBootServices (mImageHandle, MapKey);
        if (!EFI_ERROR (St)) {
          break;
        }
      }
    }

    if (EFI_ERROR (St)) {
      UnicodeSPrint (Buf, sizeof (Buf), L"ExitBootServices -> %r\r\n", St);
      gST->ConOut->OutputString (gST->ConOut, Buf);

      /* 完整内存图: 先按类型计数, 再逐条列出 runtime 属性条目 */
      if (mMapBuf != NULL) {
        UINTN  Total = 0;
        UINTN  RtCnt = 0;
        UINTN  k;
        EFI_MEMORY_DESCRIPTOR  *D;

        MapSize = mMapBufSize;
        if (!EFI_ERROR (gBS->GetMemoryMap (&MapSize, mMapBuf, &MapKey, &DescSize, &DescVer))) {
          Total = MapSize / DescSize;
          UnicodeSPrint (Buf, sizeof (Buf), L"total=%Lu descsize=%Lu\r\n",
                         (UINT64) Total, (UINT64) DescSize);
          gST->ConOut->OutputString (gST->ConOut, Buf);

          for (k = 0; k < Total; k++) {
            D = (EFI_MEMORY_DESCRIPTOR *) ((UINT8 *) mMapBuf + k * DescSize);
            if ((D->Attribute & EFI_MEMORY_RUNTIME) != 0) {
              RtCnt++;
            }
          }
          UnicodeSPrint (Buf, sizeof (Buf), L"runtime-attr=%Lu\r\n", (UINT64) RtCnt);
          gST->ConOut->OutputString (gST->ConOut, Buf);

          /* 逐条列出(最后打印, 保持在屏幕上) */
          for (k = 0; k < Total; k++) {
            D = (EFI_MEMORY_DESCRIPTOR *) ((UINT8 *) mMapBuf + k * DescSize);
            if ((D->Attribute & EFI_MEMORY_RUNTIME) != 0) {
              UnicodeSPrint (Buf, sizeof (Buf), L"k=%Lu t=%Lu s=0x%Lx p=%Lu a=%Lx\r\n",
                             (UINT64) k, (UINT64) D->Type, (UINT64) D->PhysicalStart,
                             (UINT64) D->NumberOfPages, (UINT64) D->Attribute);
              gST->ConOut->OutputString (gST->ConOut, Buf);
            }
          }
        }
      }
    } else {
      /* 退出成功: 关中断 -> 换栈 -> x0=设备树 -> 跳到内核入口 (M4) */
      if (mKernelStack != 0) {
        JumpToPayload ((UINTN) PayloadEntry, mFdtAddr, mKernelStack);
      }
    }

    /* M3 的裸机自绘保留作对照, 正常流程不会走到 */
    if (0) {
      CHAR8   Num[20];
      CHAR8   ElBuf[4];
      UINT32  yy;

      Fill (0xFF000000);

      yy = 120;
      DrawString (40, yy, "M3", 0xFFFFFFFF);
      yy += LINE_H;

      ElBuf[0] = 'E';
      ElBuf[1] = 'L';
      ElBuf[2] = (CHAR8) ('0' + (char) CurrentEl ());
      ElBuf[3] = 0;
      DrawString (40, yy, ElBuf, 0xFFFFFFFF);
      yy += LINE_H;

      DrawString (40, yy, "PITCH", 0xFFFFFFFF);
      Hex ((UINT64) mFbPitch, Num);
      DrawString (40 + 6 * 8 * SCALE, yy, Num, 0xFFFFFFFF);
      yy += LINE_H;

      DrawString (40, yy, "FB", 0xFFFFFFFF);
      Hex ((UINT64) (UINTN) mFb, Num);
      DrawString (40 + 3 * 8 * SCALE, yy, Num, 0xFFFFFFFF);
      yy += LINE_H;

      DrawString (40, yy, "FDT", 0xFFFFFFFF);
      Hex (mFdtAddr, Num);
      DrawString (40 + 4 * 8 * SCALE, yy, Num, 0xFFFFFFFF);

      /* 帧缓冲被映射成可缓存内存, 少量分散写不会立刻到显存。
         这里对每个缓存行做一次"读-改-写", 把脏行挤出去。 */
      {
        volatile UINT32  *Pix = (volatile UINT32 *) mFb;
        UINTN             Total = mFbSize / 4;
        UINTN             k;
        for (k = 0; k < Total; k += 16) {
          UINT32  tmp = Pix[k];
          Pix[k] = tmp;
        }
      }
    }
  }

  Halt ();
}
}
EFI_STATUS
EFIAPI
ProbeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                     Status;
  EFI_EVENT                      Event;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop = NULL;
  UINTN                          i;

  (VOID) SystemTable;
  mImageHandle = ImageHandle;

  mCfgCount = gST->NumberOfTableEntries;

  /* 找设备树: 不依赖 GUID —— 直接看哪一块配置表数据的开头是 DTB 魔数。
     (实测这版固件没有按 b1b621d5-... 发布, 所以按 GUID 找不到) */
  for (i = 0; i < gST->NumberOfTableEntries; i++) {
    UINT8   *F = (UINT8 *) gST->ConfigurationTable[i].VendorTable;
    UINT32   M = 0;
    if (F == NULL) {
      continue;
    }
    M = ((UINT32) F[0] << 24) | ((UINT32) F[1] << 16) |
        ((UINT32) F[2] << 8)  | (UINT32) F[3];
    if (M == 0xD00DFEEDUL) {
      mFdtAddr = (UINTN) F;
      mFdtSize = ((UINT32) F[4] << 24) | ((UINT32) F[5] << 16) |
                 ((UINT32) F[6] << 8) | (UINT32) F[7];
      mFdtFound = 1;
      break;
    }
  }

  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **) &Gop);
  mLocateStatus = Status;

  /* 预分配内存图缓冲: 让退出循环里不再有任何分配 */
  mMapBufSize = 128 * 1024;
  mMapBuf     = AllocatePool (mMapBufSize);

  /* 给内核准备 64KB 栈 */
  {
    EFI_PHYSICAL_ADDRESS  StackBase = 0;
    if (!EFI_ERROR (gBS->AllocatePages (AllocateAnyPages, EfiBootServicesData,
                                       16, &StackBase))) {
      mKernelStack = (UINTN) StackBase + 16 * EFI_PAGE_SIZE;
    }
  }
  if (!EFI_ERROR (Status) && Gop != NULL && Gop->Mode != NULL) {
    mFb     = (UINT8 *) (UINTN) Gop->Mode->FrameBufferBase;
    mFbSize = (UINTN) Gop->Mode->FrameBufferSize;
    mFbW     = (UINT32) Gop->Mode->Info->HorizontalResolution;
    mFbH     = (UINT32) Gop->Mode->Info->VerticalResolution;
    mGopFmt  = (UINT32) Gop->Mode->Info->PixelFormat;
    /* GOP 鎶ョ殑 PixelsPerScanLine 涓嶅彲淇?瀹炴祴 1000 < 瀹藉害 1080)銆?       鐢ㄥ抚缂撳啿鎬诲瓧鑺傛暟鍙嶆帹鐪熷疄 stride: FBSZ / 4 / H */
    mFbPitch = mFbW;
    if (mFbH != 0 && (mFbSize % ((UINTN) mFbH * 4)) == 0) {
      UINTN  derived = mFbSize / ((UINTN) mFbH * 4);
      if (derived >= mFbW && derived < 16384) {
        mFbPitch = (UINT32) derived;
      }
    }
  }
  Fill (0xFF00FF00);

  Status = gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, OnTextOut, NULL, &Event);
  if (EFI_ERROR (Status)) {
    Halt ();
  }
  return gBS->RegisterProtocolNotify (&gEfiSimpleTextOutProtocolGuid, Event, &(VOID *){0});
}