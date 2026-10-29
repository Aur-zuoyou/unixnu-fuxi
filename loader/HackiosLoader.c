/** @file  HackiosLoader.c
  Fuxi bring-up shim: patch and start HackiOS prebuilt/loader.efi.

  The prebuilt loader in the repository is an old redwood/QEMU build.  It only
  recognises the redwood framebuffer and falls into its QEMU fw_cfg path on
  fuxi (writing 0x09020008).  We copy the embedded PE image to a writable pool
  buffer and patch it for fuxi before LoadImage:
    - NOP the "not redwood" branch at file offset 0x1654
    - patch platform_redwood GIC fields to fuxi SM8550 values
  The complete XNUP payload is supplied in the Android boot image's ramdisk
  field; this code does not touch 0xF0000000.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>

extern CONST UINT8   gHackiosLoaderData[];
extern CONST UINT32  gHackiosLoaderSize;

STATIC EFI_HANDLE  mParentImageHandle;
STATIC BOOLEAN     mHackiosArmed;

#define LOADER_PATCH_BRANCH_OFF   0x1654u
#define LOADER_PATCH_GIC_OFF      0xaa78u
#define LOADER_PATCH_REDIST_OFF   0xaa80u
#define LOADER_PATCH_SMALL_OFF    0xaaa4u
#define LOADER_EXPECTED_SIZE      68096u

STATIC
VOID
HackiosPrint (
  IN CONST CHAR16  *Text
  )
{
  if (gST != NULL && gST->ConOut != NULL) {
    gST->ConOut->OutputString (gST->ConOut, (CHAR16 *) Text);
  }
}

STATIC
VOID
HackiosPrintStatus (
  IN CONST CHAR16  *What,
  IN EFI_STATUS     Status
  )
{
  CHAR16  Buf[160];

  UnicodeSPrint (Buf, sizeof (Buf), L"hackios: %s: %r\r\n", What, Status);
  HackiosPrint (Buf);
}

/** Copy the embedded loader and apply the fuxi fixes. */
STATIC
VOID *
HackiosLoaderImage (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT8       *Image;
  UINT32      Gic;
  UINT64      Gic64;
  UINT32      Small;

  if (gHackiosLoaderSize != LOADER_EXPECTED_SIZE) {
    HackiosPrint (L"hackios: unexpected embedded loader size\r\n");
    return NULL;
  }

  Image = NULL;
  Status = gBS->AllocatePool (EfiLoaderData, gHackiosLoaderSize, (VOID **) &Image);
  if (EFI_ERROR (Status) || Image == NULL) {
    HackiosPrintStatus (L"AllocatePool(loader)", Status);
    return NULL;
  }

  CopyMem (Image, (VOID *) gHackiosLoaderData, gHackiosLoaderSize);

  /* The old loader's conditional branch goes to the QEMU fw_cfg path. */
  Image[LOADER_PATCH_BRANCH_OFF + 0] = 0x1f;
  Image[LOADER_PATCH_BRANCH_OFF + 1] = 0x20;
  Image[LOADER_PATCH_BRANCH_OFF + 2] = 0x03;
  Image[LOADER_PATCH_BRANCH_OFF + 3] = 0xd5;

  /* platform_redwood: GIC distributor/redistributor and fuxi little cores. */
  Gic64 = 0x0000000017100000ULL;
  CopyMem (Image + LOADER_PATCH_GIC_OFF, &Gic64, sizeof (Gic64));
  Gic64 = 0x0000000017180000ULL;
  CopyMem (Image + LOADER_PATCH_REDIST_OFF, &Gic64, sizeof (Gic64));
  Small = 0x07;
  CopyMem (Image + LOADER_PATCH_SMALL_OFF, &Small, sizeof (Small));

  /* Keep the Gic variable used on compilers that warn about it. */
  Gic = 0;
  (VOID) Gic;

  return Image;
}

/** Synchronously load and start the embedded HackiOS loader. */
VOID
HackiosLoaderStart (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  LoaderHandle;
  VOID        *LoaderImage;

  mParentImageHandle = ImageHandle;
  mHackiosArmed      = FALSE;

  HackiosPrint (L"hackios: starting embedded loader.efi (fuxi patches)\r\n");
  HackiosPrint (L"hackios: XNUP payload is expected at 0xF0000000 (ramdisk)\r\n");

  LoaderImage = HackiosLoaderImage ();
  if (LoaderImage == NULL) {
    CpuDeadLoop ();
  }

  Status = gBS->LoadImage (
                   FALSE,
                   mParentImageHandle,
                   NULL,
                   LoaderImage,
                   (UINTN) gHackiosLoaderSize,
                   &LoaderHandle
                   );
  if (EFI_ERROR (Status)) {
    HackiosPrintStatus (L"LoadImage(loader.efi)", Status);
    CpuDeadLoop ();
  }

  Status = gBS->StartImage (LoaderHandle, NULL, NULL);
  HackiosPrintStatus (L"loader.efi returned", Status);
  CpuDeadLoop ();
}

/** Mark the old Probe path as inactive and remember the parent image. */
VOID
HackiosLoaderInit (
  IN EFI_HANDLE  ImageHandle
  )
{
  mParentImageHandle = ImageHandle;
  mHackiosArmed      = TRUE;
}

/** TRUE while the old Probe/ExitBootServices path must stay out of the way. */
BOOLEAN
HackiosLoaderPending (
  VOID
  )
{
  return mHackiosArmed;
}
