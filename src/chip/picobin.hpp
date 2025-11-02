#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_constant/string_constant.hpp>

namespace Kvasir { namespace Picobin {

    template<typename... Items>
    constexpr std::size_t count_item_words() {
        if constexpr(sizeof...(Items) == 0) {
            return 0;
        } else {
            return (Items::words.size() + ...);
        }
    }

    template<typename Item>
    constexpr std::span<std::uint32_t> write_to(std::span<std::uint32_t> dest) {
        std::ranges::copy(Item::words, dest.begin());
        return dest.subspan(Item::words.size());
    }

    inline constexpr std::uint32_t PICOBIN_BLOCK_MARKER_START = 0xffffded3u;
    inline constexpr std::uint32_t PICOBIN_BLOCK_MARKER_END   = 0xab123579u;

    inline constexpr std::uint32_t PICOBIN_MAX_BLOCK_SIZE                 = 0x280u;
    inline constexpr std::uint32_t PICOBIN_MAX_IMAGE_DEF_BLOCK_SIZE       = 0x180u;
    inline constexpr std::uint32_t PICOBIN_MAX_PARTITION_TABLE_BLOCK_SIZE = 0x280u;

    enum class ItemType : std::uint8_t {
        VectorTable     = 0x03,
        RollingWindow   = 0x05,
        LoadMap         = 0x06,
        Signature       = 0x09,
        PartitionTable  = 0x0a,
        Salt            = 0x0c,
        NextBlockOffset = 0x41,
        ImageType       = 0x42,
        EntryPoint      = 0x44,
        HashDef         = 0x47,
        Version         = 0x48,
        HashValue       = 0x4b,
        Ignored         = 0x7e,
        Last            = 0x7f
    };

    template<ItemType Type, std::uint8_t Size, bool SizeFlag = false, std::uint16_t ExtraData = 0>
    struct ItemHeader {
        static_assert(Size > 0 || SizeFlag,
                      "Size must be greater than 0 unless SizeFlag is set");
        static_assert(Size <= 255,
                      "Size must fit in 8 bits");

        static constexpr std::uint32_t value
          = (static_cast<std::uint32_t>(Type) << 0) | (SizeFlag ? (1u << 7) : 0u)
          | (static_cast<std::uint32_t>(Size) << 8) | (static_cast<std::uint32_t>(ExtraData) << 16);
    };

    enum class PartitionPermission : std::uint32_t {
        S_R      = 0x04000000u,
        S_W      = 0x08000000u,
        NS_R     = 0x10000000u,
        NS_W     = 0x20000000u,
        NSBOOT_R = 0x40000000u,
        NSBOOT_W = 0x80000000u
    };

    constexpr std::uint32_t operator|(PartitionPermission a,
                                      PartitionPermission b) {
        return static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b);
    }

    constexpr std::uint32_t operator|(std::uint32_t       a,
                                      PartitionPermission b) {
        return a | static_cast<std::uint32_t>(b);
    }

    namespace Permissions {
        constexpr std::uint32_t S_RO  = static_cast<std::uint32_t>(PartitionPermission::S_R);
        constexpr std::uint32_t NS_RO = static_cast<std::uint32_t>(PartitionPermission::NS_R);
        constexpr std::uint32_t NSBOOT_RO
          = static_cast<std::uint32_t>(PartitionPermission::NSBOOT_R);

        constexpr std::uint32_t S_RW
          = static_cast<std::uint32_t>(PartitionPermission::S_R | PartitionPermission::S_W);
        constexpr std::uint32_t NS_RW
          = static_cast<std::uint32_t>(PartitionPermission::NS_R | PartitionPermission::NS_W);
        constexpr std::uint32_t NSBOOT_RW = static_cast<std::uint32_t>(
          PartitionPermission::NSBOOT_R | PartitionPermission::NSBOOT_W);

