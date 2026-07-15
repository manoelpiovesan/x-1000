#include "audio/AudioScheduler.hpp"
#include "audio/TimeStretch.hpp"
#include "core/Logger.hpp"
#include <algorithm>
#include <cmath>

namespace xpad::audio {

double divisionToBeats(QuantizeDivision div) noexcept {
    switch (div) {
        case QuantizeDivision::Whole:        return 4.0;
        case QuantizeDivision::Half:         return 2.0;
        case QuantizeDivision::Quarter:      return 1.0;
        case QuantizeDivision::Eighth:       return 0.5;
        case QuantizeDivision::Sixteenth:    return 0.25;
        case QuantizeDivision::ThirtySecond: return 0.125;
    }
    return 1.0;
}

double quantizeNextBeat(double currentBeat, QuantizeDivision div) noexcept {
    const double gridSize = divisionToBeats(div);
    return (std::floor(currentBeat / gridSize) + 1.0) * gridSize;
}

AudioScheduler::AudioScheduler(std::shared_ptr<xpad::samples::SampleBank> bank)
    : bank_(std::move(bank)) {
    for (auto& v : voices_) {
        v.active = false;
    }
    for (auto& f : padActive_) f.store(false);
    for (auto& f : padScheduled_) f.store(false);
}

void AudioScheduler::setBank(std::shared_ptr<xpad::samples::SampleBank> bank) {
    std::scoped_lock lock(bankMutex_);
    bank_ = std::move(bank);
}

void AudioScheduler::schedulePad(int padIndex,
                                  double currentBeat,
                                  QuantizeDivision quantization,
                                  float volume) {
    if (padIndex < 0 || padIndex >= xpad::samples::kPadCount) return;

    const double triggerAtBeat = quantizeNextBeat(currentBeat, quantization);

    std::shared_ptr<xpad::samples::Sample> sample;
    {
        std::scoped_lock lock(bankMutex_);
        if (bank_) sample = bank_->pad(padIndex);
    }
    if (!sample || !sample->loaded()) return;

    const bool isLoop =
        sample->meta.mode == xpad::samples::PadMode::Loop ||
        sample->meta.mode == xpad::samples::PadMode::Hold;

    {
        std::scoped_lock lock(scheduleMutex_);
        // Remove any existing pending event for this pad
        pendingEvents_.erase(
            std::remove_if(pendingEvents_.begin(), pendingEvents_.end(),
                           [padIndex](const ScheduledEvent& e){ return e.padIndex == padIndex; }),
            pendingEvents_.end());

        pendingEvents_.push_back(ScheduledEvent{
            .padIndex = padIndex,
            .triggerAtBeat = triggerAtBeat,
            .loop = isLoop,
            .volume = volume,
        });
    }

    padScheduled_[static_cast<std::size_t>(padIndex)].store(true);
}

void AudioScheduler::releasePad(int padIndex) {
    if (padIndex < 0 || padIndex >= xpad::samples::kPadCount) return;

    std::shared_ptr<xpad::samples::Sample> sample;
    {
        std::scoped_lock lock(bankMutex_);
        if (bank_) sample = bank_->pad(padIndex);
    }
    if (!sample) return;

    if (sample->meta.mode == xpad::samples::PadMode::Hold ||
        sample->meta.mode == xpad::samples::PadMode::Loop) {
        stopVoice(padIndex);

        std::scoped_lock lock(scheduleMutex_);
        pendingEvents_.erase(
            std::remove_if(pendingEvents_.begin(), pendingEvents_.end(),
                           [padIndex](const ScheduledEvent& e){ return e.padIndex == padIndex; }),
            pendingEvents_.end());

        padScheduled_[static_cast<std::size_t>(padIndex)].store(false);
    }
}

void AudioScheduler::processAudio(float* outputBuffer,
                                   std::uint32_t frameCount,
                                   std::uint32_t sampleRate,
                                   double currentBeat,
                                   double tempoBpm) {
    std::fill(outputBuffer, outputBuffer + frameCount * 2, 0.0f);

    if (frameCount == 0 || sampleRate == 0) return;

    // -- Fire pending events that have reached their trigger beat --
    {
        std::scoped_lock lock(scheduleMutex_);
        auto it = pendingEvents_.begin();
        while (it != pendingEvents_.end()) {
            if (currentBeat >= it->triggerAtBeat) {
                activateVoice(it->padIndex, it->triggerAtBeat, it->volume, it->loop);
                padScheduled_[static_cast<std::size_t>(it->padIndex)].store(false);
                it = pendingEvents_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // -- Mix all active voices into the output buffer --
    for (auto& voice : voices_) {
        if (!voice.active || !voice.sample || !voice.sample->loaded()) continue;

        const auto& smp = *voice.sample;

        // Compute stretch rate based on BPM
        double stretchRate = 1.0;
        if (smp.meta.referenceBpm > 0.0 && tempoBpm > 0.0) {
            stretchRate = tempoBpm / smp.meta.referenceBpm;
        }

        const float vol = voice.volume * static_cast<float>(smp.meta.volume);

        for (std::uint32_t f = 0; f < frameCount; ++f) {
            if (voice.readPosition >= smp.frameCount) {
                if (voice.loop) {
                    voice.readPosition = 0;
                } else {
                    voice.active = false;
                    padActive_[static_cast<std::size_t>(voice.padIndex)].store(false);
                    break;
                }
            }

            // Simple rate-based time stretch: nearest-neighbour sample position
            const std::uint64_t sourceFrame = voice.readPosition;
            const float left  = smp.data[sourceFrame * 2 + 0] * vol;
            const float right = smp.data[sourceFrame * 2 + 1] * vol;

            outputBuffer[f * 2 + 0] += left;
            outputBuffer[f * 2 + 1] += right;

            // Advance by stretchRate (rate < 1 → slows down, rate > 1 → speeds up)
            const double nextPos = static_cast<double>(voice.readPosition) + stretchRate;
            voice.readPosition = static_cast<std::uint64_t>(nextPos);
        }
    }

    // Soft clip to prevent distortion
    for (std::uint32_t i = 0; i < frameCount * 2; ++i) {
        const float s = outputBuffer[i];
        outputBuffer[i] = s > 1.0f ? 1.0f : (s < -1.0f ? -1.0f : s);
    }

    lastProcessedBeat_ = currentBeat;
}

bool AudioScheduler::isPadActive(int padIndex) const noexcept {
    if (padIndex < 0 || padIndex >= xpad::samples::kPadCount) return false;
    return padActive_[static_cast<std::size_t>(padIndex)].load();
}

bool AudioScheduler::isPadScheduled(int padIndex) const noexcept {
    if (padIndex < 0 || padIndex >= xpad::samples::kPadCount) return false;
    return padScheduled_[static_cast<std::size_t>(padIndex)].load();
}

void AudioScheduler::activateVoice(int padIndex, double triggerBeat, float volume, bool loop) {
    std::shared_ptr<xpad::samples::Sample> sample;
    {
        std::scoped_lock lock(bankMutex_);
        if (bank_) sample = bank_->pad(padIndex);
    }
    if (!sample || !sample->loaded()) return;

    // For retrigger: reuse existing voice for this pad
    Voice* target = nullptr;
    for (auto& v : voices_) {
        if (v.padIndex == padIndex) {
            target = &v;
            break;
        }
    }
    // Otherwise find first free voice
    if (!target) {
        for (auto& v : voices_) {
            if (!v.active) {
                target = &v;
                break;
            }
        }
    }
    if (!target) return; // Voice steal not implemented yet

    target->sample = sample;
    target->readPosition = 0;
    target->volume = volume;
    target->active = true;
    target->loop = loop;
    target->scheduledAtBeat = triggerBeat;
    target->padIndex = padIndex;

    padActive_[static_cast<std::size_t>(padIndex)].store(true);
}

void AudioScheduler::stopVoice(int padIndex) {
    for (auto& v : voices_) {
        if (v.padIndex == padIndex && v.active) {
            v.active = false;
            padActive_[static_cast<std::size_t>(padIndex)].store(false);
        }
    }
}

} // namespace xpad::audio

