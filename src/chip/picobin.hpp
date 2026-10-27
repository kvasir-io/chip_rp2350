#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>
#include <utility>

namespace Kvasir { namespace Picobin {

    template<typename... Items>
    constexpr std::size_t countItemWords() {
        if constexpr(sizeof...(Items) == 0) {
            return 0;
        } else {
            return (Items::words.size() + ...);
        }
    }

    template<typename Item>
    constexpr std::span<std::uint32_t> writeTo(std::span<std::uint32_t> dest) {
        std::ranges::copy(Item::words, dest.begin());
        return dest.subspan(Item::words.size());
    }

    inline constexpr std::uint32_t picobinBlockMarkerStart = 0xffffded3U;
    inline constexpr std::uint32_t picobinBlockMarkerEnd   = 0xab123579U;

    inline constexpr std::uint32_t picobinMaxBlockSize               = 0x280U;
    inline constexpr std::uint32_t picobinMaxImageDefBlockSize       = 0x180U;
    inline constexpr std::uint32_t picobinMaxPartitionTableBlockSize = 0x280U;

    enum class ItemType : std::uint8_t {
        vectorTable     = 0x03,
        rollingWindow   = 0x05,
        loadMap         = 0x06,
        signature       = 0x09,
        partitionTable  = 0x0a,
        salt            = 0x0c,
        nextBlockOffset = 0x41,
        imageType       = 0x42,
        entryPoint      = 0x44,
        hashDef         = 0x47,
        version         = 0x48,
        hashValue       = 0x4b,
        ignored         = 0x7e,
        last            = 0x7f
    };

    template<ItemType Type, std::uint8_t Size, bool SizeFlag = false, std::uint16_t ExtraData = 0>
    struct ItemHeader {
        // Size is the 8-bit size field of the item header; every item here is far below 255 words (a block is at
        // most 160, DS 5.9.1).
        static_assert(Size > 0 || SizeFlag,
                      "Size must be greater than 0 unless SizeFlag is set");

        static constexpr std::uint32_t value
          = (static_cast<std::uint32_t>(Type) << 0) | (SizeFlag ? (1U << 7) : 0U)
          | (static_cast<std::uint32_t>(Size) << 8) | (static_cast<std::uint32_t>(ExtraData) << 16);
    };

    // RP2350 DS 5.9.1: "size of blocks is limited to 640 bytes for PARTITION_TABLEs and 384 bytes for IMAGE_DEFs.
    // Blocks larger than this are ignored", and a block is an IMAGE_DEF or a PARTITION_TABLE by its first item. The
    // three picobinMax* limits above are BYTES (PICOBIN_MAX_* in pico-sdk's boot/picobin.h); every size in this
    // header is counted in words and converted here.
    constexpr std::size_t bytesOf(std::size_t words) { return words * 4U; }

    template<typename FirstItem>
    constexpr std::uint32_t maxBlockBytes() {
        constexpr auto type
          = FirstItem::words[0] & 0x7fU;   // item_type:7 (DS 5.9.1 block format table)
        if constexpr(type == static_cast<std::uint32_t>(ItemType::imageType)) {
            return picobinMaxImageDefBlockSize;
        } else if constexpr(type == static_cast<std::uint32_t>(ItemType::partitionTable)) {
            return picobinMaxPartitionTableBlockSize;
        } else {
            return picobinMaxBlockSize;
        }
    }

    // The whole block in words: HEADER marker + items + LAST item + LINK + FOOTER marker (DS 5.9.1).
    template<typename... Items>
    constexpr std::size_t blockWords() {
        return countItemWords<Items...>() + 1 + 1 + 2;
    }

    // A predicate, so a too-big block can be asserted (!blockFits) without instantiating its words.
    template<typename First,
             typename... Rest>
    constexpr bool blockFits() {
        return bytesOf(blockWords<First, Rest...>()) <= maxBlockBytes<First>();
    }

    enum class PartitionPermission : std::uint32_t {
        sR      = 0x04000000U,
        sW      = 0x08000000U,
        nsR     = 0x10000000U,
        nsW     = 0x20000000U,
        nsbootR = 0x40000000U,
        nsbootW = 0x80000000U
    };

