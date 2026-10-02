#pragma once
#include "chip/rp_common/ResetMap.hpp"
#include "core/core.hpp"
#include "kvasir/Common/Core.hpp"
#include "kvasir/StartUp/LinkerSymbols.hpp"
#include "picobin.hpp"
#include "rp_common/Multicore.hpp"
#include "rp_common/bootrom_functions.hpp"

#include <array>
#include <cstdint>

namespace Kvasir { namespace Startup {
    [[gnu::used, gnu::section(".after_vectors")]] static constexpr auto ImageDef
      = Picobin::BlockLoop<Picobin::Block<Picobin::ArmSecure>>::words;

    template<typename... Ts>
    struct FirstInitStep<Tag::User, Ts...> {
        void operator()() {
            Core::startup();

            // VTOR at this image's own table. The bootrom points it where it found the image:
            // for a flash image that is the table, but for a RAM image packaged in flash (a
            // load map and no VECTOR_TABLE item: `<product>_release_ram_only_packaged.uf2`) it
            // is the flash copy -- every exception vector then comes through XIP, and
            // isFlashBinary() calls the image FLASH. The shipped image already runs with VTOR
            // here (its decryption bootloader sets it), so for it this changes nothing.
            // VTOR = 0xE000ED08, TBLOFF bits [31:7] (Armv8-M ARM DDI0553B.y D1.2.272, RP2350
            // data sheet Table 202). The table has to be naturally aligned (B3.30, RVDPD): 68
            // entries -> 512 bytes; it is the first thing at ORIGIN(flash) / ORIGIN(ram).
            apply(write(Kvasir::Peripheral::SCB::Registers<>::VTOR::FULLREGISTER,
                        static_cast<std::uint32_t>(
                          reinterpret_cast<std::uintptr_t>(&_LINKER_vectors_start_))));
            asm volatile("dsb\n isb" ::: "memory");

            // Core 1 back into the bootrom's holding pen, before this image touches RAM. A
            // debugger's flash-and-reset restarts core 0 only: a core 1 the image before had
            // launched keeps running that image's code -- out of RAM this one is about to
            // fill with its own data, and from there anywhere. Seen 2026-09-19 (i2c_testing):
            // a firmware that never starts core 1, flashed over one that had it spinning in a
            // RAM loop, got a sanitizer report from a function no path could have reached with
            // that state, log lines with absurd values on core 1's log ring, and core 1 found
            // in lockup (PC 0xEFFFFFFE) afterwards. PSM FRCE_OFF.PROC1 (data sheet, table
            // 531); it is the one FRCE_OFF bit erratum RP2350-E19 allows to be set.
            Multicore::resetCore1();

            using PPB_S = Kvasir::Peripheral::PPB::Registers<0>;

            //enable FPU and RCP coprocessor
            apply(PPB_S::CPACR::overrideDefaults(write(PPB_S::CPACR::cp11, Register::value<3>()),
                                                 write(PPB_S::CPACR::cp10, Register::value<3>()),
                                                 write(PPB_S::CPACR::cp7, Register::value<3>())));
            asm volatile("dsb\n isb" ::: "memory");

            // The RCP's salt, if nothing has written it: the boot ROM seeds it on its boot path,
            // which an image a debugger started in RAM never went through. With the salt invalid
            // any RCP instruction but canary_status and a salt write is an RCP fault (NMI), and
            // the boot ROM's API functions run RCP canary checks (RP2350 datasheet 3.6.3.1,
            // md l.5075-5081; 3.2.1, l.3799). Status 0xa500a500 valid / 0x00c300c3 invalid
            // (l.5331); writing a valid salt is a fault too (l.5083). Core 0's salt only. The
            // value: 64 ROSC RANDOMBIT reads (0x400e8000 + 0x20, md l.1552, l.28148), not a
            // secret. Encodings as pico-sdk's hardware/rcp.h (rcp_canary_status, rcp_salt_core0).
            {
                std::uint32_t status{};
                asm volatile("mrc p7, #1, %0, c0, c0, #0" : "=r"(status));
                if(status == 0x00C3'00C3U) {
                    auto const bit = []() {
                        return *reinterpret_cast<std::uint32_t const volatile*>(0x400E'8020U) & 1U;
                    };
                    std::uint32_t lo{};
                    std::uint32_t hi{};
                    for(int i = 0; i != 32; ++i) {
                        lo = (lo << 1) | bit();
                        hi = (hi << 1) | bit();
                    }
                    asm volatile("mcrr p7, #8, %0, %1, c0" : : "r"(lo), "r"(hi) : "memory");
                }
            }

#if defined(KVASIR_MULTICORE) && KVASIR_MULTICORE
            // Exclusive accesses (ldrex/strex, i.e. every std::atomic RMW and every
            // Atomic::Spinlock) only reach the bus fabric's global monitor for memory the
            // core treats as shareable, and by default nothing is. Without this bit the
            // two cores' exclusives never arbitrate: both strex succeed, and a lock holds
            // or fails depending on which core is a cycle ahead. EXTEXCLALL sends every
            // exclusive to the global monitor. Per core: SecondaryCoreInit sets it on
            // core 1. (pico-sdk: spinlock_set_extexclall.)
            apply(set(PPB_S::ACTLR::extexclall));
            asm volatile("dsb\n isb" ::: "memory");
#endif

            using Reset = Kvasir::Peripheral::RESETS::Registers<>::RESET;
            apply(set(Reset::usbctrl),
                  set(Reset::uart1),
                  set(Reset::uart0),
                  set(Reset::trng),
                  set(Reset::timer1),
                  set(Reset::timer0),
                  set(Reset::tbman),
                  clear(Reset::sysinfo),
                  clear(Reset::syscfg),
                  set(Reset::spi1),
                  set(Reset::spi0),
                  set(Reset::sha256),
                  set(Reset::pwm),
                  set(Reset::pll_usb),
                  clear(Reset::pll_sys),
                  set(Reset::pio2),
                  set(Reset::pio1),
                  set(Reset::pio0),
                  clear(Reset::pads_qspi),
                  set(Reset::pads_bank0),
                  set(Reset::jtag),
                  clear(Reset::io_qspi),
                  set(Reset::io_bank0),
                  set(Reset::i2c1),
                  set(Reset::i2c0),
                  set(Reset::hstx),
                  set(Reset::dma),
                  set(Reset::busctrl),
                  set(Reset::adc));

            using PSM_WDSEL = Kvasir::Peripheral::PSM::Registers<>::WDSEL;
            apply(set(PSM_WDSEL::proc1),
                  set(PSM_WDSEL::proc0),
                  set(PSM_WDSEL::accessctrl),
                  set(PSM_WDSEL::sio),
                  set(PSM_WDSEL::xip),
                  set(PSM_WDSEL::sram9),
                  set(PSM_WDSEL::sram8),
                  set(PSM_WDSEL::sram7),
                  set(PSM_WDSEL::sram6),
                  set(PSM_WDSEL::sram5),
                  set(PSM_WDSEL::sram4),
                  set(PSM_WDSEL::sram3),
                  set(PSM_WDSEL::sram2),
                  set(PSM_WDSEL::sram1),
                  set(PSM_WDSEL::sram0),
                  set(PSM_WDSEL::bootram),
                  set(PSM_WDSEL::rom),
                  set(PSM_WDSEL::busfabric),
                  set(PSM_WDSEL::ready),
                  set(PSM_WDSEL::clocks),
                  set(PSM_WDSEL::resets),
                  set(PSM_WDSEL::xosc),
                  set(PSM_WDSEL::rosc),
                  set(PSM_WDSEL::otp),
                  set(PSM_WDSEL::proc_cold));

            using WDSEL = Kvasir::Peripheral::RESETS::Registers<>::WDSEL;
            apply(set(WDSEL::usbctrl),
                  set(WDSEL::uart1),
                  set(WDSEL::uart0),
                  set(WDSEL::trng),
                  set(WDSEL::timer1),
                  set(WDSEL::timer0),
                  set(WDSEL::tbman),
                  set(WDSEL::sysinfo),
                  set(WDSEL::syscfg),
                  set(WDSEL::spi1),
                  set(WDSEL::spi0),
                  set(WDSEL::sha256),
                  set(WDSEL::pwm),
                  set(WDSEL::pll_usb),
                  set(WDSEL::pll_sys),
                  set(WDSEL::pio2),
                  set(WDSEL::pio1),
                  set(WDSEL::pio0),
                  set(WDSEL::pads_qspi),
                  set(WDSEL::pads_bank0),
                  set(WDSEL::jtag),
                  set(WDSEL::io_qspi),
                  set(WDSEL::io_bank0),
                  set(WDSEL::i2c1),
                  set(WDSEL::i2c0),
                  set(WDSEL::hstx),
                  set(WDSEL::dma),
                  set(WDSEL::busctrl),
                  set(WDSEL::adc));
        }
    };

