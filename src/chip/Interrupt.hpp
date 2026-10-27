#pragma once
#include "core/CoreInterrupts.hpp"
#include "kvasir/Common/Interrupt.hpp"
#include "kvasir/Port/Interrupts.hpp"
#include "peripherals/Interrupts.hpp"
#include "rp2350.hpp"

#include <array>

namespace Kvasir {
// The core's exceptions (core/CoreInterrupts.hpp) and the device lines of chip.svd (svd_converter:
// peripherals/Interrupts.hpp, the SVD's names lower-cased without IRQ: TIMER0_IRQ_0 -> timer0_0; RP2350A and B share
// the table). chip.svd carries PROC0/1_IRQ_CTI (40, 41), which the vendor SVD lacked.
struct Interrupt
  : CoreInterrupts
  , Peripheral::DeviceInterrupts {};

namespace Nvic {
    template<>
    struct InterruptOffsetTraits<Kvasir::Tag::User>
      : Core::ChipInterruptTraits<Core::InterruptRules,
                                  Peripheral::interruptEnd,
                                  Peripheral::interruptGaps> {
        // Lines that exist once per core: their enable and status are banked, so both cores
        // may install a handler. Every other line reaches both NVICs and belongs to one core
        // (Startup refuses it in both vector tables).
        static constexpr std::array perCore = {Interrupt::io_bank0.value,
                                               Interrupt::io_bank0_ns.value,
                                               Interrupt::io_qspi.value,
                                               Interrupt::io_qspi_ns.value,
                                               Interrupt::sio_fifo.value,
                                               Interrupt::sio_bell.value,
                                               Interrupt::sio_fifo_ns.value,
                                               Interrupt::sio_bell_ns.value,
                                               Interrupt::sio_mtimecmp.value};
    };
}   // namespace Nvic

}   // namespace Kvasir
