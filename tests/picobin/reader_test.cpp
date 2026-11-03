// Host test of chip/PicobinReader.hpp, the boot ROM's block walk read back:
// - blocks another writer made: picotool's partition tables and a picotool-sealed image (data/, README.md there);
// - the edges where the ROM's walk differs from "a block is at most 640 bytes, the first one is in the first 4 KiB":
//   each case built by hand, its expected outcome worked out from pico-bootrom-rp2350 A4's varm_blocks.c;
// - the parser's rejections (bad_item), and which IMAGE_DEF / PARTITION_TABLE wins;
// - Walker fed in pieces of every size (the reads a PICOBOOT client makes) against the same walk over memory, on
//   generated loops and random mutations of them (a fixed seed: the same cases every run).
#include <algorithm>
#include <array>
#include <chip/PicobinReader.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Pb = Kvasir::Picobin;

namespace {
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

/// Words laid out as flash bytes.
struct Image {
    std::vector<std::uint32_t> words;

    explicit Image(std::size_t   n,
                   std::uint32_t fill = 0U)
      : words(n,
              fill) {}

    [[nodiscard]] std::vector<std::byte> bytes() const {
        std::vector<std::byte> out(words.size() * 4U);
        for(std::size_t i = 0; i < out.size(); ++i) {
            out[i] = static_cast<std::byte>((words[i / 4U] >> (8U * (i % 4U))) & 0xffU);
        }
        return out;
    }

