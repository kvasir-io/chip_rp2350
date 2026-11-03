#pragma once
// The Picobin::BootRules of the chip this code runs on (chip/PicobinReader.hpp): the boot ROM's revision and the OTP
// settings its flash boot reads, so the reader decides as this chip's boot ROM does.
#include "PicobinReader.hpp"
#include "rp_common/BootInfo.hpp"
#include "rp_common/Rom.hpp"

#include <array>
#include <cstdint>
#include <kvasir/Register/Memory.hpp>

namespace Kvasir { namespace Picobin {

    /// The boot ROM's revision: its byte at 0x13 (RP2350 datasheet 5.4.1 "Bootrom version", 2 for A2;
    /// pico-bootrom-rp2350 arm8_bootrom_rt0.S `_minor_version`: 2, 3, 4 for A2, A3, A4). A later ROM is taken as A4.
    [[nodiscard]] inline RomVersion romVersion() {
        constexpr std::uintptr_t versionByte = 0x13U;
        auto const               v           = Kvasir::Memory::load<std::uint8_t>(versionByte);
        if(v <= 2U) { return RomVersion::a2; }
        if(v == 3U) { return RomVersion::a3; }
        return RomVersion::a4;
    }

    namespace Detail {
        // OTP rows (RP2350 datasheet 13.10, OTP data list): CRIT1 0x040 (RBIT-8), BOOT_FLAGS0 0x048 (RBIT-3: three raw
        // copies, the boot ROM takes the majority, pico-bootrom varm_otp.c s_varm_step_safe_otp_read_rbit3_guarded),
        // FLASH_PARTITION_SLOT_SIZE 0x055 (ECC); CRIT1 comes through get_sys_info
        inline constexpr std::uint32_t otpBootFlags0Row = 0x048U;
        inline constexpr std::uint32_t otpSlotSizeRow   = 0x055U;
        // BOOT_FLAGS0 bits (datasheet 13.10 BOOT_FLAGS0)
        inline constexpr std::uint32_t bfDisableWatchdogScratch = 1U << 16U;
        inline constexpr std::uint32_t bfRollbackRequired       = 1U << 11U;
        inline constexpr std::uint32_t bfHashedPartitionTable   = 1U << 10U;
        inline constexpr std::uint32_t bfSecurePartitionTable   = 1U << 9U;
        inline constexpr std::uint32_t bfDisableAutoSwitchArch  = 1U << 8U;
        inline constexpr std::uint32_t bfSingleFlashBinary      = 1U << 7U;
        inline constexpr std::uint32_t bfOverrideSlotSize       = 1U << 6U;
    }   // namespace Detail

    /// This chip's BootRules, read through the boot ROM (otp_access under its boot lock, get_sys_info for CRIT1). An
    /// OTP read the ROM refuses leaves that rule at its default (unprogrammed OTP).
    [[nodiscard]] inline BootRules bootRulesOfThisChip() {
        BootRules                    rules{.rom = romVersion()};
        std::array<std::uint32_t, 3> raw{};   // raw reads: 4 bytes per row, the 24 data bits
        if(Bootrom::otpAccess(reinterpret_cast<std::uint8_t*>(raw.data()),
                              sizeof(raw),
                              Detail::otpBootFlags0Row)
           == 0)
        {
            auto const f = (raw[0] & raw[1]) | (raw[1] & raw[2]) | (raw[2] & raw[0]);
            rules.watchdogScratchDisabled = (f & Detail::bfDisableWatchdogScratch) != 0U;
            rules.rollbackRequired        = (f & Detail::bfRollbackRequired) != 0U;
            rules.hashedPartitionTable    = (f & Detail::bfHashedPartitionTable) != 0U;
            rules.securePartitionTable    = (f & Detail::bfSecurePartitionTable) != 0U;
            rules.autoSwitchArchDisabled  = (f & Detail::bfDisableAutoSwitchArch) != 0U;
            rules.singleFlashBinary       = (f & Detail::bfSingleFlashBinary) != 0U;
            if((f & Detail::bfOverrideSlotSize) != 0U) {
                std::array<std::uint16_t, 1> slot{};   // an ECC read: 2 bytes per row
                if(Bootrom::otpAccess(reinterpret_cast<std::uint8_t*>(slot.data()),
                                      sizeof(slot),
                                      Detail::otpSlotSizeRow | Bootrom::OtpCmd::ecc)
                   == 0)
                {
                    rules.slotSize = (std::uint32_t{slot[0]} + 1U) * 4096U;
                }
            }
        }
        // CRIT1 through get_sys_info (datasheet 5.4.8.17, flag 0x0002 CRITICAL: word 1 bit 0 SECURE_BOOT_ENABLE)
        constexpr std::uint32_t      critical = 0x0002U;
        std::array<std::uint32_t, 2> info{};
        if(RomFunctions::getSysInfo(info.data(), info.size(), critical) >= 2
           && (info[0] & critical) != 0U)
        {
            rules.secure = (info[1] & 1U) != 0U;
        }
        return rules;
    }

}}   // namespace Kvasir::Picobin
