#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace xpad::midi {

struct MidiMessage {
    std::uint8_t status{0};
    std::uint8_t data1{0};
    std::uint8_t data2{0};
    double timestamp{0.0};

    [[nodiscard]] std::uint8_t channel() const noexcept { return static_cast<std::uint8_t>(status & 0x0F); }
    [[nodiscard]] std::uint8_t type() const noexcept    { return static_cast<std::uint8_t>(status & 0xF0); }
    [[nodiscard]] bool isNoteOn() const noexcept  { return type() == 0x90 && data2 > 0; }
    [[nodiscard]] bool isNoteOff() const noexcept { return type() == 0x80 || (type() == 0x90 && data2 == 0); }
    [[nodiscard]] bool isCC() const noexcept      { return type() == 0xB0; }
};

using MidiCallback = std::function<void(const MidiMessage&)>;

class MidiManager {
public:
    MidiManager();
    ~MidiManager();

    MidiManager(const MidiManager&) = delete;
    MidiManager& operator=(const MidiManager&) = delete;

    [[nodiscard]] std::vector<std::string> listPorts() const;
    bool openPort(std::uint32_t portIndex);
    bool openPortByName(const std::string& name);
    void closePort();

    void setCallback(MidiCallback callback);

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] std::string openPortName() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Default mapping for Akai LPD8 — pad index → MIDI note number
struct PadMapping {
    int padIndex{-1};
    std::uint8_t noteNumber{0};
    float volume{1.0f};
    int quantizationDiv{2}; // index into QuantizeDivision enum
};

std::vector<PadMapping> defaultLpd8Mapping();

} // namespace xpad::midi

