#pragma once

// The small JUCE surface the OB-Xd 2.10 DSP headers (vendor/obxd/Source/Engine)
// use, implemented without JUCE. Only the engine includes this, through
// patches/obxd/0001-engine-juce-free-include.patch; the plugin itself speaks
// the CLAP C ABI directly.
//
// Behaviour notes:
// - Random is JUCE's 48-bit LCG with the same nextInt()/nextFloat() mapping.
// - Random::getSystemRandom() and default-constructed Random are seeded from a
//   fixed value rather than the wall clock, so a WCLAP render is reproducible.
//   Each voice still draws its own detune/noise stream from that sequence.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

namespace juce {

using int32 = int32_t;
using int64 = int64_t;
using uint32 = uint32_t;
using uint64 = uint64_t;
using String = std::string;

constexpr float float_Pi = 3.14159265358979323846f;
constexpr double double_Pi = 3.14159265358979323846;

template <typename T> constexpr T jmin(T a, T b) { return b < a ? b : a; }
template <typename T> constexpr T jmax(T a, T b) { return a < b ? b : a; }
template <typename T> constexpr T jlimit(T lower, T upper, T value) {
    return value < lower ? lower : (upper < value ? upper : value);
}
inline void zeromem(void *memory, size_t numBytes) noexcept { std::memset(memory, 0, numBytes); }

template <typename T> int roundToInt(T value) noexcept {
    return static_cast<int>(std::lround(static_cast<double>(value)));
}

class Random {
  public:
    static constexpr int64 kDefaultSeed = 0x4f42586400LL; // "OBXd"

    explicit Random(int64 seedValue) noexcept : seed_(seedValue) {}
    Random() noexcept : seed_(getSystemRandom().nextInt64()) {}

    int nextInt() noexcept {
        seed_ = static_cast<int64>(((static_cast<uint64>(seed_) * 0x5deece66dULL) + 11) &
                                   0xffffffffffffULL);
        return static_cast<int>(seed_ >> 16);
    }

    int64 nextInt64() noexcept {
        return static_cast<int64>((static_cast<uint64>(static_cast<uint32>(nextInt())) << 32) |
                                  static_cast<uint64>(static_cast<uint32>(nextInt())));
    }

    float nextFloat() noexcept {
        const float result = static_cast<float>(static_cast<uint32>(nextInt())) /
                             (static_cast<float>(std::numeric_limits<uint32>::max()) + 1.0f);
        return result == 1.0f ? 1.0f - std::numeric_limits<float>::epsilon() : result;
    }

    static Random &getSystemRandom() noexcept {
        static Random system(kDefaultSeed);
        return system;
    }

  private:
    int64 seed_;
};

} // namespace juce

using namespace juce;
