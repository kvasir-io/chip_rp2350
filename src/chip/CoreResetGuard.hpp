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
// resets. PM::resetCause() is unchanged.

#include "kvasir/Port/LinkerSymbols.hpp"
#include "kvasir/Register/Register.hpp"
#include "kvasir/Util/attributes.hpp"
#include "peripherals/DMA.hpp"
#include "peripherals/PSM.hpp"
#include "peripherals/RESETS.hpp"
#include "rp_common/RegisterAlias.hpp"
#include "rp_common/WatchdogBoot.hpp"

#include <cstdint>

namespace Kvasir {
struct CoreResetGuard {
    static constexpr std::uint32_t bootMagic = Rp::WatchdogBoot::magic;   // datasheet 5.2.4

    using ResetDone = Kvasir::Peripheral::RESETS::Registers<>::RESET_DONE;
    using ChanAbort = Kvasir::Peripheral::DMA::Registers<>::CHAN_ABORT;
    using FrceOff   = Kvasir::Peripheral::PSM::Registers<>::FRCE_OFF;

    // The routine: called by the ROM with the stack pointer from SCRATCH6, returns to it. Registers r0-r3 only (the
    // ROM calls it as a function), every literal in its own pool (.ltorg) in RAM with it. Every address and bit is an
    // operand from the generated registers: RESETS RESET_DONE.DMA (7.5.3), DMA CHAN_ABORT (bits 15:0, high until
    // in-flight transfers are flushed; 12.6.10 Table 1182), PSM FRCE_OFF.PROC1 (7.4.4 Table 531) through the atomic
    // set / clear aliases (2.1.3, rp_common/RegisterAlias.hpp).
    [[KVASIR_RAM_FUNC_ATTRIBUTES,
      gnu::naked,
      gnu::used]] static void
    entry() {
        KVASIR_RAM_FUNC_MARK();
        asm volatile(
          ".syntax unified\n"
          // DMA: every channel aborted, if the block is out of reset (an access to a block held in reset
          // is not answered)
          "  ldr  r0, =%c[resetDone]\n"
          "  ldr  r1, [r0]\n"
          "  tst  r1, %[dmaDone]\n"
          "  beq  12f\n"
          "  ldr  r0, =%c[chanAbort]\n"
          "  movw r1, %[allChannels]\n"
          "  str  r1, [r0]\n"
          "11:\n"
          "  ldr  r1, [r0]\n"
          "  cmp  r1, #0\n"
          "  bne  11b\n"
          "12:\n"
          // core 1: FRCE_OFF.PROC1 set, read back set, released
          "  ldr  r0, =%c[frceOffSet]\n"
          "  mov  r1, %[proc1]\n"
          "  str  r1, [r0]\n"
          "  ldr  r2, =%c[frceOff]\n"
          "13:\n"
          "  ldr  r3, [r2]\n"
          "  tst  r3, r1\n"
          "  beq  13b\n"
          "  ldr  r0, =%c[frceOffClr]\n"
          "  str  r1, [r0]\n"
          "  bx   lr\n"
          ".ltorg\n" ::[resetDone] "i"(ResetDone::Addr::value),
          [dmaDone] "i"(ResetDone::dma.mask),
          [chanAbort] "i"(ChanAbort::Addr::value),
          [allChannels] "i"(ChanAbort::chan_abort.mask),
          [frceOff] "i"(FrceOff::Addr::value),
          [frceOffSet] "i"(FrceOff::Addr::value + Rp::aliasSet),
          [frceOffClr] "i"(FrceOff::Addr::value + Rp::aliasClr),
          [proc1] "i"(FrceOff::proc1.mask));
    }

    // After initMemory (the Startup list's runtimeInit): the routine is in RAM with .data. Armed through
    // Rp::WatchdogBoot::arm (SCRATCH4 last). The stack: the image's initial one - nothing on it outlives a reset.
    static void runtimeInit() {
        auto const at = reinterpret_cast<std::uint32_t>(&entry) | 1U;   // Thumb
        Rp::WatchdogBoot::arm(at, static_cast<std::uint32_t>(Startup::PrimaryStackTop::value()));
    }

    // The watchdog boot vector is set: a reset of core 0 alone runs the routine.
    [[nodiscard]] static bool armed() { return Rp::WatchdogBoot::armed(); }
};
}   // namespace Kvasir
