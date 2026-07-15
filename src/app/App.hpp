#pragma once
#include "audio/AudioEngine.hpp"
#include "audio/AudioScheduler.hpp"
#include "config/Config.hpp"
#include "gui/MainWindow.hpp"
#include "link/LinkManager.hpp"
#include "midi/MidiManager.hpp"
#include "samples/SampleBank.hpp"
#include <memory>
#include <string>

namespace xpad::app {

class App {
public:
    struct Config {
        double initialTempoBpm{126.0};
        double quantum{4.0};
        std::chrono::milliseconds printEvery{250};
        std::chrono::seconds runFor{5};
        bool simulateTempoChange{false};
        bool preferAbletonLink{true};
        bool headless{false};
        std::string configFile{};
    };

    App();
    explicit App(Config config);
    ~App();

    int run();

private:
    void initSubsystems();
    void shutdownSubsystems();
    void loadBank();
    void runHeadless();
    void runGui();

    void onMidiMessage(const xpad::midi::MidiMessage& msg);
    void onPadPress(int padIndex, float volume);
    void onPadRelease(int padIndex);

    Config cfg_;
    xpad::config::XPadConfig xCfg_;

    std::shared_ptr<xpad::samples::SampleBank>   bank_;
    std::shared_ptr<xpad::link::LinkManager>      linkManager_;
    std::shared_ptr<xpad::audio::AudioScheduler>  scheduler_;
    std::shared_ptr<xpad::audio::AudioEngine>     audioEngine_;
    std::shared_ptr<xpad::midi::MidiManager>      midiManager_;
    std::unique_ptr<xpad::gui::MainWindow>        window_;

    bool tempoChanged_{false};
};

} // namespace xpad::app






