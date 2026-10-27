#pragma once
// The chip this package is: the RP2350A or the RP2350B. cmake/chip.cmake passes KVASIR_CHIP_RP2350 and
// KVASIR_CHIP_RP2350A or KVASIR_CHIP_RP2350B (the cache option KVASIR_RP2350_MPU) to every TU of an image (kvasir_chip
// DEFINITIONS); rp_common/PinConfig.hpp defines Chip::Part and Chip::part from them. chip/Interrupt.hpp includes this
// header, so every chip user gets the check below. Build scripts test for the file name to tell the packages apart.
#include "rp_common/PinConfig.hpp"

#include <string_view>

#ifndef KVASIR_CHIP_RP2350
    #error \
      "chip_rp2350's chip/rp2350.hpp without KVASIR_CHIP_RP2350: compiled with another chip package's kvasir_chip DEFINITIONS?"
#endif

namespace Kvasir::Chip {
// which part this image is for, as text (the value is Chip::part)
inline constexpr std::string_view partName = part == Part::rp2350b ? "RP2350B" : "RP2350A";
}   // namespace Kvasir::Chip