    // What core 1 must do for itself on entry. The bootrom parks core 1 with only cp7 (the
    // RCP) enabled in CPACR; the FPU is off, and a hard-float build touches it in its first
    // frame. SecondaryCore's trampoline ORs this mask into CPACR before any compiled code
    // runs. cp7 is kept in the mask so a ROM call from core 1 (which uses RCP instructions)
    // keeps working even if the trampoline ever wrote instead of ORed. Everything else in
    // FirstInitStep above (resets, PSM, watchdog select) is chip-wide and core 0's job.
    template<typename... Ts>
    struct SecondaryCoreInit<Tag::User, Ts...> {
        static constexpr std::uint32_t cpacrEnable
          = Core::SecondaryCoreTraits::cpacrEnable | (3U << 14U);   // + cp7

        // The counterpart of the EXTEXCLALL write in FirstInitStep: without it core 1's
        // exclusives use its local monitor only and no lock in the system holds.
        void operator()() {
            using PPB_S = Kvasir::Peripheral::PPB::Registers<0>;
            apply(set(PPB_S::ACTLR::extexclall));
            asm volatile("dsb\n isb" ::: "memory");
        }

        // From core 0: the FIFO handshake with the bootrom's holding pen, and the PSM reset
        // that puts core 1 back into it.
        [[nodiscard]] static bool launch(std::uint32_t entry,
                                         std::uint32_t sp,
                                         std::uint32_t vtor) {
            return Multicore::launchCore1(entry, sp, vtor);
        }

        static void reset() { Multicore::resetCore1(); }
    };

    // The stack guard's per-boot value: the boot ROM's BOOT_RANDOM, "a 128-bit random number
    // generated on each boot" (RP2350 datasheet 5.4.8.17 get_sys_info, flag 0x0010, 4 words),
    // from the TRNG ROSC sampled into SHA-256 on every boot (5.2 boot sequence, "Generate Boot
    // Random"). get_sys_info returns the words filled: the supported-flags word, then the 4.
    template<typename... Ts>
    struct StackGuardEntropy<Tag::User, Ts...> {
        std::uint32_t operator()() const {
            constexpr std::uint32_t      BootRandom = 0x0010;
            std::array<std::uint32_t, 5> buffer{};
            auto const words = detail::get_sys_info(buffer.data(), buffer.size(), BootRandom);
            if(words < 5 || (buffer[0] & BootRandom) == 0) { return 0xdeadc0deU; }
            return buffer[1] ^ buffer[2] ^ buffer[3] ^ buffer[4];
        }
    };
}}   // namespace Kvasir::Startup

#include "kvasir/StartUp/StartUp.hpp"
