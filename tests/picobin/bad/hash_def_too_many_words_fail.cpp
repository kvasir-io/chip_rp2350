// expect: BlockWordsHashed cannot exceed maximum block size
// A HASH_DEF that hashes 161 words (644 bytes): more than any block can hold (RP2350 DS 5.9.1, 5.9.2.2).
#include <chip/picobin.hpp>

auto const& tooMany = Kvasir::Picobin::HashDef<161>::words;