        constexpr std::uint32_t ALL_RO         = S_RO | NS_RO | NSBOOT_RO;
        constexpr std::uint32_t ALL_RW         = S_RW | NS_RW | NSBOOT_RW;
        constexpr std::uint32_t SECURE_ONLY_RW = S_RW;
        constexpr std::uint32_t NONSECURE_RW   = NS_RW | NSBOOT_RW;

        constexpr std::uint32_t NONE = 0;
    }   // namespace Permissions

    namespace PartitionFlags {
        constexpr std::uint32_t HAS_ID   = 0x00000001u;
        constexpr std::uint32_t HAS_NAME = 0x00001000u;

        constexpr std::uint32_t LINK_TYPE_BITS            = 0x00000006u;
        constexpr std::uint32_t LINK_TYPE_NONE            = 0x00000000u;
        constexpr std::uint32_t LINK_TYPE_A_PARTITION     = 0x00000002u;
        constexpr std::uint32_t LINK_TYPE_OWNER_PARTITION = 0x00000004u;

        constexpr std::uint32_t LINK_VALUE_BITS = 0x00000078u;

        constexpr std::uint32_t IGNORED_DURING_ARM_BOOT   = 0x00000200u;
        constexpr std::uint32_t IGNORED_DURING_RISCV_BOOT = 0x00000400u;

        constexpr std::uint32_t UF2_DOWNLOAD_AB_NON_BOOTABLE = 0x00000800u;
        constexpr std::uint32_t UF2_DOWNLOAD_NO_REBOOT       = 0x00002000u;

        constexpr std::uint32_t ACCEPTS_DEFAULT_FAMILY_RP2040        = 0x00004000u;
        constexpr std::uint32_t ACCEPTS_DEFAULT_FAMILY_ABSOLUTE      = 0x00008000u;
        constexpr std::uint32_t ACCEPTS_DEFAULT_FAMILY_DATA          = 0x00010000u;
        constexpr std::uint32_t ACCEPTS_DEFAULT_FAMILY_RP2350_ARM_S  = 0x00020000u;
        constexpr std::uint32_t ACCEPTS_DEFAULT_FAMILY_RP2350_RISCV  = 0x00040000u;
        constexpr std::uint32_t ACCEPTS_DEFAULT_FAMILY_RP2350_ARM_NS = 0x00080000u;

        constexpr std::uint32_t EXTRA_FAMILY_IDS = 0x00000180u;
    }   // namespace PartitionFlags

    enum class ImageType : std::uint32_t { INVALID = 0, EXE = 1, DATA = 2 };

    enum class ExeSecurity : std::uint32_t { UNSPECIFIED = 0, NS = 1, S = 2 };

    enum class ExeCpu : std::uint32_t { ARM = 0, RISCV = 1 };

    enum class ExeChip : std::uint32_t { RP2040 = 0, RP2350 = 1 };

    enum class HashAlgorithm : std::uint16_t { SHA256 = 0x0001 };

    enum class SignatureAlgorithm : std::uint16_t { SECP256K1 = 0x0001 };

    template<ImageType   Type,
             ExeSecurity Security = ExeSecurity::UNSPECIFIED,
             ExeCpu      Cpu      = ExeCpu::ARM,
             ExeChip     Chip     = ExeChip::RP2350,
             bool        TBYB     = false>
    struct ImageDef {
        static_assert(Type != ImageType::INVALID,
                      "ImageType cannot be INVALID");
        static_assert(Type == ImageType::EXE || Security == ExeSecurity::UNSPECIFIED,
                      "Security only applies to EXE images");

    private:
        static constexpr std::uint16_t flags
          = (static_cast<std::uint16_t>(Type) << 0) | (static_cast<std::uint16_t>(Security) << 4)
          | (static_cast<std::uint16_t>(Cpu) << 8) | (static_cast<std::uint16_t>(Chip) << 12)
          | (TBYB ? (1u << 15) : 0u);

