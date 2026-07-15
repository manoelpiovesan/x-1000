#pragma once
#include "samples/Sample.hpp"
#include <memory>
#include <string>

namespace xpad::samples {

// Loads WAV/FLAC/MP3/OGG from disk using miniaudio's decoder.
// Returns nullptr if the file could not be loaded.
std::shared_ptr<Sample> loadSample(const std::string& filePath,
                                   const SampleMetadata& meta,
                                   std::uint32_t targetSampleRate = 48000);

} // namespace xpad::samples

