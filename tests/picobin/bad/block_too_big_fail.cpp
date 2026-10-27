// expect: 640 for a PARTITION_TABLE
// A block of 161 words (644 bytes) whose first item is no IMAGE_DEF: one word over 640 bytes (RP2350 DS 5.9.1).
#include <chip/picobin.hpp>

namespace {
template<unsigned N>
struct Filler {
    static constexpr std::array<std::uint32_t, N> words{};
};
}   // namespace

auto const& tooBig = Kvasir::Picobin::BlockLoop<
  Kvasir::Picobin::Block<Kvasir::Picobin::Version<1, 0>, Filler<155>>>::words;