    public:
        static constexpr std::array<std::uint32_t, 1> words
          = {ItemHeader<ItemType::ImageType, 1, false, flags>::value};
    };

    using ArmSecure    = ImageDef<ImageType::EXE, ExeSecurity::S, ExeCpu::ARM, ExeChip::RP2350>;
    using ArmNonSecure = ImageDef<ImageType::EXE, ExeSecurity::NS, ExeCpu::ARM, ExeChip::RP2350>;
    using RiscvSecure  = ImageDef<ImageType::EXE, ExeSecurity::S, ExeCpu::RISCV, ExeChip::RP2350>;
    using RiscvNonSecure
      = ImageDef<ImageType::EXE, ExeSecurity::NS, ExeCpu::RISCV, ExeChip::RP2350>;
    using DataImage
      = ImageDef<ImageType::DATA, ExeSecurity::UNSPECIFIED, ExeCpu::ARM, ExeChip::RP2350>;

    template<std::uint16_t Major, std::uint16_t Minor>
    struct Version {
        static constexpr std::array<std::uint32_t, 2> words
          = {ItemHeader<ItemType::Version, 2>::value,
             (static_cast<std::uint32_t>(Major) << 16) | Minor};
    };

    template<std::uint32_t StorageOffset, std::uint32_t RuntimeAddr, std::uint32_t Size>
    struct LoadMapEntryRelative {
        static constexpr std::array<std::uint32_t, 3> words = {StorageOffset, RuntimeAddr, Size};
    };

    template<std::uint32_t StorageAddr, std::uint32_t RuntimeAddr, std::uint32_t StorageEndAddr>
    struct LoadMapEntryAbsolute {
        static constexpr std::array<std::uint32_t, 3> words
          = {StorageAddr, RuntimeAddr, StorageEndAddr};
    };

    template<bool Absolute, typename... Entries>
    struct LoadMap {
        static_assert(sizeof...(Entries) > 0,
                      "LoadMap must contain at least one entry");
        static_assert(sizeof...(Entries) <= 127,
                      "LoadMap can contain at most 127 entries (7 bits)");

    private:
        static constexpr std::size_t   num_entries = sizeof...(Entries);
        static constexpr std::size_t   total_size  = 1 + (num_entries * 3);
        static constexpr std::uint16_t flags
          = (Absolute ? 0x8000 : 0x0000) | ((num_entries & 0x7F) << 8);

        static constexpr auto build_words() {
            std::array<std::uint32_t, total_size> arr{};
            std::span<std::uint32_t>              remaining{arr};
            remaining[0] = ItemHeader<ItemType::LoadMap, total_size, false, flags>::value;
            remaining    = remaining.subspan(1);
            (void)((remaining = write_to<Entries>(remaining)), ...);
            return arr;
        }

    public:
        static constexpr auto words = build_words();
    };

    template<std::uint32_t StorageOffset, std::uint32_t RuntimeAddr, std::uint32_t Size>
    using LoadMapRelative = LoadMap<false, LoadMapEntryRelative<StorageOffset, RuntimeAddr, Size>>;

    template<std::uint32_t StorageAddr, std::uint32_t RuntimeAddr, std::uint32_t StorageEndAddr>
    using LoadMapAbsolute
      = LoadMap<true, LoadMapEntryAbsolute<StorageAddr, RuntimeAddr, StorageEndAddr>>;

    template<std::uint32_t PC, std::uint32_t SP>
    struct EntryPoint {
        static_assert((PC & 0x1) == 0,
                      "PC must be 2-byte aligned (even address)");
        static_assert((SP & 0x3) == 0,
                      "SP must be 4-byte aligned");

        static constexpr std::array<std::uint32_t, 3> words
          = {ItemHeader<ItemType::EntryPoint, 3>::value, PC, SP};
    };

