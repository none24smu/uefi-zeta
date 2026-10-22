##
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################
[Defines]
  PLATFORM_NAME                  = zeta
  PLATFORM_GUID                  = 5D0A817C-A48B-4EC0-9855-0146BCF7D969
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/zetaPkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = zetaPkg/zeta.fdf
  USE_CUSTOM_DISPLAY_DRIVER      = 0

  #
  # 0 = Mu drivers only. ABL's splash keeps scanning out, so the boot
  #     manager is visible without any vendor hardware driver.
  # The stock vendor hardware drivers (SMEM, clocks, RPMh, UFS, USB, display)
  # are imported by UniXNU-zeta but not wired into this platform yet.
  #
  ZETA_VENDOR_HW                 = 0

  #
  # 0 = SM8350
  # 1 = SM8350-AB
  # 2 = SM8350-AC
  #
  SOC_TYPE                       = 0

!include LahainaPkg/LahainaPkg.dsc.inc

!if $(ZETA_VENDOR_HW) != 0
  !error The stock vendor driver set is not ported to zeta yet
!endif
!if $(USE_CUSTOM_DISPLAY_DRIVER) != 0
  !error The stock DisplayDxe chain needs the vendor driver set
!endif

[PcdsFixedAtBuild]
  #
  # DDR Memory
  #
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000

  #
  # UEFI Stack (stock uefiplat.cfg "UEFI Stack")
  #
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0x9FF90000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"Microsoft Corporation"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"Surface Duo 2"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"zeta"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"1995"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"zeta"

  #
  # Simple Frame Buffer
  #
  # One 1344x1892 panel, the mode WOA's SurfaceDuo2Pkg uses on the same ABL
  # splash ("Display Reserved", 0xE1000000). The line pitch ABL leaves is
  # assumed equal to the width; unverified on hardware.
  #
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1344
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|1892
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32
  # Pause per log line so a video of the screen shows what loads last.
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferDelay|30000

  #
  # Platform PEI
  #
  gQcomPkgTokenSpaceGuid.PcdPlatformType|"LA"
  # Without the stock vendor drivers nothing reads the XBL HOBs, and the
  # stock UEFI's leftover FD window must not be scanned (sheng int-103).
  gQcomPkgTokenSpaceGuid.PcdBuildXblHobs|FALSE

[LibraryClasses]
  #
  # Memory Libraries
  #
  MemoryMapLib|zetaPkg/Library/MemoryMapLib/MemoryMapLib.inf

  #
  # QCOM Libraries
  #
  ConfigurationMapLib|zetaPkg/Library/ConfigurationMapLib/ConfigurationMapLib.inf

[Components]
  UniXNUPkg/Application/UniXNU/UniXNU.inf
  UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf
  zetaPkg/Drivers/ZetaBootMemoryDxe/ZetaBootMemoryDxe.inf
  zetaPkg/Drivers/ZetaKeyInputDxe/ZetaKeyInputDxe.inf
  DarwinBootPkg/Driver/DarwinBoot.inf

[LibraryClasses]
  UniXNULib|UniXNUPkg/Library/UniXNULib/UniXNULib.inf
  FdtLib|MdePkg/Library/BaseFdtLib/BaseFdtLib.inf
