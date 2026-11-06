#pragma once
// Reading picobin blocks back (RP2350 datasheet 5.9, "Metadata block details"): which IMAGE_DEF the boot ROM launches
// from an image or partition and whether its hash holds, which partition table it loads from which slot, and which
// of an A/B pair it boots. chip/picobin.hpp writes the blocks at compile time; this reads them at run time, from bytes
// held in memory (a UF2's payload laid out from the partition start, the firmware's own flash through XIP) or, through
// Walker / FlashWalker, from a reader that gets the flash a read at a time (a board over PICOBOOT, asynchronously).
// Standard library only, no heap, no exceptions; everything is constexpr.
//
// It is the boot ROM's own logic, ported statement by statement from its source (pico-bootrom-rp2350, tags A2, A3,
// A4, src/main/arm/) for an Arm boot:
// - Walker: one window (a partition, or flash from a slot on) - its block_scan_t, s_varm_crit_next_block
//   (varm_blocks.c) with the loop of s_varm_crit_search_window (varm_flash_boot.c), s_varm_crit_parse_block
//   (Detail::parse), then s_varm_crit_ram_trash_verify_block in place (the hash, the load map's reads included).
//   footprint(): the bytes the launched image takes up (its loop and its load map's sources), without the hash.
// - chooseAb: s_varm_crit_choose_by_tbyb_flash_update_boot_and_version - partition A or B by verification, TBYB,
//   flash update boot and version, and which first sector a buy erases (the version downgrade).
// - FlashWalker: s_varm_crit_ram_trash_pick_boot_slot - slot 0 at flash byte 0, slot 1 at the slot size unless slot 0
//   decides alone, the same choice between their PARTITION_TABLEs.
// - predictBoot: the flash boot after the scan - the slot's own image, else the table's partitions in order, and the
//   launch's checks (s_varm_crit_ram_trash_perform_flash_scan_and_maybe_run_image, varm_launch_image.c).
// BootRules carries what besides flash decides: the ROM revision and the OTP settings. Where A2 differs from A4 (A3
// fixed all but D4/D5): D1 a window read to its end with words left in the buffer is invalid on A2; D2 A2 has no
// 256-block limit (a cycle hangs it: Outcome::hangs); D3 A2's prefer rule tests the held block (RP2350-E13); D4
// EXTRA_SECURITY and the ENTRY_POINT/VECTOR_TABLE value 14 are A4's; D5 A4 refuses a load map size that is no multiple
// of 4, A2/A3 round a zero fill up and hash a copy's last word through a shift by size*8 (Hash::unchecked).
// The datasheet describes the results but not these edges, which decide whether a block counts:
// - the ROM reads at most 128 words at a time into a 160-word buffer (PICOBIN_MAX_BLOCK_SIZE). A block found at the
//   start of a read is parsed from that one read - at most 512 bytes; one found further in is first moved to the
//   buffer's start and the buffer filled up - up to 640. Its end marker is read 2 words past the LAST item even when
//   those words were not loaded: then the buffer's older contents decide (kept here as the ROM keeps them, the
//   unknown start taken as zero).
// - the first block: the first start marker, word aligned, from the search start on that starts a well-formed block.
//   The search stops 4 KiB on (5.1.5.2, BLOCK_LIST_SEARCH_MAX) only once a buffer holds no marker: a marker behind a
//   block that runs off the buffer is still found past it. A malformed item (zero size, a LAST of the wrong size, no
//   end marker) ends the walk at once, first block found or not (unsupported_block_error returns -1).
// - the link: signed bytes from the block's start marker, divided by 4 toward zero. The loop is closed when a link
//   reaches the first block again; a non-first block linking to itself, a link below the first block (5.1.5.2), a
//   malformed block after the first, a read outside the window or a 256th block (the ROM counts in 8 bits) make the
//   loop invalid, and then nothing in it counts (5.9.1).
// - the IMAGE_DEF launched (5.1.5.2: "generally the last one seen"): the first executable IMAGE_DEF is held even when
//   the parser rejected it; a later one replaces it unless rejected, or for another CPU while the held one is for Arm
//   (s_varm_crit_prefer_new_image_def). The same for PARTITION_TABLEs (s_varm_crit_prefer_block_common).
// - "rejected" (the parser's bad_item): an item of the wrong size, an EXE for another chip, rollback rows on a block
//   that is not an IMAGE_DEF or a rollback version they cannot hold, items that do not end exactly at LAST, an
//   ENTRY_POINT or VECTOR_TABLE of 14 or, with EXTRA_SECURITY, none. The parser stops at the first: what comes after
//   it is not recorded.
// - the hash: checked only when the block has a HASH_VALUE, and then it needs a SHA-256 HASH_DEF. SHA-256 over each
//   load map entry's source in the window (a zero-fill entry: its size as one word), then the block's first N words
//   (HASH_DEF; N is 8 bits) with the IMAGE_DEF's TBYB bit cleared; HASH_VALUE's 1..8 words are the digest's first
//   words. An entry the ROM refuses (unaligned, outside the window, a runtime range neither RAM nor XIP) fails it.
// Not modelled: the ECDSA (secp256k1) check of a signature and the signing-key match on a secured chip (such a block's
// hash is `unchecked`; the key match is the caller's, against OTP's BOOTKEYn: ParsedBlock::signerKeyHash), the OTP
// rollback rows' value, a FLASH_DEVINFO-sized window (pass the flash's bytes), RISC-V boots, and RP2350-E17/E18 (an
// A2/A3 ROM faulting on a bad ECC pair in the slot size rows).
#include "picobin.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace Kvasir { namespace Picobin {

    /// The first block of a loop is searched for in this many bytes from the search start (5.1.5.2), see the header.
    inline constexpr std::uint32_t firstBlockSearchBytes = 4096U;
    /// The boot ROM's block buffer, PICOBIN_MAX_BLOCK_SIZE: no block is longer.
    inline constexpr std::uint32_t blockBufferWords = picobinMaxBlockSize / 4U;
    /// The most a Walker asks for at once: a block search reads at most 512 bytes (varm_blocks.c: "limit read to
    /// 0x200"), the hash's reads of a load map entry come in pieces of this size.
    inline constexpr std::uint32_t maxReadBytes = 4096U;
    /// The most a block search reads at once (varm_blocks.c: "limit read to 0x200 so we don't overshoot 0x1000").
    inline constexpr std::uint32_t blockReadWords = 0x200U / 4U;
    /// The most blocks a loop may have: the boot ROM counts them in a uint8_t.
    inline constexpr std::uint32_t maxLoopBlocks = 255U;
    /// PARTITION_TABLE_MAX_PARTITIONS of the boot ROM.
    inline constexpr std::size_t maxPartitions = 16U;
    /// IMAGE_TYPE's TBYB flag (PICOBIN_IMAGE_TYPE_EXE_TBYB_BITS): bit 15 of the flags, the item word's upper half.
    inline constexpr std::uint16_t imageTypeTbyb = 0x8000U;
    /// Where the boot ROM looks for partition table slot 1 unless OTP FLASH_PARTITION_SLOT_SIZE says otherwise
    /// ((value + 1) << 12, varm_flash_boot.c).
    inline constexpr std::uint32_t defaultSlotSize = 4096U;
    /// The XIP address of flash byte 0.
    inline constexpr std::uint32_t xipBaseAddress = 0x1000'0000U;

    /// The boot ROM's revision: the byte at ROM address 0x13 (RP2350 datasheet 5.4.1 "Bootrom version"; 2, 3, 4 for the
    /// A2, A3, A4 ROM - pico-bootrom-rp2350 arm8_bootrom_rt0.S `_minor_version`). Where the revisions decide differently
    /// the reader follows the one named (see the header).
    enum class RomVersion : std::uint8_t { a2 = 2, a3 = 3, a4 = 4 };

    /// What the boot ROM's decisions depend on besides flash: its revision and the OTP settings it reads (RP2350
    /// datasheet 13.10 BOOT_FLAGS0 row 0x048, FLASH_PARTITION_SLOT_SIZE 0x055, CRIT1 0x040). The defaults are a chip
    /// with an A4 ROM and unprogrammed OTP.
    struct BootRules {
        RomVersion    rom{RomVersion::a4};
        std::uint32_t slotSize{
          defaultSlotSize};   ///< (FLASH_PARTITION_SLOT_SIZE + 1) * 4096 with its override bit
        bool hashedPartitionTable{};   ///< BOOT_FLAGS0.HASHED_PARTITION_TABLE: a table needs a hash
        bool
          rollbackRequired{};   ///< BOOT_FLAGS0.ROLLBACK_REQUIRED: an IMAGE_DEF needs a rollback version
        bool
          singleFlashBinary{};   ///< BOOT_FLAGS0.SINGLE_FLASH_BINARY: no partition table is looked for
        bool
          watchdogScratchDisabled{};   ///< BOOT_FLAGS0.DISABLE_WATCHDOG_SCRATCH: no flash update boot
        bool autoSwitchArchDisabled{};   ///< BOOT_FLAGS0.DISABLE_AUTO_SWITCH_ARCH
        /// CRIT1.SECURE_BOOT_ENABLE: IMAGE_DEFs need a signature (and with secureBootPartitionTable, tables too). The
        /// signature itself is not checked here: such a block's hash comes out `unchecked`.
        bool secure{};
        bool securePartitionTable{};   ///< BOOT_FLAGS0.SECURE_PARTITION_TABLE (with `secure` only)

        constexpr bool operator==(BootRules const&) const = default;
    };

    /// Words as the flash holds them, little endian: what the readers here take (chip/picobin.hpp's blocks, a block
    /// loop made up at run time).
    template<std::size_t N>
    [[nodiscard]] constexpr std::array<std::byte,
                                       N * 4U>
    asBytes(std::array<std::uint32_t,
                       N> const& words) {
        std::array<std::byte, N * 4U> out{};
        for(std::size_t i = 0; i < out.size(); ++i) {
            out[i] = static_cast<std::byte>((words[i / 4U] >> (8U * (i % 4U))) & 0xffU);
        }
        return out;
    }

    /// SHA-256 (FIPS 180-4), byte stream in, digest out: what the boot ROM's SHA-256 block computes for its hashes.
    class Sha256 {
    public:
        constexpr void update(std::span<std::byte const> bytes) {
            for(auto const b : bytes) { put(std::to_integer<std::uint8_t>(b)); }
        }

        /// One word as the flash holds it (little endian): the ROM's s_varm_sha256_put_word_inc.
        constexpr void updateWord(std::uint32_t word) {
            for(unsigned i = 0; i < 4U; ++i) { put(static_cast<std::uint8_t>(word >> (8U * i))); }
        }

        [[nodiscard]] constexpr std::array<std::byte,
                                           32>
        finish() {
            std::uint64_t const bits = total_ * 8U;
            put(0x80U);
            while(used_ != 56U) { put(0U); }
            for(unsigned i = 0; i < 8U; ++i) {
                put(static_cast<std::uint8_t>(bits >> (56U - (8U * i))));
            }
            std::array<std::byte, 32> out{};
            for(std::size_t i = 0; i < out.size(); ++i) {
                out[i]
                  = static_cast<std::byte>((state_[i / 4U] >> (24U - (8U * (i % 4U)))) & 0xffU);
            }
            return out;
        }

    private:
        static constexpr std::array<std::uint32_t, 64> k{
          0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
          0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
          0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
          0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
          0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
          0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
          0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
          0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
          0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
          0xc67178f2U};

        std::array<std::uint32_t, 8> state_{0x6a09e667U,
                                            0xbb67ae85U,
                                            0x3c6ef372U,
                                            0xa54ff53aU,
                                            0x510e527fU,
                                            0x9b05688cU,
                                            0x1f83d9abU,
                                            0x5be0cd19U};
        std::array<std::uint8_t, 64> block_{};
        std::uint32_t                used_{};
        std::uint64_t                total_{};

        static constexpr std::uint32_t rotr(std::uint32_t x,
                                            unsigned      n) {
            return (x >> n) | (x << (32U - n));
        }

        constexpr void put(std::uint8_t b) {
            block_[used_++] = b;
            ++total_;
            if(used_ == 64U) {
                compress();
                used_ = 0U;
            }
        }

        constexpr void compress() {
            std::array<std::uint32_t, 64> w{};
            for(std::size_t i = 0; i < 16U; ++i) {
                w[i] = (std::uint32_t{block_[4U * i]} << 24U)
                     | (std::uint32_t{block_[(4U * i) + 1U]} << 16U)
                     | (std::uint32_t{block_[(4U * i) + 2U]} << 8U)
                     | std::uint32_t{block_[(4U * i) + 3U]};
            }
            for(std::size_t i = 16U; i < 64U; ++i) {
                auto const s0 = rotr(w[i - 15U], 7U) ^ rotr(w[i - 15U], 18U) ^ (w[i - 15U] >> 3U);
                auto const s1 = rotr(w[i - 2U], 17U) ^ rotr(w[i - 2U], 19U) ^ (w[i - 2U] >> 10U);
                w[i]          = w[i - 16U] + s0 + w[i - 7U] + s1;
            }
            auto v = state_;
            for(std::size_t i = 0; i < 64U; ++i) {
                auto const s1  = rotr(v[4], 6U) ^ rotr(v[4], 11U) ^ rotr(v[4], 25U);
                auto const ch  = (v[4] & v[5]) ^ (~v[4] & v[6]);
                auto const t1  = v[7] + s1 + ch + k[i] + w[i];
                auto const s0  = rotr(v[0], 2U) ^ rotr(v[0], 13U) ^ rotr(v[0], 22U);
                auto const maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
                auto const t2  = s0 + maj;
                v              = {t1 + t2, v[0], v[1], v[2], v[3] + t1, v[4], v[5], v[6]};
            }
            for(std::size_t i = 0; i < 8U; ++i) { state_[i] += v[i]; }
        }
    };

    /// What one block says, as the boot ROM's parser records it, and whether its hash holds.
    struct ParsedBlock {
        /// The VERSION item's major.minor (5.9.2.1: minor in the low half, major in the high half; the boot ROM
        /// compares the word unsigned, major first).
        struct Version {
            std::uint16_t major{};
            std::uint16_t minor{};

            constexpr auto operator<=>(Version const&) const = default;
        };

        /// The hash check (s_varm_crit_ram_trash_verify_block): `none` without a HASH_VALUE item (nothing to check,
        /// the ROM takes the block), `bad` for a mismatch or anything the ROM refuses on the way, `unchecked` where
        /// what decides is not computed here: a signature a secured chip requires, or an A2/A3 load map entry whose
        /// size is no multiple of 4 (their source masks the last word with `(1u << (size * 8u)) - 1`, a shift past
        /// 31 - what the ROM hashes then is its compiler's, not the source's).
        enum class Hash : std::uint8_t { none, ok, bad, unchecked };

        std::uint32_t offset{};       ///< its start marker, bytes from the window start
        std::int64_t  nextOffset{};   ///< where its link leads: offset + the link, in whole words
        bool          imageDef{};     ///< the first item is a one-word IMAGE_TYPE
        bool          partitionTable{};   ///< the first item is PARTITION_TABLE
        std::uint16_t
          imageTypeFlags{};   ///< IMAGE_TYPE's flags (the item's upper half); 0 when no IMAGE_DEF
        bool                   exe{};      ///< IMAGE_TYPE says EXE
        bool                   tbyb{};     ///< an EXE with the TBYB bit: not bought (5.1.17)
        std::optional<Version> version;    ///< a VERSION item
        std::uint16_t rollbackVersion{};   ///< VERSION's rollback version; 0 without OTP rows
        bool signature{};   ///< a secp256k1 SIGNATURE item, the only kind the boot ROM checks
        /// The signer's key as OTP knows it: SHA-256 of the secp256k1 SIGNATURE item's public key. The item (RP2350
        /// datasheet 5.9.2.4) is the header, 16 words of public key, 16 of signature; picotool writes the key as X then
        /// Y, 32 bytes each, big endian (mbedtls_mpi_write_binary), and the digest is over those 64 bytes as flash
        /// holds them - what the boot ROM hashes (s_varm_crit_is_signing_key_match) and compares with each valid
        /// BOOTKEYn (16 OTP rows: the digest's bytes 2k, 2k+1 in row k, low byte first). picotool seal --sign writes
        /// the same digest as its OTP json's `bootkey0`, and SHA-256 of the key file's last 64 DER bytes is it too. So
        /// whether a block is signed with a key a chip accepts is decided without ECDSA: a valid BOOTKEYn equal to
        /// this; whether the signature holds is not (see the header). nullopt without a secp256k1 SIGNATURE item; of
        /// several the last counts, as in the ROM.
        std::optional<std::array<std::byte, 32>> signerKeyHash;
        bool hashDef{};   ///< a SHA-256 HASH_DEF item, the only kind the boot ROM uses
        /// The parser would not use it (bad_item, see the header): chosen only when nothing came before it, and then
        /// not booted.
        bool rejected{};
        Hash hash{Hash::none};   ///< set by the Walker that found the block, once it is chosen
        std::uint32_t rollingWindowDelta{};   ///< ROLLING_WINDOW_DELTA, 0 without one

        /// What the ROM calls verified: parsed without objection and its hash, if any, holds.
        [[nodiscard]] constexpr bool verified() const {
            return !rejected && (hash == Hash::none || hash == Hash::ok);
        }

        /// Whether verified() is the ROM's answer (false: `unchecked`).
        [[nodiscard]] constexpr bool decided() const { return rejected || hash != Hash::unchecked; }

        constexpr bool operator==(ParsedBlock const&) const = default;
    };

    /// One partition of a partition table: the two words the boot ROM keeps of it (5.9.4, PICOBIN_PARTITION_*), and
    /// its id when it has one.
    struct PartitionEntry {
        std::uint32_t                permissionsAndLocation{};
        std::uint32_t                permissionsAndFlags{};
        std::optional<std::uint64_t> id;

        [[nodiscard]] constexpr std::uint32_t firstSector() const {
            return Picobin::firstSector(permissionsAndLocation);
        }

        [[nodiscard]] constexpr std::uint32_t lastSector() const {
            return Picobin::lastSector(permissionsAndLocation);
        }

        constexpr bool operator==(PartitionEntry const&) const = default;
    };

    /// A partition table as its PARTITION_TABLE item holds it, in the order the boot ROM numbers the partitions.
    struct PartitionTable {
        std::uint32_t                             blockOffset{};   ///< where its block starts
        std::uint32_t                             unpartitionedPermissionsAndFlags{};
        std::uint8_t                              count{};
        bool                                      singleton{};
        std::array<PartitionEntry, maxPartitions> partitions{};

        constexpr bool operator==(PartitionTable const&) const = default;
    };

    namespace Detail {
        inline constexpr std::uint32_t itemTypeMask = 0x7fU;
        inline constexpr std::uint32_t lastItemByte
          = 0x80U | static_cast<std::uint32_t>(ItemType::last);

        // IMAGE_TYPE flags (pico-sdk boot/picobin.h PICOBIN_IMAGE_TYPE_*; chip/picobin.hpp's ImageDef writes them)
        inline constexpr std::uint32_t imageTypeItem
          = 0x0100U | static_cast<std::uint32_t>(ItemType::imageType);
        inline constexpr std::uint32_t imageTypeMask = 0x000fU;
        inline constexpr unsigned      cpuShift      = 8U;
        inline constexpr std::uint32_t cpuMask       = 0x0700U;
        inline constexpr std::uint32_t extraSecurity = 0x0800U;
        inline constexpr unsigned      chipShift     = 12U;
        inline constexpr std::uint32_t chipMask      = 0x7000U;
        inline constexpr std::uint32_t tbybBit       = imageTypeTbyb;
        // ENTRY_POINT / VECTOR_TABLE "needed but not set" (EXTRA_SECURITY): the ROM's marker, refused as an address
        inline constexpr std::uint32_t unsetAddress = 14U;
        inline constexpr std::uint32_t secp256k1    = 1U;   // PICOBIN_SIGNATURE_SECP256K1

        // PARTITION_TABLE item (5.9.4.1; the ROM's varm_blocks.c and get_pt_partition_info, picotool's metadata.h):
        // byte 3 = singleton (bit 7) and the count (low 7 bits, at most 16); then the unpartitioned word; per partition
        // its two words, 2 words of id (HAS_ID), one word per extra family, a name (HAS_NAME: a length byte, bit 7
        // reserved, the characters, padded to a word). The item must end exactly after the last partition.
        inline constexpr std::uint32_t ptSingleton        = 0x8000'0000U;
        inline constexpr unsigned      ptCountShift       = 24U;
        inline constexpr std::uint32_t ptCountMask        = 0x7fU;
        inline constexpr std::uint32_t ptHasId            = 0x0000'0001U;
        inline constexpr unsigned      ptExtraFamilyShift = 7U;
        inline constexpr std::uint32_t ptExtraFamilyMask  = 0x3U;
        inline constexpr std::uint32_t ptHasName          = 0x0000'1000U;
        inline constexpr std::uint32_t ptNameLengthMask   = 0x7fU;

        // the memory map the ROM checks a load map against (pico-sdk addressmap.h; varm_misc.S varm_is_sram_or_xip_ram,
        // varm_blocks.c inline_s_is_xip)
        inline constexpr std::uint32_t xipSramBase = 0x13ff'c000U;
        inline constexpr std::uint32_t sramEnd     = 0x2008'2000U;

        [[nodiscard]] constexpr bool isSramOrXipRam(std::uint32_t address) {
            return (address >> 14U) == (xipSramBase >> 14U)
                || ((address >> 25U) == 0x10U && address < sramEnd);
        }

        [[nodiscard]] constexpr bool isXip(std::uint32_t address) {
            return (address >> 25U) == (xipBaseAddress >> 25U);
        }

        [[nodiscard]] constexpr std::uint32_t code(ItemType type) {
            return static_cast<std::uint32_t>(type);
        }

        /// An item's size in words, header included (s_varm_decode_item_size_impl, varm_misc.S).
        [[nodiscard]] constexpr std::uint32_t itemWords(std::uint32_t header) {
            return (header & 0x80U) != 0U ? (header >> 8U) & 0xffffU : (header >> 8U) & 0xffU;
        }

        /// The little-endian word at byte `at` of `bytes`.
        [[nodiscard]] constexpr std::uint32_t wordAt(std::span<std::byte const> bytes,
                                                     std::size_t                at) {
            return std::to_integer<std::uint32_t>(bytes[at])
                 | (std::to_integer<std::uint32_t>(bytes[at + 1U]) << 8U)
                 | (std::to_integer<std::uint32_t>(bytes[at + 2U]) << 16U)
                 | (std::to_integer<std::uint32_t>(bytes[at + 3U]) << 24U);
        }

        /// The ROM's buffer: the block from its start marker on (word 0), `last` = the LAST item's index (the ROM's
        /// end_marker_buf_pos, the block's size in words). Words up to last + 2 are there.
        using BlockWords = std::span<std::uint32_t const>;

        /// get_pt_partition_info's walk of a PARTITION_TABLE item (varm_apis.c): the entries, or nullopt where the ROM
        /// answers BOOTROM_ERROR_INVALID_DATA (an entry past the item, the item not ending after the last one).
        [[nodiscard]] constexpr std::optional<PartitionTable>
        readPartitionTable(BlockWords    block,
                           std::uint32_t last,
                           std::uint32_t blockOffset) {
            auto const    header   = block[1];
            auto const    itemSize = itemWords(header);
            auto const    count    = (header >> ptCountShift) & ptCountMask;
            std::uint32_t pos      = 2U;   // item words: 0 the header, 1 the unpartitioned word
            auto const    word     = [&](std::uint32_t i) -> std::optional<std::uint32_t> {
                // the item lies inside the block; a walk past the block reads nothing the ROM would trust either
                if(1U + i >= last) { return std::nullopt; }
                return block[1U + i];
            };
            PartitionTable out{};
            out.blockOffset                      = blockOffset;
            out.singleton                        = (header & ptSingleton) != 0U;
            out.count                            = static_cast<std::uint8_t>(count);
            out.unpartitionedPermissionsAndFlags = block[2];
            for(std::uint32_t i = 0U; i < count; ++i) {
                auto const location = word(pos);
                auto const flags    = word(pos + 1U);
                if(!location || !flags) { return std::nullopt; }
                pos += 2U;
                auto& entry                  = out.partitions[i];
                entry.permissionsAndLocation = *location;
                entry.permissionsAndFlags    = *flags;
                if((*flags & ptHasId) != 0U) {
                    auto const lo = word(pos);
                    auto const hi = word(pos + 1U);
                    if(!lo || !hi) { return std::nullopt; }
                    entry.id = std::uint64_t{*lo} | (std::uint64_t{*hi} << 32U);
                    pos += 2U;
                }
                auto end = pos + ((*flags >> ptExtraFamilyShift) & ptExtraFamilyMask);
                if((*flags & ptHasName) != 0U) {
                    auto const length = word(end);
                    if(!length) { return std::nullopt; }
                    end += ((*length & ptNameLengthMask) + 4U) / 4U;
                }
                if(end > itemSize) { return std::nullopt; }
                pos = end;
            }
            if(pos != itemSize) { return std::nullopt; }
            return out;
        }

        /// What s_varm_crit_parse_block makes of a block, with what its verification needs: the block's words (TBYB
        /// cleared, as the parser leaves them for the hash) and where its hash items and load map are.
        struct Parsed {
            ParsedBlock                                 block;
            std::optional<PartitionTable>               table;
            std::array<std::uint32_t, blockBufferWords> words{};
            std::uint32_t hashDefWords{};   // HASH_DEF's words of the block, 8 bits
            std::uint32_t hashValueAt{};    // the HASH_VALUE's first word, 0 for none
            std::uint32_t hashValueWords{};
            std::uint32_t loadMapAt{};       // the LOAD_MAP item's header, 0 for none
            std::uint32_t rollingDelta{};    // ROLLING_WINDOW_DELTA
            std::uint32_t signatureWord{};   // the word the ROM disturbs, 0 for none
        };

        [[nodiscard]] constexpr std::optional<Parsed> parse(BlockWords       block,
                                                            std::uint32_t    last,
                                                            std::uint32_t    offset,
                                                            std::int64_t     nextOffset,
                                                            BootRules const& rules,
                                                            bool             partitionTables) {
            if(last < 2U) { return std::nullopt; }   // "Invalid block size words"
            Parsed out{};
            // the block and what follows its LAST in the buffer (a HASH_DEF may count those words in)
            std::copy_n(block.begin(),
                        std::min<std::size_t>(block.size(), blockBufferWords),
                        out.words.begin());
            auto& b      = out.block;
            b.offset     = offset;
            b.nextOffset = nextOffset;
            std::uint32_t word{};
            std::uint32_t flags{};
            // the flash scan tries PARTITION_TABLE first, then IMAGE_DEF (s_varm_crit_search_window)
            if(partitionTables && (block[1] & itemTypeMask) == code(ItemType::partitionTable)) {
                b.partitionTable = true;
                auto const size  = itemWords(block[1]);
                auto const count = (block[1] >> ptCountShift) & ptCountMask;
                if(size < 2U || count > maxPartitions) {
                    b.rejected = true;
                    return out;
                }
                out.table = readPartitionTable(block, last, offset);
                word      = size + 1U;
            } else if((block[1] & 0xffffU) == imageTypeItem) {
                b.imageDef       = true;
                flags            = block[1] >> 16U;
                b.imageTypeFlags = static_cast<std::uint16_t>(flags);
                b.exe = (flags & imageTypeMask) == static_cast<std::uint32_t>(ImageType::exe);
                if(b.exe) {
                    b.tbyb = (flags & tbybBit) != 0U;
                    out.words[1]
                      &= ~(tbybBit << 16U);   // "clear tbyb flag so we don't break sig check"
                    if(((flags & chipMask) >> chipShift)
                       != static_cast<std::uint32_t>(ExeChip::rp2350))
                    {
                        b.rejected = true;   // "executable image_def is not for RP2350"
                        return out;
                    }
                }
                word = 2U;
            } else {
                return std::nullopt;
            }

            // EXTRA_SECURITY and the 14 marker are A4's (A2/A3 ignore the bit)
            bool const    a4         = rules.rom >= RomVersion::a4;
            bool const    extra      = a4 && b.exe && (flags & extraSecurity) != 0U;
            std::uint32_t entryPoint = extra ? unsetAddress : 0U;
            std::uint32_t vectorTab  = extra ? unsetAddress : 0U;
            auto const    bad        = [&] {
                b.rejected = true;
                return out;
            };
            while(word < last) {
                auto const header = block[word++];
                auto const size   = itemWords(header);
                auto const type   = header & itemTypeMask;
                auto const data
                  = header >> 24U;   // byte 3: hash type, signature type, OTP row count
                if(size == 0U || type == code(ItemType::last)) { break; }
                if(type == code(ItemType::hashDef)) {
                    if(size != 2U) { return bad(); }
                    if(data == static_cast<std::uint32_t>(HashAlgorithm::sha256)) {
                        b.hashDef        = true;
                        out.hashDefWords = block[word] & 0xffU;   // block_word_index_t is a uint8_t
                    }
                } else if(type == code(ItemType::hashValue)) {
                    if(size < 2U || size > 9U) { return bad(); }
                    out.hashValueAt    = word;
                    out.hashValueWords = size - 1U;
                } else if(type == code(ItemType::signature)) {
                    if(size != 33U) { return bad(); }
                    if(data == secp256k1) {
                        b.signature = true;
                        out.signatureWord
                          = word
                          + 16U;   // the ROM adds a per-boot value to it (CTAG_SIG_DISRUPTER)
                        // the public key's 16 words, hashed as the ROM hashes them (sb_sha256_update_32 over
                        // the buffer: each word's bytes as flash holds them). A key that runs past LAST is the
                        // block's end - the item does too, and the block is rejected below.
                        if(word + 16U <= last) {
                            Sha256 key{};
                            for(std::uint32_t i = 0; i < 16U; ++i) {
                                key.updateWord(block[word + i]);
                            }
                            b.signerKeyHash = key.finish();
                        }
                    }
                } else if(type == code(ItemType::version)) {
                    // 5.9.2.1: 2 + ((rows != 0) + rows + 1) / 2 words; word 2 the rollback version (low half) and
                    // the first row, the rest of the rows two per word
                    auto const rows = data;
                    if(size != 2U + ((rows != 0U ? 1U : 0U) + rows + 1U) / 2U) { return bad(); }
                    b.version = ParsedBlock::Version{
                      .major = static_cast<std::uint16_t>(block[word] >> 16U),
                      .minor = static_cast<std::uint16_t>(block[word] & 0xffffU)};
                    if(rows != 0U) {
                        if(!b.imageDef) {
                            return bad();
                        }   // "Rollback version is only supported on IMAGE_DEF"
                        auto const rollback = block[word + 1U] & 0xffffU;
                        if(rollback >= rows * 24U) { return bad(); }   // more than the rows' bits
                        b.rollbackVersion = static_cast<std::uint16_t>(rollback);
                    }
                } else if(b.imageDef) {
                    if(type == code(ItemType::vectorTable)) {
                        if(size != 2U) { return bad(); }
                        vectorTab = block[word];
                    } else if(type == code(ItemType::entryPoint)) {
                        if(size != 3U && size != 4U) { return bad(); }
                        entryPoint = block[word];
                    } else if(type == code(ItemType::rollingWindow)) {
                        if(size != 2U) { return bad(); }
                        out.rollingDelta     = block[word];
                        b.rollingWindowDelta = block[word];
                    } else if(type == code(ItemType::loadMap)) {
                        // the entry count is bits 24..30
                        if(size != 1U + ((header << 1U) >> 25U) * 3U) { return bad(); }
                        out.loadMapAt = word - 1U;
                    }
                }
                word += size - 1U;
            }
            if(word != last) { return bad(); }   // "block size mismatch"
            if(b.imageDef && rules.rollbackRequired && b.rollbackVersion == 0U) {
                return bad();   // "no rollback version, but OTP says rollback version required"
            }
            if(a4 && b.imageDef && (entryPoint == unsetAddress || vectorTab == unsetAddress)) {
                return bad();
            }
            return out;
        }

        /// Whether `candidate` replaces `held` (s_varm_crit_prefer_new_image_def on an Arm boot, executable IMAGE_DEFs
        /// only; s_varm_crit_prefer_block_common for a PARTITION_TABLE when `imageDef` is false).
        [[nodiscard]] constexpr bool replaces(std::optional<Parsed> const& held,
                                              ParsedBlock const&           candidate,
                                              bool                         imageDef,
                                              RomVersion                   rom) {
            if(imageDef && !(candidate.imageDef && candidate.exe)) { return false; }
            if(!held.has_value()) { return true; }
            // A2 tests the HELD block's parse (RP2350-E13, fixed in A3): a rejected block held refuses every later one,
            // a rejected later one replaces a good one
            if(rom == RomVersion::a2 ? held->block.rejected : candidate.rejected) { return false; }
            if(!imageDef) { return true; }
            auto const forArm = [](ParsedBlock const& block) {
                return ((std::uint32_t{block.imageTypeFlags} & cpuMask) >> cpuShift)
                    == static_cast<std::uint32_t>(ExeCpu::arm);
            };
            return forArm(candidate) || !forArm(held->block);
        }

        /// inline_is_version_greater (varm_boot_path.h): the rollback version first, then major.minor as one word.
        [[nodiscard]] constexpr bool versionGreater(ParsedBlock const& a,
                                                    ParsedBlock const& b) {
            auto const word = [](ParsedBlock const& x) {
                return x.version ? (std::uint32_t{x.version->major} << 16U) | x.version->minor : 0U;
            };
            return a.rollbackVersion > b.rollbackVersion
                || (a.rollbackVersion == b.rollbackVersion && word(a) > word(b));
        }
    }   // namespace Detail

    /// The window a Walker searches: `bytes` of flash (a partition, or the whole flash), `xipAddress` its first byte's
    /// XIP address (0x10000000 + the partition's offset; the load map's absolute addresses roll by it), `searchStart`
    /// where the search for the first block begins (a partition table slot: 0, or the slot size).
    struct Window {
        std::uint32_t bytes{0xffff'fffcU};
        std::uint32_t xipAddress{xipBaseAddress};
        std::uint32_t searchStart{};
    };

    /// The bytes of a window an image takes up, [first, end) from the window start (Walker::footprint()).
    struct Footprint {
        std::uint32_t first{};
        std::uint32_t end{};
        /// The IMAGE_DEF has a LOAD_MAP: its sources are in the range. Without one only the block loop is.
        bool loadMap{};

        constexpr bool operator==(Footprint const&) const = default;
    };

    /// The boot ROM's walk of one window for a reader that gets the bytes a piece at a time and maybe asynchronously:
    /// want() says which bytes to read next (at most maxReadBytes), feed() takes them, until want() is empty. Then
    /// imageDef() is the IMAGE_DEF the boot ROM launches from the window and partitionTable() the table of its loop,
    /// each with its hash checked. A feed shorter than asked for is the end of the window.
    class Walker {
    public:
        enum class Outcome : std::uint8_t {
            running,
            noBlock,   ///< no well-formed block where a first one may be
            invalid,   ///< a loop, but broken: nothing in it counts
            closed,    ///< a valid loop
            /// A2 only: a cycle that never reaches the first block again - A2 has no block limit (A3 added the
            /// 256th-block rule) and walks it for ever
            hangs,
        };

        /// `rules`: the ROM revision and OTP settings; `partitionTables`: whether PARTITION_TABLE blocks count as
        /// such (the flash scan; a partition's own search does not look for them, dont_scan_for_partition_tables);
        /// `hashImageDef`: whether the IMAGE_DEF's hash is checked - which reads every load map entry's source, the
        /// whole image. False for a reader that wants the partition table only: the walk and every choice in it are
        /// the same, the IMAGE_DEF's hash comes out `unchecked` (none without a HASH_VALUE), and the reads end with
        /// the loop. A firmware reading its own table in a main-loop turn takes that: a software SHA-256 over an
        /// image is tens of ms in a release build and the better part of a second in a checked one.
        constexpr explicit Walker(Window    window,
                                  BootRules rules           = {},
                                  bool      partitionTables = true,
                                  bool      hashImageDef    = true)
          : window_{window}
          , rules_{rules}
          , partitionTables_{partitionTables}
          , hashImageDef_{hashImageDef}
          , windowWords_{window.bytes / 4U}
          , nextRead_{window.searchStart / 4U}
          , firstMax_{std::min(window.bytes / 4U,
                               (window.searchStart + firstBlockSearchBytes) / 4U)} {
            run();
        }

        /// A partition or image of `windowBytes` (the default leaves the size to the first short feed) at flash byte 0.
        constexpr explicit Walker(std::uint32_t windowBytes = 0xffff'fffcU)
          : Walker{Window{.bytes = windowBytes}} {}

        /// {offset, length} in bytes from the window start to read next; nullopt once the walk is done.
        [[nodiscard]] constexpr std::optional<std::pair<std::uint32_t,
                                                        std::uint32_t>>
        want() const {
            if(phase_ == Phase::done) { return std::nullopt; }
            return std::pair{readAt_, readBytes_};
        }

        /// The bytes read for the last want(); fewer than asked for where the window ends. Ignored once done.
        constexpr void feed(std::span<std::byte const> bytes) {
            if(phase_ == Phase::walk) {
                auto const words = std::min<std::size_t>(bytes.size() / 4U, toRead_);
                for(std::size_t i = 0; i < words; ++i) {
                    buffer_[count_ + i] = Detail::wordAt(bytes, 4U * i);
                }
                if(
                  words
                  < toRead_) {   // the window ends here: what the ROM, knowing its size, would have read
                    windowWords_ = nextRead_ + static_cast<std::uint32_t>(words);
                    firstMax_    = std::min(firstMax_, windowWords_);
                }
                nextRead_ += static_cast<std::uint32_t>(words);
                count_ += static_cast<std::uint32_t>(words);
                run(true);
            } else if(phase_ == Phase::hash) {
                if(bytes.size() < readBytes_) {   // the window ended inside an entry
                    endHash(false);
                    return;
                }
                sha_.update(bytes.first(readBytes_));
                hashed_ += readBytes_;
                nextHashRead();
            }
        }

        [[nodiscard]] constexpr Outcome outcome() const { return outcome_; }

        [[nodiscard]] constexpr bool done() const { return phase_ == Phase::done; }

        /// Where the first block was found, once it was.
        [[nodiscard]] constexpr std::optional<std::uint32_t> firstBlock() const {
            return first_ < 0
                   ? std::nullopt
                   : std::optional<std::uint32_t>{static_cast<std::uint32_t>(first_) * 4U};
        }

        /// Once done: the IMAGE_DEF the boot ROM picks from the window - nullopt for no closed loop or no executable
        /// IMAGE_DEF. One the ROM picks but does not boot is returned too: `verified()` false (rejected, a bad hash).
        [[nodiscard]] constexpr std::optional<ParsedBlock> imageDef() const {
            if(!done() || outcome_ != Outcome::closed || !image_) { return std::nullopt; }
            return image_->block;
        }

        /// Once done: the PARTITION_TABLE block of the loop (`verified()`: its hash holds), nullopt for none.
        [[nodiscard]] constexpr std::optional<ParsedBlock> partitionTableBlock() const {
            if(!done() || outcome_ != Outcome::closed || !table_) { return std::nullopt; }
            return table_->block;
        }

        /// Once done: the partition table the boot ROM loads from this loop - nullopt for none, for no closed loop, for
        /// a table that is not verified, or one whose entries do not check out (the ROM's INVALID_DATA).
        [[nodiscard]] constexpr std::optional<PartitionTable> partitionTable() const {
            if(!done() || outcome_ != Outcome::closed || !table_.has_value()
               || !table_->block.verified())
            {
                return std::nullopt;
            }
            return table_->table;
        }

        /// The window, its size as far as the reads found it.
        [[nodiscard]] constexpr Window window() const {
            return Window{.bytes       = windowWords_ * 4U,
                          .xipAddress  = window_.xipAddress,
                          .searchStart = window_.searchStart};
        }

        /// Once done: the singleton flag of the loop's PARTITION_TABLE (byte 3 bit 7), whatever its entries say.
        [[nodiscard]] constexpr bool partitionTableSingleton() const {
            return partitionTableBlock().has_value() && table_.has_value()
                && (table_->words[1] & Detail::ptSingleton) != 0U;
        }

        /// Once done: the bytes of the window the image of imageDef() takes up as the boot ROM reads it - which sectors
        /// it occupies. Taken from the blocks alone, nothing read or hashed for it: a walk without the image's hash
        /// (`hashImageDef` false, the loop's few reads) has it too. The smallest range [first, end) from the window
        /// start that holds
        /// - the block loop: every block of it, from its start marker to its end marker (the loop's PARTITION_TABLE
        ///   block too, if there is one);
        /// - each LOAD_MAP entry's source - what the ROM copies and hashes: a relative entry's from the LOAD_MAP item
        ///   on, an absolute one's as the ROM translates it, the runtime address 0x10000000 at the window's byte
        ///   searchStart + ROLLING_WINDOW_DELTA (what the launch's rolling window maps there). A zero fill has none.
        /// nullopt for no IMAGE_DEF (imageDef()) and for a load map entry the ROM refuses (a runtime range unaligned or
        /// neither RAM nor XIP, a source unaligned or outside the window: verify_fail, nothing launches). For an
        /// IMAGE_DEF that is not verified() it is what the parser recorded. An image without a LOAD_MAP (`loadMap`
        /// false) names no bytes besides its blocks: a flash image run in place, unsealed, has a vector table in front
        /// of its first block and its code after it that the range need not cover.
        [[nodiscard]] constexpr std::optional<Footprint> footprint() const {
            if(!done() || outcome_ != Outcome::closed || !image_) { return std::nullopt; }
            Footprint out{.first = loopFirst_, .end = loopEnd_, .loadMap = image_->loadMapAt != 0U};
            for(std::uint32_t i = 0; i < loadMapEntries(*image_); ++i) {
                auto const entry = loadMapEntry(*image_, i);
                if(entry.kind == LoadMapEntry::Kind::refused) { return std::nullopt; }
                if(entry.kind == LoadMapEntry::Kind::zeroFill || entry.size == 0U) { continue; }
                // a size no multiple of 4 (A2/A3 take it): the ROM reads the last word whole
                out.first = std::min(out.first, entry.from);
                out.end   = std::max(out.end, entry.from + ((entry.size + 3U) & ~3U));
            }
            return out;
        }

    private:
        /// One load map entry as s_varm_crit_ram_trash_verify_block resolves it, before anything is read.
        struct LoadMapEntry {
            enum class Kind : std::uint8_t {
                refused,   ///< the ROM's verify_fail: an invalid runtime range, a source outside the window
                zeroFill,   ///< `size` is the word the ROM hashes for it
                source,     ///< `size` bytes from `from` (bytes from the window start)
                /// a source whose size is no multiple of 4 (A2/A3; A4 refuses it): see ParsedBlock::Hash::unchecked
                oddSource,
            };
            Kind          kind{Kind::refused};
            std::uint32_t from{};
            std::uint32_t size{};
        };

        /// The LOAD_MAP's entry count (bits 24..30 of its header), 0 without one.
        [[nodiscard]] static constexpr std::uint32_t loadMapEntries(Detail::Parsed const& p) {
            return p.loadMapAt == 0U ? 0U : (p.words[p.loadMapAt] << 1U) >> 25U;
        }

        /// Entry `index` of `p`'s LOAD_MAP: its checks, a zero fill's size word, where its source is.
        [[nodiscard]] constexpr LoadMapEntry loadMapEntry(Detail::Parsed const& p,
                                                          std::uint32_t         index) const {
            bool const rel
              = p.loadMapAt != 0U && static_cast<std::int32_t>(p.words[p.loadMapAt]) >= 0;
            auto const roll
              = window_.xipAddress + window_.searchStart - xipBaseAddress + p.rollingDelta;
            auto const    at      = p.loadMapAt + 1U + (3U * index);
            auto const    storage = p.words[at];
            auto const    vma     = p.words[at + 1U];
            std::uint32_t size    = p.words[at + 2U];
            std::uint32_t to      = vma;
            if(vma < xipBaseAddress + (16U * 1024U * 1024U)) { to += roll; }
            if(!rel) { size -= vma; }
            auto const toEnd = to + size;
            // A4 refuses a size that is no multiple of 4 ("A2/A3 had a bug"); A2/A3 take it
            bool const a4    = rules_.rom >= RomVersion::a4;
            bool const valid = (to & 3U) == 0U && (!a4 || (size & 3U) == 0U)
                            && (Detail::isSramOrXipRam(to) || Detail::isXip(to))
                            && (Detail::isSramOrXipRam(toEnd - (size != 0U ? 1U : 0U))
                                || Detail::isXip(toEnd - (size != 0U ? 1U : 0U)))
                            && ((toEnd - to) >> 27U) == 0U;
            if(!valid) { return {}; }
            if(storage
               == 0U) {   // zero fill: the ROM hashes its size (A2/A3: rounded up to a word)
                return {.kind = LoadMapEntry::Kind::zeroFill,
                        .size = a4 ? size : (size + 3U) & ~3U};
            }
            // the source, as an XIP address and then as bytes from the window start
            std::uint32_t from = storage;
            if(!rel) {
                if(!Detail::isSramOrXipRam(from)) { from += roll; }
            } else {
                from = storage + window_.xipAddress + p.block.offset + (4U * p.loadMapAt);
            }
            auto const windowEnd
              = std::uint64_t{window_.xipAddress} + (std::uint64_t{windowWords_} * 4U);
            if((from & 3U) != 0U || from < window_.xipAddress
               || std::uint64_t{from} + size >= windowEnd || from + size < from)
            {
                return {};
            }
            return {.kind = (size & 3U) != 0U ? LoadMapEntry::Kind::oddSource
                                              : LoadMapEntry::Kind::source,
                    .from = from - window_.xipAddress,
                    .size = size};
        }

        static constexpr std::uint32_t noFirst
          = 0xffff'ffffU;   // the ROM's first block offset -1, compared unsigned

        enum class Phase : std::uint8_t { walk, hash, done };

        Window    window_;
        BootRules rules_;
        bool      partitionTables_;
        bool      hashImageDef_;
        // The ROM's buffer, its 160 words and two more: the end marker test reads 2 words past a LAST item at the
        // buffer's last word. What the ROM reads there is its workarea's next words; kept as zero here.
        std::array<std::uint32_t, blockBufferWords + 2U> buffer_{};
        std::uint32_t                                    count_{};
        std::uint32_t                                    pos_{};
        std::uint32_t                                    bufStart_{};
        std::uint32_t                                    windowWords_;
        std::uint32_t                                    nextRead_;
        std::int64_t                                     first_{-1};
        std::uint32_t                                    firstMax_;
        std::uint32_t                                    toRead_{};
        std::uint32_t                                    blocks_{};
        Outcome                                          outcome_{Outcome::running};
        Phase                                            phase_{Phase::walk};
        std::uint32_t                                    readAt_{};
        std::uint32_t                                    readBytes_{};
        // the loop's extent so far (footprint()): the lowest block start, the furthest block end
        std::uint32_t                 loopFirst_{0xffff'ffffU};
        std::uint32_t                 loopEnd_{};
        std::optional<Detail::Parsed> image_;
        std::optional<Detail::Parsed> table_;
        // the image's hash, in flight: the load map entry and how far into its source
        Sha256        sha_{};
        std::uint32_t entry_{};
        std::uint32_t entryFrom_{};
        std::uint32_t entryBytes_{};
        std::uint32_t hashed_{};

        [[nodiscard]] constexpr std::uint32_t firstWord() const {
            return first_ < 0 ? noFirst : static_cast<std::uint32_t>(first_);
        }

        /// s_varm_crit_search_window's part, for each block next_block returns.
        constexpr void take(std::uint32_t last,
                            std::int64_t  nextOffset) {
            // the block from its start marker (the buffer's word 0) to its end marker: LAST, the link, the marker
            loopFirst_ = std::min(loopFirst_, bufStart_ * 4U);
            loopEnd_   = std::max(loopEnd_, (bufStart_ + last + 3U) * 4U);
            ++blocks_;
            if(rules_.rom == RomVersion::a2) {
                // no limit in A2: a loop longer than the window has block starts revisits one and never ends
                if(blocks_ > std::min(windowWords_, std::uint32_t{1U << 22U}) + 1U) {
                    outcome_ = Outcome::hangs;
                    return;
                }
            } else if(blocks_ > maxLoopBlocks) {
                outcome_ = Outcome::invalid;
                return;
            }
            auto parsed = Detail::parse(std::span{buffer_}.first(last + 3U),
                                        last,
                                        bufStart_ * 4U,
                                        nextOffset,
                                        rules_,
                                        partitionTables_);
            if(!parsed.has_value()) { return; }
            if(parsed->block.partitionTable) {
                if(Detail::replaces(table_, parsed->block, false, rules_.rom)) {
                    table_ = std::move(parsed);
                }
            } else if(Detail::replaces(image_, parsed->block, true, rules_.rom)) {
                image_ = std::move(parsed);
            }
        }

        /// The walk is over: the hashes of what it chose, then done.
        constexpr void walked() {
            if(outcome_ != Outcome::closed) {
                phase_ = Phase::done;
                return;
            }
            if(table_) {
                // a secured chip with SECURE_PARTITION_TABLE wants a signature, HASHED_PARTITION_TABLE a hash
                table_->block.hash = rules_.secure && rules_.securePartitionTable
                                     ? signatureVerdict(*table_)
                                     : blockOnlyHash(*table_, rules_.hashedPartitionTable);
            }
            if(image_ && rules_.secure) {   // every IMAGE_DEF needs a signature on a secured chip
                image_->block.hash = signatureVerdict(*image_);
                phase_             = Phase::done;
                return;
            }
            if(!image_ || image_->hashValueAt == 0U) {
                phase_ = Phase::done;
                return;
            }
            if(!hashImageDef_) {   // the table only: the IMAGE_DEF's hash is not read for
                endUnchecked();
                return;
            }
            if(!image_->block.hashDef
               || image_->block.rejected) {   // HASH_VALUE without a SHA-256 HASH_DEF
                endHash(false);
                return;
            }
            phase_ = Phase::hash;
            entry_ = 0U;
            startEntry();
        }

        /// The digest of the block words after whatever went into `sha` before, against its HASH_VALUE.
        [[nodiscard]] static constexpr bool matches(Detail::Parsed const& p,
                                                    Sha256                sha) {
            if(p.signatureWord != 0U && p.signatureWord < p.hashDefWords) {
                return false;   // a word the ROM changes per boot is hashed: it never matches
            }
            for(std::uint32_t i = 0; i < p.hashDefWords; ++i) {
                sha.updateWord(i < blockBufferWords ? p.words[i] : 0U);
            }
            auto const digest = sha.finish();
            for(std::uint32_t i = 0; i < p.hashValueWords; ++i) {
                std::uint32_t const word = Detail::wordAt(digest, std::size_t{4U} * i);
                if(word != p.words[p.hashValueAt + i]) { return false; }
            }
            return true;
        }

        /// A required signature: none is `bad` (verify_block's verify_fail), one is the signature check's to decide.
        [[nodiscard]] static constexpr ParsedBlock::Hash signatureVerdict(Detail::Parsed const& p) {
            return p.block.signature ? ParsedBlock::Hash::unchecked : ParsedBlock::Hash::bad;
        }

        [[nodiscard]] static constexpr ParsedBlock::Hash blockOnlyHash(Detail::Parsed const& p,
                                                                       bool required) {
            if(p.hashValueAt == 0U) {
                return required ? ParsedBlock::Hash::bad : ParsedBlock::Hash::none;
            }
            if(!p.block.hashDef) { return ParsedBlock::Hash::bad; }
            return matches(p, Sha256{}) ? ParsedBlock::Hash::ok : ParsedBlock::Hash::bad;
        }

        constexpr void endHash(bool good) {
            phase_ = Phase::done;
            if(!image_.has_value()) { return; }   // the hash phase runs only with an IMAGE_DEF held
            image_->block.hash
              = good && matches(*image_, sha_) ? ParsedBlock::Hash::ok : ParsedBlock::Hash::bad;
        }

        constexpr void endUnchecked() {
            phase_ = Phase::done;
            if(image_.has_value()) { image_->block.hash = ParsedBlock::Hash::unchecked; }
        }

        /// The next load map entry (s_varm_crit_ram_trash_verify_block, not booting: verified in place): its checks,
        /// a zero-fill entry's size word, or the reads of its source.
        constexpr void startEntry() {
            if(!image_.has_value()) {   // the hash phase runs only with an IMAGE_DEF held
                phase_ = Phase::done;
                return;
            }
            auto const& p     = *image_;
            auto const  count = loadMapEntries(p);
            for(; entry_ < count; ++entry_) {
                auto const entry = loadMapEntry(p, entry_);
                if(entry.kind == LoadMapEntry::Kind::refused) {
                    endHash(false);
                    return;
                }
                if(entry.kind == LoadMapEntry::Kind::zeroFill) {
                    sha_.updateWord(entry.size);
                    continue;
                }
                if(entry.kind == LoadMapEntry::Kind::oddSource) {   // see Hash::unchecked
                    endUnchecked();
                    return;
                }
                // An empty source: nothing to hash, the next entry in this loop. Its first read set up here, not by
                // nextHashRead() - which comes back here at an entry's end: a call each way would be a recursion one
                // level per empty entry (up to 127) in cmake/tools/stack_bound.py's call graph.
                if(entry.size == 0U) { continue; }
                entryFrom_  = entry.from;
                entryBytes_ = entry.size;
                hashed_     = 0U;
                ++entry_;
                readAt_    = entryFrom_;
                readBytes_ = std::min(maxReadBytes, entryBytes_);
                return;
            }
            endHash(true);
        }

        constexpr void nextHashRead() {
            if(hashed_ == entryBytes_) {
                startEntry();
                return;
            }
            readAt_    = entryFrom_ + hashed_;
            readBytes_ = std::min(maxReadBytes, entryBytes_ - hashed_);
        }

        /// s_varm_crit_next_block until it needs a read (want()) or the loop ends; `afterRead`: the read asked for
        /// is in. Each `continue` is the ROM's.
        constexpr void run(bool afterRead = false) {
            while(outcome_ == Outcome::running) {
                if(!afterRead) {
                    if(pos_ == count_) {
                        pos_ = count_ = 0U;
                        bufStart_     = nextRead_;
                    }
                    if(bufStart_ + pos_ == firstWord()) {   // "looped back to first block"
                        outcome_ = Outcome::closed;
                        break;
                    }
                    auto const maxRead = windowWords_ - nextRead_;
                    if(maxRead != 0U && maxRead <= windowWords_) {
                        toRead_ = std::min({blockBufferWords - count_, maxRead, blockReadWords});
                        if(toRead_ > 0U) {   // want() asks for it, feed() comes back here
                            readAt_    = nextRead_ * 4U;
                            readBytes_ = toRead_ * 4U;
                            return;
                        }
                    } else if(count_ == 0U || rules_.rom == RomVersion::a2) {
                        // "attempt to read outside of window" - A2 even with words left in the buffer (A3 fixed it)
                        outcome_ = Outcome::invalid;
                        break;
                    }
                }
                afterRead = false;

                if(first_ < 0) {
                    while(pos_ < count_ && buffer_[pos_] != picobinBlockMarkerStart) { ++pos_; }
                    if(pos_ >= count_) {
                        if(nextRead_
                           >= firstMax_) {   // "first block scan exhausted with no marker found"
                            outcome_ = blocks_ != 0U ? Outcome::closed : Outcome::noBlock;
                            break;
                        }
                        continue;
                    }
                } else if(buffer_[pos_]
                          != picobinBlockMarkerStart) {   // "no block marker at next expected pos"
                    outcome_ = Outcome::invalid;
                    break;
                }
                if(pos_ != 0U) {   // move the block to the buffer's start, fill up behind it
                    for(std::uint32_t i = 0; i < count_ - pos_; ++i) {
                        buffer_[i] = buffer_[i + pos_];
                    }
                    count_ -= pos_;
                    bufStart_ += pos_;
                    pos_ = 0U;
                    continue;
                }
                // the item walk; its errors (unsupported_block_error) end the whole walk, first block found or not -
                // only a walk that runs off what was loaded lets the search go on at the next word
                std::uint32_t item = 1U;
                std::uint32_t last = 0U;
                bool          bad  = false;
                while(item < count_) {
                    auto const header = buffer_[item];
                    auto const size   = Detail::itemWords(header);
                    if(size == 0U) {   // "invalid zero sized item"
                        bad = true;
                        break;
                    }
                    if((header & 0xffU) == Detail::lastItemByte) {
                        // the end marker is read even past what was loaded (item + 2 may be >= count_)
                        if(item - 1U != size || buffer_[item + 2U] != picobinBlockMarkerEnd) {
                            bad = true;   // "size mismatch", "no end marker in range"
                        } else {
                            last = item;
                        }
                        break;
                    }
                    item += size;
                }
                if(bad) {
                    outcome_ = Outcome::invalid;
                    break;
                }
                if(last != 0U) {
                    auto const link = static_cast<std::int32_t>(buffer_[last + 1U]);
                    auto const next
                      = static_cast<std::uint32_t>(link / 4);   // C's division, toward zero
                    if(first_ < 0) {
                        first_ = bufStart_;
                    } else if(next == 0U) {   // "non-first block loops to itself"
                        outcome_ = Outcome::invalid;
                        break;
                    }
                    take(last, std::int64_t{bufStart_} * 4 + std::int64_t{link / 4} * 4);
                    if(outcome_ != Outcome::running) { break; }
                    if(next < count_) {
                        pos_ = next;
                    } else {
                        nextRead_ += next - count_;
                        if(
                          nextRead_
                          < firstWord()) {   // "the block link goes to lower address than first block"
                            outcome_ = Outcome::invalid;
                            break;
                        }
                        pos_ = count_;
                    }
                    continue;
                }
                if(first_ >= 0) {   // "(non first) block was not valid, done"
                    outcome_ = Outcome::invalid;
                    break;
                }
                ++pos_;
            }
            walked();
        }
    };

    /// The result of chooseAb: which of the pair boots and which first sector a buy (or, without TBYB, the launch)
    /// erases.
    struct AbChoice {
        unsigned chosen{};   ///< 0 = A (slot 0), 1 = B (slot 1)
        std::optional<unsigned>
             eraseOther;   ///< the version downgrade's victim, set on a flash update boot only
        bool decided{
          true};   ///< false: a block's verdict was `unchecked`, the choice assumed it verified
    };

    /// s_varm_crit_choose_by_tbyb_flash_update_boot_and_version: between A and B (partitions of an A/B pair, each
    /// Walker's imageDef(); or partition table slots 0 and 1, partitionTableBlock()), `flashUpdate` the one a
    /// FLASH_UPDATE boot names. A is chosen unless B is verified and preferred: the flash update's, the one with the
    /// greater version when that one is no unbought TBYB image, or the only one verified. A flash update of the
    /// partition with the lower version marks the other for erasing.
    [[nodiscard]] constexpr AbChoice chooseAb(std::optional<ParsedBlock> const& a,
                                              std::optional<ParsedBlock> const& b,
                                              std::optional<unsigned> flashUpdate = std::nullopt) {
        AbChoice out{};
        out.decided = (!a || a->decided()) && (!b || b->decided());
        if(!b) { return out; }
        bool const greaterB = !a || Detail::versionGreater(*b, *a);
        if(a && flashUpdate == 0U) {
            if(a->verified()) {
                if(greaterB) {
                    out.eraseOther = 1U;
                }   // trying A, B the newer: B goes when A is bought
                return out;
            }
        } else {
            bool useB = flashUpdate == 1U || (a && a->tbyb);
            if(!useB) {
                if(greaterB && !b->tbyb) {
                    useB = true;
                } else if(a) {
                    useB = !a->verified();
                }
            }
            if(!useB) { return out; }
        }
        // consider_y
        if(b->verified()) {
            out.chosen = 1U;
            if(flashUpdate == 1U && !greaterB) { out.eraseOther = 0U; }
        }
        return out;
    }

    /// The boot ROM's flash scan for the partition table (s_varm_crit_ram_trash_pick_boot_slot), a read at a time
    /// like Walker: slot 0's loop from flash byte 0; unless it decides alone - a verified singleton PARTITION_TABLE,
    /// or a verified IMAGE_DEF and no table - slot 1's from `slotSize` on, and chooseAb between their tables.
    class FlashWalker {
    public:
        /// `flashBytes`: the flash's size (the default leaves it to the first short feed); `rules`: ROM revision and
        /// OTP (slot size, SINGLE_FLASH_BINARY, ...); `flashUpdateOffset`: the flash offset a FLASH_UPDATE boot names,
        /// if one does (ignored with DISABLE_WATCHDOG_SCRATCH: the boot ROM never sees it).
        /// `hashImageDefs`: as Walker's `hashImageDef`, for both slots' loops - false for a reader that wants the
        /// partition table only. The choice between the slots is the same as long as slot 0 holds a PARTITION_TABLE;
        /// a slot 0 without one decides alone only with a verified IMAGE_DEF, which an unchecked one is not, so slot 1
        /// is searched then.
        constexpr explicit FlashWalker(std::uint32_t                flashBytes = 0xffff'fffcU,
                                       BootRules                    rules      = {},
                                       std::optional<std::uint32_t> flashUpdateOffset
                                       = std::nullopt,
                                       bool hashImageDefs = true)
          : rules_{rules}
          , flashUpdate_{rules.watchdogScratchDisabled ? std::nullopt : flashUpdateOffset}
          , hashImageDefs_{hashImageDefs}
          , slot0_{Window{.bytes = flashBytes},
                   rules,
                   !rules.singleFlashBinary,
                   hashImageDefs} {
            settle();
        }

        [[nodiscard]] constexpr std::optional<std::pair<std::uint32_t,
                                                        std::uint32_t>>
        want() const {
            return active().want();
        }

        constexpr void feed(std::span<std::byte const> bytes) {
            active().feed(bytes);
            settle();
        }

        [[nodiscard]] constexpr bool done() const { return state_ == State::done; }

        /// Once done: whether slot 1 was searched at all (slot 0 decides alone otherwise).
        [[nodiscard]] constexpr bool slot1Searched() const { return slot1Searched_; }

        /// Once done: the slot whose loop counts (0 or 1).
        [[nodiscard]] constexpr unsigned slot() const { return chosen_; }

        /// Once done: the loop of the chosen slot.
        [[nodiscard]] constexpr Walker const& loop() const {
            return chosen_ == 0U ? slot0_ : slot1_;
        }

        /// Once done: the partition table the boot ROM loads (verified, entries in order), nullopt for none.
        [[nodiscard]] constexpr std::optional<PartitionTable> partitionTable() const {
            return done() ? loop().partitionTable() : std::nullopt;
        }

        /// Once done: the IMAGE_DEF of the chosen loop - the flash image a board without a table boots.
        [[nodiscard]] constexpr std::optional<ParsedBlock> imageDef() const {
            return done() ? loop().imageDef() : std::nullopt;
        }

    private:
        enum class State : std::uint8_t { slot0, slot1, done };

        BootRules                    rules_;
        std::optional<std::uint32_t> flashUpdate_;
        bool                         hashImageDefs_;
        Walker                       slot0_;
        Walker                       slot1_{Window{.bytes = 0U}};   // searched once slot 0 is done
        bool                         slot1Searched_{};
        State                        state_{State::slot0};
        unsigned                     chosen_{};

        [[nodiscard]] constexpr Walker const& active() const {
            return state_ == State::slot1 ? slot1_ : slot0_;
        }

        [[nodiscard]] constexpr Walker& active() {
            return state_ == State::slot1 ? slot1_ : slot0_;
        }

        constexpr void settle() {
            if(state_ == State::slot0 && slot0_.done()) {
                auto const table = slot0_.partitionTableBlock();
                auto const image = slot0_.imageDef();
                bool const alone = !rules_.secure && flashUpdate_ != rules_.slotSize
                                && ((table && slot0_.partitionTableSingleton() && table->verified())
                                    || (!table && image && image->verified()));
                if(alone) {
                    state_ = State::done;
                    return;
                }
                slot1_ = Walker{
                  Window{.bytes = slot0_.window().bytes, .searchStart = rules_.slotSize},
                  rules_,
                  !rules_.singleFlashBinary,
                  hashImageDefs_
                };
                slot1Searched_ = true;
                state_         = State::slot1;
            }
            if(state_ == State::slot1 && slot1_.done()) {
                std::optional<unsigned> update;
                if(flashUpdate_ == 0U) { update = 0U; }
                if(flashUpdate_ == rules_.slotSize) { update = 1U; }
                chosen_
                  = chooseAb(slot0_.partitionTableBlock(), slot1_.partitionTableBlock(), update)
                      .chosen;
                state_ = State::done;
            }
        }
    };

    /// Walker over bytes held in memory, `bytes` from the window's first byte on; the window ends with them or at
    /// `window.bytes`, whichever comes first.
    [[nodiscard]] constexpr Walker walk(std::span<std::byte const> bytes,
                                        Window                     window,
                                        BootRules const&           rules           = {},
                                        bool                       partitionTables = true) {
        window.bytes
          = static_cast<std::uint32_t>(std::min<std::size_t>(window.bytes, bytes.size()));
        Walker walker{window, rules, partitionTables};
        while(auto const read = walker.want()) {
            auto const [at, length] = *read;
            auto const from         = std::min<std::size_t>(at, window.bytes);
            walker.feed(bytes.subspan(from, std::min<std::size_t>(length, window.bytes - from)));
        }
        return walker;
    }

    /// Walker over a window held in memory: offset 0 = its start (a partition at `xipAddress`, or flash byte 0).
    [[nodiscard]] constexpr Walker walk(std::span<std::byte const> window,
                                        std::uint32_t              xipAddress = xipBaseAddress) {
        return walk(window, Window{.xipAddress = xipAddress});
    }

    /// The IMAGE_DEF the boot ROM launches from `window` (Walker::imageDef()).
    [[nodiscard]] constexpr std::optional<ParsedBlock>
    launchedImageDef(std::span<std::byte const> window) {
        return walk(window).imageDef();
    }

    /// FlashWalker over the flash held in memory from byte 0 (`flash`: the whole flash, or at least up to the end of
    /// the slots' loops - a link past it ends that walk as a read outside the window).
    [[nodiscard]] constexpr FlashWalker walkFlash(std::span<std::byte const>   flash,
                                                  BootRules const&             rules = {},
                                                  std::optional<std::uint32_t> flashUpdateOffset
                                                  = std::nullopt) {
        FlashWalker walker{
          static_cast<std::uint32_t>(std::min<std::size_t>(flash.size(), 0xffff'fffcU)),
          rules,
          flashUpdateOffset};
        while(auto const read = walker.want()) {
            auto const [at, length] = *read;
            auto const from         = std::min<std::size_t>(at, flash.size());
            walker.feed(flash.subspan(from, std::min<std::size_t>(length, flash.size() - from)));
        }
        return walker;
    }

    /// The partition table the boot ROM loads from `flash` (FlashWalker::partitionTable()).
    [[nodiscard]] constexpr std::optional<PartitionTable>
    partitionTable(std::span<std::byte const> flash,
                   BootRules const&           rules = {}) {
        return walkFlash(flash, rules).partitionTable();
    }

    /// What a flash boot ends in (predictBoot()).
    struct BootPrediction {
        enum class Kind : std::uint8_t {
            nothing,   ///< no image boots: the boot ROM goes on to BOOTSEL
            flashImage,   ///< the IMAGE_DEF in a partition table slot's own loop (a board without a table)
            partition,           ///< the IMAGE_DEF of `partition`
            otherArchitecture,   ///< the chosen image is for RISC-V: the boot ROM reboots into it
        };

        Kind                       kind{Kind::nothing};
        unsigned                   slot{};   ///< the slot the scan chose (0, or 1 at the slot size)
        std::optional<unsigned>    partition;   ///< the table's index of the partition booted
        std::optional<ParsedBlock> image;       ///< the IMAGE_DEF launched
        std::optional<unsigned>
             eraseOther;      ///< the partition a version downgrade erases (launch or buy)
        bool decided{true};   ///< false: an `unchecked` block took part
    };

    namespace Detail {
        // PICOBIN_PARTITION_FLAGS (pico-sdk boot/picobin.h, 5.9.4.1): link type and value, ignored for Arm boots
        inline constexpr unsigned      ptLinkTypeShift     = 1U;
        inline constexpr std::uint32_t ptLinkTypeMask      = 0x3U;
        inline constexpr std::uint32_t ptLinkTypeA         = 1U;   // a B partition: linked to its A
        inline constexpr unsigned      ptLinkValueShift    = 3U;
        inline constexpr std::uint32_t ptLinkValueMask     = 0xfU;
        inline constexpr std::uint32_t ptIgnoredArmBoot    = 0x0000'0200U;
        inline constexpr std::uint32_t sectorsIn16MiB      = (16U * 1024U * 1024U) / 4096U;
        inline constexpr std::uint32_t imageTypeSecurityNs = 0x0010U;   // EXE_SECURITY = NS
        inline constexpr std::uint32_t imageTypeSecurityMsk = 0x0030U;

        [[nodiscard]] constexpr bool isB(PartitionEntry const& e) {
            return ((e.permissionsAndFlags >> ptLinkTypeShift) & ptLinkTypeMask) == ptLinkTypeA;
        }
    }   // namespace Detail

    /// The boot ROM's flash boot over `flash` held in memory (the whole flash; pico-bootrom varm_flash_boot.c
    /// s_varm_crit_ram_trash_perform_flash_scan_and_maybe_run_image, varm_launch_image.c
    /// s_varm_crit_ram_trash_verify_and_launch_flash_image), `flashUpdateOffset` the flash offset a FLASH_UPDATE
    /// reboot named:
    /// - the slot search (FlashWalker); a table that does not verify ends the boot;
    /// - an IMAGE_DEF in the chosen slot's own loop is the one tried - with a table beside it too - and the
    ///   partitions are not;
    /// - else each partition of the table in order, skipping one that ends past 16 MiB, one marked
    ///   IGNORED_DURING_ARM_BOOT and every B (it comes with its A: the first partition linked to it); chooseAb
    ///   between A's and B's IMAGE_DEF, then the launch's checks - not verified, TBYB but not the flash update, a
    ///   rolling window that is negative or no whole sector, an Arm Non-secure image - send it on to the next
    ///   partition. An image for RISC-V ends it (the boot ROM reboots into the other architecture, unless OTP
    ///   disables that: then it goes on).
    /// Families and permissions play no part, as in the boot ROM.
    [[nodiscard]] constexpr BootPrediction
    predictBoot(std::span<std::byte const>   flash,
                BootRules const&             rules             = {},
                std::optional<std::uint32_t> flashUpdateOffset = std::nullopt) {
        auto const     update = rules.watchdogScratchDisabled ? std::nullopt : flashUpdateOffset;
        auto const     scan   = walkFlash(flash, rules, update);
        BootPrediction out{};
        out.slot           = scan.slot();
        auto const& loop   = scan.loop();
        auto const  tableB = loop.partitionTableBlock();
        if(tableB && !tableB->verified()) {   // "PT populated && !verified": the boot fails
            out.decided = tableB->decided();
            return out;
        }
        // the launch's checks for an IMAGE_DEF found in a window starting at flash offset `start`
        auto const launchable = [&](ParsedBlock const& image, std::uint32_t start) {
            out.decided = out.decided && image.decided();
            if(!image.verified()) { return false; }
            if(update != start && image.tbyb) { return false; }   // TBYB, but not the flash update
            auto const delta = image.rollingWindowDelta;
            if(static_cast<std::int32_t>(delta) < 0 && (delta << 10U) != 0U) { return false; }
            if(((start + delta) & 0xfffU) != 0U) { return false; }   // the roll is whole sectors
            bool const arm
              = ((std::uint32_t{image.imageTypeFlags} & Detail::cpuMask) >> Detail::cpuShift)
             == static_cast<std::uint32_t>(ExeCpu::arm);
            if(arm
               && (image.imageTypeFlags & Detail::imageTypeSecurityMsk)
                    == Detail::imageTypeSecurityNs)
            {
                return false;   // an Arm Non-secure image is no boot image
            }
            return arm || !rules.autoSwitchArchDisabled;
        };
        auto const kindOf = [](ParsedBlock const& image) {
            bool const arm
              = ((std::uint32_t{image.imageTypeFlags} & Detail::cpuMask) >> Detail::cpuShift)
             == static_cast<std::uint32_t>(ExeCpu::arm);
            return arm ? BootPrediction::Kind::partition : BootPrediction::Kind::otherArchitecture;
        };
        if(auto const image = loop.imageDef()) {
            std::uint32_t const start = out.slot == 0U ? 0U : rules.slotSize;
            if(launchable(*image, start)) {
                out.kind  = kindOf(*image) == BootPrediction::Kind::partition
                            ? BootPrediction::Kind::flashImage
                            : kindOf(*image);
                out.image = image;
            }
            return out;
        }
        auto const table = loop.partitionTable();
        if(!table) { return out; }
        for(unsigned pi = 0; pi < table->count; ++pi) {
            auto const& a = table->partitions[pi];
            if(a.lastSector() >= Detail::sectorsIn16MiB
               || (a.permissionsAndFlags & Detail::ptIgnoredArmBoot) != 0U || Detail::isB(a))
            {
                continue;
            }
            std::optional<unsigned> bi;
            for(unsigned j = 0; j < table->count; ++j) {
                if(Detail::isB(table->partitions[j])
                   && ((table->partitions[j].permissionsAndFlags >> Detail::ptLinkValueShift)
                       & Detail::ptLinkValueMask)
                        == pi)
                {
                    bi = j;
                    break;
                }
            }
            auto const imageIn = [&](PartitionEntry const& e) -> std::optional<ParsedBlock> {
                std::uint32_t const start = e.firstSector() * 4096U;
                std::uint32_t const end   = (e.lastSector() + 1U) * 4096U;
                if(end <= start || start >= flash.size()) { return std::nullopt; }
                return walk(flash.subspan(start),
                            Window{.bytes = end - start, .xipAddress = xipBaseAddress + start},
                            rules,
                            false)
                  .imageDef();
            };
            // B's index; read only where B exists: chooseAb picks B only with an image in it
            unsigned const      ib     = bi.value_or(pi);
            auto const          imageA = imageIn(a);
            auto const          imageB = bi ? imageIn(table->partitions[ib]) : std::nullopt;
            std::uint32_t const startA = a.firstSector() * 4096U;
            std::uint32_t const startB
              = bi ? table->partitions[ib].firstSector() * 4096U : 0xffff'ffffU;
            std::optional<unsigned> which;
            if(update == startA) { which = 0U; }
            if(update == startB) { which = 1U; }
            auto const choice = chooseAb(imageA, imageB, which);
            out.decided       = out.decided && choice.decided;
            auto const& image = choice.chosen == 0U ? imageA : imageB;
            if(!image) { continue; }
            std::uint32_t const start = choice.chosen == 0U ? startA : startB;
            if(!launchable(*image, start)) { continue; }
            out.kind      = kindOf(*image);
            out.partition = choice.chosen == 0U ? pi : ib;
            out.image     = image;
            if(choice.eraseOther && update == start) {
                out.eraseOther = *choice.eraseOther == 0U ? pi : ib;
            }
            return out;
        }
        return out;
    }

    namespace Detail { namespace ReadBackTest {
        // FIPS 180-4's "abc"
        inline constexpr auto abc = [] {
            Sha256 sha{};
            sha.update(std::array{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}});
            return sha.finish();
        }();
        static_assert(abc[0] == std::byte{0xba} && abc[1] == std::byte{0x78}
                      && abc[31] == std::byte{0xad});

        // The writer's DS 5.9.5.1 minimum Arm IMAGE_DEF reads back as the executable IMAGE_DEF the boot ROM launches.
        inline constexpr auto minimumArm = asBytes(BlockLoop<Block<ArmSecure>>::words);
        static_assert(walk(minimumArm).outcome() == Walker::Outcome::closed);
        static_assert(walk(minimumArm).firstBlock() == std::optional<std::uint32_t>{0U});
        static_assert(launchedImageDef(minimumArm).value_or(ParsedBlock{}).exe);
        static_assert(launchedImageDef(minimumArm).value_or(ParsedBlock{}).verified());
        static_assert(launchedImageDef(minimumArm).value_or(ParsedBlock{}).hash
                      == ParsedBlock::Hash::none);
        static_assert(!launchedImageDef(minimumArm).value_or(ParsedBlock{}).tbyb);
        static_assert(!launchedImageDef(minimumArm).value_or(ParsedBlock{}).signerKeyHash);
        static_assert(!partitionTable(minimumArm).has_value());
        // its five words are all it takes up: no load map
        static_assert(walk(minimumArm).footprint()
                      == std::optional{
                        Footprint{.first   = 0U,
                                  .end     = 20U,
                                  .loadMap = false}
        });

        // TBYB and a VERSION item, in a two-block loop: the last executable IMAGE_DEF wins
        using Tbyb = ImageDef<ImageType::exe, ExeSecurity::s, ExeCpu::arm, ExeChip::rp2350, true>;
        inline constexpr auto twoBlocks
          = asBytes(BlockLoop<Block<ArmSecure>, Block<Tbyb, Version<2, 5>>>::words);
        static_assert(launchedImageDef(twoBlocks).value_or(ParsedBlock{}).tbyb);
        static_assert(launchedImageDef(twoBlocks)
                        .value_or(ParsedBlock{
        })
                        .version
                      == std::optional{ParsedBlock::Version{.major = 2,
                                                            .minor = 5}});
        static_assert(launchedImageDef(twoBlocks).value_or(ParsedBlock{}).offset == 20U);
        // the loop is both blocks, the launched one's included
        static_assert(walk(twoBlocks).footprint()
                      == std::optional{
                        Footprint{.first = 0U,
                                  .end   = static_cast<std::uint32_t>(twoBlocks.size())}
        });
    }}   // namespace Detail::ReadBackTest

}}   // namespace Kvasir::Picobin
