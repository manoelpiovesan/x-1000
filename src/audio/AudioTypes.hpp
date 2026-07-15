#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace xpad::audio {

struct AudioConfig {
    std::string deviceName{};
    std::uint32_t sampleRate{48000};
    std::uint32_t bufferSizeFrames{256};
    std::uint8_t channels{2};
};

} // namespace xpad::audio

