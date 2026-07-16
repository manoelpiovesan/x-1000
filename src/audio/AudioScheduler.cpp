#include "audio/AudioScheduler.hpp"

#include <algorithm>
#include <cmath>

namespace xpad::audio {

constexpr double kPi = 3.14159265358979323846;

// Signed response curve: preserves sign, adds finer control near center.
static float applySignedCurve(float normalized, float gamma) {
    const float clamped = std::clamp(normalized, -1.0f, 1.0f);
    const float mag = std::pow(std::fabs(clamped), gamma);
    return std::copysign(mag, clamped);
}

double divisionToBeats(QuantizeDivision div) noexcept {
    switch (div) {
        case QuantizeDivision::DoubleWhole:  return 2.0;
        case QuantizeDivision::Whole:        return 1.0;
        case QuantizeDivision::Half:         return 0.5;
        case QuantizeDivision::Quarter:      return 0.25;
        case QuantizeDivision::Eighth:       return 0.125;
        case QuantizeDivision::Sixteenth:    return 0.0625;
        case QuantizeDivision::ThirtySecond: return 0.03125;
    }
    return 0.25;
}

double quantizeNextBeat(double currentBeat, QuantizeDivision div) noexcept {
    const double gridSize = divisionToBeats(div);
    return (std::floor(currentBeat / gridSize) + 1.0) * gridSize;
}

AudioScheduler::AudioScheduler(std::shared_ptr<xpad::samples::SampleBank> bank)
    : bank_(std::move(bank)) {
    for (auto& v : voices_) v.active = false;
    for (auto& f : padActive_) f.store(false);
    for (auto& f : padScheduled_) f.store(false);
}

void AudioScheduler::setBank(std::shared_ptr<xpad::samples::SampleBank> bank) {
    std::scoped_lock lock(bankMutex_);
    bank_ = std::move(bank);
}

void AudioScheduler::setMasterVolume(float value) noexcept {
    masterVolume_.store(std::clamp(value, 0.0f, 1.0f));
}

void AudioScheduler::setPitchSemitones(float semitones) noexcept {
    const float clamped = std::clamp(semitones, -12.0f, 12.0f);
    pitchSemitones_.store(clamped);

    // Curved response: finer around 0 st, stronger near +-12 st.
    constexpr float kPitchGamma = 1.7f;
    const float normalized = clamped / 12.0f;
    const float curvedSemitones = applySignedCurve(normalized, kPitchGamma) * 12.0f;
    pitchRatio_.store(std::pow(2.0f, curvedSemitones / 12.0f));
}

void AudioScheduler::setFilterAmount(float amount) noexcept {
    filterAmount_.store(std::clamp(amount, -1.0f, 1.0f));
}

float AudioScheduler::masterVolume() const noexcept { return masterVolume_.load(); }
float AudioScheduler::pitchSemitones() const noexcept { return pitchSemitones_.load(); }
float AudioScheduler::filterAmount() const noexcept { return filterAmount_.load(); }

void AudioScheduler::schedulePad(int padIndex,
                                 double currentBeat,
                                 QuantizeDivision quantization,
                                 float volume) {
    if (padIndex < 0 || padIndex >= xpad::samples::kPadCount) return;

    std::shared_ptr<xpad::samples::Sample> sample;
    {
        std::scoped_lock lock(bankMutex_);
        if (bank_) sample = bank_->pad(padIndex);
    }
    if (!sample || !sample->loaded()) return;

    const bool isLoop =
        sample->meta.mode == xpad::samples::PadMode::Loop ||
        sample->meta.mode == xpad::samples::PadMode::Hold;

    const double triggerAtBeat = quantizeNextBeat(currentBeat, quantization);
    {
        std::scoped_lock lock(scheduleMutex_);
        pendingEvents_.erase(
            std::remove_if(pendingEvents_.begin(), pendingEvents_.end(),
                           [padIndex](const ScheduledEvent& e){ return e.padIndex == padIndex; }),
            pendingEvents_.end());
        pendingEvents_.push_back(ScheduledEvent{
            .padIndex = padIndex,
            .triggerAtBeat = triggerAtBeat,
            .loop = isLoop,
            .volume = volume,
            .quantization = quantization,
        });
    }

    padScheduled_[static_cast<std::size_t>(padIndex)].store(true);
}

void AudioScheduler::startRoll(int padIndex,
                               double currentBeat,
                               QuantizeDivision rollDivision,
                               float volume) {
    if (padIndex < 0 || padIndex >= xpad::samples::kPadCount) return;

    std::shared_ptr<xpad::samples::Sample> sample;
    {
        std::scoped_lock lock(bankMutex_);
        if (bank_) sample = bank_->pad(padIndex);
    }
    if (!sample || !sample->loaded()) return;

    const double triggerAtBeat = quantizeNextBeat(currentBeat, rollDivision);
    {
        std::scoped_lock lock(scheduleMutex_);
        pendingEvents_.erase(
            std::remove_if(pendingEvents_.begin(), pendingEvents_.end(),
                           [padIndex](const ScheduledEvent& e){ return e.padIndex == padIndex; }),
            pendingEvents_.end());
        pendingEvents_.push_back(ScheduledEvent{
            .padIndex = padIndex,
            .triggerAtBeat = triggerAtBeat,
            .loop = true,
            .volume = volume,
            .quantization = rollDivision,
        });
    }

    padScheduled_[static_cast<std::size_t>(padIndex)].store(true);
}

void AudioScheduler::stopRoll(int padIndex) {
    if (padIndex < 0 || padIndex >= xpad::samples::kPadCount) return;

    {
        std::scoped_lock lock(scheduleMutex_);
        pendingEvents_.erase(
            std::remove_if(pendingEvents_.begin(), pendingEvents_.end(),
                           [padIndex](const ScheduledEvent& e){ return e.padIndex == padIndex; }),
            pendingEvents_.end());
    }

    stopVoice(padIndex);
    padScheduled_[static_cast<std::size_t>(padIndex)].store(false);
}

void AudioScheduler::releasePad(int padIndex) {
    stopRoll(padIndex);
}

std::uint64_t AudioScheduler::loopSliceFrames(const Voice& voice, double tempoBpm) const {
    if (!voice.sample || !voice.sample->loaded()) return 0;
    const double bpm = tempoBpm > 0.0 ? tempoBpm : 120.0;
    const double secondsPerBeat = 60.0 / bpm;
    const double loopSeconds = divisionToBeats(voice.loopDivision) * secondsPerBeat;
    const double framesD = loopSeconds * static_cast<double>(voice.sample->sampleRate);
    const std::uint64_t frames = static_cast<std::uint64_t>(std::max(1.0, std::floor(framesD)));
    return std::min<std::uint64_t>(frames, voice.sample->frameCount);
}

void AudioScheduler::processAudio(float* outputBuffer,
                                  std::uint32_t frameCount,
                                  std::uint32_t sampleRate,
                                  double currentBeat,
                                  double tempoBpm) {
    std::fill(outputBuffer, outputBuffer + frameCount * 2, 0.0f);
    if (frameCount == 0 || sampleRate == 0) return;

    {
        std::scoped_lock lock(scheduleMutex_);
        auto it = pendingEvents_.begin();
        while (it != pendingEvents_.end()) {
            if (currentBeat >= it->triggerAtBeat) {
                activateVoice(it->padIndex,
                              it->triggerAtBeat,
                              it->volume,
                              it->loop,
                              it->quantization);
                padScheduled_[static_cast<std::size_t>(it->padIndex)].store(false);
                it = pendingEvents_.erase(it);
            } else {
                ++it;
            }
        }
    }

    const float master = masterVolume_.load();
    const float pitchRatio = pitchRatio_.load();

    for (auto& voice : voices_) {
        if (!voice.active || !voice.sample || !voice.sample->loaded()) continue;

        const auto& smp = *voice.sample;
        const float vol = voice.volume * static_cast<float>(smp.meta.volume) * master;

        std::uint64_t loopFrames = 0;
        if (voice.loop && voice.useLoopSlice) {
            loopFrames = loopSliceFrames(voice, tempoBpm);
            if (loopFrames == 0) loopFrames = smp.frameCount;
        }

        for (std::uint32_t f = 0; f < frameCount; ++f) {
            const double endFrame = static_cast<double>((voice.loop && voice.useLoopSlice) ? loopFrames : smp.frameCount);
            if (endFrame <= 1.0) {
                continue;
            }

            // IMPORTANT:
            // - For loop/roll mode: timeline stays locked to Link (advance = 1 frame),
            //   pitch changes only which source position is read.
            // - For one-shot mode: pitch changes playback duration as expected.
            double sourcePos = 0.0;

            if (voice.loop) {
                if (voice.readPosition >= endFrame) {
                    voice.readPosition = std::fmod(voice.readPosition, endFrame);
                }

                sourcePos = std::fmod(voice.readPosition * static_cast<double>(pitchRatio), endFrame);
                if (sourcePos < 0.0) {
                    sourcePos += endFrame;
                }

                // Keep rhythmic period fixed to grid.
                voice.readPosition += 1.0;
            } else {
                if (voice.readPosition >= endFrame) {
                    voice.active = false;
                    if (voice.padIndex >= 0 && voice.padIndex < xpad::samples::kPadCount) {
                        padActive_[static_cast<std::size_t>(voice.padIndex)].store(false);
                    }
                    break;
                }

                sourcePos = voice.readPosition;
                voice.readPosition += static_cast<double>(pitchRatio);
            }

            const std::uint64_t i0 = static_cast<std::uint64_t>(sourcePos);
            const std::uint64_t i1 = std::min<std::uint64_t>(i0 + 1, smp.frameCount - 1);
            const float frac = static_cast<float>(sourcePos - static_cast<double>(i0));

            const float l0 = smp.data[i0 * 2 + 0];
            const float r0 = smp.data[i0 * 2 + 1];
            const float l1 = smp.data[i1 * 2 + 0];
            const float r1 = smp.data[i1 * 2 + 1];

            const float left  = (l0 + (l1 - l0) * frac) * vol;
            const float right = (r0 + (r1 - r0) * frac) * vol;

            outputBuffer[f * 2 + 0] += left;
            outputBuffer[f * 2 + 1] += right;
        }
    }

    const float filterAmt = filterAmount_.load();

    // Curved DJ-style filter response: more precision near center.
    constexpr float kFilterGamma = 1.8f;
    const float curvedFilterAmt = applySignedCurve(filterAmt, kFilterGamma);
    const float absAmt = std::fabs(curvedFilterAmt);
    if (absAmt > 0.0001f) {
        // One-knob DJ style filter:
        //  < 0 => HPF, 0 => bypass, > 0 => LPF
        const bool useLpf = curvedFilterAmt > 0.0f;
        const double cutoffHz = useLpf
            ? (18000.0 - static_cast<double>(absAmt) * (18000.0 - 200.0))
            : (30.0 + static_cast<double>(absAmt) * (14000.0 - 30.0));
        const double a = std::exp(-2.0 * kPi * cutoffHz / static_cast<double>(sampleRate));
        const double b = 1.0 - a;

        for (std::uint32_t i = 0; i < frameCount; ++i) {
            const double inL = outputBuffer[i * 2 + 0];
            const double inR = outputBuffer[i * 2 + 1];

            // Low component from one-pole LPF state (reused for both modes).
            lpfStateL_ = b * inL + a * lpfStateL_;
            lpfStateR_ = b * inR + a * lpfStateR_;

            const double outL = useLpf ? lpfStateL_ : (inL - lpfStateL_);
            const double outR = useLpf ? lpfStateR_ : (inR - lpfStateR_);
            outputBuffer[i * 2 + 0] = static_cast<float>(outL);
            outputBuffer[i * 2 + 1] = static_cast<float>(outR);
        }
    }

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

void AudioScheduler::activateVoice(int padIndex,
                                   double triggerBeat,
                                   float volume,
                                   bool loop,
                                   QuantizeDivision quantization) {
    std::shared_ptr<xpad::samples::Sample> sample;
    {
        std::scoped_lock lock(bankMutex_);
        if (bank_) sample = bank_->pad(padIndex);
    }
    if (!sample || !sample->loaded()) return;

    Voice* target = nullptr;
    for (auto& v : voices_) {
        if (v.padIndex == padIndex) {
            target = &v;
            break;
        }
    }
    if (!target) {
        for (auto& v : voices_) {
            if (!v.active) {
                target = &v;
                break;
            }
        }
    }
    if (!target) return;

    target->sample = sample;
    target->readPosition = 0.0;
    target->volume = volume;
    target->active = true;
    target->loop = loop;
    target->scheduledAtBeat = triggerBeat;
    target->padIndex = padIndex;
    target->useLoopSlice = loop;
    target->loopDivision = quantization;

    if (padIndex >= 0 && padIndex < xpad::samples::kPadCount) {
        padActive_[static_cast<std::size_t>(padIndex)].store(true);
    }
}

void AudioScheduler::stopVoice(int padIndex) {
    for (auto& v : voices_) {
        if (v.padIndex == padIndex && v.active) {
            v.active = false;
            if (padIndex >= 0 && padIndex < xpad::samples::kPadCount) {
                padActive_[static_cast<std::size_t>(padIndex)].store(false);
            }
        }
    }
}

} // namespace xpad::audio


