#pragma once
#include "core/core.hpp"
#include "kvasir/Common/Core.hpp"
#include "picobin.hpp"

#include <array>
#include <cstdint>

namespace Kvasir { namespace Startup {
    [[gnu::used, gnu::section(".after_vectors")]] static constexpr auto ImageDef
      = Picobin::BlockLoop<Picobin::Block<Picobin::ArmSecure>>::words;

    template<typename... Ts>
    struct FirstInitStep<Tag::User, Ts...> {
        void operator()() {
            Core::startup();

            using PPB_S = Kvasir::Peripheral::PPB::Registers<0>;

            //enable FPU and RCP coprocessor
            apply(PPB_S::CPACR::overrideDefaults(write(PPB_S::CPACR::cp11, Register::value<3>()),
                                                 write(PPB_S::CPACR::cp10, Register::value<3>()),
                                                 write(PPB_S::CPACR::cp7, Register::value<3>())));

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
                  set(PSM_WDSEL::psm_ready),
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
}}   // namespace Kvasir::Startup

#include "kvasir/StartUp/StartUp.hpp"
