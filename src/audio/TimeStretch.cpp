#include "audio/TimeStretch.hpp"
#include "core/Logger.hpp"

#include <rubberband/RubberBandStretcher.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace xpad::audio {

std::shared_ptr<xpad::samples::Sample> timeStretch(
    const xpad::samples::Sample& source,
    double rate,
    double pitchScale) {

    if (!source.loaded() || source.channels == 0 || source.frameCount == 0) {
        return nullptr;
    }

    if (std::fabs(rate - 1.0) < 0.001 && std::fabs(pitchScale - 1.0) < 0.001) {
        // No stretch needed, shallow copy
        return std::make_shared<xpad::samples::Sample>(source);
    }

    using namespace RubberBand;

    RubberBandStretcher::Options opts =
        RubberBandStretcher::OptionProcessOffline |
        RubberBandStretcher::OptionStretchPrecise |
        RubberBandStretcher::OptionPitchHighQuality;

    RubberBandStretcher stretcher(source.sampleRate, source.channels, opts, 1.0 / rate, pitchScale);

    const std::uint64_t frameCount = source.frameCount;
    const int channels = static_cast<int>(source.channels);

    // Deinterleave input
    std::vector<std::vector<float>> channelData(channels, std::vector<float>(frameCount));
    for (std::uint64_t f = 0; f < frameCount; ++f) {
        for (int c = 0; c < channels; ++c) {
            channelData[c][f] = source.data[f * channels + c];
        }
    }

    std::vector<const float*> ptrs(channels);
    for (int c = 0; c < channels; ++c) {
        ptrs[c] = channelData[c].data();
    }

    stretcher.study(ptrs.data(), static_cast<std::size_t>(frameCount), true);
    stretcher.process(ptrs.data(), static_cast<std::size_t>(frameCount), true);

    const int available = static_cast<int>(stretcher.available());
    std::vector<std::vector<float>> out(channels, std::vector<float>(available));
    std::vector<float*> outPtrs(channels);
    for (int c = 0; c < channels; ++c) {
        outPtrs[c] = out[c].data();
    }
    stretcher.retrieve(outPtrs.data(), static_cast<std::size_t>(available));

    auto result = std::make_shared<xpad::samples::Sample>();
    result->meta = source.meta;
    result->sampleRate = source.sampleRate;
    result->channels = source.channels;
    result->frameCount = static_cast<std::uint64_t>(available);
    result->data.resize(result->frameCount * channels);
    for (int f = 0; f < available; ++f) {
        for (int c = 0; c < channels; ++c) {
            result->data[f * channels + c] = out[c][f];
        }
    }

    return result;
}

} // namespace xpad::audio

