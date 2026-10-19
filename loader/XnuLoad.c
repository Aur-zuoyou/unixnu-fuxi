/** @file
  XnuLoad.c —— 从块设备（dtbo 分区）把 XNU payload 读进内存

  为什么不在 EFI 里内嵌内核：
    内核 15.8MB，把 Probe.efi 撑到 16MB 后 UEFI 加载它会崩（实测
    "Asynchronous Exception"）。所以改成：EFI 保持小体积，内核单独
    刷进 dtbo 分区（25MB），启动时读进来。

  分区镜像格式（tools/mkpayload_img.py 生成）：
      offset 0   "FUXIPAY1"
      offset 8   u64 长度
      offset 16  payload 数据
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/BlockIo.h>

#define PAYLOAD_MAGIC        "FUXIPAY1"
#define PAYLOAD_MIN_MEDIA    (16ULL * 1024 * 1024)   /* 分区至少 16MB */
#define PAYLOAD_MAX_MEDIA    (64ULL * 1024 * 1024)   /* 最多 64MB */

STATIC
BOOLEAN
MagicOk (
  IN CONST UINT8  *P
  )
{
  UINTN  I;

  for (I = 0; I < 8; I++) {
    if (P[I] != (UINT8)PAYLOAD_MAGIC[I]) {
      return FALSE;
    }
  }

  return TRUE;
}

/**
  在块设备里找 payload 并读进新分配的内存。

  @param[out] Buf   读到的缓冲（调用者负责 FreePool）
  @param[out] Size  长度

  @retval EFI_SUCCESS  找到并读成功
  @retval others       没找到
**/
EFI_STATUS
XnuLoadFromBlock (
  OUT VOID   **Buf,
  OUT UINTN  *Size
  )
{
  EFI_STATUS             Status;
  EFI_HANDLE             *Handles = NULL;
  UINTN                  Count = 0;
  UINTN                  I;
  EFI_BLOCK_IO_PROTOCOL  *Bio;
  UINT8                  *Hdr;
  UINT64                 Len;
  UINTN                  MediaSize;
  UINTN                  BlkSize;
  UINT8                  *Data;
  UINTN                  Off;
  UINTN                  Chunk;

  *Buf  = NULL;
  *Size = 0;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiBlockIoProtocolGuid,
                  NULL,
                  &Count,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (I = 0; I < Count; I++) {
    Bio = NULL;
    if (EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiBlockIoProtocolGuid, (VOID **)&Bio))) {
      continue;
    }
    if ((Bio == NULL) || (Bio->Media == NULL) || !Bio->Media->MediaPresent) {
      continue;
    }
    if (Bio->Media->LastBlock == 0) {
      continue;
    }

    BlkSize   = Bio->Media->BlockSize;
    MediaSize = (UINTN)((Bio->Media->LastBlock + 1) * BlkSize);
    if ((MediaSize < PAYLOAD_MIN_MEDIA) || (MediaSize > PAYLOAD_MAX_MEDIA)) {
      continue;
    }

    /* 读第一块，看魔数 */
    Hdr = AllocatePool (BlkSize);
    if (Hdr == NULL) {
      continue;
    }
    Status = Bio->ReadBlocks (Bio, Bio->Media->MediaId, 0, BlkSize, Hdr);
    if (EFI_ERROR (Status) || !MagicOk (Hdr)) {
      FreePool (Hdr);
      continue;
    }
    CopyMem (&Len, Hdr + 8, sizeof (Len));
    FreePool (Hdr);

    if ((Len == 0) || (Len > MediaSize) || (Len > (256ULL * 1024 * 1024))) {
      continue;
    }

    Data = AllocatePool ((UINTN)Len);
    if (Data == NULL) {
      continue;
    }

    /* 从偏移 16 开始按块读 */
    Off = 0;
    while (Off < (UINTN)Len) {
      UINT64  Lba = (16 + Off) / BlkSize;
      UINTN   InBlock = (16 + Off) % BlkSize;
      UINTN   Want = (UINTN)Len - Off;
      UINT8   *Tmp;

      Chunk = BlkSize;
      if (Want < Chunk) {
        Chunk = Want;
      }

      Tmp = AllocatePool (BlkSize);
      if (Tmp == NULL) {
        break;
      }
      Status = Bio->ReadBlocks (Bio, Bio->Media->MediaId, Lba, BlkSize, Tmp);
      if (EFI_ERROR (Status)) {
        FreePool (Tmp);
        break;
      }
      CopyMem (Data + Off, Tmp + InBlock, Chunk);
      FreePool (Tmp);
      Off += Chunk;
    }

    if (Off == (UINTN)Len) {
      *Buf  = Data;
      *Size = (UINTN)Len;
      FreePool (Handles);
      return EFI_SUCCESS;
    }

    FreePool (Data);
  }

  if (Handles != NULL) {
    FreePool (Handles);
  }
  return EFI_NOT_FOUND;
}
