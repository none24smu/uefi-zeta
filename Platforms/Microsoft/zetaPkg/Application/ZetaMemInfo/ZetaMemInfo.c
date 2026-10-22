/** @file
  Bring-up probe: prints the values the zeta ADT needs that only the running
  phone knows.

  * the /memory ranges ABL wrote into the device tree it handed over (taken
    from the SMEM RAM partition table at boot; the dumped tree has a zero
    placeholder), and the contiguous DRAM run from the lowest bank;
  * the boot CPU MPIDR, the current exception level and CNTFRQ_EL0;
  * the GOP mode, including the line pitch.

  The tree is found through the entry argument HOB that SEC keeps (x0 from
  ABL), the same source ZetaBootMemoryDxe uses. Every line starts with
  "ZETAINFO:". Run it from a payload volume as \EFI\BOOT\BOOTAA64.EFI; it waits
  for a key before returning to the menu.
**/

#include <PiDxe.h>
#include <AArch64/AArch64.h>
#include <Library/ArmLib.h>
#include <Library/BaseLib.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/GraphicsOutput.h>

#define MAX_BANKS  64

typedef struct {
  UINT64  Base;
  UINT64  End;       // exclusive
} RANGE;

extern EFI_GUID  gSiliciumEntryArgHobGuid;

STATIC
UINT64
ReadCells (
  IN CONST VOID  *Data,
  IN UINT32      Cells
  )
{
  if (Cells == 2) {
    return Fdt64ToCpu (ReadUnaligned64 (Data));
  }

  return Fdt32ToCpu (ReadUnaligned32 (Data));
}

STATIC
UINT32
GetCells (
  IN CONST VOID  *Fdt,
  IN INT32       Node,
  IN CONST CHAR8 *Name,
  IN UINT32      Default
  )
{
  CONST VOID  *Prop;
  INT32       Length;

  Prop = FdtGetProp (Fdt, Node, Name, &Length);
  if ((Prop == NULL) || (Length != sizeof (UINT32))) {
    return Default;
  }

  return Fdt32ToCpu (ReadUnaligned32 (Prop));
}

STATIC
VOID
PrintMemory (
  IN CONST VOID  *Fdt
  )
{
  INT32        Root;
  INT32        Node;
  INT32        Length;
  INT32        Offset;
  CONST CHAR8  *Type;
  CONST UINT8  *Reg;
  UINT32       AddressCells;
  UINT32       SizeCells;
  UINT32       Entry;
  RANGE        Banks[MAX_BANKS];
  UINTN        Count;
  UINTN        Index;
  UINTN        Other;
  RANGE        Swap;
  UINT64       Total;
  UINT64       RunEnd;

  Root         = FdtPathOffset (Fdt, "/");
  AddressCells = GetCells (Fdt, Root, "#address-cells", 2);
  SizeCells    = GetCells (Fdt, Root, "#size-cells", 2);
  Entry        = (AddressCells + SizeCells) * sizeof (UINT32);
  Count        = 0;

  for (Node = FdtFirstSubnode (Fdt, Root); Node >= 0; Node = FdtNextSubnode (Fdt, Node)) {
    Type = FdtGetProp (Fdt, Node, "device_type", &Length);
    if ((Type == NULL) || (AsciiStrCmp (Type, "memory") != 0)) {
      continue;
    }

    Reg = FdtGetProp (Fdt, Node, "reg", &Length);
    if (Reg == NULL) {
      continue;
    }

    Print (L"ZETAINFO: node %a: %d reg bytes, %u+%u cells\n", FdtGetName (Fdt, Node, NULL), Length, AddressCells, SizeCells);
    for (Offset = 0; (Offset + (INT32)Entry <= Length) && (Count < MAX_BANKS); Offset += Entry) {
      Banks[Count].Base = ReadCells (Reg + Offset, AddressCells);
      Banks[Count].End  = Banks[Count].Base + ReadCells (Reg + Offset + AddressCells * sizeof (UINT32), SizeCells);
      if (Banks[Count].End > Banks[Count].Base) {
        Count++;
      }
    }
  }

  for (Index = 0; Index < Count; Index++) {
    for (Other = Index + 1; Other < Count; Other++) {
      if (Banks[Other].Base < Banks[Index].Base) {
        Swap          = Banks[Index];
        Banks[Index]  = Banks[Other];
        Banks[Other]  = Swap;
      }
    }
  }

  Total = 0;
  for (Index = 0; Index < Count; Index++) {
    Print (
      L"ZETAINFO: bank %u 0x%lx-0x%lx size 0x%lx\n",
      (UINT32)Index,
      Banks[Index].Base,
      Banks[Index].End - 1,
      Banks[Index].End - Banks[Index].Base
      );
    Total += Banks[Index].End - Banks[Index].Base;
  }

  if (Count == 0) {
    Print (L"ZETAINFO: no /memory ranges\n");
    return;
  }

  RunEnd = Banks[0].End;
  for (Index = 1; (Index < Count) && (Banks[Index].Base <= RunEnd); Index++) {
    RunEnd = MAX (RunEnd, Banks[Index].End);
  }

  Print (L"ZETAINFO: %u banks, total 0x%lx (%lu MiB)\n", (UINT32)Count, Total, RShiftU64 (Total, 20));
  Print (L"ZETAINFO: contiguous from 0x%lx: size 0x%lx\n", Banks[0].Base, RunEnd - Banks[0].Base);
}

