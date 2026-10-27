#pragma once
// The RP2350's steps of the RP boot sequence (rp_common/BootSequence.hpp firstInit, from FirstInitStep in
// chip/StartUp.hpp): registers only, before .data/.bss, every step always_inline.
//
// VTOR is already on this image's own table: Core::cleanCore() (core/StartUp.hpp) wrote it right before FirstInitStep,
// on every entry. On the RP2350 that matters for a RAM image packaged in flash (a load map and no VECTOR_TABLE item:
// `<product>_release_ram_only_packaged.uf2`): the bootrom leaves VTOR on the flash copy, and every exception vector
// would then come through XIP. An image started with VTOR already on its table is not changed by it. VTOR is
// 0xE000ED08 (RP2350 datasheet 3.7, Table 202); the table, 68 entries -> 512 bytes, is the first thing at
// ORIGIN(flash) / ORIGIN(ram), so naturally aligned.
#include "Blocks.hpp"
#include "WatchdogDomains.hpp"
#include "core/Barrier.hpp"
#include "kvasir/Register/Register.hpp"
#include "peripherals/PPB.hpp"
#include "peripherals/RESETS.hpp"
#include "peripherals/WATCHDOG.hpp"
#include "rp_common/BootInfo.hpp"
#include "rp_common/BootSequence.hpp"
#include "rp_common/Rosc.hpp"
#include "rp_common/WatchdogTick.hpp"

#include <cstdint>

namespace Kvasir::Rp {
struct BootTraits {
    using PpbS = Kvasir::Peripheral::PPB::Registers<0>;

    // Its tick running from here on, so any watchdog reset of this run keeps SRAM - also one this image does not make
    // itself (the boot ROM's reboot(), a debugger's): WatchdogTick.hpp.
    [[gnu::always_inline]] static void keepWatchdogTickRunning() {
        Kvasir::Rp::keepWatchdogTickRunning();
    }

    // Enable the RCP (cp7) and the double-precision coprocessor (cp4: the DCP of the current security state, RP2350
    // datasheet 3.6.2, md l.4715; CPACR.CP4 bits 9:8, l.9704), which dcp_double.S puts behind the runtime's double
    // functions; a coprocessor is enabled in CPACR before any access from Secure code (3.6, l.4562). The FPU (cp10,
    // cp11) is the core's: core_cortex_m33's Startup::Core::startup() opened it before this runs, so these two fields
    // are a read-modify-write that leaves CP10/CP11 as they are (no overrideDefaults: it would close the FPU again).
    [[gnu::always_inline]] static void enableCoprocessors() {
        apply(write(PpbS::CPACR::cp7, Register::value<3>()),
              write(PpbS::CPACR::cp4, Register::value<3>()));
        Kvasir::Core::dsb();
        Kvasir::Core::isb();
    }

    // The RCP's salt, if nothing has written it: the boot ROM seeds it on its boot path, which an image a debugger
    // started in RAM never went through. With the salt invalid any RCP instruction but canary_status and a salt write
    // is an RCP fault (NMI), and the boot ROM's API functions run RCP canary checks (RP2350 datasheet 3.6.3.1,
    // md l.5075-5081; 3.2.1, l.3799). Status 0xa500a500 valid / 0x00c300c3 invalid (l.5331); writing a valid salt is a
    // fault too (l.5083). Core 0's salt only. The value: 64 ROSC RANDOMBIT reads (rp_common/Rosc.hpp), not a secret.
    // Encodings as pico-sdk's hardware/rcp.h (rcp_canary_status, rcp_salt_core0).
    [[gnu::always_inline]] static void seedRcpSaltIfInvalid() {
        std::uint32_t status{};
        asm volatile("mrc p7, #1, %0, c0, c0, #0" : "=r"(status));
        if(status == 0x00C3'00C3U) {
            std::uint32_t lo{};
            std::uint32_t hi{};
            for(int i = 0; i != 32; ++i) {
                lo = (lo << 1) | Rosc::randomBit();
                hi = (hi << 1) | Rosc::randomBit();
            }
            asm volatile("mcrr p7, #8, %0, %1, c0" : : "r"(lo), "r"(hi) : "memory");
        }
    }

    // Watchdog off until the image arms it: only a chip-level reset resets it (RP2350 datasheet 12.9.1), so a watchdog
    // reboot leaves CTRL.ENABLE set and a later flash download reloads the counter. Not on a TBYB trial (5.1.17): an
    // image launched on trial runs under the boot ROM's watchdog (16.7 s) until it buys itself, and that watchdog is
    // what turns a hang before the image's own watchdog is armed into the reset that boots the other A/B partition.
    // Its Watchdog's periphery init takes over from there. After the salt and CPACR.CP7: bootInfo() is a boot ROM
    // call, and those run RCP canary checks.
    [[gnu::always_inline]] static void watchdogOffUnlessTrial() {
        if(!Kvasir::Bootrom::bootInfo().trialBoot()) {
            apply(clear(Kvasir::Peripheral::WATCHDOG::Registers<>::CTRL::enable));
        }
    }

    // Exclusive accesses (ldrex/strex, i.e. every std::atomic RMW and every Atomic::Spinlock) only reach the bus
    // fabric's global monitor for memory the core treats as shareable, and by default nothing is. Without this bit the
    // two cores' exclusives never arbitrate: both strex succeed, and a lock holds or fails depending on which core is
    // a cycle ahead. EXTEXCLALL sends every exclusive to the global monitor. Per core: core 0 here in a multicore
    // image, core 1 in SecondaryCoreInit. (pico-sdk: spinlock_set_extexclall.)
    [[gnu::always_inline]] static void routeExclusivesGlobally() {
        apply(set(PpbS::ACTLR::extexclall));
        Kvasir::Core::dsb();
        Kvasir::Core::isb();
    }

    [[gnu::always_inline]] static void afterCore1Parked() {
        keepWatchdogTickRunning();
        enableCoprocessors();
        seedRcpSaltIfInvalid();
        watchdogOffUnlessTrial();
#if defined(KVASIR_MULTICORE) && KVASIR_MULTICORE
        routeExclusivesGlobally();
#endif
    }

    // Every block of chip/Blocks.hpp into reset, and JTAG (no row: no driver); SYSINFO, SYSCFG, PLL_SYS and the QSPI
    // pads and IO keep running (chip/Blocks.hpp asserts the table and these cover every RESETS bit).
    [[gnu::always_inline]] static void holdBlocksInReset() {
        using Reset = Kvasir::Peripheral::RESETS::Registers<>::RESET;
        Rp::holdBlocksInReset<Kvasir::Chip::Blocks::Table>(set(Reset::jtag),
                                                           clear(Reset::sysinfo),
                                                           clear(Reset::syscfg),
                                                           clear(Reset::pll_sys),
                                                           clear(Reset::pads_qspi),
                                                           clear(Reset::io_qspi));
    }

    [[gnu::always_inline]] static void afterResetsHeld() {}

    [[gnu::always_inline]] static void selectWatchdogDomains() {
        WatchdogDomains::onTimeout();
        // every block (RESETS.WDSEL has RESETS.RESET's bits: RP2350 datasheet Tables 535/536, md l.25266/25316)
        Rp::setEveryField<Kvasir::Peripheral::RESETS::Registers<>::WDSEL>();
    }
};
}   // namespace Kvasir::Rp
