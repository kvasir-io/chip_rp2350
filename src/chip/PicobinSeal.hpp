#pragma once
// Sealing an image, the counterpart of PicobinReader.hpp: what `picotool seal --hash --major M --minor N` does to an
// image, for an RP2350 A/B partition update - with the TBYB flag set if asked. It makes a development image that carries
// no version one the boot ROM keeps as an A/B update.
//
// The image is the flash bytes from the first byte of the partition it will run from, linked to run with that byte at
// the XIP base (0x10000000, the boot ROM maps a partition there; a UF2 converted at 0x10000000, laid out flat). Sealing
// leaves it partition-independent: the load map's absolute addresses are runtime ones (PicobinReader.hpp startEntry:
// the boot ROM rolls them by the partition's place). sealOf():
// - walks it as the boot ROM launches it (Walker, the partition's own search: no PARTITION_TABLE blocks) and takes
//   the IMAGE_DEF it would launch. One with a SIGNATURE is refused: a signed image is the signer's to seal;
// - makes one block for after the image's last byte, word aligned: IMAGE_TYPE (the launched one's, TBYB as asked), the
//   launched one's items that describe how it starts (VECTOR_TABLE, ENTRY_POINT, ROLLING_WINDOW_DELTA), VERSION, a
//   LOAD_MAP - the launched one's, made absolute, or else one absolute entry over the whole image as it runs from
//   flash (with a positive ROLLING_WINDOW_DELTA its mapped part) - then HASH_DEF and HASH_VALUE (SHA-256 over the load map's sources and the block up to the HASH_VALUE, the
//   TBYB bit cleared as the boot ROM clears it), LAST;
// - links it into the loop: the loop's last block links to it (one word of the image changes), it links back to the
//   first. The boot ROM launches the last executable IMAGE_DEF it sees in a loop (5.1.5.2): the new one.
// No allocation and constexpr: a firmware can seal what it writes into a partition, word by word (Seal::wordOf).
// seal() (host: std::vector) returns the sealed bytes, and only once a walk of them finds the new block to be what the
// boot ROM would launch, verified, with the version and TBYB asked for; sealOf() leaves that walk to its caller.
#include "PicobinReader.hpp"
#include "picobin.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

namespace Kvasir { namespace Picobin {

    struct SealRequest {
        ParsedBlock::Version version;
        bool                 tbyb{true};
    };

    enum class SealError : std::uint8_t {
        noImageDef,   ///< no closed block loop with an executable IMAGE_DEF the boot ROM would launch
        rejected,     ///< the boot ROM's parser refuses the IMAGE_DEF it would launch
        signedImage,   ///< it carries a SIGNATURE: sealing would drop it
        badLoadMap,    ///< its LOAD_MAP is malformed, or a source lies outside the image
        tooBig,        ///< the new block would exceed the boot ROM's IMAGE_DEF limit
        verifyFailed,   ///< seal(): the result is not what the boot ROM would launch as asked (a bug here)
    };

    /// What sealing changes: one word of the image (the loop's last block's link) and the block appended behind it.
    struct Seal {
        static constexpr std::uint32_t maxWords = picobinMaxImageDefBlockSize / 4U;

        std::uint32_t linkAt{};        ///< the changed word's byte offset in the image
        std::uint32_t link{};          ///< its new value
        std::uint32_t blockOffset{};   ///< the new block's start: the image's end, word aligned
        std::array<std::uint32_t, maxWords>
                      words{};   ///< the new block, start marker to end marker
        std::uint32_t wordCount{};
        std::uint32_t digestAt{};   ///< the HASH_VALUE's first digest word, an index of words

        [[nodiscard]] constexpr std::span<std::uint32_t const> block() const {
            return std::span{words}.first(wordCount);
        }

        /// The sealed image's size in bytes.
        [[nodiscard]] constexpr std::uint32_t bytes() const {
            return blockOffset + (4U * wordCount);
        }

        /// The sealed image's word at byte `at` (a multiple of 4, below bytes()), `image` the bytes sealOf() was given:
        /// the image's words, the new link, 0xff padding up to the block, the block.
        [[nodiscard]] constexpr std::uint32_t wordOf(std::span<std::byte const> image,
                                                     std::uint32_t              at) const {
            if(at == linkAt) { return link; }
            if(at >= blockOffset) { return words[(at - blockOffset) / 4U]; }
            std::uint32_t w{};
            for(std::uint32_t i = 0; i < 4U; ++i) {
                std::uint32_t const b
                  = at + i < image.size() ? std::to_integer<std::uint32_t>(image[at + i]) : 0xffU;
                w |= b << (8U * i);
            }
            return w;
        }
    };

