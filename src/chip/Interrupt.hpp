#pragma once
#include "core/CoreInterrupts.hpp"
#include "kvasir/Common/Interrupt.hpp"
#include "rp_common/PinConfig.hpp"

#include <array>

namespace Kvasir {

template<PinConfig::ChipVariant Chip>
struct InterruptImpl : CoreInterrupts {};

template<>
struct InterruptImpl<PinConfig::ChipVariant::RP2350A> : CoreInterrupts {
    // Device-specific interrupts for RP2350
    static constexpr Type<0>  timer0_0{};
    static constexpr Type<1>  timer0_1{};
    static constexpr Type<2>  timer0_2{};
    static constexpr Type<3>  timer0_3{};
    static constexpr Type<4>  timer1_0{};
    static constexpr Type<5>  timer1_1{};
    static constexpr Type<6>  timer1_2{};
    static constexpr Type<7>  timer1_3{};
    static constexpr Type<8>  pwm_wrap_0{};
    static constexpr Type<9>  pwm_wrap_1{};
    static constexpr Type<10> dma_0{};
    static constexpr Type<11> dma_1{};
    static constexpr Type<12> dma_2{};
    static constexpr Type<13> dma_3{};
    static constexpr Type<14> usbctrl{};
    static constexpr Type<15> pio0_0{};
    static constexpr Type<16> pio0_1{};
    static constexpr Type<17> pio1_0{};
    static constexpr Type<18> pio1_1{};
    static constexpr Type<19> pio2_0{};
    static constexpr Type<20> pio2_1{};
    static constexpr Type<21> io_bank0{};
    static constexpr Type<22> io_bank0_ns{};
    static constexpr Type<23> io_qspi{};
    static constexpr Type<24> io_qspi_ns{};
    static constexpr Type<25> sio_fifo{};
    static constexpr Type<26> sio_bell{};
    static constexpr Type<27> sio_fifo_ns{};
    static constexpr Type<28> sio_bell_ns{};
    static constexpr Type<29> sio_mtimecmp{};
    static constexpr Type<30> clocks{};
    static constexpr Type<31> spi0{};
    static constexpr Type<32> spi1{};
    static constexpr Type<33> uart0{};
    static constexpr Type<34> uart1{};
    static constexpr Type<35> adc_fifo{};
    static constexpr Type<36> i2c0{};
    static constexpr Type<37> i2c1{};
    static constexpr Type<38> otp{};
    static constexpr Type<39> trng{};
    static constexpr Type<40> proc0_cti{};
    static constexpr Type<41> proc1_cti{};
    static constexpr Type<42> pll_sys{};
    static constexpr Type<43> pll_usb{};
    static constexpr Type<44> powman_pow{};
    static constexpr Type<45> powman_timer{};
    static constexpr Type<46> spareirq_0{};
    static constexpr Type<47> spareirq_1{};
    static constexpr Type<48> spareirq_2{};
    static constexpr Type<49> spareirq_3{};
    static constexpr Type<50> spareirq_4{};
    static constexpr Type<51> spareirq_5{};

    struct InterruptOffsetTraits {
        static constexpr int        begin    = -14;
        static constexpr int        end      = 52;
        static constexpr std::array disabled = {-4, -3};
        static constexpr std::array noEnable
          = {nonMaskableInt.index(), sVCall.index(), pendSV.index()};
        static constexpr std::array noDisable
          = {nonMaskableInt.index(), sVCall.index(), pendSV.index()};
        static constexpr std::array noSetPending = {sVCall.index(), hardFault.index()};
        static constexpr std::array noClearPending
          = {nonMaskableInt.index(), sVCall.index(), hardFault.index()};
        static constexpr std::array noSetPriority = {nonMaskableInt.index(), hardFault.index()};

        using FaultInterruptIndexs           = brigand::list<decltype(hardFault),
                                                             decltype(memoryManagement),
                                                             decltype(busFault),
                                                             decltype(usageFault),
                                                             decltype(secureFault)>;
        using FaultInterruptIndexsNeedEnable = brigand::list<>;
    };
};

template<>
struct InterruptImpl<PinConfig::ChipVariant::RP2350B>
  : InterruptImpl<PinConfig::ChipVariant::RP2350A> {};

using Interrupt = InterruptImpl<PinConfig::CurrentChip>;

namespace Nvic {
    template<>
    struct InterruptOffsetTraits<void>
      : InterruptImpl<PinConfig::CurrentChip>::InterruptOffsetTraits {};
}   // namespace Nvic

}   // namespace Kvasir
