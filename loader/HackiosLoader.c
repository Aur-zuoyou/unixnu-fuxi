/** @file  HackiosLoader.c
  Fuxi loader shim.  Keeps the UEFI image small enough for ABL's 0x650000
  boot-image limit while patching the old embedded loader for fuxi.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>

extern CONST UINT8   gHackiosLoaderData[];
extern CONST UINT32  gHackiosLoaderSize;

STATIC EFI_HANDLE  mParentImageHandle;
STATIC BOOLEAN     mHackiosArmed;

#define P_BRANCH  0x1654u
#define P_GIC     0xaa78u
#define P_REDIST  0xaa80u
#define P_SMALL   0xaaa4u
#define P_REBOOT  0x1a44u
#define P_WDT     0x1704u

STATIC
VOID *
HackiosLoaderImage (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT8       *Image;
  UINT64      Value;
  UINT32      Word;

  if (gHackiosLoaderSize != 68096u) {
    return NULL;
  }

  Image = NULL;
  Status = gBS->AllocatePool (EfiLoaderData, gHackiosLoaderSize, (VOID **) &Image);
  if (EFI_ERROR (Status) || Image == NULL) {
    return NULL;
  }

  CopyMem (Image, (VOID *) gHackiosLoaderData, gHackiosLoaderSize);

  Word = 0x14000000u;                 /* reboot(): b . */
  CopyMem (Image + P_REBOOT, &Word, sizeof (Word));

  Word = 0x14000037u;                 /* skip inlined watchdog arm */
  CopyMem (Image + P_WDT, &Word, sizeof (Word));

  Image[P_BRANCH + 0] = 0x1f;         /* NOP the fw_cfg branch */
  Image[P_BRANCH + 1] = 0x20;
  Image[P_BRANCH + 2] = 0x03;
  Image[P_BRANCH + 3] = 0xd5;

  Value = 0x17100000ULL;
  CopyMem (Image + P_GIC, &Value, sizeof (Value));
  Value = 0x17180000ULL;
  CopyMem (Image + P_REDIST, &Value, sizeof (Value));
  Word = 7;
  CopyMem (Image + P_SMALL, &Word, sizeof (Word));

  return Image;
}

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

  if (gST != NULL && gST->RuntimeServices != NULL) {
    gST->RuntimeServices->ResetSystem = (EFI_RESET_SYSTEM) CpuDeadLoop;
  }

  LoaderImage = HackiosLoaderImage ();
  if (LoaderImage == NULL) {
    CpuDeadLoop ();
  }

  Status = gBS->LoadImage (FALSE, mParentImageHandle, NULL, LoaderImage,
                           (UINTN) gHackiosLoaderSize, &LoaderHandle);
  if (EFI_ERROR (Status)) {
    CpuDeadLoop ();
  }

  Status = gBS->StartImage (LoaderHandle, NULL, NULL);
  (VOID) Status;
  CpuDeadLoop ();
}

VOID
HackiosLoaderInit (
  IN EFI_HANDLE  ImageHandle
  )
{
  mParentImageHandle = ImageHandle;
  mHackiosArmed      = TRUE;
}

BOOLEAN
HackiosLoaderPending (
  VOID
  )
{
  return mHackiosArmed;
}