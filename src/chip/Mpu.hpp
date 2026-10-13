#pragma once

// The Cortex-M33's MPU (PMSAv8, 8 regions) over the image's own layout: code and constants
// read-only, everything writable never executed. A stray write into code or a jump into data
// becomes a MemManage fault at the faulting instruction (Fault::Handler records MMFAR).
//
//     using Startup = Kvasir::Startup::Startup<HW::ClockSettings, ..., Kvasir::Mpu::ImageProtection>;
//
// The regions (imageLayout(), from the linker scripts' 32-byte boundaries):
//
//   RAM image (vectors linked into SRAM: common_ram_only.ld)
//     vectors + picobin block   read-only, no execute
//     .noInitLowRam             read/write, no execute
//     .text/.rodata/.ramfunc    read-only, execute
//     .ramExec                  read/write, execute
//     .data .. end of SRAM      read/write, no execute (bss, noInit, stack, heap)
//   flash image (common_flash.ld + common_ram.ld)
//     flash, vectors .. end of the .data copy   read-only, execute (a write into the XIP window
//                                               would otherwise land in the cache)
//     SRAM below .data          read/write, no execute (noInitLowRam, stack)
//     .data with .ramfunc, .ramExec             read/write, execute
//     SRAM above them           read/write, no execute
//
// .ramExec holds code written at run time (the bootrom's XIP setup function, bootrom_functions.hpp
// FlashXipDisabler; anything else via [[KVASIR_SECTION_MEMBER(".ramExec")]]).
//
// Everything else keeps the default memory map (PRIVDEFENA). Privileged code only; HardFault and
// NMI run with the MPU off (HFNMIENA = 0); DMA and the debugger are not checked. Core 0 only.
// Scratch-bank objects (KVASIR_CORE_SCRATCH) are not in the layout yet and are refused below.
//
// Programmed in preEnableRuntimeInit, before interrupts are enabled; off while a debugger is
// attached (see ImageProtection).

#include "core/Debug.hpp"
#include "kvasir/Register/Register.hpp"
#include "kvasir/StartUp/Hooks.hpp"
#include "kvasir/StartUp/LinkerSymbols.hpp"
#include "peripherals/PPB.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#if defined(KVASIR_CORE_SCRATCH) && KVASIR_CORE_SCRATCH
    #error "Kvasir::Mpu: the image layout has no region for the scratch banks yet (SCRATCH_BANKS)"
#endif

namespace Kvasir { namespace Mpu {

    struct Region {
        std::uint32_t begin;   // 32-byte aligned
        std::uint32_t end;     // exclusive, 32-byte aligned; begin == end: no region
        bool          readOnly;
        bool          execute;
    };

    struct Words {
        std::uint32_t rbar;
        std::uint32_t rlar;
    };

    // MPU_RBAR: BASE[31:5], SH[4:3] (non-shareable), AP[2:1] (00 read/write, 10 read-only:
    // privileged only), XN[0]. MPU_RLAR: LIMIT[31:5] (the last 32-byte block), AttrIndx[3:1]
    // (0: kMair0Attr0), EN[0]. Fields: RP2350 datasheet 3.7, Tables 233 (MPU_CTRL), 235, 236; the
    // AP encodings: Armv8-M ARM DDI0553B.y D1.2.171.
    constexpr Words encode(Region const& r) {
        using Ppb = Kvasir::Peripheral::PPB::Registers<0>;
        using Kvasir::Register::deposit;
        return {.rbar = deposit(Ppb::MPU_RBAR::base, r.begin >> 5U) | deposit(Ppb::MPU_RBAR::sh, 0U)
                      | deposit(Ppb::MPU_RBAR::ap, r.readOnly ? 0b10U : 0U)
                      | deposit(Ppb::MPU_RBAR::xn, r.execute ? 0U : 1U),
                .rlar = deposit(Ppb::MPU_RLAR::limit, (r.end - 1U) >> 5U)
                      | deposit(Ppb::MPU_RLAR::attrindx, 0U) | deposit(Ppb::MPU_RLAR::en, 1U)};
    }

