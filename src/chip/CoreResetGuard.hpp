#pragma once
// RP2350: core 1 and DMA stopped before the boot ROM touches the flash after a reset of core 0 alone. Armed by every
// image that starts core 1 (SecondaryCoreInit::armCoreResetGuard; NoCoreResetGuard in SecondaryCore's list opts out,
// RAM_ONLY images go without).
//
// AIRCR.SYSRESETREQ is a warm reset of the requesting core only (RP2350 datasheet 13.9); core 1 and DMA run on and keep
// reading flash through the QMI while the boot ROM sets it up: their reads can return junk and the ROM can stall.
//
// The ROM's watchdog boot vector (5.2.4, Table 451: SCRATCH4..7, entry with the Thumb bit) is called before the ROM
// touches the QMI and may return. The routine here (a RAM function) aborts DMA, puts core 1 back into the ROM's holding
// pen and returns. The ROM keeps its pre-boot stack in boot RAM (4.3), so main SRAM is as the image left it.
//
// Not covered: the RCP salt fault of a core-0 reset while core 1 runs (seeded before the vector); debugger resets/
// downloads (pre-reset commands clear SCRATCH4); Kvasir's own reboots (the ROM's reboot); power-on/brown-out/RUN-pin
// resets. PM::reset_cause() is unchanged.

#include "kvasir/Register/Register.hpp"
#include "kvasir/StartUp/LinkerSymbols.hpp"
#include "kvasir/Util/attributes.hpp"
#include "peripherals/WATCHDOG.hpp"

#include <cstdint>

namespace Kvasir {
struct CoreResetGuard {
    static constexpr std::uint32_t BootMagic = 0xB007'C0D3U;   // datasheet 5.2.4

    // The routine: called by the ROM with the stack pointer from SCRATCH6, returns to it. Registers r0-r3 only (the
    // ROM calls it as a function), every literal in its own pool (.ltorg) in RAM with it. Addresses: RESETS 0x40020000
    // (RESET_DONE +0x08, bit 2 DMA; 7.5.3), DMA 0x50000000 (CHAN_ABORT +0x464, bits 15:0, high until in-flight
    // transfers are flushed; 12.6.10 Table 1182), PSM 0x40018000 (FRCE_OFF +0x04, bit 24 PROC1, 7.4.4 Table 531),
    // atomic set / clear aliases +0x2000 / +0x3000 (2.1.3).
    [[KVASIR_RAM_FUNC_ATTRIBUTES,
      gnu::naked,
      gnu::used]] static void
    entry() {
        KVASIR_RAM_FUNC_MARK();
        asm volatile(
          ".syntax unified\n"
          // DMA: every channel aborted, if the block is out of reset (an access to a block held in reset
          // is not answered)
          "  ldr  r0, =0x40020008\n"
          "  ldr  r1, [r0]\n"
          "  tst  r1, #4\n"
          "  beq  12f\n"
          "  ldr  r0, =0x50000464\n"
          "  movw r1, #0xFFFF\n"
          "  str  r1, [r0]\n"
          "11:\n"
          "  ldr  r1, [r0]\n"
          "  cmp  r1, #0\n"
          "  bne  11b\n"
          "12:\n"
          // core 1: FRCE_OFF.PROC1 set, read back set, released
          "  ldr  r0, =0x4001A004\n"
          "  mov  r1, #0x01000000\n"
          "  str  r1, [r0]\n"
          "  ldr  r2, =0x40018004\n"
          "13:\n"
          "  ldr  r3, [r2]\n"
          "  tst  r3, r1\n"
          "  beq  13b\n"
          "  ldr  r0, =0x4001B004\n"
          "  str  r1, [r0]\n"
          "  bx   lr\n"
          ".ltorg\n");
    }

    // After initMemory (the Startup list's runtimeInit): the routine is in RAM with .data. Armed with SCRATCH4 last,
    // so a reset in between finds no half-written vector (the ROM checks both magic words). The stack: the image's
    // initial one - nothing on it outlives a reset.
    static void runtimeInit() {
        using WD      = Kvasir::Peripheral::WATCHDOG::Registers<>;
        auto const at = reinterpret_cast<std::uint32_t>(&entry) | 1U;   // Thumb
        apply(write(WD::SCRATCH7::FULLREGISTER, at));
        apply(
          write(WD::SCRATCH6::FULLREGISTER,
                static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&_LINKER_stack_end_))));
        apply(write(WD::SCRATCH5::FULLREGISTER, at ^ (0U - BootMagic)));
        apply(write(WD::SCRATCH4::FULLREGISTER, Register::value<BootMagic>()));
    }
};
}   // namespace Kvasir
