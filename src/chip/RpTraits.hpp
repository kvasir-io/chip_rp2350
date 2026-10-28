#pragma once
// The RP2350's data constants that chip_rp_common's drivers read (Kvasir::Rp::ChipTraits); each comment there is at
// the user. Generated register headers only.
#include "peripherals/XIP_AUX.hpp"

#include <cstdint>

namespace Kvasir::Rp {
struct ChipTraits {
    // 0x14000000 "XIP, Uncached" (address map md l.14751, 4.4.1 md l.17868); 0x1c000000 "XIP, uncached, untranslated"
    // (md l.14753, 4.4.1 l.17872) - rp_common flash.hpp
    static constexpr std::uint32_t xipUncachedBase     = 0x1400'0000U;
    static constexpr std::uint32_t xipUntranslatedBase = 0x1c00'0000U;
    // the fastest clk_sys Kvasir runs the chip at (rp_common clock_config.hpp vregInit, WaitBounds.hpp)
    static constexpr std::uint64_t cpuHzCeiling = 266'000'000;
    // The ring oscillator before its divider, at most: "During boot, the ROSC runs at a nominal 11MHz and is guaranteed
    // to be in the range 4.6MHz to 19.6MHz without randomisation and 4.6MHz to 24.0MHz with randomisation" (RP2350
    // datasheet 8.3.1, md l.27985) at the boot path's divisor 8 ("the ROSC divider from 8 to 2", md l.66193; the A3+
    // boot ROM at DIV 2: 18.4 to 96.0 MHz, md l.27989): 192 MHz undivided, nothing in Kvasir writes FREQA/FREQB/
    // FREQ_RANGE (rp_common clock_config.hpp roscRefDivisor). Its slowest output: 4.6 MHz at divisor 8.
    static constexpr std::uint64_t roscBaseHzCeiling = 192'000'000;
    static constexpr std::uint64_t roscHzFloor       = 4'600'000;
    // The fastest clk_ref on the ring oscillator Kvasir lets it run at (rp_common clock_config.hpp onRingOscillator
    // divides it there; WaitBounds.hpp XoscStableBoundFor counts at it, clk_sys = clk_ref then): "The maximum clk_ref
    // frequency is 25 MHz" (8.2.2, md l.27756).
    static constexpr std::uint64_t clkRefOnRoscHzCeiling = 25'000'000;
    // ROSC.DIV: written 0xaa00 + div, div 1..127 divides by div, 0 by 128, any other value by 128 (Table 611, md
    // l.28310)
    static constexpr std::uint32_t roscDivMax     = 127;
    static constexpr std::uint32_t roscDivOfZero  = 128;
    static constexpr std::uint32_t roscDivOfOther = 128;
    // the XIP streaming FIFO through the auxiliary window (rp_common Xip.hpp)
    static constexpr std::uint32_t xipStreamFifoAux
      = Kvasir::Peripheral::XIP_AUX::Registers<>::STREAM::Addr::value;
};
}   // namespace Kvasir::Rp
