// expect: 384 bytes for an IMAGE_DEF
// An IMAGE_DEF block of 97 words (388 bytes): one word over the boot ROM's limit (RP2350 DS 5.9.1).
#include <chip/picobin.hpp>

namespace {
template<unsigned N>
struct Filler {
    static constexpr std::array<std::uint32_t, N> words{};
};
}   // namespace

auto const& tooBig = Kvasir::Picobin::BlockLoop<
  Kvasir::Picobin::Block<Kvasir::Picobin::ArmSecure, Filler<92>>>::words;
