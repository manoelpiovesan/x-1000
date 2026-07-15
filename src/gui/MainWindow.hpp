#pragma once
#include "audio/AudioEngine.hpp"
#include "audio/AudioScheduler.hpp"
#include "config/Config.hpp"
#include "link/LinkManager.hpp"
#include "midi/MidiManager.hpp"
#include "samples/SampleBank.hpp"
#include <functional>
#include <memory>
#include <string>

namespace xpad::gui {

struct GuiHandlers {
    std::function<void(int padIndex, float volume)> onPadPress;
    std::function<void(int padIndex)>               onPadRelease;
    std::function<void(float volume)>               onMasterVolumeChange;
    std::function<void(int padIndex, int quantDiv)> onPadQuantChange;
    std::function<void(const std::string& port)>    onMidiPortSelect;
    std::function<void(const std::string& device)>  onAudioDeviceSelect;
    std::function<void()>                           onSaveConfig;
};

class MainWindow {
public:
    explicit MainWindow(GuiHandlers handlers);
    ~MainWindow();

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    bool init(int width, int height, const char* title);
    void shutdown();

    // Run the render loop; returns when user closes the window.
    void run(
        const xpad::link::LinkManager& linkManager,
        const xpad::audio::AudioScheduler& scheduler,
        xpad::config::XPadConfig& cfg,
        const std::vector<std::string>& midiPorts,
        const std::vector<std::string>& audioDevices
    );

    [[nodiscard]] bool isOpen() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    GuiHandlers handlers_;
};

} // namespace xpad::gui

