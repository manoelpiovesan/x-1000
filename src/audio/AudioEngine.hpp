#pragma once
#include "audio/AudioTypes.hpp"
#include "audio/AudioScheduler.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace xpad::audio {

// Callback called before each audio buffer, so the engine can pull
// the current Link beat/bpm from the app thread.
using LinkStateProvider = std::function<std::pair<double, double>()>; // {beat, bpm}

class AudioEngine {
public:
    explicit AudioEngine(AudioConfig config = {});
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    void setScheduler(std::shared_ptr<AudioScheduler> scheduler);
    void setLinkStateProvider(LinkStateProvider provider);

    bool start();
    void stop();

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] std::uint32_t sampleRate() const noexcept;
    [[nodiscard]] const AudioConfig& config() const noexcept;

    [[nodiscard]] static std::vector<std::string> listDevices();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    AudioConfig config_;
};

} // namespace xpad::audio

