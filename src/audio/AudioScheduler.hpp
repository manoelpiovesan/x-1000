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
    Whole   = 0, // 1/1
    Half    = 1, // 1/2
    Quarter = 2, // 1/4
    Eighth  = 3, // 1/8
    Sixteenth = 4, // 1/16
    ThirtySecond = 5, // 1/32
};

double divisionToBeats(QuantizeDivision div) noexcept;

// A single playing voice — one sample being read in the audio callback
struct Voice {
    std::shared_ptr<xpad::samples::Sample> sample;
    std::uint64_t readPosition{0};    // in frames
    float volume{1.0f};
    bool active{false};
    bool loop{false};
    double scheduledAtBeat{0.0};
    int padIndex{-1};
};

// A pending trigger waiting to fire at a specific beat
struct ScheduledEvent {
    int padIndex{-1};
    double triggerAtBeat{0.0};
    bool loop{false};
    float volume{1.0f};
};

// Calculates the next quantized beat boundary given the current beat
double quantizeNextBeat(double currentBeat, QuantizeDivision div) noexcept;

constexpr int kMaxVoices = 16;

// Thread-safe scheduler between MIDI thread and audio callback
class AudioScheduler {
public:
    explicit AudioScheduler(std::shared_ptr<xpad::samples::SampleBank> bank);

    // Called from MIDI/app thread: schedule a pad trigger at the next grid point
    void schedulePad(int padIndex,
                     double currentBeat,
                     QuantizeDivision quantization,
                     float volume = 1.0f);

    // Called from MIDI/app thread: stop a pad (for Hold/Loop modes)
    void releasePad(int padIndex);

    // Called ONLY from audio callback thread — fills outputBuffer with mixed audio.
    // currentBeat must come from the Link clock at the moment of this callback.
    // outputBuffer: interleaved float stereo, frameCount frames.
    void processAudio(float* outputBuffer,
                      std::uint32_t frameCount,
                      std::uint32_t sampleRate,
                      double currentBeat,
                      double tempoBpm);

    void setBank(std::shared_ptr<xpad::samples::SampleBank> bank);

    // Pad visual state for GUI: true = currently active/playing
    [[nodiscard]] bool isPadActive(int padIndex) const noexcept;
    [[nodiscard]] bool isPadScheduled(int padIndex) const noexcept;

private:
    void activateVoice(int padIndex, double triggerBeat, float volume, bool loop);
    void stopVoice(int padIndex);

    mutable std::mutex bankMutex_;
    std::shared_ptr<xpad::samples::SampleBank> bank_;

    std::mutex scheduleMutex_;
    std::vector<ScheduledEvent> pendingEvents_;

    // Voices are modified only from audio callback; reads from GUI are atomic snapshots
    std::array<Voice, kMaxVoices> voices_{};

    // Per-pad active flags readable from any thread
    std::array<std::atomic<bool>, xpad::samples::kPadCount> padActive_{};
    std::array<std::atomic<bool>, xpad::samples::kPadCount> padScheduled_{};

    double lastProcessedBeat_{-1.0};
};

} // namespace xpad::audio

