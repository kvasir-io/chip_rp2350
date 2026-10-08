include(${CMAKE_CURRENT_LIST_DIR}/../core/cmake/core.cmake)

set(TARGET_MPU RP2350_M33_0)
set(TARGET_UF2_CODE 0xE48BFF59)
set(TARGET_FLASH_SIZE 4194048)
set(TARGET_RAM_SIZE 532480) # 520 KiB: SRAM0-9, the datasheet's single 520 kB memory (RP2350 datasheet 4.2)
set(TARGET_EEPROM_SIZE 0)
set(TARGET_EXTRA_FLASH_SECTIONS .boot2)
# IMAGE_CRC leaves the picobin block loop out (.after_vectors, linker/common_vectors_body.inc.ld): picotool seal and
# sign rewrite its link word after the link, and the bootrom checks that block itself.
set(TARGET_IMAGE_CRC_EXCLUDE _LINKER_INTERN_after_vectors_start_ _LINKER_INTERN_after_vectors_end_)

# J-Link Commander lines after `connect` in the flash/reset/connect scripts (Kvasir_SDK cmake/jlink.cmake): core 1 back
# into its boot ROM before anything is reset or written. The commander's `r` restarts the connected core only, and a
# core 1 the old image launched runs on through the erase into the new image's RAM. PSM FRCE_OFF.PROC1 set, then
# cleared, through the atomic aliases: PSM_BASE 0x40018000 + FRCE_OFF 0x4, PROC1 = bit 24 (RP2350 datasheet 7.4.4 "List
# of registers", Table 531), +0x2000 set / +0x3000 clear (2.1.3 "Atomic register access"). uc_log's printer gets the
# same lines (util.cmake PRE_RESET_COMMANDS -> --pre_reset_command) and writes them before its own resets and downloads
# - it understands only `w4 <address> <value>`, keep them to that.
set(TARGET_JLINK_CONNECT_COMMANDS "w4 0x4001A004 0x01000000" "w4 0x4001B004 0x01000000")

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