    /// A block at word `at`: the start marker, `items` (each a header and its words), LAST, the link in bytes, the end
    /// marker. Returns the word after it.
    std::size_t block(std::size_t                       at,
                      std::vector<std::uint32_t> const& items,
                      std::int32_t                      link) {
        std::size_t i = at;
        words[i++]    = Pb::picobinBlockMarkerStart;
        for(auto const w : items) { words[i++] = w; }
        words[i++] = 0xffU | (static_cast<std::uint32_t>(items.size()) << 8U);
        words[i++] = static_cast<std::uint32_t>(link);
        words[i++] = Pb::picobinBlockMarkerEnd;
        return i;
    }
};

/// value_or()'s stand-in where a missing block must fail a "not rejected" check
constexpr Pb::ParsedBlock rejectedBlock = [] {
    Pb::ParsedBlock b{};
    b.rejected = true;
    return b;
}();

constexpr std::uint32_t armExe
  = 0x1021'0142U;   // IMAGE_TYPE: EXE, Secure, Arm, RP2350 (DS 5.9.5.1)
constexpr std::uint32_t armExeTbyb = 0x9021'0142U;
constexpr std::uint32_t riscvExe   = 0x1121'0142U;
constexpr std::uint32_t rp2040Exe  = 0x0021'0142U;

/// `n` words of IGNORED items (type 0x7e), as few items as fit.
std::vector<std::uint32_t> filler(std::vector<std::uint32_t> items,
                                  std::uint32_t              n) {
    while(n != 0U) {
        auto const size = std::min<std::uint32_t>(n, 255U);
        items.push_back(0x7eU | (size << 8U));
        items.insert(items.end(), size - 1U, 0U);
        n -= size;
    }
    return items;
}

std::uint32_t rnd(std::mt19937& rng) { return static_cast<std::uint32_t>(rng()); }

std::optional<std::uint32_t> launchedAt(std::vector<std::byte> const& bytes) {
    auto const def = Pb::launchedImageDef(bytes);
    return def ? std::optional{def->offset} : std::nullopt;
}

/// Walker fed in pieces: each want() answered in `chunks` reads of the sizes `split` picks, as a PICOBOOT client
/// might; the pieces are concatenated before feed() (feed() takes one read's answer).
Pb::Walker walkInPieces(std::span<std::byte const> window,
                        bool                       knownSize,
                        std::mt19937&              rng,
                        Pb::BootRules const&       rules = {}) {
    Pb::Walker walker{
      Pb::Window{.bytes = knownSize ? static_cast<std::uint32_t>(window.size()) : 0xffff'fffcU},
      rules};
    int guard{};
    while(auto const read = walker.want()) {
        if(++guard > 100'000) { break; }
        auto const [at, length] = *read;
        check(length <= Pb::maxReadBytes && at % 4U == 0U && length % 4U == 0U && length != 0U,
              "Walker asks for at most maxReadBytes, word aligned");
        std::vector<std::byte> answer;
        for(std::uint32_t got = 0; got < length;) {
            auto const piece = std::min<std::uint32_t>(length - got, 1U + (rnd(rng) % 300U));
            auto const from  = std::min<std::size_t>(std::size_t{at} + got, window.size());
            auto const end   = std::min<std::size_t>(from + piece, window.size());
            answer.insert(answer.end(),
                          window.begin() + static_cast<std::ptrdiff_t>(from),
                          window.begin() + static_cast<std::ptrdiff_t>(end));
            got += piece;
        }
        walker.feed(answer);
    }
    check(guard <= 100'000, "Walker ends");
    return walker;
}

void picotoolTables() {
    // picotool's default A/B table (json/default-pt.json)
    auto const ab = readFile("pt_ab.bin");
    auto const t  = Pb::partitionTable(ab);
    check(t.has_value(), "pt_ab: a table");
    if(t) {
        check(t->count == 2 && !t->singleton && t->blockOffset == 0U,
              "pt_ab: two partitions, not singleton");
        check(t->partitions[0].firstSector() == 2U && t->partitions[0].lastSector() == 512U,
              "pt_ab: A sectors 2..512");
        check(t->partitions[1].firstSector() == 513U && t->partitions[1].lastSector() == 1023U,
              "pt_ab: B sectors 513..1023");
        check(t->partitions[0].id == std::optional<std::uint64_t>{0U}
                && t->partitions[1].id == std::optional<std::uint64_t>{1U},
              "pt_ab: ids 0 and 1");
        // B is linked to A: LINK_TYPE A_PARTITION (1), value 0 (picobin.h PICOBIN_PARTITION_FLAGS_LINK_*)
        check(((t->partitions[1].permissionsAndFlags >> 1U) & 3U) == 1U
                && ((t->partitions[1].permissionsAndFlags >> 3U) & 0xfU) == 0U,
              "pt_ab: B linked to A");
        check(t->unpartitionedPermissionsAndFlags == 0xfc00'8000U,
              "pt_ab: unpartitioned space rw, family absolute");
    }
    auto const walker = Pb::walk(ab);
    check(walker.outcome() == Pb::Walker::Outcome::closed && !walker.imageDef(),
          "pt_ab: a closed loop, no IMAGE_DEF");

    auto const single = Pb::partitionTable(readFile("pt_ab_singleton_nohash.bin"));
    check(single.has_value() && single->singleton && single->count == 2,
          "singleton table: bit 31, two partitions");

    // pico-examples' flash/partition_info/pt.json: ids 1..3, names, an extra family on the first
    auto const named = Pb::partitionTable(readFile("pt_named.bin"));
    check(named.has_value() && named->count == 3, "pt_named: three partitions");
    if(named) {
        check(named->partitions[0].firstSector() == 0U && named->partitions[0].lastSector() == 127U
                && named->partitions[1].firstSector() == 128U
                && named->partitions[2].lastSector() == 383U,
              "pt_named: 512K each from sector 0");
        check(named->partitions[2].id == std::optional<std::uint64_t>{3U}, "pt_named: third id 3");
        check(((named->partitions[0].permissionsAndFlags >> 7U) & 3U) == 1U,
              "pt_named: one extra family on the first");
    }

    // the same table with a word added to its item: the ROM's exact-fit check (get_pt_partition_info) refuses it
    auto words = ab;
    words.resize(words.size() + 4U);
    {
        Image img{words.size() / 4U};
        for(std::size_t i = 0; i < img.words.size(); ++i) {
            img.words[i] = Pb::Detail::wordAt(words, 4U * i);
        }
        // rebuild: header size 12 -> 13, a zero word after the item, LAST size +1; drop the hash items (they follow)
        std::vector<std::uint32_t> item(img.words.begin() + 1, img.words.begin() + 1 + 12);
        item[0] = (item[0] & ~0xff00U) | (13U << 8U);
        item.push_back(0U);
        Image grown{64};
        grown.block(0, item, 0);
        check(!Pb::partitionTable(grown.bytes()).has_value(),
              "a PARTITION_TABLE item one word too long is refused");
        Image same{64};
        item.pop_back();
        item[0] = (item[0] & ~0xff00U) | (12U << 8U);
        same.block(0, item, 0);
        check(Pb::partitionTable(same.bytes()) == t,
              "the same item without the hash items reads the same");
    }
}

void picotoolSealed() {
    // picotool seal --hash --major 3 --minor 7: the image's block at 0x100 links to the one picotool appended at 0x400
    auto const sealed = readFile("tiny_sealed.bin");
    auto const w      = Pb::walk(sealed);
    check(w.outcome() == Pb::Walker::Outcome::closed
            && w.firstBlock() == std::optional<std::uint32_t>{0x100U},
          "sealed: closed loop from 0x100");
    auto const def = w.imageDef();
    check(def.has_value() && def->offset == 0x400U, "sealed: the appended IMAGE_DEF at 0x400 wins");
    if(def) {
        check(def->version == std::optional{Pb::ParsedBlock::Version{.major = 3, .minor = 7}}, "sealed: version 3.7");
        check(def->hashDef && !def->signature && !def->rejected && def->exe && !def->tbyb,
              "sealed: SHA-256 hash, no signature, accepted, bought");
        check(def->nextOffset == 0x100, "sealed: links back to the first block");
    }
}

void romEdges() {
    // (A) a 584-byte block at the window start: the ROM's first read is 512 bytes and a block at a read's start is
    // parsed from that read alone - its LAST is never seen: no loop
    {
        Image img{1024};
        img.block(0, filler({armExe}, 140), 0);
        check(!launchedAt(img.bytes()), "A: a 584-byte block at a read's start is not one");
        check(Pb::walk(img.bytes()).outcome() == Pb::Walker::Outcome::noBlock, "A: no first block");
        Image small{1024};
        small.block(0, filler({armExe}, 120), 0);   // 504 bytes: within the read
        check(launchedAt(small.bytes()) == std::optional<std::uint32_t>{0U},
              "A: a 504-byte block is");
    }
    // (B) a link to a 584-byte block outside the buffer: that block starts a fresh read too - the loop is invalid
    {
        Image img{1024};
        img.block(0, {armExe}, 1024);
        img.block(256, filler({armExe}, 140), -1024);
        check(Pb::walk(img.bytes()).outcome() == Pb::Walker::Outcome::invalid,
              "B: link to a too-long block: invalid");
    }
    // a block found further into a read is moved to the buffer's start and topped up: 584 bytes fit then
    {
        Image img{1024};
        img.block(4, filler({armExe}, 140), 0);
        check(launchedAt(img.bytes()) == std::optional<std::uint32_t>{16U},
              "a 584-byte block 16 bytes in is one");
    }
    // (C) a stray marker at 4000 whose walk runs off the buffer, then a block at 4120: the search goes past 4 KiB
    {
        Image img{1100};
        img.words[1000] = Pb::picobinBlockMarkerStart;
        img.words[1001] = 0x7eU | (200U << 8U);
        img.block(1030, {armExe}, 0);
        check(launchedAt(img.bytes()) == std::optional<std::uint32_t>{4120U},
              "C: a block past 4 KiB behind a stray marker");
        Image alone{1100};
        alone.block(1030, {armExe}, 0);
        check(!launchedAt(alone.bytes()), "C: but not past 4 KiB on its own");
    }
    // ... but a marker whose items are malformed - a zero-sized item, a LAST of the wrong size, no end marker - ends
    // the search for good (unsupported_block_error returns -1 there too): the block behind it is never reached
    {
        Image img{1100};
        img.words[16] = Pb::picobinBlockMarkerStart;   // then a 0 word: a zero-sized item
        img.block(64, {armExe}, 0);
        check(Pb::walk(img.bytes()).outcome() == Pb::Walker::Outcome::invalid,
              "a zero-sized item ends the search");
        Image noEnd{1100};
        noEnd.block(16, {armExe}, 0);
        noEnd.words[16 + 4] = 0U;   // its end marker
        noEnd.block(64, {armExe}, 0);
        check(Pb::walk(noEnd.bytes()).outcome() == Pb::Walker::Outcome::invalid,
              "a missing end marker ends the search");
    }
    // (D) a 600-byte block at 4092: moved to the buffer's start it has 516 bytes, runs off, the next marker at 4160
    // starts the loop
    {
        Image img{1300};
        img.block(1023, filler({armExe}, 140), 0);
        img.block(1040, {armExe}, 0);
        check(launchedAt(img.bytes()) == std::optional<std::uint32_t>{4160U},
              "D: the block at 4160, not the one at 4092");
    }
    // a block at 4092 that fits
    {
        Image img{1100};
        img.block(1023, {armExe}, 0);
        check(launchedAt(img.bytes()) == std::optional<std::uint32_t>{4092U},
              "a block starting at 4092");
    }
    // links: below the first block, to itself (not first), the 256th block, toward zero
    {
        Image img{1024};
        img.block(16, {armExe}, 64);
        img.block(32, {armExe}, -96);   // to 32 * 4 - 96 = 32 < 64
        check(Pb::walk(img.bytes()).outcome() == Pb::Walker::Outcome::invalid,
              "a link below the first block");
        Image self{1024};
        self.block(0, {armExe}, 64);
        self.block(16, {armExe}, 0);
        check(Pb::walk(self.bytes()).outcome() == Pb::Walker::Outcome::invalid,
              "a non-first block linking to itself");
        Image round{1024};
        round.block(0, {armExe}, 67);     // 67 / 4 = 16 words, toward zero
        round.block(16, {armExe}, -66);   // -66 / 4 = -16
        check(Pb::walk(round.bytes()).outcome() == Pb::Walker::Outcome::closed,
              "links divided toward zero");
    }
    for(std::uint32_t const blocks : {255U, 256U}) {
        Image img{blocks * 5U + 8U};
        for(std::uint32_t b = 0; b < blocks; ++b) {
            img.block(std::size_t{b} * 5U,
                      {armExe},
                      b + 1U == blocks ? -static_cast<std::int32_t>(b * 20U) : 20);
        }
        auto const outcome = Pb::walk(img.bytes()).outcome();
        check(outcome
                == (blocks == 255U ? Pb::Walker::Outcome::closed : Pb::Walker::Outcome::invalid),
              blocks == 255U ? "255 blocks are a loop" : "a 256th block breaks it");
    }
}

void parser() {
    auto const one = [](std::vector<std::uint32_t> const& items) {
        Image img{256};
        img.block(0, items, 0);
        return Pb::launchedImageDef(img.bytes());
    };
    check(one({rp2040Exe}).value_or(Pb::ParsedBlock{}).rejected,
          "an EXE for the RP2040 is rejected");
    {
        auto const zero = one({armExe, 0x0000'0247U, 4U});
        check(zero.has_value() && !zero->rejected && !zero->hashDef,
              "HASH_DEF with a hash type of 0: accepted, no hash");
    }
    check(one({armExe, 0x0100'0347U, 4U, 0U}).value_or(Pb::ParsedBlock{}).rejected,
          "HASH_DEF of 3 words is rejected");
    check(one({armExe, 0x0100'0247U, 4U}).value_or(rejectedBlock).hashDef, "HASH_DEF SHA-256");
    // VERSION with one OTP row: 2 + (1 + 1 + 1) / 2 = 3 words; rollback 23 fits 24 bits, 24 does not
    check(!one({armExe, 0x0100'0348U, 0x0001'0002U, 0x0100'0017U}).value_or(rejectedBlock).rejected,
          "rollback 23 in one row");
    auto const                         rb = one({armExe, 0x0100'0348U, 0x0001'0002U, 0x0100'0018U});
    constexpr Pb::ParsedBlock::Version oneTwo{.major = 1, .minor = 2};
    check(rb.has_value() && rb->rejected && rb->version == std::optional{oneTwo},
          "rollback 24 in one row is rejected, the version still recorded");
    check(one({
                armExe,
                0x0000'0248U,
                0x0001'0002U
    })
              .value_or(Pb::ParsedBlock{})
              .version
            == std::optional{Pb::ParsedBlock::Version{.major = 1, .minor = 2}},
          "VERSION 1.2");
    // EXTRA_SECURITY needs ENTRY_POINT and VECTOR_TABLE; 14 is no address for either
    constexpr std::uint32_t armExtra = armExe | (0x0800U << 16U);
    check(one({armExtra}).value_or(Pb::ParsedBlock{}).rejected,
          "EXTRA_SECURITY without ENTRY_POINT is rejected");
    check(!one({armExtra, 0x0000'0344U, 0x1000'0101U, 0x2008'0000U, 0x0000'0203U, 0x1000'0000U})
             .value_or(rejectedBlock)
             .rejected,
          "EXTRA_SECURITY with both is accepted");
    check(one({armExe, 0x0000'0203U, 14U}).value_or(Pb::ParsedBlock{}).rejected,
          "VECTOR_TABLE 14 is rejected");
    // a one-word 0x7f item right before LAST ends the item walk exactly there: accepted; one earlier is not
    check(!one({armExe, 0x0000'017fU}).value_or(rejectedBlock).rejected, "0x7f right before LAST");
    // (a 0 word would be a zero-sized item, which ends the ROM's walk of the block: no block at all)
    check(one({armExe, 0x0000'017fU, 0x0000'017eU}).value_or(Pb::ParsedBlock{}).rejected,
          "0x7f before other words");
    // LOAD_MAP: 1 + 3 per entry
    check(!one({armExe, 0x0100'0406U, 0U, 0U, 0U}).value_or(rejectedBlock).rejected,
          "LOAD_MAP one entry");
    check(one({armExe, 0x0100'0306U, 0U, 0U}).value_or(Pb::ParsedBlock{}).rejected,
          "LOAD_MAP short");

    // which IMAGE_DEF: the first executable one is held even rejected; a rejected one later does not replace; one for
    // RISC-V does not replace one for Arm; a data IMAGE_DEF is never taken
    Image img{256};
    img.block(0, {rp2040Exe}, 20);
    img.block(5, {armExe}, 20);
    img.block(10, {armExe | 0x3000'0000U /* chip 3 */}, 20);
    img.block(15, {riscvExe}, 20);
    img.block(20, {0x0002'0142U /* DATA */}, -80);
    auto const chosen = Pb::launchedImageDef(img.bytes());
    check(chosen.has_value() && chosen->offset == 20U && !chosen->rejected,
          "the accepted Arm EXE at 20 wins");
    Image first{64};
    first.block(0, {rp2040Exe}, 0);
    check(Pb::launchedImageDef(first.bytes()).value_or(Pb::ParsedBlock{}).rejected,
          "a lone rejected EXE is chosen");
    Image tb{64};
    tb.block(0, {armExe}, 20);
    tb.block(5, {armExeTbyb}, -20);
    check(Pb::launchedImageDef(tb.bytes()).value_or(Pb::ParsedBlock{}).tbyb,
          "the TBYB one is last");
}

std::string hex(std::span<std::byte const> bytes) {
    std::string out;
    for(auto const b : bytes) {
        constexpr std::string_view digits = "0123456789abcdef";
        out += digits[std::to_integer<unsigned>(b) >> 4U];
        out += digits[std::to_integer<unsigned>(b) & 0xfU];
    }
    return out;
}

std::string sha256Of(std::string_view text) {
    Pb::Sha256 sha{};
    for(auto const c : text) { sha.update(std::array{static_cast<std::byte>(c)}); }
    auto const digest = sha.finish();
    return hex(digest);
}

void sha256Vectors() {
    // FIPS 180-4 / NIST CAVP examples
    check(sha256Of("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "SHA-256 of nothing");
    check(sha256Of("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA-256 of abc");
    check(sha256Of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
            == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "SHA-256 of the 448-bit message");
    check(sha256Of(std::string(1'000'000, 'a'))
            == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
          "SHA-256 of a million a");
}

void picotoolHashes() {
    // picotool hashes its tables and sealed images: the reader's hash must agree, and a byte changed must not
    auto       ab    = readFile("pt_ab.bin");
    auto const table = Pb::walkFlash(ab).loop().partitionTableBlock();
    check(table.has_value() && table->hash == Pb::ParsedBlock::Hash::ok,
          "pt_ab: picotool's hash holds");
    ab[12] ^= std::byte{0x01};   // partition A's location word, inside the hashed words
    auto const broken = Pb::walkFlash(ab);
    check(broken.loop().partitionTableBlock().value_or(Pb::ParsedBlock{}).hash
              == Pb::ParsedBlock::Hash::bad
            && !broken.partitionTable().has_value(),
          "pt_ab with one bit changed: the hash fails, no table loads");

    auto       sealed = readFile("tiny_sealed.bin");
    auto const def    = Pb::launchedImageDef(sealed);
    check(def.has_value() && def->hash == Pb::ParsedBlock::Hash::ok && def->verified(),
          "sealed: the hash over the load map's 1 KiB and the block holds");
    auto image = sealed;
    image[0x20] ^= std::byte{0x80};   // image bytes the load map covers
    check(Pb::launchedImageDef(image).value_or(rejectedBlock).hash == Pb::ParsedBlock::Hash::bad,
          "sealed with an image byte changed: the hash fails");
    auto value = sealed;
    value[0x430] ^= std::byte{0x01};   // inside HASH_VALUE
    check(!Pb::launchedImageDef(value).value_or(Pb::ParsedBlock{}).verified(),
          "sealed with its HASH_VALUE changed: not verified");
    // in pieces of every size, the hash reads too
    std::mt19937 rng{7U};
    for(int i = 0; i < 50; ++i) {
        auto const w = walkInPieces(sealed, (i % 2) == 0, rng);
        check(w.imageDef() == def, "sealed, read in pieces: the same IMAGE_DEF and hash");
    }
    // a real image sealed by picotool: an absolute load map over flash
    auto blink = readFile("blink_sealed.bin");
    check(Pb::launchedImageDef(blink).value_or(rejectedBlock).hash == Pb::ParsedBlock::Hash::ok,
          "a sealed Kvasir image: picotool's hash over its absolute load map holds");
    blink[0x200] ^= std::byte{0x01};
    check(Pb::launchedImageDef(blink).value_or(rejectedBlock).hash == Pb::ParsedBlock::Hash::bad,
          "and fails with one code bit changed");
    // a pico-sdk copy_to_ram image sealed with --clear: a zero-fill entry (hashed as its size word) and a copy into SRAM
    auto ram = readFile("copy_to_ram_sealed.bin");
    check(Pb::launchedImageDef(ram).value_or(rejectedBlock).hash == Pb::ParsedBlock::Hash::ok,
          "copy_to_ram + --clear: picotool's hash over zero fill, RAM copy and flash holds");
    auto ramCopy = ram;
    ramCopy[0x400] ^= std::byte{0x01};   // inside the copy into SRAM's source (0x220..0x1e38)
    check(Pb::launchedImageDef(ramCopy).value_or(rejectedBlock).hash == Pb::ParsedBlock::Hash::bad,
          "and fails with a bit of the RAM copy's source changed");
    // a HASH_VALUE without a SHA-256 HASH_DEF is a hash the ROM cannot check: bad
    Image noDef{64};
    noDef.block(0, {armExe, 0x0000'024bU, 0x1234'5678U}, 0);
    check(Pb::launchedImageDef(noDef.bytes()).value_or(Pb::ParsedBlock{}).hash
            == Pb::ParsedBlock::Hash::bad,
          "HASH_VALUE without HASH_DEF: bad");
}

void slots() {
    auto const v1     = readFile("pt_ab.bin");
    auto const v2     = readFile("pt_ab_v2.bin");
    auto const single = readFile("pt_ab_singleton_nohash.bin");
    auto const flashOf
      = [](std::vector<std::byte> const& slot0, std::vector<std::byte> const& slot1) {
            std::vector<std::byte> flash(std::size_t{3U} * Pb::defaultSlotSize, std::byte{0xff});
            std::ranges::copy(slot0, flash.begin());
            std::ranges::copy(slot1, flash.begin() + Pb::defaultSlotSize);
            return flash;
        };
    auto const both = flashOf(v1, v2);
    auto const w    = Pb::walkFlash(both);
    check(w.done() && w.slot() == 1U
            && w.partitionTable().value_or(Pb::PartitionTable{}).blockOffset == Pb::defaultSlotSize,
          "slot 1's table, the greater version, wins");
    check(Pb::walkFlash(flashOf(v2, v1)).slot() == 0U, "slot 0's, when it has the greater version");
    check(Pb::walkFlash(flashOf(v1, v1)).slot() == 0U, "equal versions: slot 0");
    auto badV2 = v2;
    badV2[12] ^= std::byte{0x01};
    check(Pb::walkFlash(flashOf(v1, badV2)).slot() == 0U, "slot 1's table with a bad hash loses");
    check(Pb::walkFlash(flashOf(single, v2)).slot() == 0U,
          "a verified singleton table in slot 0: slot 1 is not looked at");
    check(Pb::walkFlash(flashOf(v1, {})).slot() == 0U, "nothing in slot 1: slot 0");
    check(Pb::walkFlash(flashOf(v2, v1), {}, Pb::defaultSlotSize).slot() == 1U,
          "a flash update of slot 1 takes it whatever its version");
    check(Pb::walkFlash(flashOf(v1, v2), {}, 0U).slot() == 0U,
          "a flash update of slot 0 keeps it against a greater slot 1");

    // a board without a table: a flat image at 0 that verifies decides alone
    Image flat{2048};
    flat.block(4, {armExe}, 0);
    auto const flatWalk = Pb::walkFlash(flat.bytes());
    check(flatWalk.slot() == 0U && flatWalk.imageDef().has_value()
            && !flatWalk.partitionTable().has_value(),
          "a flat image at 0: slot 0, no table");
}

void chooser() {
    auto const block = [](std::uint16_t major, bool tbyb = false, bool bad = false) {
        Pb::ParsedBlock b{};
        b.imageDef = true;
        b.exe      = true;
        b.tbyb     = tbyb;
        if(major != 0U) { b.version = Pb::ParsedBlock::Version{.major = major, .minor = 0}; }
        b.hash = bad ? Pb::ParsedBlock::Hash::bad : Pb::ParsedBlock::Hash::none;
        return b;
    };
    using O         = std::optional<Pb::ParsedBlock>;
    auto const pick = [](O a, O b, std::optional<unsigned> update = std::nullopt) {
        return Pb::chooseAb(a, b, update);
    };
    check(pick(block(1), block(1)).chosen == 0U, "equal versions: A");
    check(pick(block(1), block(2)).chosen == 1U, "B newer: B");
    check(pick(block(1), block(2, true)).chosen == 0U, "B newer but not bought: A");
    check(pick(block(2, true), block(1)).chosen == 1U, "A not bought: B");
    check(pick(block(1), block(2, false, true)).chosen == 0U, "B newer but its hash fails: A");
    check(pick(block(1, false, true), block(1)).chosen == 1U, "A's hash fails: B");
    check(pick(std::nullopt, block(1)).chosen == 1U && pick(block(1), std::nullopt).chosen == 0U,
          "only one: that one");
    auto const downB = pick(block(2), block(1, true), 1U);
    check(downB.chosen == 1U && downB.eraseOther == std::optional<unsigned>{0U},
          "flash update of the older B: B, A erased on buy");
    auto const upB = pick(block(1), block(2, true), 1U);
    check(upB.chosen == 1U && !upB.eraseOther, "flash update of the newer B: B, nothing erased");
    auto const downA = pick(block(1, true), block(2), 0U);
    check(downA.chosen == 0U && downA.eraseOther == std::optional<unsigned>{1U},
          "flash update of the older A: A, B erased");
    check(pick(block(1, false, true), block(2), 0U).chosen == 1U,
          "flash update of an A that fails its hash: B");
}

constexpr Pb::BootRules a2{.rom = Pb::RomVersion::a2};
constexpr Pb::BootRules a3{.rom = Pb::RomVersion::a3};
constexpr Pb::BootRules a4{};

Pb::Walker walkWith(std::vector<std::byte> const& bytes,
                    Pb::BootRules const&          rules) {
    return Pb::walk(bytes, Pb::Window{.bytes = static_cast<std::uint32_t>(bytes.size())}, rules);
}

/// A block with a hash the ROM revisions compute alike except for a zero-fill entry of `zeroBytes` (A2/A3 hash the size
/// rounded up to a word, A4 refuses a size that is no multiple of 4): IMAGE_TYPE, a relative LOAD_MAP with that one entry,
/// HASH_DEF over words 0..7, HASH_VALUE as the ROM revision `hashAs` computes it.
Image zeroFillImage(std::uint32_t zeroBytes,
                    std::uint32_t hashedSize) {
    Image                      img{64};
    std::vector<std::uint32_t> items{armExe,
                                     0x0100'0406U,   // LOAD_MAP, 4 words, 1 entry, relative
                                     0U,             // storage 0: zero fill
                                     0x2000'0000U,   // runtime: SRAM
                                     zeroBytes,
                                     0x0100'0247U,    // HASH_DEF SHA-256
                                     8U,              // words 0..7 of the block
                                     0x0000'094bU};   // HASH_VALUE, 8 words
    items.insert(items.end(), 8U, 0U);
    img.block(0, items, 0);
    Pb::Sha256 sha{};
    sha.updateWord(hashedSize);
    for(std::size_t i = 0; i < 8U; ++i) { sha.updateWord(img.words[i]); }
    auto const digest = sha.finish();
    for(std::size_t i = 0; i < 8U; ++i) { img.words[9U + i] = Pb::Detail::wordAt(digest, 4U * i); }
    return img;
}

void romVersions() {
    // D1 (A2: varm_blocks.c, `else` where A3 has `else if (!buf_word_count)`): a window read to its end in one go - A2
    // gives up at the next refill though words are left in the buffer
    {
        Image img{128};   // 512 bytes: one read
        img.block(0, {armExe}, 0x100);
        img.block(64, {armExe}, -0x100);
        check(walkWith(img.bytes(), a4).outcome() == Pb::Walker::Outcome::closed,
              "D1: A4 walks a one-read window");
        check(walkWith(img.bytes(), a3).outcome() == Pb::Walker::Outcome::closed, "D1: A3 too");
        check(walkWith(img.bytes(), a2).outcome() == Pb::Walker::Outcome::invalid,
              "D1: A2 calls it invalid");
    }
    // D2: a cycle that never reaches the first block again - A3+ stop at the 256th block, A2 walks it for ever
    {
        Image img{256};
        img.block(0, {armExe}, 20 * 4);
        img.block(20, {armExe}, 20 * 4);
        img.block(40, {armExe}, -20 * 4);   // back to 20: above the first block, so allowed
        check(walkWith(img.bytes(), a4).outcome() == Pb::Walker::Outcome::invalid,
              "D2: A4 invalid at block 256");
        check(walkWith(img.bytes(), a2).outcome() == Pb::Walker::Outcome::hangs, "D2: A2 hangs");
    }
    // D3 (RP2350-E13, fixed in A3): a rejected IMAGE_DEF after a good one replaces it on A2; a rejected one first
    // refuses every later one on A2
    {
        // 256 words: more than one read, so D1 does not end A2's walk first
        Image good{256};
        good.block(0, {armExe}, 20);
        good.block(5, {rp2040Exe}, -20);
        check(!walkWith(good.bytes(), a4).imageDef().value_or(rejectedBlock).rejected,
              "D3: A4 keeps the good one");
        check(walkWith(good.bytes(), a2).imageDef().value_or(Pb::ParsedBlock{}).rejected,
              "D3: A2 takes the rejected one");
        Image bad{256};
        bad.block(0, {rp2040Exe}, 20);
        bad.block(5, {armExe}, -20);
        check(!walkWith(bad.bytes(), a3).imageDef().value_or(rejectedBlock).rejected,
              "D3: A3 replaces a rejected first");
        check(walkWith(bad.bytes(), a2).imageDef().value_or(Pb::ParsedBlock{}).rejected,
              "D3: A2 keeps the rejected first");
    }
    // D4: EXTRA_SECURITY and the 14 marker are A4's
    {
        Image img{64};
        img.block(0, {armExe | (0x0800U << 16U)}, 0);
        check(walkWith(img.bytes(), a4).imageDef().value_or(Pb::ParsedBlock{}).rejected,
              "D4: A4 rejects EXTRA_SECURITY alone");
        check(!walkWith(img.bytes(), a3).imageDef().value_or(rejectedBlock).rejected,
              "D4: A3 ignores the bit");
        Image vt{64};
        vt.block(0, {armExe, 0x0000'0203U, 14U}, 0);
        check(!walkWith(vt.bytes(), a2).imageDef().value_or(rejectedBlock).rejected,
              "D4: VECTOR_TABLE 14 is fine on A2");
    }
    // D5: a load map size that is no multiple of 4
    {
        auto const rounded
          = zeroFillImage(6U, 8U).bytes();   // hashed as A2/A3 do: the size rounded up
        check(walkWith(rounded, a2).imageDef().value_or(rejectedBlock).hash
                == Pb::ParsedBlock::Hash::ok,
              "D5: A2 hashes a 6-byte zero fill as 8");
        check(walkWith(rounded, a3).imageDef().value_or(rejectedBlock).hash
                == Pb::ParsedBlock::Hash::ok,
              "D5: A3 too");
        check(walkWith(rounded, a4).imageDef().value_or(rejectedBlock).hash
                == Pb::ParsedBlock::Hash::bad,
              "D5: A4 refuses the size");
        auto const exact = zeroFillImage(8U, 8U).bytes();
        check(walkWith(exact, a2).imageDef().value_or(rejectedBlock).hash
                  == Pb::ParsedBlock::Hash::ok
                && walkWith(exact, a4).imageDef().value_or(rejectedBlock).hash
                     == Pb::ParsedBlock::Hash::ok,
              "D5: a multiple of 4 is the same on all");
        // a copy entry of 6 bytes: A2/A3 mask the last word with an out-of-range shift - unchecked
        Image copy{64};
        copy.block(
          0,
          {armExe, 0x0100'0406U, 0U - 4U, 0x1000'0000U, 6U, 0x0100'0247U, 8U, 0x0000'024bU, 0U},
          0);
        check(walkWith(copy.bytes(), a2).imageDef().value_or(rejectedBlock).hash
                == Pb::ParsedBlock::Hash::unchecked,
              "D5: A2's 6-byte copy entry is unchecked");
        check(walkWith(copy.bytes(), a4).imageDef().value_or(rejectedBlock).hash
                == Pb::ParsedBlock::Hash::bad,
              "D5: A4 refuses it");
    }
}

void otpRules() {
    auto const          hashed   = readFile("pt_ab.bin");
    auto const          unhashed = readFile("pt_ab_singleton_nohash.bin");
    Pb::BootRules const hashedPt{.hashedPartitionTable = true};
    check(Pb::partitionTable(hashed, hashedPt).has_value()
            && !Pb::partitionTable(unhashed, hashedPt).has_value()
            && Pb::partitionTable(unhashed).has_value(),
          "HASHED_PARTITION_TABLE: a table without a hash is not loaded");
    check(!Pb::partitionTable(hashed, Pb::BootRules{.singleFlashBinary = true}).has_value(),
          "SINGLE_FLASH_BINARY: no table looked for");
    Image plain{64};
    plain.block(0, {armExe}, 0);
    check(walkWith(plain.bytes(), Pb::BootRules{.rollbackRequired = true})
            .imageDef()
            .value_or(Pb::ParsedBlock{})
            .rejected,
          "ROLLBACK_REQUIRED: an IMAGE_DEF without a rollback version is rejected");
    Image rollback{64};
    rollback.block(0, {armExe, 0x0100'0348U, 0x0001'0002U, 0x0100'0005U}, 0);
    check(!walkWith(rollback.bytes(), Pb::BootRules{.rollbackRequired = true})
             .imageDef()
             .value_or(rejectedBlock)
             .rejected,
          "ROLLBACK_REQUIRED: one with rollback version 5 is fine");

    auto const             v1 = readFile("pt_ab.bin");
    auto const             v2 = readFile("pt_ab_v2.bin");
    std::vector<std::byte> flash(std::size_t{4} * Pb::defaultSlotSize, std::byte{0xff});
    std::ranges::copy(v1, flash.begin());
    std::ranges::copy(v2, flash.begin() + (std::ptrdiff_t{2} * Pb::defaultSlotSize));
    check(Pb::walkFlash(flash).slot() == 0U, "slot size 4 KiB: the table at 8 KiB is not slot 1");
    check(Pb::walkFlash(flash, Pb::BootRules{.slotSize = 2U * Pb::defaultSlotSize}).slot() == 1U,
          "FLASH_PARTITION_SLOT_SIZE 8 KiB: it is, and wins");
    std::ranges::copy(v1, flash.begin() + Pb::defaultSlotSize);   // slot 1 at 4 KiB: the older one
    std::ranges::copy(v2, flash.begin());
    check(
      Pb::walkFlash(flash, {}, Pb::defaultSlotSize).slot() == 1U
        && Pb::walkFlash(flash, Pb::BootRules{.watchdogScratchDisabled = true}, Pb::defaultSlotSize)
               .slot()
             == 0U,
      "DISABLE_WATCHDOG_SCRATCH: the flash update is never seen");

    // a secured chip: no signature = not verified, a signature = the ROM's ECDSA decides (not done here)
    Pb::BootRules const secure{.secure = true};
    check(walkWith(plain.bytes(), secure).imageDef().value_or(Pb::ParsedBlock{}).hash
            == Pb::ParsedBlock::Hash::bad,
          "secure: an unsigned IMAGE_DEF does not verify");
    std::vector<std::uint32_t> signedItems{armExe, 0x0100'2109U};   // SIGNATURE secp256k1, 33 words
    signedItems.insert(signedItems.end(), 32U, 0U);
    Image signedImg{128};
    signedImg.block(0, signedItems, 0);
    auto const signedDef = walkWith(signedImg.bytes(), secure).imageDef();
    check(signedDef.has_value() && signedDef->hash == Pb::ParsedBlock::Hash::unchecked
            && !signedDef->decided(),
          "secure: a signed IMAGE_DEF is unchecked");
}

/// A PARTITION_TABLE block without hash: `entries` as {first sector, last sector, flags}.
void tableAt(Image&                            img,
             std::size_t                       at,
             std::vector<std::array<std::uint32_t,
                                    3>> const& entries) {
    std::vector<std::uint32_t> items{
      0x0aU | ((2U + (2U * static_cast<std::uint32_t>(entries.size()))) << 8U)
        | (static_cast<std::uint32_t>(entries.size()) << 24U),
      0xfc00'8000U};
    for(auto const& e : entries) {
        items.push_back(0xfc00'0000U | e[0] | (e[1] << 13U));
        items.push_back(0xfc00'0000U | e[2]);
    }
    img.block(at, items, 0);
}

void bootOrder() {
    using K                          = Pb::BootPrediction::Kind;
    constexpr std::uint32_t linkToA0 = (1U << 1U) | (0U << 3U);   // link type A_PARTITION, value 0
    constexpr std::uint32_t ignored  = 0x200U;
    auto const flashWith = [](std::vector<std::array<std::uint32_t, 3>> const& entries) {
        Image img{std::size_t{6} * 1024U};   // 24 KiB: six sectors
        tableAt(img, 0, entries);
        return img;
    };
    auto const imageIn
      = [](Image& img, std::uint32_t sector, std::uint32_t type, std::int32_t extraLink = 0) {
            (void)extraLink;
            img.block(std::size_t{sector} * 1024U, {type}, 0);
        };
    {
        auto img = flashWith({
          {1, 2,        0},
          {3, 4, linkToA0}
        });
        imageIn(img, 1, armExe);
        auto const p = Pb::predictBoot(img.bytes());
        check(p.kind == K::partition && p.partition == std::optional<unsigned>{0U},
              "boot: A's image");
        auto both = flashWith({
          {1, 2,        0},
          {3, 4, linkToA0}
        });
        imageIn(both, 3, armExe);
        check(Pb::predictBoot(both.bytes()).partition == std::optional<unsigned>{1U},
              "boot: B's when A is empty");
    }
    {
        auto img = flashWith({
          {1, 2, ignored},
          {3, 4,       0}
        });
        imageIn(img, 1, armExe);
        imageIn(img, 3, armExe);
        check(Pb::predictBoot(img.bytes()).partition == std::optional<unsigned>{1U},
              "boot: a partition IGNORED_DURING_ARM_BOOT is skipped");
    }
    {
        auto img = flashWith({
          {1, 2, 0},
          {3, 4, 0}
        });
        imageIn(img, 1, armExeTbyb);
        imageIn(img, 3, armExe);
        check(Pb::predictBoot(img.bytes()).partition == std::optional<unsigned>{1U},
              "boot: an unbought TBYB image is refused, the next partition boots");
        check(Pb::predictBoot(img.bytes(), {}, 4096U).partition == std::optional<unsigned>{0U},
              "boot: the same image as the flash update boots");
    }
    {
        auto img = flashWith({
          {1, 2, 0},
          {3, 4, 0}
        });
        imageIn(img, 1, 0x1011'0142U);   // Arm, Non-secure
        imageIn(img, 3, riscvExe);
        check(Pb::predictBoot(img.bytes()).kind == K::otherArchitecture,
              "boot: NS refused, RISC-V switches the CPU");
        check(Pb::predictBoot(img.bytes(), Pb::BootRules{.autoSwitchArchDisabled = true}).kind
                == K::nothing,
              "boot: with DISABLE_AUTO_SWITCH_ARCH nothing boots");
    }
    {
        auto img = flashWith({
          {1,    2, 0},
          {3, 8191, 0}
        });   // the second ends past 16 MiB
        imageIn(img, 3, armExe);
        check(Pb::predictBoot(img.bytes()).kind == K::nothing,
              "boot: a partition ending past 16 MiB is skipped");
    }
    {
        Image flat{2048};
        flat.block(4, {armExe}, 0);
        auto const p = Pb::predictBoot(flat.bytes());
        check(p.kind == K::flashImage && p.slot == 0U, "boot: a flat image");
    }
    {
        auto img = flashWith({
          {1, 2,        0},
          {3, 4, linkToA0}
        });
        img.block(std::size_t{1} * 1024U, {armExe, 0x0000'0248U, 0x0001'0000U}, 0);   // A: 1.0
        img.block(std::size_t{3} * 1024U, {armExeTbyb}, 0);   // B: 0.0, TBYB, the flash update
        auto const p = Pb::predictBoot(img.bytes(), {}, 3U * 4096U);
        check(p.partition == std::optional<unsigned>{1U}
                && p.eraseOther == std::optional<unsigned>{0U},
              "boot: a downgrade's flash update boots B and will erase A");
    }
}

/// A random loop: 1..6 blocks of random items at random places, links that mostly close it.
Image randomLoop(std::mt19937& rng) {
    auto const                 n = 64U + (rnd(rng) % 2400U);
    Image                      img{n, (rnd(rng) % 4U) == 0U ? 0xffff'ffffU : 0U};
    std::vector<std::uint32_t> at;
    for(auto k = 1U + (rnd(rng) % 6U); k != 0U; --k) { at.push_back(rnd(rng) % (n - 8U)); }
    std::ranges::sort(at);
    for(std::size_t k = 0; k < at.size(); ++k) {
        std::vector<std::uint32_t> items;
        switch(rnd(rng) % 7U) {
        case 0: items = {armExe}; break;
        case 1: items = {armExeTbyb, 0x0000'0348U, rnd(rng)}; break;
        case 2: items = {riscvExe}; break;
        case 3:
            items = {0x0200'0c0aU,
                     0xfc00'8000U,
                     0xfc40'0002U,
                     0xfc06'0000U,
                     0xfc7f'e201U,
                     0xfc06'0000U};
            break;
        case 4: items = {rp2040Exe}; break;
        case 5:
            items = {armExe, 0x0100'0247U, 4U, 0x0000'034bU, rnd(rng), rnd(rng)};
            break;   // a (wrong) hash
        default: items = {}; break;
        }
        auto const fill = (rnd(rng) % 4U) != 0U ? rnd(rng) % 20U : rnd(rng) % 170U;
        if(at[k] + items.size() + fill + 4U >= n) { continue; }
        items           = filler(items, fill);
        auto const next = at[(k + 1U) % at.size()];
        auto link = static_cast<std::int32_t>(next) * 4 - static_cast<std::int32_t>(at[k]) * 4;
        if(rnd(rng) % 8U == 0U) { link += static_cast<std::int32_t>(rnd(rng) % 7U) - 3; }
        img.block(at[k], items, link);
    }
    if(rnd(rng) % 4U == 0U) { img.words[rnd(rng) % n] = Pb::picobinBlockMarkerStart; }
    return img;
}

void walkerAgainstMemory() {
    std::mt19937 rng{20261102U};
    int          launched{};
    for(int it = 0; it < 20'000; ++it) {
        auto img = randomLoop(rng);
        // random damage: a byte here and there
        if(it % 3 == 0) {
            for(auto k = rnd(rng) % 4U; k != 0U; --k) {
                auto& w = img.words[rnd(rng) % img.words.size()];
                w ^= 0xffU << (8U * (rnd(rng) % 4U));
            }
        }
        auto const bytes = img.bytes();
        // every third loop with the A2 ROM's rules: its own refill and prefer paths
        auto const rules
          = (it % 3) == 1 ? Pb::BootRules{.rom = Pb::RomVersion::a2} : Pb::BootRules{};
        auto const whole  = Pb::walk(bytes, Pb::Window{}, rules);
        auto const pieces = walkInPieces(bytes, true, rng, rules);
        check(whole.outcome() == pieces.outcome() && whole.imageDef() == pieces.imageDef()
                && whole.partitionTable() == pieces.partitionTable()
                && whole.firstBlock() == pieces.firstBlock(),
              "Walker in pieces = the walk over memory");
        if(auto const def = whole.imageDef()) {
            ++launched;
            check(def->offset % 4U == 0U && def->offset < bytes.size() && def->imageDef && def->exe,
                  "a launched IMAGE_DEF is an executable one inside the window");
        }
        check(whole.outcome() != Pb::Walker::Outcome::running, "the walk ends");
    }
    std::printf("walker: 20000 loops, %d with an IMAGE_DEF launched\n", launched);
    check(launched > 2000, "the generator makes enough launchable loops");
}
}   // namespace

int main() {
    sha256Vectors();
    picotoolTables();
    picotoolSealed();
    picotoolHashes();
    slots();
    chooser();
    romVersions();
    otpRules();
    bootOrder();
    romEdges();
    parser();
    walkerAgainstMemory();
    std::printf("reader: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