    // The location word of a partition entry, the inverse of permissions_and_location():
    // the first and last sector (4 KB units) in two 13-bit fields below the permissions.
    constexpr std::uint32_t firstSector(std::uint32_t locationWord) {
        return locationWord & 0x1FFFU;
    }

    constexpr std::uint32_t lastSector(std::uint32_t locationWord) {
        return (locationWord >> 13) & 0x1FFFU;
    }

    namespace PartitionFlags {
        constexpr std::uint32_t hasId = 0x00000001U;
    }   // namespace PartitionFlags

    enum class ImageType : std::uint32_t { invalid = 0, exe = 1, data = 2 };

    enum class ExeSecurity : std::uint32_t { unspecified = 0, ns = 1, s = 2 };

    enum class ExeCpu : std::uint32_t { arm = 0, riscv = 1 };

    enum class ExeChip : std::uint32_t { rp2040 = 0, rp2350 = 1 };

    enum class HashAlgorithm : std::uint16_t { sha256 = 0x0001 };

    template<ImageType   Type,
             ExeSecurity Security = ExeSecurity::unspecified,
             ExeCpu      Cpu      = ExeCpu::arm,
             ExeChip     Chip     = ExeChip::rp2350,
             bool        TBYB     = false>
    struct ImageDef {
        static_assert(Type != ImageType::invalid,
                      "ImageType cannot be INVALID");
        static_assert(Type == ImageType::exe || Security == ExeSecurity::unspecified,
                      "Security only applies to EXE images");

    private:
        static constexpr std::uint16_t flags
          = (static_cast<std::uint16_t>(Type) << 0) | (static_cast<std::uint16_t>(Security) << 4)
          | (static_cast<std::uint16_t>(Cpu) << 8) | (static_cast<std::uint16_t>(Chip) << 12)
          | (TBYB ? (1U << 15) : 0U);

    public:
        static constexpr std::array<std::uint32_t, 1> words
          = {ItemHeader<ItemType::imageType, 1, false, flags>::value};
    };

    using ArmSecure = ImageDef<ImageType::exe, ExeSecurity::s, ExeCpu::arm, ExeChip::rp2350>;
    using DataImage
      = ImageDef<ImageType::data, ExeSecurity::unspecified, ExeCpu::arm, ExeChip::rp2350>;

    template<std::uint16_t Major, std::uint16_t Minor>
    struct Version {
        static constexpr std::array<std::uint32_t, 2> words
          = {ItemHeader<ItemType::version, 2>::value,
             (static_cast<std::uint32_t>(Major) << 16) | Minor};
    };

    template<std::uint32_t BlockWordsHashed, HashAlgorithm Algorithm = HashAlgorithm::sha256>
    struct HashDef {
        static_assert(BlockWordsHashed > 0,
                      "BlockWordsHashed must be greater than 0");
        // a word count (DS 5.9.2.2: "Number of words of block hashed"), the limit is bytes
        static_assert(bytesOf(BlockWordsHashed) <= picobinMaxBlockSize,
                      "BlockWordsHashed cannot exceed maximum block size");

        // RP2350 DS 5.9.2.2: byte 0 type 0x47, byte 1 size, byte 2 pad, byte 3 the hash type; word 1 the number of
        // words hashed (16 bits) and 16 bits of pad. Two words, size 2: the table lists words 0 and 1 only (its size_lo
        // byte says 0x03), picotool's hash_def_item writes size 2, and the boot ROM ignores a HASH_DEF whose size is
        // not 2.
        static constexpr std::array<std::uint32_t, 2> words
          = {ItemHeader<ItemType::hashDef,
                        2,
                        false,
                        static_cast<std::uint16_t>(static_cast<std::uint16_t>(Algorithm)
                                                   << 8U)>::value,
             BlockWordsHashed};
    };

    template<std::size_t Size>
    struct LastItem {
        static constexpr std::array<std::uint32_t, 1> words
          = {ItemHeader<ItemType::last, Size, true>::value};
    };

    template<std::int32_t Offset>
    struct Link {
        static constexpr std::array<std::uint32_t, 1> words = {static_cast<std::uint32_t>(Offset)};
    };

    template<typename... Items>
    struct Block {
        static_assert(sizeof...(Items) > 0,
                      "Block must contain at least one item");