    template<std::uint32_t PC, std::uint32_t SP, std::uint32_t SPLimit>
    struct EntryPointWithLimit {
        static_assert((PC & 0x1) == 0,
                      "PC must be 2-byte aligned (even address)");
        static_assert((SP & 0x3) == 0,
                      "SP must be 4-byte aligned");
        static_assert((SPLimit & 0x3) == 0,
                      "SPLimit must be 4-byte aligned");
        static_assert(SPLimit <= SP,
                      "SPLimit must be less than or equal to SP");

        static constexpr std::array<std::uint32_t, 4> words
          = {ItemHeader<ItemType::EntryPoint, 4>::value, PC, SP, SPLimit};
    };

    template<std::int32_t Delta>
    struct RollingWindowDelta {
        static constexpr std::array<std::uint32_t, 2> words
          = {ItemHeader<ItemType::RollingWindow, 2>::value, static_cast<std::uint32_t>(Delta)};
    };

    template<std::uint32_t VectorTableAddr>
    struct VectorTable {
        static_assert((VectorTableAddr & 0x7F) == 0,
                      "VectorTable address must be 128-byte aligned (ARM requirement)");

        static constexpr std::array<std::uint32_t, 2> words
          = {ItemHeader<ItemType::VectorTable, 2>::value, VectorTableAddr};
    };

    template<std::uint32_t BlockWordsHashed, HashAlgorithm Algorithm = HashAlgorithm::SHA256>
    struct HashDef {
        static_assert(BlockWordsHashed > 0,
                      "BlockWordsHashed must be greater than 0");
        static_assert(BlockWordsHashed <= PICOBIN_MAX_BLOCK_SIZE,
                      "BlockWordsHashed cannot exceed maximum block size");

        static constexpr std::array<std::uint32_t, 3> words
          = {ItemHeader<ItemType::HashDef, 3, false, static_cast<std::uint16_t>(Algorithm)>::value,
             BlockWordsHashed,
             0x00000000u};
    };

    template<SignatureAlgorithm Algorithm = SignatureAlgorithm::SECP256K1,
             std::uint32_t... SigWords>
    struct Signature {
        static_assert(sizeof...(SigWords) == 32,
                      "Signature must contain exactly 32 words (16 pubkey + 16 sig)");
        static constexpr std::array<std::uint32_t, 33> words = {
          ItemHeader<ItemType::Signature, 33, false, static_cast<std::uint16_t>(Algorithm)>::value,
          SigWords...};
    };

    template<std::uint32_t... HashWords>
    struct HashValue {
        static_assert(sizeof...(HashWords) >= 1 && sizeof...(HashWords) <= 8,
                      "Hash must contain 1-8 words");
        static constexpr std::array<std::uint32_t, 1 + sizeof...(HashWords)> words
          = {ItemHeader<ItemType::HashValue, 1 + sizeof...(HashWords)>::value, HashWords...};
    };

    template<std::uint32_t... SaltWords>
    struct Salt {
        static_assert(sizeof...(SaltWords) == 8,
                      "Salt must contain exactly 8 words (32 bytes)");
        static constexpr std::array<std::uint32_t, 9> words
          = {ItemHeader<ItemType::Salt, 9>::value, SaltWords...};
    };

    template<std::int32_t Offset>
    struct NextBlockOffset {
        static constexpr std::array<std::uint32_t, 2> words
          = {ItemHeader<ItemType::NextBlockOffset, 2>::value, static_cast<std::uint32_t>(Offset)};
    };

    template<std::uint32_t LinkType, std::uint8_t LinkPartitionNumber>
    struct PartitionLink {
        static_assert(LinkPartitionNumber < 16,
                      "Link partition number must be 0-15 (4 bits)");
        static_assert(LinkType == PartitionFlags::LINK_TYPE_NONE
                        || LinkType == PartitionFlags::LINK_TYPE_A_PARTITION
                        || LinkType == PartitionFlags::LINK_TYPE_OWNER_PARTITION,
                      "Invalid link type");
        static constexpr std::uint32_t value = LinkType | ((LinkPartitionNumber & 0xF) << 3);
    };

