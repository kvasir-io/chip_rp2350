#pragma once

// the specialisations name the chip's interrupts
#include "Interrupt.hpp"
#include "rp_common/PinConfig.hpp"

namespace Kvasir { namespace PinConfig {
    template<>
    struct ChipTraits<Chip::Part::rp2350a> {
        static constexpr std::size_t pinCount
          = static_cast<std::size_t>(pinCountOf(Chip::Part::rp2350a));
    };

    template<>
    struct ChipTraits<Chip::Part::rp2350b> {
        static constexpr std::size_t pinCount
          = static_cast<std::size_t>(pinCountOf(Chip::Part::rp2350b));
    };

    // A and B: the same DMA and PWM blocks
    template<Chip::Part P>
        requires(!isRp2040(P))
    struct DmaTraits<P> {
        static constexpr std::size_t channelCount   = 16;
        static constexpr std::size_t interruptCount = 4;

        static constexpr auto interrupts = brigand::list<decltype(Kvasir::Interrupt::dma_0),
                                                         decltype(Kvasir::Interrupt::dma_1),
                                                         decltype(Kvasir::Interrupt::dma_2),
                                                         decltype(Kvasir::Interrupt::dma_3)>{};
    };

    template<Chip::Part P>
        requires(!isRp2040(P))
    struct PwmTraits<P> {
        static constexpr auto interrupts = brigand::list<decltype(Kvasir::Interrupt::pwm_wrap_0)>{};
    };

    // the pins' UART/SPI/I2C roles: rp_common/PinConfig.hpp functionSelect (formulas from Table 645)

}}   // namespace Kvasir::PinConfig
