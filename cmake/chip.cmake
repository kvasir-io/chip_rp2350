include(${CMAKE_CURRENT_LIST_DIR}/../core/cmake/core.cmake)

# The chip's part of the contract (Kvasir_SDK cmake/kvasir_chip.cmake): the kvasir_chip() call at the end, its values
# collected here with their reasons.

# Which chip this is, for every TU of an image (kvasir_chip DEFINITIONS reach them through kvasir::chip): the RP2350A
# (QFN-60, 30 GPIOs, 4 analogue inputs) or the RP2350B (QFN-80, 48 GPIOs, 8 analogue inputs) (RP2350 datasheet chapter
# 1, Table 1 "RP2350 device family"; the silicon says which in SYSINFO.PACKAGE_SEL, 0 = QFN80, 1 = QFN60, 12.15.1.2
# Table 1308). Set it before include(kvasir.cmake) or as -DKVASIR_RP2350_MPU=...; kvasir_chip's MPU stays the J-Link
# device name. src/chip/rp2350.hpp and the shared rp_common headers test the macros with `#if defined(...)` (gcc
# -Wundef), and rp_common/PinConfig.hpp defines Chip::part from them.
set(KVASIR_RP2350_MPU
    RP2350A
    CACHE STRING "Which RP2350: RP2350A (QFN-60) or RP2350B (QFN-80)")
set_property(CACHE KVASIR_RP2350_MPU PROPERTY STRINGS RP2350A RP2350B)
if(NOT KVASIR_RP2350_MPU MATCHES "^RP2350[AB]$")
    message(FATAL_ERROR "KVASIR_RP2350_MPU is '${KVASIR_RP2350_MPU}'; chip_rp2350 knows RP2350A and RP2350B")
endif()
set(_chip_definitions KVASIR_CHIP_${KVASIR_RP2350_MPU}=1 KVASIR_CHIP_RP2350=1 KVASIR_CHIP_FAMILY_RP=1)
# The chip's Register::ExecuteSeam (Kvasir_SDK Register/Exec.hpp includes the header named here at its end, so every TU
# that reaches apply() has it): atomic() through the SET/CLR/XOR alias windows, chip_rp_common RegisterAlias.hpp.
list(APPEND _chip_definitions [[KVASIR_CHIP_REGISTER_SEAM="chip/rp_common/RegisterAlias.hpp"]])
# IMAGE_CRC leaves the picobin block loop out (.after_vectors, linker/common_vectors_body.inc.ld): picotool seal and
# sign rewrite its link word after the link, and the bootrom checks that block itself.
set(_chip_image_crc_exclude _LINKER_INTERN_after_vectors_start_ _LINKER_INTERN_after_vectors_end_)

# J-Link Commander lines after `connect` in the flash/reset/connect scripts (Kvasir_SDK cmake/jlink.cmake): core 1 back
# into its boot ROM before anything is reset or written. The commander's `r` restarts the connected core only, and a
# core 1 the old image launched runs on through the erase into the new image's RAM. PSM FRCE_OFF.PROC1 set, then
# cleared, through the atomic aliases: PSM_BASE 0x40018000 + FRCE_OFF 0x4, PROC1 = bit 24 (RP2350 datasheet 7.4.4 "List
# of registers", Table 531), +0x2000 set / +0x3000 clear (2.1.3 "Atomic register access"). uc_log's printer gets the
# same lines (util.cmake PRE_RESET_COMMANDS -> --pre_reset_command) and writes them before its own resets and downloads
# - it understands only `w4 <address> <value>`, keep them to that. Then WATCHDOG SCRATCH4 (WATCHDOG_BASE 0x400d8000 +
# 0x1c, 12.9.7 "List of registers") to 0: disarms Kvasir::CoreResetGuard's watchdog boot vector (5.2.4), whose RAM
# routine a download overwrites.
set(_chip_jlink_connect "w4 0x4001A004 0x01000000" "w4 0x4001B004 0x01000000" "w4 0x400d801c 0x00000000")