    namespace Detail { namespace Sealing {
        inline constexpr std::uint32_t loadMapAbsolute   = 0x8000'0000U;
        inline constexpr unsigned      loadMapCountShift = 24U;
        inline constexpr std::uint32_t loadMapCountMask  = 0x7fU;

        /// A block of the loop: where it starts, where its items are (the words between the start marker and LAST),
        /// where its link word is, and where it links to.
        struct LoopBlock {
            std::uint32_t offset{};
            std::uint32_t itemsAt{};
            std::uint32_t itemWords{};
            std::uint32_t linkAt{};
            std::int64_t  next{};
        };

        /// The block starting at `offset`: nullopt when it is no well-formed block (the Walker said the loop is
        /// closed, so this only guards the reads).
        [[nodiscard]] constexpr std::optional<LoopBlock> blockAt(std::span<std::byte const> image,
                                                                 std::uint32_t offset) {
            auto const words = [&](std::uint64_t at) -> std::optional<std::uint32_t> {
                if(at + 4U > image.size()) { return std::nullopt; }
                return wordAt(image, static_cast<std::size_t>(at));
            };
            if(words(offset) != picobinBlockMarkerStart) { return std::nullopt; }
            LoopBlock     block{.offset = offset, .itemsAt = offset + 4U};
            std::uint64_t at = block.itemsAt;
            while(true) {
                auto const header = words(at);
                if(!header) { return std::nullopt; }
                auto const size = itemWords(*header);
                if((*header & itemTypeMask) == code(ItemType::last)) {
                    if(size != block.itemWords) { return std::nullopt; }
                    block.linkAt    = static_cast<std::uint32_t>(at + 4U);
                    auto const link = words(block.linkAt);
                    if(!link || words(block.linkAt + 4U) != picobinBlockMarkerEnd) {
                        return std::nullopt;
                    }
                    block.next = std::int64_t{offset} + static_cast<std::int32_t>(*link);
                    return block;
                }
                if(size == 0U || block.itemWords + size > blockBufferWords) { return std::nullopt; }
                block.itemWords += size;
                at += std::uint64_t{4U} * size;
            }
        }

        /// The block being built, refusing to grow past the boot ROM's IMAGE_DEF limit.
        struct Builder {
            std::array<std::uint32_t, Picobin::Seal::maxWords> words{};
            std::uint32_t                                      count{};

            constexpr bool push(std::uint32_t w) {
                if(count == words.size()) { return false; }
                words[count++] = w;
                return true;
            }
        };

        /// SHA-256 over the sealed image's bytes [from, from + size), the link changed, 0xff past the image.
        constexpr void hashImage(Sha256&                    sha,
                                 std::span<std::byte const> image,
                                 Picobin::Seal const&       s,
                                 std::uint32_t              from,
                                 std::uint32_t              size) {
            std::uint32_t const end = from + size;
            std::uint32_t       at  = from;
            auto const          run = [&](std::uint32_t to) {   // the image's own bytes up to `to`
                auto const stop = std::min<std::uint64_t>(to, image.size());
                if(at < stop) {
                    sha.update(image.subspan(at, static_cast<std::size_t>(stop - at)));
                    at = static_cast<std::uint32_t>(stop);
                }
            };
            if(s.linkAt >= at && s.linkAt < end) {
                run(s.linkAt);
                // the bytes of the link word inside the range (it is word aligned, and so are the sources)
                for(std::uint32_t i = 0; i < 4U && at < end; ++i, ++at) {
                    sha.update(std::array{static_cast<std::byte>((s.link >> (8U * i)) & 0xffU)});
                }
            }
            run(end);
            for(; at < end; ++at) {
                sha.update(std::array{std::byte{0xff}});
            }   // the padding up to the block
        }
    }}   // namespace Detail::Sealing

