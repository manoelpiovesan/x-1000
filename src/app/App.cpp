#include "app/App.hpp"
#include "core/Logger.hpp"
#include "samples/SampleLoader.hpp"

#include <filesystem>
#include <iomanip>
#include <sstream>
#include <thread>
#include <algorithm>

namespace xpad::app {

App::App() : App(Config{}) {}

App::App(Config config) : cfg_(config) {
    if (!cfg_.configFile.empty()) {
        xCfg_.load(cfg_.configFile);
    } else {
        xCfg_.load(xpad::config::XPadConfig::defaultPath());
    }
    xCfg_.initialTempoBpm = cfg_.initialTempoBpm;
    xCfg_.quantum         = cfg_.quantum;
}

App::~App() {
    shutdownSubsystems();
}

int App::run() {
    initSubsystems();
    if (cfg_.headless) {
        runHeadless();
    } else {
        runGui();
    }
    shutdownSubsystems();
    return 0;
}

void App::initSubsystems() {
    core::Logger::info("XPad Link — inicializando...");

    // 1. Link
    linkManager_ = std::make_shared<xpad::link::LinkManager>(
        xpad::link::LinkManager::Config{
            .initialTempoBpm   = xCfg_.initialTempoBpm,
            .quantum           = xCfg_.quantum,
            .preferAbletonLink = cfg_.preferAbletonLink,
        });
    linkManager_->start();
    core::Logger::info(std::string("Link: ") + linkManager_->modeName());

    // 2. Sample Bank
    bank_ = std::make_shared<xpad::samples::SampleBank>();
    bank_->name = xCfg_.activeBankName;
    loadBank();

    // 3. Scheduler
    scheduler_ = std::make_shared<xpad::audio::AudioScheduler>(bank_);

    // 4. Audio engine
    audioEngine_ = std::make_shared<xpad::audio::AudioEngine>(
        xpad::audio::AudioConfig{
            .sampleRate       = xCfg_.sampleRate,
            .bufferSizeFrames = xCfg_.bufferSizeFrames,
        });
    audioEngine_->setScheduler(scheduler_);
    audioEngine_->setLinkStateProvider([this]() -> std::pair<double, double> {
        const auto snap = linkManager_->snapshot();
        return {snap.beat, snap.tempoBpm};
    });
    audioEngine_->start();

    // 5. MIDI
    midiManager_ = std::make_shared<xpad::midi::MidiManager>();
    midiManager_->setCallback([this](const xpad::midi::MidiMessage& msg) {
        onMidiMessage(msg);
    });

    if (xCfg_.padMappings.empty()) {
        xCfg_.padMappings = xpad::midi::defaultLpd8Mapping();
    }

    const auto midiPorts = midiManager_->listPorts();
    if (!xCfg_.midiPortName.empty()) {
        midiManager_->openPortByName(xCfg_.midiPortName);
    } else if (!midiPorts.empty()) {
        midiManager_->openPort(0);
        xCfg_.midiPortName = midiPorts[0];
    } else {
        core::Logger::warn("MIDI: nenhuma porta encontrada");
    }
}

void App::shutdownSubsystems() {
    // Auto-save config on exit
    xCfg_.save(xpad::config::XPadConfig::defaultPath());

    if (window_) { window_->shutdown(); window_.reset(); }
    if (audioEngine_) audioEngine_->stop();
    if (midiManager_) midiManager_->closePort();
    if (linkManager_) linkManager_->stop();
}

void App::loadBank() {
    if (!std::filesystem::exists(xCfg_.samplesDirectory)) {
        core::Logger::warn("SampleBank: diretorio nao encontrado: " + xCfg_.samplesDirectory);
        std::filesystem::create_directories(xCfg_.samplesDirectory);
        return;
    }

    const std::array<std::string, 8> defaultNames{
        "pad1","pad2","pad3","pad4","pad5","pad6","pad7","pad8"
    };

    int loaded = 0;
    for (int i = 0; i < xpad::samples::kPadCount; ++i) {
        for (const auto& ext : {".wav",".flac",".mp3",".ogg"}) {
            const std::string path = xCfg_.samplesDirectory + "/" + defaultNames[i] + ext;
            if (std::filesystem::exists(path)) {
                xpad::samples::SampleMetadata meta;
                meta.name       = defaultNames[i];
                meta.filePath   = path;
                meta.mode       = xpad::samples::PadMode::OneShot;

                auto sample = xpad::samples::loadSample(path, meta, xCfg_.sampleRate);
                if (sample) { bank_->setPad(i, sample); ++loaded; }
                break;
            }
        }
    }
    core::Logger::info("SampleBank: " + std::to_string(loaded) + " sample(s) carregado(s)");
}

void App::onMidiMessage(const xpad::midi::MidiMessage& msg) {
    for (const auto& mapping : xCfg_.padMappings) {
        if (msg.data1 != mapping.noteNumber) continue;
        if (msg.isNoteOn()) {
            const float vol = static_cast<float>(msg.data2) / 127.0f * mapping.volume;
            onPadPress(mapping.padIndex, vol);
        } else if (msg.isNoteOff()) {
            onPadRelease(mapping.padIndex);
        }
    }
}

void App::onPadPress(int padIndex, float volume) {
    if (!scheduler_) return;
    const auto snap = linkManager_->snapshot();
    const int qIdx  = (padIndex >= 0 && padIndex < 8) ? xCfg_.padQuantization[padIndex] : 2;
    const auto div  = static_cast<xpad::audio::QuantizeDivision>(qIdx);
    scheduler_->schedulePad(padIndex, snap.beat, div, volume * xCfg_.masterVolume);
}

void App::onPadRelease(int padIndex) {
    if (scheduler_) scheduler_->releasePad(padIndex);
}

void App::runHeadless() {
    core::Logger::info("Modo headless (sem GUI)");
    const auto startedAt  = std::chrono::steady_clock::now();
    const auto finishesAt = startedAt + cfg_.runFor;

    while (std::chrono::steady_clock::now() < finishesAt) {
        const auto snap = linkManager_->snapshot();
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2)
            << "mode=" << linkManager_->modeName()
            << " connected=" << (snap.connected ? "yes" : "no")
            << " peers=" << snap.peerCount
            << " bpm=" << snap.tempoBpm
            << " beat=" << snap.beat;
        core::Logger::info(oss.str());
        std::this_thread::sleep_for(cfg_.printEvery);
    }
    core::Logger::info("Headless finalizado.");
}

