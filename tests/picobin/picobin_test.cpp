// Host test of chip/picobin.hpp: encodings against the RP2350 datasheet section 5.9 and picotool's block writer
// (bintool/metadata.h), and the boot ROM's block size limits (DS 5.9.1). The static_asserts are the test; main()
// compares the same words at run time and prints them, so a failing check also names the word.
#include <array>
#include <chip/picobin.hpp>
#include <cstdint>
#include <cstdio>
#include <span>

namespace Pb = Kvasir::Picobin;

namespace {
// ---- HASH_DEF (DS 5.9.2.2): byte 0 0x47, byte 1 the size, byte 2 pad, byte 3 the hash type (0x01 SHA-256); word 1
// the number of block words hashed in its low 16 bits. picotool writes `encode_type_and_size(2) | (hash_type << 24)`
// and the word count, two words; the boot ROM reads the type from `header >> 24` and ignores a size other than 2.
// The DS table's size_lo byte says 0x03 while it lists two words: the ROM and picotool decide (size 2).
constexpr std::array<std::uint32_t, 2> hashDef5{0x01000247U, 5U};
static_assert(Pb::HashDef<5>::words == hashDef5);
static_assert(Pb::HashDef<160>::words
              == std::array<std::uint32_t,
                            2>{0x01000247U,
                               160U});
static_assert((Pb::HashDef<5>::words[0] >> 24U) == 0x01U);             // hash type in byte 3
static_assert(((Pb::HashDef<5>::words[0] >> 16U) & 0xffU) == 0x00U);   // byte 2 is pad
static_assert(((Pb::HashDef<5>::words[0] >> 8U) & 0xffU) == 0x02U);    // size: two words

// ---- the minimum Arm IMAGE_DEF (DS 5.9.5.1), the block every RP2350 image carries
constexpr std::array<std::uint32_t, 5> minimumArm{0xffffded3U,
                                                  0x10210142U,
                                                  0x000001ffU,
                                                  0x00000000U,
                                                  0xab123579U};
static_assert(Pb::BlockLoop<Pb::Block<Pb::ArmSecure>>::words == minimumArm);

// ---- a block with a HASH_DEF: LAST covers the three item words (0x03 << 8 | 0xff, DS 5.9.1)
constexpr std::array<std::uint32_t, 7> armWithHashDef{0xffffded3U,
                                                      0x10210142U,
                                                      0x01000247U,
                                                      0x00000004U,
                                                      0x000003ffU,
                                                      0x00000000U,
                                                      0xab123579U};
using ArmHashed = Pb::BlockLoop<Pb::Block<Pb::ArmSecure, Pb::HashDef<4>>>;
static_assert(ArmHashed::words == armWithHashDef);

// ---- the size limits (DS 5.9.1): bytes, 384 for an IMAGE_DEF, 640 for a PARTITION_TABLE and any other block
static_assert(Pb::picobinMaxImageDefBlockSize == 384U);
static_assert(Pb::picobinMaxPartitionTableBlockSize == 640U);
static_assert(Pb::picobinMaxBlockSize == 640U);
static_assert(Pb::maxBlockBytes<Pb::ArmSecure>() == 384U);
static_assert(Pb::maxBlockBytes<Pb::DataImage>() == 384U);
static_assert(Pb::maxBlockBytes<Pb::Version<1,
                                            0>>()
              == 640U);
static_assert(Pb::blockWords<Pb::ArmSecure>() == 5U);
static_assert(Pb::bytesOf(96U) == 384U);

template<std::size_t N>
struct Filler {
    static constexpr std::array<std::uint32_t, N> words{};
};

// IMAGE_DEF: 1 + 91 + 4 = 96 words = 384 bytes is the largest that fits
static_assert(Pb::blockFits<Pb::ArmSecure,
                            Filler<91>>());
static_assert(!Pb::blockFits<Pb::ArmSecure,
                             Filler<92>>());
// a block of 155 words (620 bytes) with an IMAGE_DEF first compiled while the limits were compared in words
static_assert(!Pb::blockFits<Pb::ArmSecure,
                             Filler<150>>());
// other first items: 160 words = 640 bytes
static_assert(Pb::blockFits<Pb::Version<1,
                                        0>,
                            Filler<154>>());
static_assert(!Pb::blockFits<Pb::Version<1,
                                         0>,
                             Filler<155>>());
// the largest IMAGE_DEF block is built, and its size is the limit
static_assert(Pb::BlockLoop<Pb::Block<Pb::ArmSecure,
                                      Filler<91>>>::words.size()
                * 4U
              == 384U);

int failures = 0;

template<std::size_t N,
         std::size_t M>
void expect(char const*          name,
            std::array<std::uint32_t,
                       N> const& got,
            std::span<std::uint32_t const,
                      M>         want) {
    bool ok = N == M;
    for(std::size_t i = 0; ok && i < N; ++i) { ok = got[i] == want[i]; }
    std::printf("%s %s:", ok ? "ok  " : "FAIL", name);
    for(auto w : got) { std::printf(" %08x", static_cast<unsigned>(w)); }
    std::printf("\n");
    if(!ok) { ++failures; }
}
}   // namespace

int main() {
    // run-time copies: the compile-time checks above read the same arrays
    expect("HASH_DEF(5) DS 5.9.2.2 / picotool", Pb::HashDef<5>::words, std::span{hashDef5});
    expect("minimum Arm IMAGE_DEF DS 5.9.5.1",
           Pb::BlockLoop<Pb::Block<Pb::ArmSecure>>::words,
           std::span{minimumArm});
    expect("IMAGE_DEF + HASH_DEF(4)", ArmHashed::words, std::span{armWithHashDef});
    std::printf("block limits: IMAGE_DEF %u bytes, other blocks %u bytes\n",
                static_cast<unsigned>(Pb::maxBlockBytes<Pb::ArmSecure>()),
                static_cast<unsigned>(Pb::maxBlockBytes<Pb::Version<1, 0>>()));
    return failures == 0 ? 0 : 1;
}
