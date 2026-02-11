#pragma once

#include "rp_common/PinConfig.hpp"

namespace Kvasir { namespace PinConfig {
    template<>
    struct ChipTraits<ChipVariant::RP2350A> {
        static constexpr std::size_t pinCount = 30;
    };

    template<>
    struct ChipTraits<ChipVariant::RP2350B> {
        static constexpr std::size_t pinCount = 48;
    };

    template<>
    struct DmaTraits<ChipVariant::RP2350A> {
        static constexpr std::size_t channelCount   = 16;
        static constexpr std::size_t interruptCount = 4;

        static constexpr auto Interrupts = brigand::list<decltype(Kvasir::Interrupt::dma_0),
                                                         decltype(Kvasir::Interrupt::dma_1),
                                                         decltype(Kvasir::Interrupt::dma_2),
                                                         decltype(Kvasir::Interrupt::dma_3)>{};
    };

    template<>
    struct DmaTraits<ChipVariant::RP2350B> {
        static constexpr std::size_t channelCount   = 16;
        static constexpr std::size_t interruptCount = 4;

        static constexpr auto Interrupts = brigand::list<decltype(Kvasir::Interrupt::dma_0),
                                                         decltype(Kvasir::Interrupt::dma_1),
                                                         decltype(Kvasir::Interrupt::dma_2),
                                                         decltype(Kvasir::Interrupt::dma_3)>{};
    };

    template<>
    struct PwmTraits<ChipVariant::RP2350A> {
        static constexpr auto Interrupts = brigand::list<decltype(Kvasir::Interrupt::pwm_wrap_0)>{};
    };

    template<>
    struct PwmTraits<ChipVariant::RP2350B> {
        static constexpr auto Interrupts = brigand::list<decltype(Kvasir::Interrupt::pwm_wrap_0)>{};
    };

    template<>
    struct UartPinMap<ChipVariant::RP2350A> {
        using PinEntry                                 = std::pair<UartPinType, UartPinType>;
        static constexpr std::array<PinEntry, 30> pins = {
          PinEntry{UartPinType::Tx0,  UartPinType::Tx0},
          PinEntry{UartPinType::Rx0,  UartPinType::Rx0},
          PinEntry{UartPinType::Tx0, UartPinType::Cts0},
          PinEntry{UartPinType::Rx0, UartPinType::Rts0},
          PinEntry{UartPinType::Tx1,  UartPinType::Tx1},
          PinEntry{UartPinType::Rx1,  UartPinType::Rx1},
          PinEntry{UartPinType::Tx1, UartPinType::Cts1},
          PinEntry{UartPinType::Rx1, UartPinType::Rts1},
          PinEntry{UartPinType::Tx1,  UartPinType::Tx1},
          PinEntry{UartPinType::Rx1,  UartPinType::Rx1},
          PinEntry{UartPinType::Tx1, UartPinType::Cts1},
          PinEntry{UartPinType::Rx1, UartPinType::Rts1},
          PinEntry{UartPinType::Tx0,  UartPinType::Tx0},
          PinEntry{UartPinType::Rx0,  UartPinType::Rx0},
          PinEntry{UartPinType::Tx0, UartPinType::Cts0},
          PinEntry{UartPinType::Rx0, UartPinType::Rts0},
          PinEntry{UartPinType::Tx0,  UartPinType::Tx0},
          PinEntry{UartPinType::Rx0,  UartPinType::Rx0},
          PinEntry{UartPinType::Tx0, UartPinType::Cts0},
          PinEntry{UartPinType::Rx0, UartPinType::Rts0},
          PinEntry{UartPinType::Tx1,  UartPinType::Tx1},
          PinEntry{UartPinType::Rx1,  UartPinType::Rx1},
          PinEntry{UartPinType::Tx1, UartPinType::Cts1},
          PinEntry{UartPinType::Rx1, UartPinType::Rts1},
          PinEntry{UartPinType::Tx1,  UartPinType::Tx1},
          PinEntry{UartPinType::Rx1,  UartPinType::Rx1},
          PinEntry{UartPinType::Tx1, UartPinType::Cts1},
          PinEntry{UartPinType::Rx1, UartPinType::Rts1},
          PinEntry{UartPinType::Tx0,  UartPinType::Tx0},
          PinEntry{UartPinType::Rx0,  UartPinType::Rx0}
        };
    };