STATIC
VOID
PrintGop (
  VOID
  )
{
  EFI_STATUS                            Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL          *Gop;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info;

  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  if (EFI_ERROR (Status)) {
    Print (L"ZETAINFO: no GOP: %r\n", Status);
    return;
  }

  Info = Gop->Mode->Info;
  Print (
    L"ZETAINFO: GOP mode %u/%u %ux%u pitch %u px format %u\n",
    Gop->Mode->Mode,
    Gop->Mode->MaxMode,
    Info->HorizontalResolution,
    Info->VerticalResolution,
    Info->PixelsPerScanLine,
    (UINT32)Info->PixelFormat
    );
  Print (L"ZETAINFO: GOP frame buffer 0x%lx size 0x%lx\n", Gop->Mode->FrameBufferBase, (UINT64)Gop->Mode->FrameBufferSize);
}

EFI_STATUS
EFIAPI
ZetaMemInfoEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_HOB_GUID_TYPE  *Hob;
  UINT64             Dtb;
  UINTN              Index;
  EFI_INPUT_KEY      Key;

  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"ZETAINFO: zeta bring-up probe\n");
  Print (
    L"ZETAINFO: MPIDR 0x%lx CurrentEL %u CNTFRQ %lu Hz\n",
    (UINT64)ArmReadMpidr (),
    (UINT32)(ArmReadCurrentEL () >> 2),
    (UINT64)ArmReadCntFrq ()
    );

  Hob = GetFirstGuidHob (&gSiliciumEntryArgHobGuid);
  if (Hob == NULL) {
    Print (L"ZETAINFO: no entry argument HOB\n");
  } else {
    Dtb = *(UINT64 *)GET_GUID_HOB_DATA (Hob);
    if ((Dtb == 0) || ((Dtb & 3) != 0) || (FdtCheckHeader ((VOID *)(UINTN)Dtb) != 0)) {
      Print (L"ZETAINFO: x0 0x%lx is not a device tree\n", Dtb);
    } else {
      Print (L"ZETAINFO: device tree at 0x%lx, %u bytes\n", Dtb, (UINT32)FdtTotalSize ((VOID *)(UINTN)Dtb));
      PrintMemory ((VOID *)(UINTN)Dtb);
    }
  }

  PrintGop ();
  Print (L"ZETAINFO: press any button to return\n");
  gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Index);
  gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
  return EFI_SUCCESS;
}
