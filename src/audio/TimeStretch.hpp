#pragma once
#include "samples/Sample.hpp"
#include <memory>

namespace xpad::audio {

// Applies pitch-preserving time stretch to a sample at a given playback rate.
// rate = targetBpm / referenceBpm
// Returns a new Sample with stretched audio data.
std::shared_ptr<xpad::samples::Sample> timeStretch(
    const xpad::samples::Sample& source,
    double rate,
    double pitchScale = 1.0);

} // namespace xpad::audio

