# zeta bring-up candidate

Microsoft Surface Duo 2 (zeta), SM8350 (Lahaina), for the UniXNU boot chain.
This platform is not hardware validated. It uses the Lahaina support of this
Mu-Silicium tree (UniXNU's uefi-sheng fork) and follows the sheng first-boot
layout: Mu drivers only, the ABL splash as the frame buffer, the boot image
ramdisk as the UniXNU payload volume.

## Inputs

Every board value comes from the phone's own firmware, slot b (active), from an
EDL partition dump pinned by UniXNU-zeta `proprietary-files.txt`:

| Input | Source |
| --- | --- |
| Stock UEFI FV | XBL ELF segment at 0x9FC00000, 0x300000 bytes (`BOOT.MXF.1.0-00893-LAHAINA-1.7786.2`, bootloader 2023.501.53) |
| Vendor modules | 110 AArch64 modules and 46 raw files from that FV, GUIDs, PE bytes and DEPEX unchanged (`scripts/import-zeta-uefi.py`) |
| Memory map | stock `uefiplat.cfg` plus every fixed `/reserved-memory` region of the stock device tree (`scripts/gen-zeta-memory-map.py`) |
| Configuration map | numeric stock `[ConfigParameters]` with the single-core headless overrides (`scripts/gen-zeta-config-map.py`) |
| Device tree | vendor_boot Lahaina V2.1 base (fastboot `hw-revision` 20001) with the dtbo "Surface Duo2 MP" overlay (`scripts/make-zeta-dtb.py`) |

The vendor binaries stay in UniXNU-zeta `.cache/firmware`; `Binaries/zeta` is a
symbolic link to them. The UniXNU-zeta preflight `scripts/check-zeta.py`
regenerates both maps, requires them to match the files here, and checks
alignment, overlaps, reservations, stock coverage, the FD, the stack and the
frame buffer.

## Values

| Item | Value | Evidence |
| --- | --- | --- |
| FD | 0x9FC00000, 0x300000 | stock XBL segment and stock "UEFI FD" region |
| Stack | 0x9FF90000, 0x40000 | stock "UEFI Stack" |
| DXE heap | 0x9B800000, 0x3C00000 | stock "DXE Heap" (found by name in SEC) |
| Frame buffer | 0xE1000000, 1344x1892x32 | stock "Display Reserved" = dtbo `splash_region`; WOA SurfaceDuo2Pkg panel mode |
| Boot image | header v1, gzip kernel, appended DTB | venus and q2q (SM8350, Android 11 ABL); stock is v3 |
| SPMI arbiter | v5: core 0xC440000, cnfg 0xC40A000, obsrvr 0xE600000 | stock DT and stock `spmi-pmic-arb.c` |
| Power / volume down | PMK8350 SID 0 PON 0x1300, RT_STS bits 7 / 6 | stock DT `pon_hlos@1300` |
| Volume up | PM8350 SID 1 GPIO6 (PPID 0x18D), active low | stock DT `gpio_keys/vol_up` |

The stock register map has two overlaps (IMEM Cookie Base inside IMEM Base,
USB running into AOSS); the generator splits them so every descriptor is its
own resource HOB, as sheng's map does by hand.

## Drivers

* `ZetaBootMemoryDxe`: sheng's device-tree memory and initrd driver. It also
  keeps the initrd and the device tree out of the DRAM it adds, because nothing
  shows that zeta's ABL places them inside a firmware region.
* `ZetaKeyInputDxe`: sheng's console key driver with an arbiter v5 back end
  (`ZetaSpmiKeys.h`, host test in `Test/keys_host.c`). sheng's SM8550 arbiter is
  v7; none of its offsets apply.

## Not done

* `ZETA_VENDOR_HW = 1` (SMEM, clocks, RPMh, UFS, USB, stock DisplayDxe with the
  `Surface_Elgin_Panel_*` XMLs) is not wired; the DSC refuses it.
* The second panel is not used.

## Hardware status

2026-10-21, DEBUG build, `fastboot boot` of a header v1 image with no ramdisk
(bootloader 2023.501.53, slot b): ABL accepts the image, the firmware starts
without a reset, the UniXNU menu appears on the frame buffer, and the volume
keys move the selection (stages 1 and 2 of the UniXNU bring-up order).
With a 4 MiB FAT ramdisk holding `\EFI\BOOT\BOOTAA64.EFI`, the menu adds
"EFI loader on volume 1": `ZetaBootMemoryDxe` publishes the initrd as a
block device and the payload volume mounts. That loader did not start: the
Secure Boot keys SecureBootHandler enrolls refuse unsigned images from a
volume, so bring-up tools go in the FV and payload entries start them as
`fv:<FILE_GUID>`.

`Application/ZetaMemInfo` (FV, started from a payload `unixnu.toml` entry)
read on the phone:

| Item | Value |
| --- | --- |
| Boot CPU | MPIDR 0x81000000 (MT, core in Aff1: DT `reg` 0), EL1 |
| CNTFRQ_EL0 | 19200000 Hz (= DT timer `clock-frequency`) |
| ABL tree | 0xAF9FE000, 746298 bytes |
| `/memory` | 0x80000000+0x39700000, 0xB9D00000+0x1E00000, 0xC0000000+0x1C0000000 (8117 MiB, all inside 0x80000000..0x280000000) |
| GOP | 1344x1892, pitch 1344 pixels, PixelBlueGreenRedReserved8BitPerColor, 0xE1000000 + 0x9B3400 |

The two holes (0xB9700000+0x600000, 0xBBB00000+0x4500000) are not in the
device tree's reserved-memory; ABL leaves them out.
