#pragma once

// The RP2350's gated blocks: every block FirstInitStep holds in reset, with its RESETS field, interrupt lines and
// DMA requests (chip_rp_common Block.hpp explains the row; kvasir/Hal/Block.hpp the check it drives). A row names its
// block's base address as a number from the address map (RP2350 datasheet 2.2.4 "APB registers", md l.1502-1583,
// and 2.2.5 "AHB registers", md l.1587-1606): APB blocks are 32 KiB apart, AHB blocks 1 MiB. `Rp::Block<Regs>` finds
// a row by the generated `Regs::baseAddr`, so a wrong number is "no block of the chip's table" at the first driver,
// and tests/conformance asserts every row against its register set. HSTX has two rows: control on APB, FIFO on AHB.
// The table includes only RESETS, the interrupt list and (through Block.hpp) DMA's request enum: a TU that names
// `Rp::Block<Regs>` includes Regs' own header.

#include "rp_common/Block.hpp"

#include <peripherals/Interrupts.hpp>
#include <peripherals/RESETS.hpp>

namespace Kvasir { namespace Chip { namespace Blocks {
    using Reset = Peripheral::RESETS::Registers<>::RESET;
    using Done  = Peripheral::RESETS::Registers<>::RESET_DONE;
    using D     = ::Kvasir::Rp::Dreq;
    using I     = Peripheral::DeviceInterrupts;
    template<typename... Is>
    using Irqs = brigand::list<Is...>;
    template<std::uint32_t Base,
             std::uint32_t Size,
             auto const&   R,
             auto const&   Dn,
             typename Is = brigand::list<>,
             D Tx        = D::permanent,
             D Rx        = D::permanent>
    using Row = ::Kvasir::Rp::BlockRow<Base, Size, R, Dn, Is, Tx, Rx>;

    using Table = ::Kvasir::Hal::BlockTable<
      Row<0x400a0000,
          0x8000,
          Reset::adc,
          Done::adc,
          Irqs<decltype(I::adc_fifo)>,
          D::permanent,
          D::adc>,
      Row<0x40068000, 0x8000, Reset::busctrl, Done::busctrl>,
      Row<0x50000000, 0x100000, Reset::dma, Done::dma>,
      Row<0x400c0000, 0x8000, Reset::hstx, Done::hstx, brigand::list<>, D::hstx>,
      Row<0x50600000,
          0x100000,
          Reset::hstx,
          Done::hstx,
          brigand::list<>,
          D::hstx>,   // control on APB, FIFO on AHB
      Row<0x40090000,
          0x8000,
          Reset::i2c0,
          Done::i2c0,
          Irqs<decltype(I::i2c0)>,
          D::i2c0_tx,
          D::i2c0_rx>,
      Row<0x40098000,
          0x8000,
          Reset::i2c1,
          Done::i2c1,
          Irqs<decltype(I::i2c1)>,
          D::i2c1_tx,
          D::i2c1_rx>,
      Row<0x40028000, 0x8000, Reset::io_bank0, Done::io_bank0>,
      Row<0x40038000, 0x8000, Reset::pads_bank0, Done::pads_bank0>,
      Row<0x50200000,
          0x100000,
          Reset::pio0,
          Done::pio0,
          Irqs<decltype(I::pio0_0), decltype(I::pio0_1)>,
          D::pio0_tx0,
          D::pio0_rx0>,
      Row<0x50300000,
          0x100000,
          Reset::pio1,
          Done::pio1,
          Irqs<decltype(I::pio1_0), decltype(I::pio1_1)>,
          D::pio1_tx0,
          D::pio1_rx0>,
      Row<0x50400000,
          0x100000,
          Reset::pio2,
          Done::pio2,
          Irqs<decltype(I::pio2_0), decltype(I::pio2_1)>,
          D::pio2_tx0,
          D::pio2_rx0>,
      Row<0x40058000, 0x8000, Reset::pll_usb, Done::pll_usb>,
      Row<0x400a8000, 0x8000, Reset::pwm, Done::pwm>,
      Row<0x400f8000, 0x8000, Reset::sha256, Done::sha256, brigand::list<>, D::sha256>,
      Row<0x40080000,
          0x8000,
          Reset::spi0,
          Done::spi0,
          Irqs<decltype(I::spi0)>,
          D::spi0_tx,
          D::spi0_rx>,
      Row<0x40088000,
          0x8000,
          Reset::spi1,
          Done::spi1,
          Irqs<decltype(I::spi1)>,
          D::spi1_tx,
          D::spi1_rx>,
      Row<0x40160000, 0x8000, Reset::tbman, Done::tbman>,
      Row<0x400b0000, 0x8000, Reset::timer0, Done::timer0>,
      Row<0x400b8000, 0x8000, Reset::timer1, Done::timer1>,
      Row<0x400f0000, 0x8000, Reset::trng, Done::trng>,
      Row<0x40070000,
          0x8000,
          Reset::uart0,
          Done::uart0,
          Irqs<decltype(I::uart0)>,
          D::uart0_tx,
          D::uart0_rx>,
      Row<0x40078000,
          0x8000,
          Reset::uart1,
          Done::uart1,
          Irqs<decltype(I::uart1)>,
          D::uart1_tx,
          D::uart1_rx>,
      Row<0x50100000,
          0x100000,
          Reset::usbctrl,
          Done::usbctrl>   // DPRAM and USB registers (0x50110000)
      >;

}}}   // namespace Kvasir::Chip::Blocks

template<>
struct Kvasir::Hal::BlockGates<Kvasir::Tag::User> {
    using type = Kvasir::Chip::Blocks::Table;
};

namespace Kvasir { namespace Chip { namespace Blocks {
    // Every RESETS bit is either a row, kept running by FirstInitStep, or has no driver (jtag).
    inline constexpr std::uint32_t rowMask
      = []<typename... Rows>(::Kvasir::Hal::BlockTable<Rows...>*) {
            return (0U | ... | (1U << Rows::id));
        }(static_cast<Table*>(nullptr));
    static_assert((rowMask | Reset::sysinfo.mask | Reset::syscfg.mask | Reset::pll_sys.mask
                   | Reset::pads_qspi.mask | Reset::io_qspi.mask | Reset::jtag.mask)
                    == 0x1FFFFFFFU,
                  "a RESETS bit is neither a block row nor released by FirstInitStep");
    static_assert((rowMask
                   & (Reset::sysinfo.mask | Reset::syscfg.mask | Reset::pll_sys.mask
                      | Reset::pads_qspi.mask | Reset::io_qspi.mask))
                    == 0U,
                  "a block FirstInitStep keeps running is not claimed");
}}}   // namespace Kvasir::Chip::Blocks
