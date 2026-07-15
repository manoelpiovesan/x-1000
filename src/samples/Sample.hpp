#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <atomic>

namespace xpad::samples {

enum class PadMode {
    OneShot,
    Loop,
    Retrigger,
    Hold
};

struct SampleMetadata {
    std::string name{};
    std::string filePath{};
    double referenceBpm{0.0};  // 0 = not beat-synced
    PadMode mode{PadMode::OneShot};
    double volume{1.0};
    double quantization{0.25}; // 1/4 beat by default
};

struct Sample {
    SampleMetadata meta{};
    std::vector<float> data{};   // interleaved stereo float32
    std::uint32_t sampleRate{48000};
    std::uint32_t channels{2};
    std::uint64_t frameCount{0};

    [[nodiscard]] bool loaded() const noexcept { return !data.empty(); }
    [[nodiscard]] double durationSeconds() const noexcept {
        return sampleRate > 0 ? static_cast<double>(frameCount) / sampleRate : 0.0;
    }
};

} // namespace xpad::samples

