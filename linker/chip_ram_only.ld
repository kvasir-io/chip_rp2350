/* RP2350, RAM only: the whole image in SRAM, nothing in flash. For images loaded by the
   bootrom's UF2 path or a debugger straight into RAM (flash/nuke and its kind). */
MEMORY{
    ram    (xrw) : ORIGIN = 0x20000000, LENGTH = 520K
}

INCLUDE common_ram_only.ld

INCLUDE common.ld
