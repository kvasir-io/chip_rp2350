/* Linker script for RP2350 with 4MB Flash */

MEMORY{
    flash  (xr ) : ORIGIN = 0x10000000, LENGTH = 4M
    ram    (xrw) : ORIGIN = 0x20000000, LENGTH = 520K
}

INCLUDE common_flash.ld
/*INCLUDE common_eeprom.ld*/
INCLUDE common_ram.ld

INCLUDE common.ld

