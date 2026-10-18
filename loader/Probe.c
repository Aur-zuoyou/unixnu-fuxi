/** @file  Probe.c

  fuxi 探针 v11 —— M3: 裸机 payload 自己画字

  已知:
    M1: EL1 / GOP 可用 / 能打印
    M2: ExitBootServices 之后仍能运行(整屏刷蓝)

  问题: 退出 Boot Services 后没有控制台, 想输出只能自己往显存画像素。
  v11 就做这件事: 内置 8x8 点阵字体, 放大 6 倍画到帧缓冲。

  预期屏幕: 蓝底 + 白色文字
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

/* ---------------- 8x8 点阵字体 ---------------- */

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
/** 把 16 位数值画成一排色块: 亮=1 暗=0。即使字体坏了也能读数字。 */
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

STATIC
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

  UnicodeSPrint (
    Buf, sizeof (Buf),
    L"[fuxi-probe v18] W=%u H=%u PPSL=%u FMT=%u FBSZ=%Lu\r\n",
    mFbW, mFbH, mFbPitch,
    (UINT32) (mGopFmt), (UINT64) mFbSize
    );
  for (j = 0; j < 120; j++) {
    gST->ConOut->OutputString (gST->ConOut, Buf);
    {
      UINTN  k;
      for (k = 0; k < 3000000UL; k++) {
        __asm__ __volatile__ ("");
      }
    }
  }

  for (j = 0; j < 100000000UL; j++) {
    __asm__ __volatile__ ("");
  }

  LeaveBootServices (mImageHandle);

  /* 不擦屏、不画任何东西: 让刚才打印的文字留在屏幕上 */
  Halt ();
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

  for (i = 0; i < gST->NumberOfTableEntries; i++) {
    if (CompareGuid (&gST->ConfigurationTable[i].VendorGuid, &mFdtTableGuid)) {
      UINT8  *F = (UINT8 *) gST->ConfigurationTable[i].VendorTable;
      mFdtAddr = (UINTN) F;
      if (F != NULL) {
        mFdtSize = ((UINT32) F[4] << 24) | ((UINT32) F[5] << 16) |
                   ((UINT32) F[6] << 8) | (UINT32) F[7];
      }
      break;
    }
  }

  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **) &Gop);
  if (!EFI_ERROR (Status) && Gop != NULL && Gop->Mode != NULL) {
    mFb     = (UINT8 *) (UINTN) Gop->Mode->FrameBufferBase;
    mFbSize = (UINTN) Gop->Mode->FrameBufferSize;
    mFbW     = (UINT32) Gop->Mode->Info->HorizontalResolution;
    mFbH     = (UINT32) Gop->Mode->Info->VerticalResolution;
    mFbPitch = (UINT32) Gop->Mode->Info->PixelsPerScanLine;
    mGopFmt  = (UINT32) Gop->Mode->Info->PixelFormat;
    if (mFbPitch < mFbW) {
      mFbPitch = mFbW;
    }
  }
  Fill (0xFF00FF00);

  Status = gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, OnTextOut, NULL, &Event);
  if (EFI_ERROR (Status)) {
    Halt ();
  }
  return gBS->RegisterProtocolNotify (&gEfiSimpleTextOutProtocolGuid, Event, &(VOID *){0});
}