    /// What sealing `image` changes (see the header): its new link and the block behind it. Not walked afterwards:
    /// seal() does that, a firmware walks what it wrote.
    [[nodiscard]] constexpr std::expected<Seal,
                                          SealError>
    sealOf(std::span<std::byte const> image,
           SealRequest const&         request) {
        namespace S = Detail::Sealing;
        using Detail::code;
        using Detail::itemWords;
        using Detail::wordAt;

        auto const walked   = walk(image,
                                   Window{.bytes = static_cast<std::uint32_t>(
                                            std::min<std::size_t>(image.size(), 0xffff'fffcU))},
                                   BootRules{},
                                   false);
        auto const launched = walked.imageDef();
        auto const first    = walked.firstBlock();
        if(!launched || !first || !launched->exe) { return std::unexpected{SealError::noImageDef}; }
        if(launched->rejected) { return std::unexpected{SealError::rejected}; }
        if(launched->signature) { return std::unexpected{SealError::signedImage}; }

        // The loop, first to last: the launched IMAGE_DEF's block and the last one.
        std::optional<S::LoopBlock> imageDef;
        S::LoopBlock                last{};
        std::uint32_t               blocks = 0;
        for(std::int64_t at = *first;;) {
            if(at < 0 || ++blocks > maxLoopBlocks) {
                return std::unexpected{SealError::noImageDef};
            }
            auto const block = S::blockAt(image, static_cast<std::uint32_t>(at));
            if(!block) { return std::unexpected{SealError::noImageDef}; }
            if(block->offset == launched->offset) { imageDef = block; }
            last = *block;
            at   = block->next;
            if(at == std::int64_t{*first}) { break; }
        }
        if(!imageDef) { return std::unexpected{SealError::noImageDef}; }
        auto const item
          = [&](std::uint32_t i) { return wordAt(image, imageDef->itemsAt + (4U * i)); };

        Seal out{};
        out.blockOffset = static_cast<std::uint32_t>((image.size() + 3U) & ~std::size_t{3U});
        out.linkAt      = last.linkAt;
        out.link        = out.blockOffset - last.offset;
        // The runtime address of the image's byte `o` is runtimeBase + o: the XIP base, less the rolling window's delta
        // (startEntry's roll; RP2350 datasheet 5.1.4, md l.18923-18925: a delta of 0x1000 maps partition offset 0x1000
        // to 0x10000000, and what lies before it is not mapped). A positive delta: the image's mapped part starts there.
        std::uint32_t const delta       = launched->rollingWindowDelta;
        std::uint32_t const runtimeBase = xipBaseAddress - delta;
        std::uint32_t const mappedFrom  = static_cast<std::int32_t>(delta) > 0 ? delta : 0U;

        S::Builder b{};
        bool       fits = b.push(picobinBlockMarkerStart);
        // IMAGE_TYPE: the launched one's, TBYB as asked
        auto const imageType = item(0U);
        fits                 = fits
                            && b.push(request.tbyb ? imageType | (std::uint32_t{imageTypeTbyb} << 16U)
                                                   : imageType & ~(std::uint32_t{imageTypeTbyb} << 16U));
        // its other items: those that say how it starts kept, the load map made absolute, the rest written anew or left
        // out (VERSION, HASH_DEF, HASH_VALUE, SIGNATURE - refused above -, SALT, IGNORED)
        std::uint32_t loadMapAt
          = 0U;   // the new block's LOAD_MAP header, a word index; 0: none taken
        for(std::uint32_t i = itemWords(imageType); i < imageDef->itemWords;) {
            auto const header = item(i);
            auto const size   = itemWords(header);
            auto const type   = header & Detail::itemTypeMask;
            if(type == code(ItemType::vectorTable) || type == code(ItemType::entryPoint)
               || type == code(ItemType::rollingWindow))
            {
                for(std::uint32_t w = 0; w < size; ++w) { fits = fits && b.push(item(i + w)); }
            } else if(type == code(ItemType::loadMap)) {
                auto const count = (header >> S::loadMapCountShift) & S::loadMapCountMask;
                if(size != 1U + (3U * count)) { return std::unexpected{SealError::badLoadMap}; }
                bool const absolute = (header & S::loadMapAbsolute) != 0U;
                // the item's own runtime address: a relative entry's storage counts from its header
                std::uint32_t const itemAddress = runtimeBase + imageDef->itemsAt + (4U * i);
                loadMapAt                       = b.count;
                fits                            = fits && b.push(header | S::loadMapAbsolute);
                for(std::uint32_t e = 0; e < count; ++e) {
                    auto const storage = item(i + 1U + (3U * e));
                    auto const runtime = item(i + 2U + (3U * e));
                    auto const third   = item(i + 3U + (3U * e));
                    auto const bytes   = absolute ? third - runtime : third;
                    fits               = fits
                                      && b.push(storage == 0U || absolute ? storage : storage + itemAddress)
                                      && b.push(runtime) && b.push(runtime + bytes);
                }
            }
            i += size;
        }
        if(loadMapAt == 0U) {
            // A flash image without one: hash the whole image as it runs, in place - its mapped part.
            if(mappedFrom >= out.blockOffset) { return std::unexpected{SealError::badLoadMap}; }
            loadMapAt = b.count;
            fits      = fits
                     && b.push(code(ItemType::loadMap) | (4U << 8U) | (1U << S::loadMapCountShift)
                               | S::loadMapAbsolute)
                     && b.push(runtimeBase + mappedFrom) && b.push(runtimeBase + mappedFrom)
                     && b.push(runtimeBase + out.blockOffset);
        }
        fits = fits && b.push(code(ItemType::version) | (2U << 8U))
            && b.push((std::uint32_t{request.version.major} << 16U) | request.version.minor);
        // HASH_DEF: SHA-256 over the block's words up to the HASH_VALUE (the start marker is word 0)
        std::uint32_t const hashDefWords = b.count + 2U;
        fits                             = fits
                                        && b.push(code(ItemType::hashDef) | (2U << 8U)
                                                  | (std::uint32_t{static_cast<std::uint16_t>(HashAlgorithm::sha256)} << 24U))
                                        && b.push(hashDefWords) && b.push(code(ItemType::hashValue) | (9U << 8U));
        out.digestAt                     = b.count;
        for(std::uint32_t w = 0; w < 8U; ++w) { fits = fits && b.push(0U); }
        std::uint32_t const items = b.count - 1U;
        fits = fits && b.push(0xffU | (items << 8U))   // LAST: the size of the items before it
            && b.push(static_cast<std::uint32_t>(static_cast<std::int32_t>(*first)
                                                 - static_cast<std::int32_t>(out.blockOffset)))
            && b.push(picobinBlockMarkerEnd);
        if(!fits || (4U * b.count) > picobinMaxImageDefBlockSize) {
            return std::unexpected{SealError::tooBig};
        }
        out.words     = b.words;
        out.wordCount = b.count;

        // The hash, as the boot ROM takes it (Walker's hash phase): each entry's source, a zero fill's size, then the
        // block's words with the TBYB bit cleared.
        Sha256     sha{};
        auto const count = (out.words[loadMapAt] >> S::loadMapCountShift) & S::loadMapCountMask;
        for(std::uint32_t e = 0; e < count; ++e) {
            auto const storage = out.words[loadMapAt + 1U + (3U * e)];
            auto const runtime = out.words[loadMapAt + 2U + (3U * e)];
            auto const bytes   = out.words[loadMapAt + 3U + (3U * e)] - runtime;
            if(storage == 0U) {
                sha.updateWord(bytes);
                continue;
            }
            auto const from = storage - runtimeBase;
            if(storage < runtimeBase || std::uint64_t{from} + bytes > out.blockOffset) {
                return std::unexpected{SealError::badLoadMap};
            }
            S::hashImage(sha, image, out, from, bytes);
        }
        for(std::uint32_t w = 0; w < hashDefWords; ++w) {
            sha.updateWord(w == 1U ? out.words[1] & ~(std::uint32_t{imageTypeTbyb} << 16U)
                                   : out.words[w]);
        }
        auto const digest = sha.finish();
        for(std::size_t w = 0; w < 8U; ++w) {
            out.words[out.digestAt + w] = wordAt(std::span<std::byte const>{digest}, 4U * w);
        }
        return out;
    }

    /// `image` sealed as the boot ROM's A/B update (see the header): the new bytes, longer by the appended block -
    /// once a walk of them finds what was asked for.
    [[nodiscard]] inline std::expected<std::vector<std::byte>,
                                       SealError>
    seal(std::span<std::byte const> image,
         SealRequest const&         request) {
        auto const s = sealOf(image, request);
        if(!s) { return std::unexpected{s.error()}; }
        std::vector<std::byte> out(s->bytes());
        for(std::uint32_t at = 0; at < s->bytes(); at += 4U) {
            auto const w = s->wordOf(image, at);
            for(std::uint32_t i = 0; i < 4U; ++i) {
                out[at + i] = static_cast<std::byte>((w >> (8U * i)) & 0xffU);
            }
        }

        // What the boot ROM makes of it.
        auto const check = walk(std::span<std::byte const>{out},
                                Window{.bytes = static_cast<std::uint32_t>(out.size())},
                                BootRules{},
                                false)
                             .imageDef();
        if(!check || check->offset != s->blockOffset || !check->verified()
           || check->hash != ParsedBlock::Hash::ok || check->version != request.version
           || check->tbyb != request.tbyb)
        {
            return std::unexpected{SealError::verifyFailed};
        }
        return out;
    }

}}   // namespace Kvasir::Picobin
