// Host test of chip/PicobinSeal.hpp, an image sealed as an A/B update (`picotool seal --hash` with a version and TBYB):
// - at compile time on the writer's minimum image (sealOf() is constexpr and allocates nothing: a firmware seals what
//   it writes);
// - picotool's sealed reference images (data/README.md) sealed again: a one-entry absolute load map (blink), a zero fill
//   and a copy into SRAM (copy_to_ram), an image already versioned (tiny) - each walked afterwards as the boot ROM
//   would: the new block launched, its hash holding, version and TBYB as asked;
// - an image whose load map is relative, and one with a rolling window delta: the sources found where the boot ROM
//   reads them;
// - what is refused: a signed image, no IMAGE_DEF, a load map too big for the boot ROM's IMAGE_DEF limit.
#include <algorithm>
#include <array>
#include <chip/PicobinSeal.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Pb = Kvasir::Picobin;

namespace {
// ---- compile time: the writer's minimum Arm IMAGE_DEF, sealed and walked
constexpr auto minimumArm = Pb::Detail::ReadBackTest::minimumArm;

constexpr std::optional<Pb::ParsedBlock> sealedAtCompileTime(Pb::SealRequest const& request) {
    auto const s = Pb::sealOf(minimumArm, request);
    if(!s) { return std::nullopt; }
    std::array<std::byte, 512> out{};
    if(s->bytes() > out.size()) { return std::nullopt; }
    for(std::uint32_t at = 0; at < s->bytes(); at += 4U) {
        auto const w = s->wordOf(minimumArm, at);
        for(std::uint32_t i = 0; i < 4U; ++i) {
            out[at + i] = static_cast<std::byte>((w >> (8U * i)) & 0xffU);
        }
    }
    return Pb::walk(std::span<std::byte const>{out}.first(s->bytes()),
                    Pb::Window{},
                    Pb::BootRules{},
                    false)
      .imageDef();
}

constexpr auto compileTimeTbyb = sealedAtCompileTime({
  .version = {.major = 2, .minor = 9},
  .tbyb    = true
});
static_assert(compileTimeTbyb.has_value() && compileTimeTbyb->hash == Pb::ParsedBlock::Hash::ok);
static_assert(compileTimeTbyb->tbyb
              && compileTimeTbyb->version
                   == Pb::ParsedBlock::Version{.major = 2,
                                               .minor = 9});
// the block goes behind the image, the image's own IMAGE_DEF stays where it was
static_assert(compileTimeTbyb->offset == minimumArm.size());
static_assert(!sealedAtCompileTime({.version = {.major = 1},
                                    .tbyb    = false})
                 .value_or(Pb::ParsedBlock{})
                 .tbyb);

int failures{};
int checks{};

void check(bool             good,
           std::string_view what) {
    ++checks;
    if(!good) {
        ++failures;
        std::printf("FAIL: %.*s\n", static_cast<int>(what.size()), what.data());
    }
}

std::vector<std::byte> readFile(std::string const& name) {
    std::ifstream     in{std::string{PICOBIN_TEST_DATA} + "/" + name, std::ios::binary};
    std::vector<char> chars{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    std::vector<std::byte> out(chars.size());
    std::ranges::transform(chars, out.begin(), [](char c) { return static_cast<std::byte>(c); });
    check(!out.empty(), "data file " + name + " read");
    return out;
}

/// Words laid out as flash bytes, with a block writer (reader_test.cpp's).
struct Image {
    std::vector<std::uint32_t> words;

    explicit Image(std::size_t n)
      : words(n,
              0U) {}

    [[nodiscard]] std::vector<std::byte> bytes() const {
        std::vector<std::byte> out(words.size() * 4U);
        for(std::size_t i = 0; i < out.size(); ++i) {
            out[i] = static_cast<std::byte>((words[i / 4U] >> (8U * (i % 4U))) & 0xffU);
        }
        return out;
    }

    void block(std::size_t                       at,
               std::vector<std::uint32_t> const& items,
               std::int32_t                      link) {
        std::size_t i = at;
        words[i++]    = Pb::picobinBlockMarkerStart;
        for(auto const w : items) { words[i++] = w; }
        words[i++] = 0xffU | (static_cast<std::uint32_t>(items.size()) << 8U);
        words[i++] = static_cast<std::uint32_t>(link);
        words[i++] = Pb::picobinBlockMarkerEnd;
    }
};

constexpr std::uint32_t armExe
  = 0x1021'0142U;   // IMAGE_TYPE: EXE, Secure, Arm, RP2350 (DS 5.9.5.1)

/// What the boot ROM launches from `bytes` (the partition's own search).
std::optional<Pb::ParsedBlock> launched(std::span<std::byte const> bytes) {
    return Pb::walk(bytes, Pb::Window{}, Pb::BootRules{}, false).imageDef();
}

/// Seal `image` and judge the result as the boot ROM would: the new block launched, its hash ok, as asked.
void sealedAndLaunched(std::vector<std::byte> const& image,
                       Pb::SealRequest const&        request,
                       std::string const&            name) {
    auto const out = Pb::seal(image, request);
    check(out.has_value(), name + ": sealed");
    if(!out) { return; }
    auto const blockOffset = static_cast<std::uint32_t>((image.size() + 3U) & ~std::size_t{3U});
    check(out->size() > blockOffset, name + ": the block behind the image");
    std::size_t differ = 0;
    for(std::size_t i = 0; i < image.size(); ++i) { differ += image[i] != (*out)[i] ? 1U : 0U; }
    check(differ <= 4U, name + ": at most the four bytes of one link changed");
    auto const def = launched(*out);
    check(def.has_value() && def->offset == blockOffset,
          name + ": the boot ROM launches the new block");
    if(!def) { return; }
    check(def->hash == Pb::ParsedBlock::Hash::ok && def->verified(), name + ": its hash holds");
    check(def->version == request.version && def->tbyb == request.tbyb,
          name + ": version and TBYB as asked");
    check(def->nextOffset >= 0 && static_cast<std::size_t>(def->nextOffset) < image.size(),
          name + ": it links back into the image, to the first block");

    // sealOf() + wordOf() are the same bytes, and the digest is where digestAt says
    auto const s = Pb::sealOf(image, request);
    check(s.has_value() && s->bytes() == out->size(), name + ": sealOf() has the same size");
    if(s) {
        bool same = true;
        for(std::uint32_t at = 0; at < s->bytes(); at += 4U) {
            same = same && s->wordOf(image, at) == Pb::Detail::wordAt(*out, at);
        }
        check(same, name + ": sealOf().wordOf() = seal()'s bytes");
        auto corrupt = *out;
        corrupt[s->blockOffset + (4U * s->digestAt)] ^= std::byte{0x01};
        check(launched(corrupt).value_or(Pb::ParsedBlock{}).hash == Pb::ParsedBlock::Hash::bad,
              name + ": a digest bit changed fails the hash");
    }
    // a byte of the image the new load map's first source covers changed: the hash fails
    if(s) {
        std::uint32_t const base
          = Pb::xipBaseAddress - launched(image).value_or(Pb::ParsedBlock{}).rollingWindowDelta;
        std::optional<std::uint32_t>
          from;   // the first entry whose source lies in the image, as an image offset
        for(std::uint32_t w = 1U; w < s->digestAt && !from; ++w) {
            if((s->words[w] & 0x7fU) != 0x06U || (s->words[w] >> 8U & 0xffU) < 4U) { continue; }
            auto const n = (s->words[w] >> 24U) & 0x7fU;
            for(std::uint32_t e = 0; e < n && !from; ++e) {
                auto const storage = s->words[w + 1U + (3U * e)];
                if(storage >= base && storage - base < image.size()) { from = storage - base; }
            }
        }
        check(from.has_value(), name + ": a load map source in the image");
        auto code = *out;
        code[from.value_or(0U) + 8U] ^= std::byte{0x10};
        check(launched(code).value_or(Pb::ParsedBlock{}).hash == Pb::ParsedBlock::Hash::bad,
              name + ": an image byte changed fails the hash");
    }
}

void referenceImages() {
    // picotool's sealed images sealed again: the launched IMAGE_DEF's load map made absolute and hashed anew
    for(auto const* name : {"blink_sealed.bin", "copy_to_ram_sealed.bin", "tiny_sealed.bin"}) {
        auto const image = readFile(name);
        sealedAndLaunched(image,
                          {
                            .version = {.major = 5, .minor = 1},
                            .tbyb    = true
        },
                          std::string{name} + " TBYB");
        sealedAndLaunched(image,
                          {
                            .version = {.major = 0, .minor = 3},
                            .tbyb    = false
        },
                          std::string{name} + " bought");
    }
    // the zero fill and the copy into SRAM stay in the new load map
    auto const ram = readFile("copy_to_ram_sealed.bin");
    auto const s   = Pb::sealOf(ram, {.version = {.major = 1}});
    check(s.has_value(), "copy_to_ram: sealOf");
    if(s) {
        bool zeroFill = false;
        bool toSram   = false;
        for(std::uint32_t w = 0; w + 3U < s->wordCount; ++w) {
            zeroFill = zeroFill || (s->words[w] == 0U && (s->words[w + 1U] >> 28U) == 2U);
            toSram   = toSram || ((s->words[w] >> 28U) == 1U && (s->words[w + 1U] >> 28U) == 2U);
        }
        check(zeroFill && toSram, "copy_to_ram: the zero fill and the flash-to-SRAM copy kept");
    }
}

void builtImages() {
    // One block like a Kvasir image's own (IMAGE_TYPE alone), at 0x100 of a 2 KiB image: one absolute entry over it all.
    {
        Image img{512};
        for(std::size_t i = 0; i < img.words.size(); ++i) {
            img.words[i] = 0x5a5a'0000U + static_cast<std::uint32_t>(i);
        }
        img.block(64, {armExe}, 0);
        auto bytes = img.bytes();
        bytes.resize(
          bytes.size()
          - 3U);   // an image whose size is no multiple of 4: padded with 0xff up to the block
        sealedAndLaunched(bytes, {.version = {.major = 1}, .tbyb = true}, "one block, odd size");
        auto const s = Pb::sealOf(bytes, {.version = {.major = 1}});
        check(s.has_value() && s->blockOffset == 2048U && s->linkAt == (64U + 3U) * 4U,
              "one block: the link of the only block, the new one at the next word");
    }
    // A relative load map: storage counted from the item.
    {
        Image img{512};
        for(std::size_t i = 0; i < img.words.size(); ++i) {
            img.words[i] = 0x1234'0000U + static_cast<std::uint32_t>(i);
        }
        // LOAD_MAP, relative, one entry: the image's first 1 KiB in place (storage = start - the item's address)
        std::uint32_t const itemAt = (100U + 2U) * 4U;
        img.block(100, {armExe, 0x0100'0406U, 0U - itemAt, Pb::xipBaseAddress, 1024U}, 0);
        auto const bytes = img.bytes();
        check(launched(bytes).value_or(Pb::ParsedBlock{}).verified(),
              "relative load map: the image itself verifies");
        sealedAndLaunched(bytes, {.version = {.major = 3}, .tbyb = true}, "relative load map");
    }
    // A rolling window delta of 4 KiB: the partition's byte 0x1000 runs at 0x10000000 (DS 5.1.4), the bytes before it
    // are not mapped - the new load map covers the mapped part. An image no longer than the delta has none: refused.
    {
        Image img{2048};
        for(std::size_t i = 0; i < img.words.size(); ++i) {
            img.words[i] = 0x7777'0000U + static_cast<std::uint32_t>(i);
        }
        img.block(16, {armExe, 0x0000'0205U, 0x1000U}, 0);   // ROLLING_WINDOW_DELTA, 2 words
        auto const bytes = img.bytes();
        auto const def   = launched(bytes);
        check(def.has_value() && def->rollingWindowDelta == 0x1000U,
              "rolling window: the reader takes the delta");
        sealedAndLaunched(bytes, {.version = {.major = 4}, .tbyb = false}, "rolling window delta");
        auto const s    = Pb::sealOf(bytes, {.version = {.major = 4}});
        bool       kept = false;
        for(std::uint32_t w = 0; s && w + 1U < s->wordCount; ++w) {
            kept = kept || (s->words[w] == 0x0000'0205U && s->words[w + 1U] == 0x1000U);
        }
        check(kept, "rolling window: the delta goes into the new block");
        // the block's tail: LOAD_MAP (header, storage, runtime, end), VERSION, HASH_DEF, HASH_VALUE's header, digest
        check(s.has_value() && s->words[s->digestAt - 8U] == Pb::xipBaseAddress
                && s->words[s->digestAt - 6U] == Pb::xipBaseAddress + 4096U,
              "rolling window: the load map from 0x10000000, the mapped part's 4 KiB");
        Image small{1024};
        small.block(16, {armExe, 0x0000'0205U, 0x1000U}, 0);
        check(Pb::sealOf(small.bytes(), {}).error_or(Pb::SealError::verifyFailed)
                == Pb::SealError::badLoadMap,
              "rolling window: nothing mapped, refused");
    }
}

void refused() {
    // signed: the signer's to seal
    Image                      signedImg{256};
    std::vector<std::uint32_t> items{armExe, 0x0100'2109U};   // SIGNATURE secp256k1, 33 words
    items.insert(items.end(), 32U, 0x11U);
    signedImg.block(0, items, 0);
    check(Pb::sealOf(signedImg.bytes(), {}).error_or(Pb::SealError::verifyFailed)
            == Pb::SealError::signedImage,
          "a signed image is refused");
    // nothing to launch
    Image empty{64};
    check(Pb::sealOf(empty.bytes(), {}).error_or(Pb::SealError::verifyFailed)
            == Pb::SealError::noImageDef,
          "no block: noImageDef");
    Image data{64};
    data.block(0, {0x0002'0142U}, 0);   // IMAGE_TYPE DATA, not EXE
    check(!Pb::sealOf(data.bytes(), {}).has_value(), "no executable IMAGE_DEF: refused");
    // a load map of 28 entries, 85 words: the image's block fits the boot ROM's 384-byte IMAGE_DEF limit, the sealed one
    // (VERSION, HASH_DEF, HASH_VALUE added) does not
    Image                      big{1024};
    std::vector<std::uint32_t> map{armExe, 0x1c00'5506U | 0x8000'0000U};
    for(std::uint32_t e = 0; e < 28U; ++e) {
        map.insert(map.end(),
                   {Pb::xipBaseAddress + (16U * e),
                    Pb::xipBaseAddress + (16U * e),
                    Pb::xipBaseAddress + (16U * e) + 16U});
    }
    big.block(0, map, 0);
    check(launched(big.bytes()).value_or(Pb::ParsedBlock{}).verified(),
          "28 entries: the image itself verifies");
    check(Pb::sealOf(big.bytes(), {}).error_or(Pb::SealError::verifyFailed)
            == Pb::SealError::tooBig,
          "a load map past the IMAGE_DEF limit: tooBig");
}
}   // namespace

int main() {
    referenceImages();
    builtImages();
    refused();
    std::printf("seal: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