    template<std::uint16_t FirstSector,
             std::uint16_t LastSector,
             std::uint32_t Permissions,
             std::uint32_t Flags,
             typename Name,
             std::uint64_t ID = 0,
             std::uint32_t... FamilyIDs>
    struct PartitionEntry {
        static_assert(FirstSector <= LastSector,
                      "FirstSector must be <= LastSector");
        static_assert(FirstSector <= 0x1FFF,
                      "FirstSector must fit in 13 bits (max 8191)");
        static_assert(LastSector <= 0x1FFF,
                      "LastSector must fit in 13 bits (max 8191)");

    private:
        static constexpr bool        has_id         = (Flags & PartitionFlags::HAS_ID) != 0;
        static constexpr bool        has_name       = (Flags & PartitionFlags::HAS_NAME) != 0;
        static constexpr std::size_t num_family_ids = sizeof...(FamilyIDs);

        static constexpr std::uint32_t actual_flags
          = (Flags & ~PartitionFlags::EXTRA_FAMILY_IDS) | ((num_family_ids & 0x3) << 7);

        static constexpr std::size_t base_size      = 2;
        static constexpr std::size_t id_size        = has_id ? 2 : 0;
        static constexpr std::size_t family_id_size = num_family_ids;

        static_assert(num_family_ids == 0 || (Flags & PartitionFlags::EXTRA_FAMILY_IDS) != 0,
                      "If FamilyIDs are provided, EXTRA_FAMILY_IDS flag must be set");
        static_assert((Flags & PartitionFlags::EXTRA_FAMILY_IDS) == 0 || num_family_ids > 0,
                      "If EXTRA_FAMILY_IDS flag is set, FamilyIDs must be provided");
        static_assert(num_family_ids <= 3,
                      "Partition can have at most 3 extra family IDs");

        static constexpr std::size_t name_bytes      = has_name ? Name::stringView.size() : 0;
        static constexpr std::size_t name_size_words = has_name ? ((1 + name_bytes + 3) / 4) : 0;

        static constexpr std::uint32_t permissions_and_location() {
            return Permissions | (static_cast<std::uint32_t>(FirstSector) & 0x1FFF)
                 | ((static_cast<std::uint32_t>(LastSector) & 0x1FFF) << 13);
        }

    public:
        static constexpr std::size_t size = base_size + id_size + family_id_size + name_size_words;

        static constexpr auto build_words() {
            std::array<std::uint32_t, size> arr{};
            std::span<std::uint32_t>        remaining{arr};
            remaining[0] = permissions_and_location();
            remaining    = remaining.subspan(1);
            remaining[0] = Permissions | actual_flags;
            remaining    = remaining.subspan(1);

            if constexpr(has_id) {
                remaining[0] = static_cast<std::uint32_t>(ID & 0xFFFFFFFFu);
                remaining[1] = static_cast<std::uint32_t>(ID >> 32);
                remaining    = remaining.subspan(2);
            }

            if constexpr(num_family_ids > 0) {
                std::size_t offset = 0;
                ((remaining[offset++] = FamilyIDs), ...);
                remaining = remaining.subspan(num_family_ids);
            }

            if constexpr(has_name) {
                std::array<std::uint8_t, name_size_words * 4> name_buffer{};
                name_buffer[0] = static_cast<std::uint8_t>(name_bytes);

                for(std::size_t i = 0; i < name_bytes; ++i) {
                    name_buffer[1 + i] = static_cast<std::uint8_t>(Name::stringView[i]);
                }

                for(std::size_t i = 0; i < name_size_words; ++i) {
                    remaining[i] = static_cast<std::uint32_t>(name_buffer[i * 4 + 0])
                                 | (static_cast<std::uint32_t>(name_buffer[i * 4 + 1]) << 8)
                                 | (static_cast<std::uint32_t>(name_buffer[i * 4 + 2]) << 16)
                                 | (static_cast<std::uint32_t>(name_buffer[i * 4 + 3]) << 24);
                }
            }

            return arr;
        }