    constexpr bool valid(Region const& r) {
        return (r.begin & 0x1FU) == 0 && (r.end & 0x1FU) == 0 && r.begin <= r.end;
    }

    // Attr0: Normal memory, write-back non-transient read/write-allocate inner and outer (0b1111
    // each half, DDI0553B.y D1.2.165), as the default map gives SRAM; keeps unaligned access legal.
    inline constexpr std::uint32_t kMair0Attr0 = 0xFFU;

    inline constexpr std::size_t kRegions = 5;   // of the chip's 8

    // a linker symbol's address as a run-time value: an empty section's start == end must not fold to false and the
    // empty region be programmed (LinkerSymbols.hpp)
    template<auto& Symbol>
    std::uint32_t at() {
        return Startup::linkerAddress<Symbol>();
    }

    constexpr std::uint32_t alignUp(std::uint32_t a) { return (a + 0x1FU) & ~0x1FU; }

    /// The regions for the image this is linked into. Which layout is the image's own: a RAM
    /// image links its vectors into SRAM (a firmware may bring its own linker script, so
    /// KVASIR_RAM_ONLY is not the test).
    inline std::array<Region,
                      kRegions>
    imageLayout() {
        auto const vectors = at<_LINKER_vectors_start_>();
        auto const ram     = at<_LINKER_ram_start_>();
        auto const ramEnd  = at<_LINKER_ram_end_>();
        if(vectors >= ram && vectors < ramEnd) {
            auto const lowRam  = at<_LINKER_vectors_end_>();
            auto const text    = at<_LINKER_text_start_>();
            auto const textEnd = at<_LINKER_text_end_>();
            auto const execEnd = at<_LINKER_ramExec_end_>();   // .ramExec starts at textEnd
            return {
              {{.begin = vectors, .end = lowRam, .readOnly = true, .execute = false},
               {.begin = lowRam, .end = text, .readOnly = false, .execute = false},
               {.begin = text, .end = textEnd, .readOnly = true, .execute = true},
               {.begin = textEnd, .end = execEnd, .readOnly = false, .execute = true},
               {.begin = execEnd, .end = ramEnd, .readOnly = false, .execute = false}}
            };
        }
        auto const flashEnd = alignUp(at<_LINKER_data_end_flash_>());
        auto const data     = at<_LINKER_data_start_>();
        auto const execEnd
          = at<_LINKER_ramExec_end_>();   // .ramExec right behind .data (common_ram.ld)
        return {
          {{.begin = vectors, .end = flashEnd, .readOnly = true, .execute = true},
           {.begin = ram, .end = data, .readOnly = false, .execute = false},
           {.begin = data, .end = execEnd, .readOnly = false, .execute = true},
           {.begin = execEnd, .end = ramEnd, .readOnly = false, .execute = false},
           {.begin = 0, .end = 0, .readOnly = false, .execute = false}}
        };
    }

    struct Status {
        bool         enabled;            // MPU_CTRL.ENABLE
        bool         debuggerAttached;   // DHCSR.C_DEBUGEN: off for the debugger, not by fault
        std::uint8_t regions;            // regions programmed
        std::uint8_t
          invalid;   // layout regions refused (misaligned bounds): left to the default map
    };

    /// A Startup-list entry: programs imageLayout() and enables the MPU while no debugger has
    /// halting debug enabled (DHCSR.C_DEBUGEN). A debugger's flash loader runs from SRAM and would
    /// fault on the execute-never regions, and the firmware cannot switch the MPU off before the
    /// debugger halts it; so every main-loop turn follows C_DEBUGEN. The regions stay programmed;
    /// only MPU_CTRL.ENABLE changes.
    struct ImageProtection {
        using Regs = Kvasir::Peripheral::PPB::Registers<0>;

        inline static std::uint8_t invalid_{};
        inline static std::uint8_t regions_{};

