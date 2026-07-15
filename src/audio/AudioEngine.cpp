// NOTE: MINIAUDIO_IMPLEMENTATION is defined in SampleLoader.cpp.
// Include miniaudio header-only here without re-implementation.
#include "miniaudio.h"

#include "audio/AudioEngine.hpp"
#include "core/Logger.hpp"
#include <atomic>
#include <cstring>
#include <stdexcept>

namespace xpad::audio {

struct AudioEngine::Impl {
    ma_device device{};
    ma_device_config deviceConfig{};
    std::atomic<bool> running{false};
    AudioConfig config{};
    std::shared_ptr<AudioScheduler> scheduler;
    LinkStateProvider linkProvider;

    static void audioCallback(ma_device* pDevice,
                               void* pOutput,
                               const void* /*pInput*/,
                               ma_uint32 frameCount) {
        auto* impl = static_cast<Impl*>(pDevice->pUserData);
        auto* out = static_cast<float*>(pOutput);
        std::fill(out, out + frameCount * 2, 0.0f);

        if (!impl || !impl->scheduler) return;

        double beat{0.0}, bpm{120.0};
        if (impl->linkProvider) {
            auto [b, t] = impl->linkProvider();
            beat = b;
            bpm = t;
        }

        impl->scheduler->processAudio(out, frameCount, impl->config.sampleRate, beat, bpm);
    }
};

AudioEngine::AudioEngine(AudioConfig config)
    : impl_(std::make_unique<Impl>())
    , config_(config) {
    impl_->config = config;
}

AudioEngine::~AudioEngine() {
    stop();
}

void AudioEngine::setScheduler(std::shared_ptr<AudioScheduler> scheduler) {
    impl_->scheduler = std::move(scheduler);
}

void AudioEngine::setLinkStateProvider(LinkStateProvider provider) {
    impl_->linkProvider = std::move(provider);
}

bool AudioEngine::start() {
    if (impl_->running.load()) return true;

    impl_->deviceConfig = ma_device_config_init(ma_device_type_playback);
    impl_->deviceConfig.playback.format   = ma_format_f32;
    impl_->deviceConfig.playback.channels = 2;
    impl_->deviceConfig.sampleRate        = config_.sampleRate;
    impl_->deviceConfig.periodSizeInFrames = config_.bufferSizeFrames;
    impl_->deviceConfig.dataCallback      = Impl::audioCallback;
    impl_->deviceConfig.pUserData         = impl_.get();

    if (ma_device_init(nullptr, &impl_->deviceConfig, &impl_->device) != MA_SUCCESS) {
        core::Logger::error("AudioEngine: failed to initialize audio device");
        return false;
    }

    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        ma_device_uninit(&impl_->device);
        core::Logger::error("AudioEngine: failed to start audio device");
        return false;
    }

    impl_->running.store(true);
    config_.sampleRate = impl_->device.sampleRate;

    core::Logger::info("AudioEngine: started — sampleRate=" +
                        std::to_string(impl_->device.sampleRate) +
                        " bufferFrames=" +
                        std::to_string(config_.bufferSizeFrames));
    return true;
}

void AudioEngine::stop() {
    if (!impl_->running.load()) return;
    impl_->running.store(false);
    ma_device_stop(&impl_->device);
    ma_device_uninit(&impl_->device);
    core::Logger::info("AudioEngine: stopped");
}

bool AudioEngine::running() const noexcept {
    return impl_->running.load();
}

std::uint32_t AudioEngine::sampleRate() const noexcept {
    return config_.sampleRate;
}

const AudioConfig& AudioEngine::config() const noexcept {
    return config_;
}

std::vector<std::string> AudioEngine::listDevices() {
    ma_context context;
    std::vector<std::string> names;
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) return names;

    ma_device_info* pPlaybackInfos{};
    ma_uint32 playbackCount{};
    ma_device_info* pCaptureInfos{};
    ma_uint32 captureCount{};

    if (ma_context_get_devices(&context, &pPlaybackInfos, &playbackCount,
                               &pCaptureInfos, &captureCount) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < playbackCount; ++i) {
            names.emplace_back(pPlaybackInfos[i].name);
        }
    }

    ma_context_uninit(&context);
    return names;
}

} // namespace xpad::audio