        static constexpr auto words = build_words();
    };

    template<bool          Singleton,
             std::uint32_t UnpartitionedPermissions,
             std::uint32_t UnpartitionedFlags,
             typename... Partitions>
    struct PartitionTable {
        static_assert(sizeof...(Partitions) > 0,
                      "PartitionTable must contain at least one partition");
        static_assert(sizeof...(Partitions) <= 15,
                      "PartitionTable can contain at most 15 partitions (4 bits)");

    private:
        static constexpr std::size_t   num_partitions = sizeof...(Partitions);
        static constexpr std::uint16_t header_flags
          = ((Singleton ? 0x80 : 0x00) | (num_partitions & 0x0F)) << 8;

        static constexpr std::size_t total_size = 1 + 1 + (Partitions::size + ...);

        static_assert(total_size <= PICOBIN_MAX_PARTITION_TABLE_BLOCK_SIZE,
                      "PartitionTable size exceeds maximum allowed");

        static constexpr auto build_words() {
            std::array<std::uint32_t, total_size> arr{};
            std::span<std::uint32_t>              remaining{arr};
            remaining[0]
              = ItemHeader<ItemType::PartitionTable, total_size, false, header_flags>::value;
            remaining    = remaining.subspan(1);
            remaining[0] = UnpartitionedPermissions | UnpartitionedFlags;
            remaining    = remaining.subspan(1);

            (void)((remaining = write_to<Partitions>(remaining)), ...);

            return arr;
        }

    public:
        static constexpr auto words = build_words();
    };

    template<std::size_t Size>
    struct LastItem {
        static constexpr std::array<std::uint32_t, 1> words
          = {ItemHeader<ItemType::Last, Size, true>::value};
    };

    struct Ignored {
        static constexpr std::array<std::uint32_t, 1> words
          = {ItemHeader<ItemType::Ignored, 0, true>::value};
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
                static constexpr std::size_t ArraySize = count_item_words<AllItems...>() + 2;
                static_assert(ArraySize <= PICOBIN_MAX_BLOCK_SIZE,
                              "Block size exceeds maximum allowed");

                std::array<std::uint32_t, ArraySize> arr{};
                std::span<std::uint32_t>             remaining{arr};
                remaining[0] = PICOBIN_BLOCK_MARKER_START;
                remaining    = remaining.subspan(1);

                (void)((remaining = write_to<AllItems>(remaining)), ...);

                remaining[0] = PICOBIN_BLOCK_MARKER_END;

                return arr;
            }

            static constexpr auto words
              = getBlock<Items..., LastItem<count_item_words<Items...>()>, Link>();
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

        static constexpr std::size_t TotalSize = getTotalSize(std::index_sequence_for<Blocks...>{});

        template<std::size_t N>
        using LinkedBlockAt =
          typename std::tuple_element_t<N, std::tuple<Blocks...>>::template LinkedBlock<
            Link<(sizeof...(Blocks) == 1) ? 0
                 : (N == sizeof...(Blocks) - 1)
                   ? -(static_cast<std::int32_t>(TotalSize - getBlockSize<N>()) * 4)
                   : static_cast<std::int32_t>(getBlockSize<N>() * 4)>>;

        template<std::size_t... Is>
        static constexpr auto getBlockLoopImpl(std::index_sequence<Is...>) {
            constexpr std::size_t ArraySize = (LinkedBlockAt<Is>::words.size() + ...);

            std::array<std::uint32_t, ArraySize> arr{};
            std::span<std::uint32_t>             remaining{arr};

            ((remaining = write_to<LinkedBlockAt<Is>>(remaining)), ...);

            return arr;
        }

