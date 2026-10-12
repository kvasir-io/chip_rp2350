include(${CMAKE_CURRENT_LIST_DIR}/../core/cmake/core.cmake)

set(TARGET_MPU RP2350_M33_0)
set(TARGET_UF2_CODE 0xE48BFF59)
set(TARGET_FLASH_SIZE 4194048)
set(TARGET_RAM_SIZE 532480) # 520 KiB: SRAM0-9, the datasheet's single 520 kB memory (RP2350 datasheet 4.2)
set(TARGET_EEPROM_SIZE 0)
# IMAGE_CRC leaves the picobin block loop out (.after_vectors, linker/common_vectors_body.inc.ld): picotool seal and
# sign rewrite its link word after the link, and the bootrom checks that block itself.
set(TARGET_IMAGE_CRC_EXCLUDE _LINKER_INTERN_after_vectors_start_ _LINKER_INTERN_after_vectors_end_)

# J-Link Commander lines after `connect` in the flash/reset/connect scripts (Kvasir_SDK cmake/jlink.cmake): core 1 back
# into its boot ROM before anything is reset or written. The commander's `r` restarts the connected core only, and a
# core 1 the old image launched runs on through the erase into the new image's RAM. PSM FRCE_OFF.PROC1 set, then
# cleared, through the atomic aliases: PSM_BASE 0x40018000 + FRCE_OFF 0x4, PROC1 = bit 24 (RP2350 datasheet 7.4.4 "List
# of registers", Table 531), +0x2000 set / +0x3000 clear (2.1.3 "Atomic register access"). uc_log's printer gets the
# same lines (util.cmake PRE_RESET_COMMANDS -> --pre_reset_command) and writes them before its own resets and downloads
# - it understands only `w4 <address> <value>`, keep them to that. Then WATCHDOG SCRATCH4 (WATCHDOG_BASE 0x400d8000 +
# 0x1c, 12.9.7 "List of registers") to 0: disarms Kvasir::CoreResetGuard's watchdog boot vector (5.2.4), whose RAM
# routine a download overwrites.
set(TARGET_JLINK_CONNECT_COMMANDS "w4 0x4001A004 0x01000000" "w4 0x4001B004 0x01000000" "w4 0x400d801c 0x00000000")

# QMI stuck in direct mode with a transfer that never ends: DIRECT_CSR (XIP_QMI_BASE 0x400d0000 + 0x00) EN (bit 0) and
# BUSY (bit 1) set (RP2350 datasheet 12.14, Table 1294), flash unreadable. A core reset does not free it; a watchdog
# reset of every domain but PROC_COLD does: SCRATCH4 to 0 (no CoreResetGuard needed), PSM.WDSEL (PSM_BASE 0x40018000 +
# 0x8, Table 532, bits 24:1), then WATCHDOG.CTRL.TRIGGER (0x400d8000 + 0x0, bit 31, Table 1248). The printer also runs
# the recovery before retrying a failed download (the QMI can wedge with DIRECT_CSR idle). util.cmake ->
# --stall_signature / --stall_recovery.
set(TARGET_JLINK_STALL_SIGNATURE "0x400d0000,0x3,0x3")
set(TARGET_JLINK_STALL_RECOVERY "w4 0x400d801c 0x00000000" "w4 0x40018008 0x01fffffe" "w4 0x400d8000 0x80000000")
# The firmware check also reads flash through XIP_NOCACHE_NOALLOC (0x14000000, RP2350 datasheet 2.2). util.cmake ->
# --flash_uncached_offset.
set(TARGET_JLINK_FLASH_UNCACHED_OFFSET "0x04000000")
# Flash readable over XIP only with DIRECT_CSR (0x400d0000) EN (bit 0) and ASSERT_CS0N (bit 2) clear: in direct mode an
# XIP access is a bus error (RP2350 datasheet 12.14.5, Table 1294). BUSY left out: XIP fetches set it too. The printer
# reads flash only while these hold. util.cmake -> --flash_ready_signature.
set(TARGET_JLINK_FLASH_READY_SIGNATURE "0x400d0000,0x5,0x0")
# The XIP windows 0x10000000..0x1fffffff (datasheet 2.2): control-socket reads there pass the same gate. util.cmake ->
# --flash_window.
set(TARGET_JLINK_FLASH_WINDOW "0x10000000,0x10000000")

set(LINKER_FILE ${CMAKE_CURRENT_LIST_DIR}/../linker/chip.ld)
# For kvasir_executable(... RAM_ONLY): everything in SRAM, no flash region.
set(LINKER_FILE_RAM_ONLY ${CMAKE_CURRENT_LIST_DIR}/../linker/chip_ram_only.ld)

# the write-only guard: every write-only field of the SVD is classified (oneToSet, a key, or <!-- Kvasir: write-only
# accepted -->), so a new one stops the build; registers with no readable field are never read
svd_convert(
    peripherals
    SVD_FILE
    ${CMAKE_CURRENT_LIST_DIR}/../chip.svd
    OUTPUT_DIRECTORY
    peripherals
    WRITE_ONLY_GUARD
    error
    WRITE_ONLY_REGISTERS
    derived
    WRITE_ONLY_MASK
    ON)

# kvasir_devices: chip.hpp includes its drivers unconditionally (rp_common/I2CQueued.hpp -> I2CBusRecovery.hpp ->
# kvasir/Devices/I2C/LineRecovery.hpp, and the USB backend), so every image needs it. Found like CHIP_ROOT
# (KVASIR_DEVICES_ROOT: variable, environment, else next to the SDK); the SDK adds it after project() unless the
# firmware has it already.
kvasir_resolve_root(KVASIR_DEVICES_ROOT kvasir_devices)
kvasir_add_package(${KVASIR_DEVICES_ROOT} kvasir_devices kvasir_devices)
target_link_libraries(peripherals INTERFACE kvasir::devices)