void App::runGui() {
    xpad::gui::GuiHandlers handlers;
    handlers.onPadPress           = [this](int i, float v) { onPadPress(i, v); };
    handlers.onPadRelease         = [this](int i)          { onPadRelease(i); };
    handlers.onMasterVolumeChange = [this](float v)        { xCfg_.masterVolume = v; };
    handlers.onPadQuantChange     = [this](int i, int q)   {
        if (i >= 0 && i < 8) xCfg_.padQuantization[i] = q;
    };
    handlers.onMidiPortSelect = [this](const std::string& port) {
        midiManager_->closePort();
        midiManager_->openPortByName(port);
        xCfg_.midiPortName = port;
    };
    handlers.onAudioDeviceSelect = [](const std::string&) {};
    handlers.onSaveConfig = [this]() {
        xCfg_.save(xpad::config::XPadConfig::defaultPath());
    };

    window_ = std::make_unique<xpad::gui::MainWindow>(std::move(handlers));
    if (!window_->init(900, 480, "XPad Link")) {
        core::Logger::error("GUI: falha na inicializacao, rodando headless");
        runHeadless();
        return;
    }

    const auto midiPorts    = midiManager_->listPorts();
    const auto audioDevices = xpad::audio::AudioEngine::listDevices();

    window_->run(*linkManager_, *scheduler_, xCfg_, midiPorts, audioDevices);
}

} // namespace xpad::app


