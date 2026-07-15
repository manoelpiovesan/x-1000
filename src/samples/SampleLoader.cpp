// Miniaudio implementation unit — compiled once.
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "samples/SampleLoader.hpp"
#include "core/Logger.hpp"

#include <stdexcept>

namespace xpad::samples {

std::shared_ptr<Sample> loadSample(const std::string& filePath,
                                   const SampleMetadata& meta,
                                   std::uint32_t targetSampleRate) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, targetSampleRate);
    ma_decoder decoder;
    if (ma_decoder_init_file(filePath.c_str(), &cfg, &decoder) != MA_SUCCESS) {
        core::Logger::error("SampleLoader: failed to open file: " + filePath);
        return nullptr;
    }

    ma_uint64 frameCount{0};
    ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount);

    auto sample = std::make_shared<Sample>();
    sample->meta = meta;
    sample->meta.filePath = filePath;
    sample->sampleRate = targetSampleRate;
    sample->channels = 2;
    sample->frameCount = frameCount;
    sample->data.resize(frameCount * 2);

    ma_uint64 framesRead{0};
    ma_decoder_read_pcm_frames(&decoder, sample->data.data(), frameCount, &framesRead);
    ma_decoder_uninit(&decoder);

    sample->frameCount = framesRead;
    sample->data.resize(framesRead * 2);

    core::Logger::info("SampleLoader: loaded '" + filePath + "' frames=" + std::to_string(framesRead));
    return sample;
}

} // namespace xpad::samples


