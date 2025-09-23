include(${CMAKE_CURRENT_LIST_DIR}/../core/cmake/core.cmake)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../src/chip/rp_common/pioasm)

set(TARGET_MPU RP2350_M33_0)
set(TARGET_UF2_CODE 0xE48BFF5B)
set(TARGET_FLASH_SIZE 4194048)
set(TARGET_RAM_SIZE 524288)
set(TARGET_EEPROM_SIZE 0)
set(TARGET_EXTRA_FLASH_SECTIONS .boot2)

set(LINKER_FILE ${CMAKE_CURRENT_LIST_DIR}/../linker/chip.ld)

svd_convert(peripherals SVD_FILE ${CMAKE_CURRENT_LIST_DIR}/../chip.svd OUTPUT_DIRECTORY peripherals)
