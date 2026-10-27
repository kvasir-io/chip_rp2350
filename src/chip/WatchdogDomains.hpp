#pragma once
// The RP2350's PSM.WDSEL: the power-on state machine stages a watchdog reset runs through (RP2350 datasheet 7.4.4,
// WDSEL Table 532), one list per intent (chip_rp_common BootSequence.hpp, BootromReboot.hpp):
// - onTimeout: what a watchdog that runs out resets, set by FirstInitStep - every stage, PROC_COLD included: a hang
//   gets the fullest reset the watchdog can give.
// - onReboot: Kvasir's own reboot (Rp::watchdogReboot) and the probe's stall recovery (cmake/chip.cmake,
//   `0x01fffffe`) - everything but PROC_COLD, as the boot ROM's reboot(): a deliberate reboot keeps the processor cold
//   domain, which by POWMAN WDSEL.RESET_SWCORE ("the same as setting RSM_WDSEL_PROC_COLD", "from a hardware debug
//   perspective ... the same effect as a power-on reset", Table 490, md l.23588) holds the debug state; that the
//   halt-on-reset bits are part of it is not stated there (unchecked). The RESETS stage is in both (every peripheral
//   back into reset), and so are the PROC stages (core 1 back into the boot ROM's holding pen).
#include "kvasir/Register/Register.hpp"
#include "peripherals/POWMAN.hpp"
#include "peripherals/PSM.hpp"
#include "peripherals/SYSCFG.hpp"
#include "rp_common/WaitBounds.hpp"
#include "rp_common/WatchdogTick.hpp"

#include <cstdint>

namespace Kvasir::Rp {
struct WatchdogDomains {
    using PsmWdsel = Kvasir::Peripheral::PSM::Registers<>::WDSEL;

    // Every stage but PROC_COLD, which the two intents set differently (`difference`)
    template<typename... Difference>
    [[gnu::always_inline]] static void selectStages(Difference... difference) {
        apply(set(PsmWdsel::proc1),
              set(PsmWdsel::proc0),
              set(PsmWdsel::accessctrl),
              set(PsmWdsel::sio),
              set(PsmWdsel::xip),
              set(PsmWdsel::sram9),
              set(PsmWdsel::sram8),
              set(PsmWdsel::sram7),
              set(PsmWdsel::sram6),
              set(PsmWdsel::sram5),
              set(PsmWdsel::sram4),
              set(PsmWdsel::sram3),
              set(PsmWdsel::sram2),
              set(PsmWdsel::sram1),
              set(PsmWdsel::sram0),
              set(PsmWdsel::bootram),
              set(PsmWdsel::rom),
              set(PsmWdsel::busfabric),
              set(PsmWdsel::ready),
              set(PsmWdsel::clocks),
              set(PsmWdsel::resets),
              set(PsmWdsel::xosc),
              set(PsmWdsel::rosc),
              set(PsmWdsel::otp),
              difference...);
    }

    [[gnu::always_inline]] static void onTimeout() { selectStages(set(PsmWdsel::proc_cold)); }

    [[gnu::always_inline]] static void onReboot() { selectStages(clear(PsmWdsel::proc_cold)); }

    // SYSCFG.AUXCTRL bit 0 (Table 1317, md l.61148): "Force POWMAN clock to switch to LPOSC ... This must be set
    // before initiating a watchdog reset of the RSM from a stage that includes CLOCKS, if POWMAN is running from
    // clk_ref" - its default (SEQ_CFG.USING_FAST_POWCK reset 1, Table 491 md l.23609), and both intents include
    // CLOCKS. The table states no time for the switch; SEQ_CFG.USING_FAST_POWCK (RO) reads 0 once "the POWMAN clock
    // is running from the low power oscillator": waited for, bounded (Chip::PowmanLposcBound). False on a timeout.
    // While the bit is set POWMAN (and the AON timer) runs from the 32 kHz LPOSC, each POWMAN access ~100 us: only
    // right before a reset of our own, as the boot ROM's reboot() does.
    [[nodiscard,
      gnu::always_inline]] static bool
    forcePowmanOnLposc() {
        using AuxCtrl = Kvasir::Peripheral::SYSCFG::Registers<>::AUXCTRL;
        using SeqCfg  = Kvasir::Peripheral::POWMAN::Registers<>::SEQ_CFG;
        apply(set(Kvasir::Register::bit<0>(AuxCtrl::auxctrl)));
        return static_cast<bool>(Kvasir::Register::waitUntil<Kvasir::Chip::PowmanLposcBound,
                                                             Kvasir::Register::OnTimeout::Report>(
          Kvasir::Register::isClear(SeqCfg::using_fast_powck)));
    }

    // The bit survives the boot ROM's reboot() (found set in the image it started): clock_config.hpp vregInit
    // clears it at boot, for every image. One read: bit 0 tested in the field's value, the field written back
    // without it (one store, AUXCTRL's other bits are reserved) - a clear(bit<0>) after the read would load the
    // register a second time.
    [[gnu::always_inline]] static void releasePowmanLposc() {
        using AuxCtrl                       = Kvasir::Peripheral::SYSCFG::Registers<>::AUXCTRL;
        constexpr std::uint32_t lposcForced = Kvasir::Register::bit<0>(AuxCtrl::auxctrl).mask;
        if(auto const aux = get<0>(apply(read(AuxCtrl::auxctrl))); (aux & lposcForced) != 0) {
            apply(write(AuxCtrl::auxctrl, aux & ~lposcForced));
        }
    }

    // Right before Kvasir triggers a watchdog reset itself (rp_common WatchdogReset.hpp, the one owner): the tick,
    // then POWMAN onto the LPOSC. A timeout of that wait triggers anyway: the reset is what was asked for, and a
    // reset that never comes would be worse.
    [[gnu::always_inline]] static void beforeReset() {
        Kvasir::Rp::
          keepWatchdogTickRunning();   // or the reset does not keep SRAM (WatchdogTick.hpp)
        (void)forcePowmanOnLposc();
    }
};
}   // namespace Kvasir::Rp