        static void preEnableRuntimeInit() {
            using namespace Kvasir::Register;
            asm volatile("dmb" ::: "memory");
            apply(
              clear(Regs::MPU_CTRL::enable, Regs::MPU_CTRL::hfnmiena, Regs::MPU_CTRL::privdefena));
            apply(write(Regs::MPU_MAIR0::attr0, value<std::uint32_t, kMair0Attr0>()),
                  write(Regs::MPU_MAIR0::attr1, value<std::uint32_t, 0U>()),
                  write(Regs::MPU_MAIR0::attr2, value<std::uint32_t, 0U>()),
                  write(Regs::MPU_MAIR0::attr3, value<std::uint32_t, 0U>()));
            auto const    layout = imageLayout();
            std::uint32_t n      = 0;
            for(auto const& r : layout) {
                if(!valid(r)) {
                    ++invalid_;
                    continue;
                }
                if(r.begin == r.end) { continue; }
                auto const w = encode(r);   // every field of both: whole-word stores
                apply(write(Regs::MPU_RNR::region, n));
                apply(write(Regs::MPU_RBAR::FULLREGISTER, w.rbar));
                apply(write(Regs::MPU_RLAR::FULLREGISTER, w.rlar));
                ++n;
            }
            regions_ = static_cast<std::uint8_t>(n);
            for(; n < 8U; ++n) {
                apply(write(Regs::MPU_RNR::region, n));
                apply(clear(Regs::MPU_RLAR::en));
            }
            follow();
        }

        /// Enabled exactly while no debugger is attached.
        static Turn turn() {
            follow();
            return Turn::idle();
        }

        using Extends = Startup::Extend<Hook::MainLoop, &ImageProtection::turn>;

        [[nodiscard]] static Status status() {
            using Kvasir::Register::apply;
            using Kvasir::Register::read;
            return {.enabled          = apply(read(Regs::MPU_CTRL::enable)) != 0U,
                    .debuggerAttached = Kvasir::Core::Debug::isDebuggerConnected(),
                    .regions          = regions_,
                    .invalid          = invalid_};
        }

    private:
        static void follow() {
            using namespace Kvasir::Register;
            bool const want = !Kvasir::Core::Debug::isDebuggerConnected();
            bool const is   = apply(read(Regs::MPU_CTRL::enable)) != 0U;
            if(want == is) { return; }
            asm volatile("dmb" ::: "memory");
            // PRIVDEFENA: the default map behind the regions; HFNMIENA off; ENABLE as wanted.
            apply(set(Regs::MPU_CTRL::privdefena),
                  clear(Regs::MPU_CTRL::hfnmiena),
                  write(Regs::MPU_CTRL::enable, want ? 1U : 0U));
            asm volatile("dsb\n isb" ::: "memory");
        }
    };

    namespace Test {
        // The SRAM code region of a RAM image: read-only, executable, the last block 0x2002'FFE0.
        static_assert(encode({.begin    = 0x2000'1000U,
                              .end      = 0x2003'0000U,
                              .readOnly = true,
                              .execute  = true})
                        .rbar
                      == 0x2000'1004U);
        static_assert(encode({.begin    = 0x2000'1000U,
                              .end      = 0x2003'0000U,
                              .readOnly = true,
                              .execute  = true})
                        .rlar
                      == 0x2002'FFE1U);
        // Writable, never executed, up to the end of the scratch banks.
        static_assert(encode({.begin    = 0x2003'0000U,
                              .end      = 0x2008'2000U,
                              .readOnly = false,
                              .execute  = false})
                        .rbar
                      == 0x2003'0001U);
        static_assert(encode({.begin    = 0x2003'0000U,
                              .end      = 0x2008'2000U,
                              .readOnly = false,
                              .execute  = false})
                        .rlar
                      == 0x2008'1FE1U);
        static_assert(!valid({.begin    = 0x2000'1004U,
                              .end      = 0x2000'2000U,
                              .readOnly = true,
                              .execute  = true}));
        static_assert(valid({.begin    = 0x2000'2000U,
                             .end      = 0x2000'2000U,
                             .readOnly = true,
                             .execute  = true}));
    }   // namespace Test
}}   // namespace Kvasir::Mpu