    template<>
    struct UartPinMap<ChipVariant::RP2350B> {
        using PinEntry                                 = std::pair<UartPinType, UartPinType>;
        static constexpr std::array<PinEntry, 48> pins = {
          PinEntry{UartPinType::Tx0,  UartPinType::Tx0},
          PinEntry{UartPinType::Rx0,  UartPinType::Rx0},
          PinEntry{UartPinType::Tx0, UartPinType::Cts0},
          PinEntry{UartPinType::Rx0, UartPinType::Rts0},
          PinEntry{UartPinType::Tx1,  UartPinType::Tx1},
          PinEntry{UartPinType::Rx1,  UartPinType::Rx1},
          PinEntry{UartPinType::Tx1, UartPinType::Cts1},
          PinEntry{UartPinType::Rx1, UartPinType::Rts1},
          PinEntry{UartPinType::Tx1,  UartPinType::Tx1},
          PinEntry{UartPinType::Rx1,  UartPinType::Rx1},
          PinEntry{UartPinType::Tx1, UartPinType::Cts1},
          PinEntry{UartPinType::Rx1, UartPinType::Rts1},
          PinEntry{UartPinType::Tx0,  UartPinType::Tx0},
          PinEntry{UartPinType::Rx0,  UartPinType::Rx0},
          PinEntry{UartPinType::Tx0, UartPinType::Cts0},
          PinEntry{UartPinType::Rx0, UartPinType::Rts0},
          PinEntry{UartPinType::Tx0,  UartPinType::Tx0},
          PinEntry{UartPinType::Rx0,  UartPinType::Rx0},
          PinEntry{UartPinType::Tx0, UartPinType::Cts0},
          PinEntry{UartPinType::Rx0, UartPinType::Rts0},
          PinEntry{UartPinType::Tx1,  UartPinType::Tx1},
          PinEntry{UartPinType::Rx1,  UartPinType::Rx1},
          PinEntry{UartPinType::Tx1, UartPinType::Cts1},
          PinEntry{UartPinType::Rx1, UartPinType::Rts1},
          PinEntry{UartPinType::Tx1,  UartPinType::Tx1},
          PinEntry{UartPinType::Rx1,  UartPinType::Rx1},
          PinEntry{UartPinType::Tx1, UartPinType::Cts1},
          PinEntry{UartPinType::Rx1, UartPinType::Rts1},
          PinEntry{UartPinType::Tx0,  UartPinType::Tx0},
          PinEntry{UartPinType::Rx0,  UartPinType::Rx0},
          PinEntry{UartPinType::Tx0, UartPinType::Cts0},
          PinEntry{UartPinType::Rx0, UartPinType::Rts0},
          PinEntry{UartPinType::Tx0,  UartPinType::Tx0},
          PinEntry{UartPinType::Rx0,  UartPinType::Rx0},
          PinEntry{UartPinType::Tx0, UartPinType::Cts0},
          PinEntry{UartPinType::Rx0, UartPinType::Rts0},
          PinEntry{UartPinType::Tx1,  UartPinType::Tx1},
          PinEntry{UartPinType::Rx1,  UartPinType::Rx1},
          PinEntry{UartPinType::Tx1, UartPinType::Cts1},
          PinEntry{UartPinType::Rx1, UartPinType::Rts1},
          PinEntry{UartPinType::Tx1,  UartPinType::Tx1},
          PinEntry{UartPinType::Rx1,  UartPinType::Rx1},
          PinEntry{UartPinType::Tx1, UartPinType::Cts1},
          PinEntry{UartPinType::Rx1, UartPinType::Rts1},
          PinEntry{UartPinType::Tx0,  UartPinType::Tx0},
          PinEntry{UartPinType::Rx0,  UartPinType::Rx0},
          PinEntry{UartPinType::Tx0, UartPinType::Cts0},
          PinEntry{UartPinType::Rx0, UartPinType::Rts0}
        };
    };

    template<>
    struct I2cPinMap<ChipVariant::RP2350A> {
        static constexpr std::array<I2cPinType, 30> pins = {
          I2cPinType::Sda0, I2cPinType::Scl0, I2cPinType::Sda1, I2cPinType::Scl1, I2cPinType::Sda0,
          I2cPinType::Scl0, I2cPinType::Sda1, I2cPinType::Scl1, I2cPinType::Sda0, I2cPinType::Scl0,
          I2cPinType::Sda1, I2cPinType::Scl1, I2cPinType::Sda0, I2cPinType::Scl0, I2cPinType::Sda1,
          I2cPinType::Scl1, I2cPinType::Sda0, I2cPinType::Scl0, I2cPinType::Sda1, I2cPinType::Scl1,
          I2cPinType::Sda0, I2cPinType::Scl0, I2cPinType::Sda1, I2cPinType::Scl1, I2cPinType::Sda0,
          I2cPinType::Scl0, I2cPinType::Sda1, I2cPinType::Scl1, I2cPinType::Sda0, I2cPinType::Scl0};
    };

