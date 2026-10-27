#pragma once
#include "BootTraits.hpp"
#include "core/core.hpp"
#include "kvasir/Port/Hooks.hpp"
#include "kvasir/Port/LinkerSymbols.hpp"
#include "kvasir/Util/attributes.hpp"
#include "peripherals/PPB.hpp"
#include "picobin.hpp"
#include "rp_common/AnnouncedResetClock.hpp"
#include "rp_common/BootSequence.hpp"
#include "rp_common/Rom.hpp"

#include <array>
#include <cstdint>

namespace Kvasir { namespace Startup {
    // inline: one block per image whichever TUs include this header (a second copy would be a second, silent block in
    // the loop); KVASIR_SECTION keeps the section under gcc's LTO (kvasir/Util/attributes.hpp)
    [[gnu::used, KVASIR_SECTION(".after_vectors")]] inline constexpr auto imageDef
      = Picobin::BlockLoop<Picobin::Block<Picobin::ArmSecure>>::words;

    // RP boot sequence (rp_common/BootSequence.hpp) over the RP2350's steps (chip/BootTraits.hpp)
    template<typename... Ts>
    struct FirstInitStep<Tag::User, Ts...> {
        void operator()() { Rp::firstInit<Rp::BootTraits>(); }
    };

}}   // namespace Kvasir::Startup

#if KVASIR_RP_CORE_RESET_GUARDED
    // Multicore flash images only (armed by SecondaryCoreInit::armCoreResetGuard); included below FirstInitStep so
    // its source lines, which a sanitizer records, do not move
    #include "CoreResetGuard.hpp"
#endif

namespace Kvasir { namespace Startup {

    // What core 1 must do for itself on entry. The bootrom parks core 1 with only cp7 (the
    // RCP) enabled in CPACR; the FPU is off, and a hard-float build touches it in its first
    // frame. SecondaryCore's trampoline ORs this mask into CPACR before any compiled code
    // runs. cp7 is kept in the mask so a ROM call from core 1 (which uses RCP instructions)
    // keeps working even if the trampoline ever wrote instead of ORed; cp4 is core 1's own DCP
    // (each core has one, RP2350 datasheet 3.6.2), used by every double operation. Everything else in
    // FirstInitStep above (resets, PSM, watchdog select) is chip-wide and core 0's job.
    template<typename... Ts>
    struct SecondaryCoreInit<Tag::User, Ts...>
      : Rp::SecondaryCoreInitBase<
#if KVASIR_RP_CORE_RESET_GUARDED
          Kvasir::CoreResetGuard
#else
          Rp::NoGuard
#endif
          > {
        // Core 1's CPACR, written by the SDK's launch trampoline: the core's FPU fields (CP10/CP11, as core 0's
        // Startup::Core::startup() opens them) and the chip's own coprocessors, as BootTraits::enableCoprocessors().
        using CPACR                                 = Kvasir::Peripheral::PPB::Registers<0>::CPACR;
        static constexpr std::uint32_t coprocessors = CPACR::cp7.mask | CPACR::cp4.mask;
        static_assert(coprocessors == ((3U << 14U) | (3U << 8U)),
                      "CPACR.CP7 bits 15:14, CP4 bits 9:8");
        static constexpr std::uint32_t cpacrEnable
          = Core::SecondaryCoreTraits::cpacrEnable | coprocessors;

        // The counterpart of FirstInitStep's EXTEXCLALL write: without it core 1's exclusives use its local monitor
        // only and no lock in the system holds.
        void operator()() { Rp::BootTraits::routeExclusivesGlobally(); }
    };

    // The stack guard's per-boot value: the boot ROM's BOOT_RANDOM, "a 128-bit random number
    // generated on each boot" (RP2350 datasheet 5.4.8.17 get_sys_info, flag 0x0010, 4 words),
    // from the TRNG ROSC sampled into SHA-256 on every boot (5.2 boot sequence, "Generate Boot
    // Random"). get_sys_info returns the words filled: the supported-flags word, then the 4.
    template<typename... Ts>
    struct StackGuardEntropy<Tag::User, Ts...> {
        std::uint32_t operator()() const {
            constexpr std::uint32_t      bootRandom = 0x0010;
            std::array<std::uint32_t, 5> buffer{};
            auto const words = RomFunctions::getSysInfo(buffer.data(), buffer.size(), bootRandom);
            if(words < 5 || (buffer[0] & bootRandom) == 0) { return 0xdeadc0deU; }
            return buffer[1] ^ buffer[2] ^ buffer[3] ^ buffer[4];
        }
    };
}}   // namespace Kvasir::Startup

// An address no bus slave answers at, for Kvasir::Fault::Inject (kvasir/Runtime/FaultInject.hpp): the rough decode on
// bits 31:28 has no segment 0xF (RP2350 datasheet 2.2 Table 8: ROM 0x0, XIP 0x1, SRAM 0x2, APB 0x4, AHB 0x5, SIO
// 0xd, M33 private 0xe), and "unmapped address ranges raise a bus error when accessed" (2.2): a precise BusFault
// for a load (PRECISERR, BFAR = 0xF0000000).
template<>
struct Kvasir::Port::UnmappedAddress<Kvasir::Tag::User> {
    static constexpr std::uintptr_t value = 0xF000'0000U;
};