        static constexpr auto getBlockLoop() {
            return getBlockLoopImpl(std::index_sequence_for<Blocks...>{});
        }

    public:
        static constexpr auto words = getBlockLoop();
    };

}}   // namespace Kvasir::Picobin

// ============================================================================
// Usage Examples
// ============================================================================
//
// Basic IMAGE_DEF:
//    [[gnu::used, gnu::section(".after_vectors")]]
//    static constexpr auto ImageDef
//      = Kvasir::Picobin::BlockLoop<Kvasir::Picobin::Block<Kvasir::Picobin::ArmSecure>>::words;
//
// IMAGE_DEF with VERSION:
//    [[gnu::used, gnu::section(".after_vectors")]]
//    static constexpr auto ImageDef = Kvasir::Picobin::BlockLoop<
//        Kvasir::Picobin::Block<Kvasir::Picobin::ArmSecure>,
//        Kvasir::Picobin::Block<Kvasir::Picobin::Version<1, 0>>>::words;
//
// IMAGE_DEF with multiple items:
//    [[gnu::used, gnu::section(".after_vectors")]]
//    static constexpr auto ImageDef = Kvasir::Picobin::BlockLoop<Kvasir::Picobin::Block<
//        Kvasir::Picobin::ArmSecure,
//        Kvasir::Picobin::Version<1, 0>,
//        Kvasir::Picobin::VectorTable<0x20000000>,
//        Kvasir::Picobin::LoadMapRelative<0, 0x20000000, 0x40000>,
//        Kvasir::Picobin::EntryPoint<0x20000000, 0x20075330>>>::words;
//
// IMAGE_DEF with hash and signature:
//    [[gnu::used, gnu::section(".after_vectors")]]
//    static constexpr auto ImageDef = Kvasir::Picobin::BlockLoop<Kvasir::Picobin::Block<
//        Kvasir::Picobin::ArmSecure,
//        Kvasir::Picobin::Version<1, 0>,
//        Kvasir::Picobin::HashDef<100>,
//        Kvasir::Picobin::LoadMapRelative<0, 0x20000000, 0x40000>,
//        Kvasir::Picobin::HashValue<0x12345678, 0xaabbccdd, 0x11223344, 0xffeeddcc>,
//        Kvasir::Picobin::EntryPoint<0x20000000, 0x20075330>>>::words;
//
// Partition Table with A/B partitions:
//    using namespace Kvasir::Picobin;
//    using namespace Kvasir::Picobin::Permissions;
//    using namespace Kvasir::Picobin::PartitionFlags;
//
//    constexpr auto APartitionFlags =
//        PartitionLink<LINK_TYPE_A_PARTITION, 1>::value |
//        HAS_ID | HAS_NAME | ACCEPTS_DEFAULT_FAMILY_RP2350_ARM_NS;
//
//    constexpr auto BPartitionFlags =
//        PartitionLink<LINK_TYPE_OWNER_PARTITION, 0>::value |
//        HAS_ID | HAS_NAME | ACCEPTS_DEFAULT_FAMILY_RP2350_ARM_NS;
//
//    using PartitionA = PartitionEntry<
//        0, 100, ALL_RW,
//        APartitionFlags | EXTRA_FAMILY_IDS,
//        STRING_CONSTANT("boot_a"),
//        0x1234567890ABCDEF,
//        0xAABBCCDD, 0x11223344>;
//
//    using PartitionB = PartitionEntry<
//        101, 200, ALL_RW,
//        BPartitionFlags | EXTRA_FAMILY_IDS,
//        STRING_CONSTANT("boot_b"),
//        0xFEDCBA0987654321,
//        0xAABBCCDD, 0x11223344>;
//
//    using MyPartitionTable = PartitionTable<true, ALL_RW, 0,
//        PartitionA, PartitionB>;
