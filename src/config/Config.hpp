#pragma once
#include "audio/AudioScheduler.hpp"
#include "midi/MidiManager.hpp"
#include <string>
#include <vector>

namespace xpad::config {

struct XPadConfig {
    // Audio
    std::string audioDevice{};
    std::uint32_t sampleRate{48000};
    std::uint32_t bufferSizeFrames{256};

    // MIDI
    std::string midiPortName{};
    std::vector<xpad::midi::PadMapping> padMappings{};

    // Bank
    std::string activeBankName{"Default"};
    std::string samplesDirectory{"samples"};

    // Link
    double initialTempoBpm{126.0};
    double quantum{4.0};

    // UI
    float masterVolume{1.0f};

    // Per-pad quantization override (QuantizeDivision index)
    std::array<int, 8> padQuantization{2,2,2,2,2,2,2,2}; // all 1/4 by default

    bool save(const std::string& filePath) const;
    bool load(const std::string& filePath);

    [[nodiscard]] static std::string defaultPath();
};

} // namespace xpad::config

