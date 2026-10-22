/** @file
  Bring-up probe: prints the values the zeta ADT needs that only the running
  phone knows.

  * the /memory ranges ABL wrote into the device tree it handed over (taken
    from the SMEM RAM partition table at boot; the dumped tree has a zero
    placeholder), and the contiguous DRAM run from the lowest bank;
  * the boot CPU MPIDR, the current exception level and CNTFRQ_EL0;
  * the GOP mode, including the line pitch;
  * the boot CPU's MIDR and ID registers, with the fields arm64e code relies
    on decoded (pointer authentication, LRCPC, FlagM, BTI, SME).

  The tree is found through the entry argument HOB that SEC keeps (x0 from
  ABL), the same source ZetaBootMemoryDxe uses. Every line starts with
  "ZETAINFO:". It is in the FV, because the enrolled Secure Boot keys refuse
  unsigned images from a volume; a payload entry starts it as
  "fv:<FILE_GUID>". It waits for a key before returning to the menu.
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

//
// Encoded names, so the reserved ID space (ID_AA64ISAR2_EL1) reads as zero
// on cores that predate it instead of needing assembler support.
//
#define READ_SYSREG(Name)  ({ UINT64 _Value; __asm__ volatile ("mrs %0, " Name : "=r" (_Value)); _Value; })
#define FIELD(Value, Shift)  ((UINT32)RShiftU64 ((Value), (Shift)) & 0xF)

STATIC
VOID
PrintCpuFeatures (
  VOID
  )
{
  UINT64  Midr;
  UINT64  Isar0;
  UINT64  Isar1;
  UINT64  Isar2;
  UINT64  Pfr0;
  UINT64  Pfr1;

  Midr  = READ_SYSREG ("S3_0_C0_C0_0");
  Isar0 = READ_SYSREG ("S3_0_C0_C6_0");
  Isar1 = READ_SYSREG ("S3_0_C0_C6_1");
  Isar2 = READ_SYSREG ("S3_0_C0_C6_2");
  Pfr0  = READ_SYSREG ("S3_0_C0_C4_0");
  Pfr1  = READ_SYSREG ("S3_0_C0_C4_1");

  Print (
    L"ZETAINFO: MIDR 0x%lx (implementer 0x%x part 0x%x r%up%u)\n",
    Midr,
    (UINT32)RShiftU64 (Midr, 24) & 0xFF,
    (UINT32)RShiftU64 (Midr, 4) & 0xFFF,
    FIELD (Midr, 20),
    FIELD (Midr, 0)
    );
  Print (L"ZETAINFO: ISAR0 0x%lx ISAR1 0x%lx ISAR2 0x%lx\n", Isar0, Isar1, Isar2);
  Print (L"ZETAINFO: PFR0 0x%lx PFR1 0x%lx\n", Pfr0, Pfr1);
  Print (
    L"ZETAINFO: PAuth APA %u API %u APA3 %u GPA %u GPI %u GPA3 %u\n",
    FIELD (Isar1, 4),
    FIELD (Isar1, 8),
    FIELD (Isar2, 12),
    FIELD (Isar1, 24),
    FIELD (Isar1, 28),
    FIELD (Isar2, 8)
    );
  Print (
    L"ZETAINFO: LRCPC %u FlagM %u Atomic %u BTI %u SME %u SVE %u\n",
    FIELD (Isar1, 20),
    FIELD (Isar0, 52),
    FIELD (Isar0, 20),
    FIELD (Pfr1, 0),
    FIELD (Pfr1, 24),
    FIELD (Pfr0, 32)
    );
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
  PrintCpuFeatures ();
  //
  // The button press that started the entry may still be queued.
  //
  while (!EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
  }

  Print (L"ZETAINFO: press any button to return\n");
  gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Index);
  gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
  return EFI_SUCCESS;
}