    template<>
    struct I2cPinMap<ChipVariant::RP2350B> {
        static constexpr std::array<I2cPinType, 48> pins = {
          I2cPinType::Sda0, I2cPinType::Scl0, I2cPinType::Sda1, I2cPinType::Scl1, I2cPinType::Sda0,
          I2cPinType::Scl0, I2cPinType::Sda1, I2cPinType::Scl1, I2cPinType::Sda0, I2cPinType::Scl0,
          I2cPinType::Sda1, I2cPinType::Scl1, I2cPinType::Sda0, I2cPinType::Scl0, I2cPinType::Sda1,
          I2cPinType::Scl1, I2cPinType::Sda0, I2cPinType::Scl0, I2cPinType::Sda1, I2cPinType::Scl1,
          I2cPinType::Sda0, I2cPinType::Scl0, I2cPinType::Sda1, I2cPinType::Scl1, I2cPinType::Sda0,
          I2cPinType::Scl0, I2cPinType::Sda1, I2cPinType::Scl1, I2cPinType::Sda0, I2cPinType::Scl0,
          I2cPinType::Sda1, I2cPinType::Scl1, I2cPinType::Sda0, I2cPinType::Scl0, I2cPinType::Sda1,
          I2cPinType::Scl1, I2cPinType::Sda0, I2cPinType::Scl0, I2cPinType::Sda1, I2cPinType::Scl1,
          I2cPinType::Sda0, I2cPinType::Scl0, I2cPinType::Sda1, I2cPinType::Scl1, I2cPinType::Sda0,
          I2cPinType::Scl0, I2cPinType::Sda1, I2cPinType::Scl1};
    };

    template<>
    struct SpiPinMap<ChipVariant::RP2350A> {
        static constexpr std::array<SpiPinType, 30> pins = {
          SpiPinType::Rx0,  SpiPinType::Cs0,  SpiPinType::Sck0, SpiPinType::Tx0,  SpiPinType::Rx0,
          SpiPinType::Cs0,  SpiPinType::Sck0, SpiPinType::Tx0,  SpiPinType::Rx1,  SpiPinType::Cs1,
          SpiPinType::Sck1, SpiPinType::Tx1,  SpiPinType::Rx1,  SpiPinType::Cs1,  SpiPinType::Sck1,
          SpiPinType::Tx1,  SpiPinType::Rx0,  SpiPinType::Cs0,  SpiPinType::Sck0, SpiPinType::Tx0,
          SpiPinType::Rx0,  SpiPinType::Cs0,  SpiPinType::Sck0, SpiPinType::Tx0,  SpiPinType::Rx1,
          SpiPinType::Cs1,  SpiPinType::Sck1, SpiPinType::Tx1,  SpiPinType::Rx1,  SpiPinType::Cs1};
    };

    template<>
    struct SpiPinMap<ChipVariant::RP2350B> {
        static constexpr std::array<SpiPinType, 48> pins = {
          SpiPinType::Rx0,  SpiPinType::Cs0,  SpiPinType::Sck0, SpiPinType::Tx0,  SpiPinType::Rx0,
          SpiPinType::Cs0,  SpiPinType::Sck0, SpiPinType::Tx0,  SpiPinType::Rx1,  SpiPinType::Cs1,
          SpiPinType::Sck1, SpiPinType::Tx1,  SpiPinType::Rx1,  SpiPinType::Cs1,  SpiPinType::Sck1,
          SpiPinType::Tx1,  SpiPinType::Rx0,  SpiPinType::Cs0,  SpiPinType::Sck0, SpiPinType::Tx0,
          SpiPinType::Rx0,  SpiPinType::Cs0,  SpiPinType::Sck0, SpiPinType::Tx0,  SpiPinType::Rx1,
          SpiPinType::Cs1,  SpiPinType::Sck1, SpiPinType::Tx1,  SpiPinType::Rx1,  SpiPinType::Cs1,
          SpiPinType::Sck1, SpiPinType::Tx1,  SpiPinType::Rx0,  SpiPinType::Cs0,  SpiPinType::Sck0,
          SpiPinType::Tx0,  SpiPinType::Rx0,  SpiPinType::Cs0,  SpiPinType::Sck0, SpiPinType::Tx0,
          SpiPinType::Rx1,  SpiPinType::Cs1,  SpiPinType::Sck1, SpiPinType::Tx1,  SpiPinType::Rx1,
          SpiPinType::Cs1,  SpiPinType::Sck1, SpiPinType::Tx1};
    };

}}   // namespace Kvasir::PinConfig
