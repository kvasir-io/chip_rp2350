#pragma once

// The Cortex-M33's MPU (PMSAv8, 8 regions) over the image's own layout: code and constants
// read-only, everything writable never executed. A stray write into code or a jump into data
// becomes a MemManage fault at the faulting instruction (Fault::HandlerFor records MMFAR).
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
// .ramExec holds code written at run time (the bootrom's XIP setup function, rp_common/XipControl.hpp
// FlashXipDisabler; anything else via [[KVASIR_SECTION_MEMBER(".ramExec")]]).
//
// Everything else keeps the default memory map (PRIVDEFENA). Privileged code only; HardFault and
// NMI run with the MPU off (HFNMIENA = 0); DMA and the debugger are not checked. Core 0 only.
// Scratch-bank objects (KVASIR_CORE_SCRATCH) are not in the layout yet and are refused below.
//
// Programmed in preEnableRuntimeInit, before interrupts are enabled; off while a debugger is
// attached (see ImageProtection). The policy is the chip's; the mechanism (regions, their words, programming, the
// barriers) is the core's, core_cortex_m33 core/Mpu.hpp.

#include "core/Mpu.hpp"
#include "cortex_common/Debug.hpp"
#include "kvasir/Port/Extends.hpp"
#include "kvasir/Port/LinkerSymbols.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#if defined(KVASIR_CORE_SCRATCH) && KVASIR_CORE_SCRATCH
    #error "Kvasir::Mpu: the image layout has no region for the scratch banks yet (SCRATCH_BANKS)"
#endif

namespace Kvasir { namespace Mpu {

    // The RP2350's Cortex-M33 has 8 regions (MPU_TYPE.DREGION resets to 0x08: RP2350 datasheet 3.7 Table 232, md
    // l.9753); the layout below takes 5 of them.
    inline constexpr unsigned    kImplementedRegions = 8;
    inline constexpr std::size_t kRegions            = 5;
    static_assert(kRegions <= kImplementedRegions);

    constexpr std::uint32_t alignUp(std::uint32_t a) { return (a + 0x1FU) & ~0x1FU; }

    /// The regions for the image this is linked into. Which layout is the image's own: a RAM
    /// image links its vectors into SRAM (a firmware may bring its own linker script, so
    /// KVASIR_RAM_ONLY is not the test).
    inline std::array<Kvasir::Core::Mpu::Region,
                      kRegions>
    imageLayout() {
        auto const vectors = Startup::linkerAddress<_LINKER_vectors_start_>();
        auto const ram     = Startup::linkerAddress<_LINKER_ram_start_>();
        auto const ramEnd  = Startup::linkerAddress<_LINKER_ram_end_>();
        if(vectors >= ram && vectors < ramEnd) {
            auto const lowRam  = Startup::linkerAddress<_LINKER_vectors_end_>();
            auto const text    = Startup::linkerAddress<_LINKER_text_start_>();
            auto const textEnd = Startup::linkerAddress<_LINKER_text_end_>();
            auto const execEnd
              = Startup::linkerAddress<_LINKER_ramExec_end_>();   // .ramExec starts at textEnd
            return {
              {{.begin = vectors, .end = lowRam, .readOnly = true, .execute = false},
               {.begin = lowRam, .end = text, .readOnly = false, .execute = false},
               {.begin = text, .end = textEnd, .readOnly = true, .execute = true},
               {.begin = textEnd, .end = execEnd, .readOnly = false, .execute = true},
               {.begin = execEnd, .end = ramEnd, .readOnly = false, .execute = false}}
            };
        }
        auto const flashEnd
          = alignUp(Startup::linkerAddress<
                    _LINKER_image_end_flash_>());   // every flash load image (common.ld)
        auto const data    = Startup::linkerAddress<_LINKER_data_start_>();
        auto const execEnd = Startup::linkerAddress<
          _LINKER_ramExec_end_>();   // .ramExec right behind .data (common_ram.ld)
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
        inline static std::uint8_t invalid_{};
        inline static std::uint8_t regions_{};

        static void preEnableRuntimeInit() {
            namespace Mpu = Kvasir::Core::Mpu;
            Mpu::disable();
            Mpu::setAttributes(Mpu::normalWriteBack);
            auto const programmed = Mpu::program<kImplementedRegions>(imageLayout());
            invalid_              = static_cast<std::uint8_t>(invalid_ + programmed.invalid);
            regions_              = programmed.regions;
            follow();
        }

        /// Enabled exactly while no debugger is attached.
        static Turn turn() {
            follow();
            return Turn::idle();
        }

        using Extends = Startup::Extend<Hook::MainLoop, &ImageProtection::turn>;

        [[nodiscard]] static Status status() {
            return {.enabled          = Kvasir::Core::Mpu::enabled(),
                    .debuggerAttached = Kvasir::Core::Debug::isDebuggerConnected(),
                    .regions          = regions_,
                    .invalid          = invalid_};
        }

    private:
        static void follow() {
            bool const want = !Kvasir::Core::Debug::isDebuggerConnected();
            if(want == Kvasir::Core::Mpu::enabled()) { return; }
            Kvasir::Core::Mpu::enable(want);
        }
    };

}}   // namespace Kvasir::Mpu
