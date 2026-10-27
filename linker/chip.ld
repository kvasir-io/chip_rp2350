/* Linker script for RP2350 with 4MB Flash. The RAM is SRAM0-9, the datasheet's single 520 kB memory (RP2350 datasheet
   4.2): the image uses all of it (also chip_ram_only.ld). */

MEMORY{
    flash  (xr ) : ORIGIN = 0x10000000, LENGTH = 4M
    ram    (xrw) : ORIGIN = 0x20000000, LENGTH = 520K
}

INCLUDE common_flash.ld
/*INCLUDE common_eeprom.ld*/
INCLUDE common_ram.ld

INCLUDE common.ld

