#pragma once
#include "samples/Sample.hpp"
#include "samples/SampleBank.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace xpad::audio {

enum class QuantizeDivision {
    DoubleWhole = 0, // 2/1
    Whole       = 1, // 1/1
    Half        = 2, // 1/2
    Quarter     = 3, // 1/4
    Eighth      = 4, // 1/8
    Sixteenth   = 5,
    ThirtySecond= 6,
};

double divisionToBeats(QuantizeDivision div) noexcept;

struct Voice {
    std::shared_ptr<xpad::samples::Sample> sample;
    std::uint64_t readPosition{0};
    float volume{1.0f};
    bool active{false};
    bool loop{false};
    double scheduledAtBeat{0.0};
    int padIndex{-1};

    // RMX-style roll looping window (from sample start)
    bool useLoopSlice{false};
    QuantizeDivision loopDivision{QuantizeDivision::Quarter};
};

struct ScheduledEvent {
    int padIndex{-1};
    double triggerAtBeat{0.0};
    bool loop{false};
    float volume{1.0f};
    QuantizeDivision quantization{QuantizeDivision::Quarter};
};

double quantizeNextBeat(double currentBeat, QuantizeDivision div) noexcept;

constexpr int kMaxVoices = 16;

class AudioScheduler {
public:
    explicit AudioScheduler(std::shared_ptr<xpad::samples::SampleBank> bank);

    void schedulePad(int padIndex,
                     double currentBeat,
                     QuantizeDivision quantization,
                     float volume = 1.0f);

    // Start a continuous roll loop for the pad with the selected division.
    void startRoll(int padIndex,
                   double currentBeat,
                   QuantizeDivision rollDivision,
                   float volume = 1.0f);

    // Stop the active roll/loop for this pad.
    void stopRoll(int padIndex);

    void releasePad(int padIndex);

    void processAudio(float* outputBuffer,
                      std::uint32_t frameCount,
                      std::uint32_t sampleRate,
                      double currentBeat,
                      double tempoBpm);

    void setBank(std::shared_ptr<xpad::samples::SampleBank> bank);

    [[nodiscard]] bool isPadActive(int padIndex) const noexcept;
    [[nodiscard]] bool isPadScheduled(int padIndex) const noexcept;

private:
    void activateVoice(int padIndex,
                       double triggerBeat,
                       float volume,
                       bool loop,
                       QuantizeDivision quantization);
    [[nodiscard]] std::uint64_t loopSliceFrames(const Voice& voice, double tempoBpm) const;
    void stopVoice(int padIndex);

    mutable std::mutex bankMutex_;
    std::shared_ptr<xpad::samples::SampleBank> bank_;

    std::mutex scheduleMutex_;
    std::vector<ScheduledEvent> pendingEvents_;

    std::array<Voice, kMaxVoices> voices_{};
    std::array<std::atomic<bool>, xpad::samples::kPadCount> padActive_{};
    std::array<std::atomic<bool>, xpad::samples::kPadCount> padScheduled_{};

    double lastProcessedBeat_{-1.0};
};

} // namespace xpad::audio