# QMI stuck in direct mode with a transfer that never ends: DIRECT_CSR (XIP_QMI_BASE 0x400d0000 + 0x00) EN (bit 0) and
# BUSY (bit 1) set (RP2350 datasheet 12.14, Table 1294), flash unreadable. A core reset does not free it; a watchdog
# reset of every domain but PROC_COLD does: SCRATCH4 to 0 (no CoreResetGuard needed; rp_common WatchdogBoot.hpp),
# PSM.WDSEL (PSM_BASE 0x40018000 + 0x8, Table 532, bits 24:1: the reboot list, src/chip/WatchdogDomains.hpp onReboot),
# then WATCHDOG.CTRL.TRIGGER (0x400d8000 + 0x0, bit 31, Table 1248). The printer also runs the recovery before retrying
# a failed download (the QMI can wedge with DIRECT_CSR idle). util.cmake -> --stall_signature / --stall_recovery.
# tools/kvasir_bench_chip.py `qmi --recover` writes these very words (it reads them here), and
# tools/test_kvasir_bench_chip.py checks them and the connect words above against chip.svd.
set(_chip_jlink_stall "0x400d0000,0x3,0x3")
set(_chip_jlink_stall_recovery "w4 0x400d801c 0x00000000" "w4 0x40018008 0x01fffffe" "w4 0x400d8000 0x80000000")
# The firmware check also reads flash through XIP_NOCACHE_NOALLOC (0x14000000, RP2350 datasheet 2.2). util.cmake ->
# --flash_uncached_offset.
set(_chip_jlink_uncached "0x04000000")
# Flash readable over XIP only with DIRECT_CSR (0x400d0000) EN (bit 0) and ASSERT_CS0N (bit 2) clear: in direct mode an
# XIP access is a bus error (RP2350 datasheet 12.14.5, Table 1294). BUSY left out: XIP fetches set it too. The printer
# reads flash only while these hold. util.cmake -> --flash_ready_signature.
set(_chip_jlink_flash_ready "0x400d0000,0x5,0x0")
# The XIP windows 0x10000000..0x1fffffff (datasheet 2.2): control-socket reads there pass the same gate. util.cmake ->
# --flash_window.
set(_chip_jlink_flash_window "0x10000000,0x10000000")

# double arithmetic, compares, int/float conversions and sqrt on the double-precision coprocessor (RP2350 datasheet
# 3.6.2) instead of the runtime's soft-float: each linked as --wrap=<name>, the runtime's own one stays __real_<name>.
# StartUp.hpp enables cp4 on both cores either way.
option(KVASIR_RP2350_DCP "RP2350: the compiler runtime's double functions on the DCP" ON)
set(_chip_sources)
set(_chip_linker_options)
if(KVASIR_RP2350_DCP)
    set(_dcp_source ${CMAKE_CURRENT_LIST_DIR}/../src/chip/dcp_double.S)
    set(_chip_sources ${_dcp_source})
    # the wrapped names are the .S's own `saving_func wrapper <name>` lines: the list and the code cannot drift
    file(STRINGS ${_dcp_source} _dcp_wrapped REGEX "^saving_func wrapper [A-Za-z_0-9]+$")
    list(TRANSFORM _dcp_wrapped REPLACE "^saving_func wrapper " "")
    if(NOT _dcp_wrapped)
        message(FATAL_ERROR "chip_rp2350: no 'saving_func wrapper <name>' line in ${_dcp_source}")
    endif()
    set_property(
        DIRECTORY
        APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS ${_dcp_source})
    foreach(f IN LISTS _dcp_wrapped)
        list(APPEND _chip_linker_options --wrap=${f})
    endforeach()
    # for code that must know: a double subnormal is flushed to zero on input and output (RP2350 datasheet 3.6.2)
    list(APPEND _chip_definitions KVASIR_RP2350_DCP=1)
endif()

# Kvasir's write semantics (svd_convert's defaults): a new unclassified write-only field stops the build
kvasir_chip_svd(peripherals ${CMAKE_CURRENT_LIST_DIR}/../chip.svd)

# DEVICES: chip/drivers.hpp builds on kvasir_devices, which the SDK then adds after project()
kvasir_chip(
    DEVICES
    MPU
    RP2350_M33_0
    UF2_FAMILY
    0xE48BFF59
    LINKER_FILE
    ${CMAKE_CURRENT_LIST_DIR}/../linker/chip.ld
    # for kvasir_executable(... RAM_ONLY): everything in SRAM, no flash region
    RAM_ONLY_LINKER_FILE
    ${CMAKE_CURRENT_LIST_DIR}/../linker/chip_ram_only.ld
    INCLUDE_DIR
    ${CMAKE_CURRENT_LIST_DIR}/../src
    REGISTERS
    peripherals
    DEFINITIONS
    ${_chip_definitions}
    SOURCES
    ${_chip_sources}
    LINKER_OPTIONS
    ${_chip_linker_options}
    IMAGE_CRC_EXCLUDE
    ${_chip_image_crc_exclude}
    JLINK_CONNECT_COMMANDS
    ${_chip_jlink_connect}
    JLINK_STALL_SIGNATURE
    ${_chip_jlink_stall}
    JLINK_STALL_RECOVERY
    ${_chip_jlink_stall_recovery}
    JLINK_FLASH_UNCACHED_OFFSET
    ${_chip_jlink_uncached}
    JLINK_FLASH_READY_SIGNATURE
    ${_chip_jlink_flash_ready}
    JLINK_FLASH_WINDOW
    ${_chip_jlink_flash_window})