        template<typename Link>
        struct BlockImpl {
            template<typename... AllItems>
            static constexpr auto getBlock() {
                static constexpr std::size_t arraySize = countItemWords<AllItems...>() + 2;
                // AllItems = Items..., LastItem, Link: ArraySize is the whole block
                static_assert(arraySize == blockWords<Items...>());
                static_assert(blockFits<Items...>(),
                              "Block exceeds the boot ROM's limit (RP2350 DS 5.9.1: 384 bytes for "
                              "an IMAGE_DEF, 640 "
                              "for a PARTITION_TABLE or any other block)");

                std::array<std::uint32_t, arraySize> arr{};
                std::span<std::uint32_t>             remaining{arr};
                remaining[0] = picobinBlockMarkerStart;
                remaining    = remaining.subspan(1);

                (void)((remaining = writeTo<AllItems>(remaining)), ...);

                remaining[0] = picobinBlockMarkerEnd;

                return arr;
            }

            static constexpr auto words
              = getBlock<Items..., LastItem<countItemWords<Items...>()>, Link>();
        };

        template<typename Link>
        using LinkedBlock = BlockImpl<Link>;
    };

    template<typename... Blocks>
    struct BlockLoop {
        static_assert(sizeof...(Blocks) > 0,
                      "BlockLoop must contain at least one block");

    private:
        template<std::size_t N>
        static constexpr std::size_t getBlockSize() {
            using Block = std::tuple_element_t<N, std::tuple<Blocks...>>;
            return Block::template LinkedBlock<Link<0>>::words.size();
        }

        template<std::size_t... Is>
        static constexpr std::size_t getTotalSize(std::index_sequence<Is...>) {
            return (getBlockSize<Is>() + ...);
        }

        static constexpr std::size_t totalSize = getTotalSize(std::index_sequence_for<Blocks...>{});

        template<std::size_t N>
        using LinkedBlockAt =
          typename std::tuple_element_t<N, std::tuple<Blocks...>>::template LinkedBlock<
            Link<(sizeof...(Blocks) == 1) ? 0
                 : (N == sizeof...(Blocks) - 1)
                   ? -(static_cast<std::int32_t>(totalSize - getBlockSize<N>()) * 4)
                   : static_cast<std::int32_t>(getBlockSize<N>() * 4)>>;

        template<std::size_t... Is>
        static constexpr auto getBlockLoopImpl(std::index_sequence<Is...>) {
            constexpr std::size_t arraySize = (LinkedBlockAt<Is>::words.size() + ...);

            std::array<std::uint32_t, arraySize> arr{};
            std::span<std::uint32_t>             remaining{arr};

            ((remaining = writeTo<LinkedBlockAt<Is>>(remaining)), ...);

            return arr;
        }

        static constexpr auto getBlockLoop() {
            return getBlockLoopImpl(std::index_sequence_for<Blocks...>{});
        }

    public:
        static constexpr auto words = getBlockLoop();
    };

    // RP2350 DS 5.9.5.1 "Minimum Arm IMAGE_DEF", word for word: the block every image carries (StartUp.hpp ImageDef).
    static_assert(BlockLoop<Block<ArmSecure>>::words
                  == std::array<std::uint32_t,
                                5>{0xffffded3U,
                                   0x10210142U,
                                   0x000001ffU,
                                   0x00000000U,
                                   0xab123579U});
    static_assert(maxBlockBytes<ArmSecure>() == 384U);   // DS 5.9.1

    namespace Detail { namespace LimitTest {
        // first word 0: item type 0, neither an IMAGE_DEF nor a PARTITION_TABLE
        template<std::size_t N>
        struct Filler {
            static constexpr std::array<std::uint32_t, N> words{};
        };

        // IMAGE_DEF: 1 + 91 + 4 = 96 words = 384 bytes fits, 97 words do not
        static_assert(blockFits<ArmSecure,
                                Filler<91>>());
        static_assert(!blockFits<ArmSecure,
                                 Filler<92>>());
        // any other first item: 640 bytes = 160 words
        static_assert(blockFits<Version<1,
                                        0>,
                                Filler<154>>());
        static_assert(!blockFits<Version<1,
                                         0>,
                                 Filler<155>>());
    }}   // namespace Detail::LimitTest

}}   // namespace Kvasir::Picobin
