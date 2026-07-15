#include "gui/MainWindow.hpp"
#include "core/Logger.hpp"
#include "audio/AudioScheduler.hpp"
#include "link/LinkManager.hpp"
#include "config/Config.hpp"
#include "samples/SampleBank.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#include <string>
#include <vector>
#include <array>

namespace xpad::gui {

static const char* kQuantLabels[] = {"1/1","1/2","1/4","1/8","1/16","1/32"};
static const char* kModeLabels[]  = {"OneShot","Loop","Retrigger","Hold"};

struct MainWindow::Impl {
    GLFWwindow* window{nullptr};
    bool open{false};
};

MainWindow::MainWindow(GuiHandlers handlers)
    : impl_(std::make_unique<Impl>())
    , handlers_(std::move(handlers)) {
}

MainWindow::~MainWindow() {
    shutdown();
}

bool MainWindow::init(int width, int height, const char* title) {
    if (!glfwInit()) {
        core::Logger::error("MainWindow: glfwInit failed");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    impl_->window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!impl_->window) {
        core::Logger::error("MainWindow: glfwCreateWindow failed");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(impl_->window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(impl_->window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    impl_->open = true;
    core::Logger::info("MainWindow: initialized");
    return true;
}

void MainWindow::shutdown() {
    if (!impl_->open) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (impl_->window) {
        glfwDestroyWindow(impl_->window);
        impl_->window = nullptr;
    }
    glfwTerminate();
    impl_->open = false;
}

bool MainWindow::isOpen() const noexcept {
    return impl_->open;
}

void MainWindow::run(
    const xpad::link::LinkManager& linkManager,
    const xpad::audio::AudioScheduler& scheduler,
    xpad::config::XPadConfig& cfg,
    const std::vector<std::string>& midiPorts,
    const std::vector<std::string>& audioDevices)
{
    if (!impl_->window) return;

    while (!glfwWindowShouldClose(impl_->window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const auto snapshot = linkManager.snapshot();

        // ── Main Window ─────────────────────────────────────────────────
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        int fbW{0}, fbH{0};
        glfwGetFramebufferSize(impl_->window, &fbW, &fbH);
        ImGui::SetNextWindowSize({static_cast<float>(fbW), static_cast<float>(fbH)}, ImGuiCond_Always);
        ImGui::Begin("XPad Link", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

        // ── Header: Link status ──────────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Text,
            snapshot.connected ? ImVec4(0.1f,1.f,0.4f,1.f) : ImVec4(0.9f,0.3f,0.3f,1.f));
        ImGui::Text("Ableton Link: %s  |  Peers: %lld",
            snapshot.connected ? "Connected" : "Disconnected",
            static_cast<long long>(snapshot.peerCount));
        ImGui::PopStyleColor();

        ImGui::SameLine(0, 30);
        ImGui::Text("BPM: %.2f", snapshot.tempoBpm);
        ImGui::SameLine(0, 30);
        ImGui::Text("Beat: %.2f", snapshot.beat);
        ImGui::SameLine(0, 30);
        ImGui::Text("Playing: %s", snapshot.isPlaying ? "Yes" : "No");

        ImGui::Separator();

        // ── Master Volume ────────────────────────────────────────────────
        float mv = cfg.masterVolume;
        ImGui::PushItemWidth(180.f);
        if (ImGui::SliderFloat("Master Volume", &mv, 0.0f, 1.0f)) {
            cfg.masterVolume = mv;
            if (handlers_.onMasterVolumeChange) handlers_.onMasterVolumeChange(mv);
        }
        ImGui::PopItemWidth();

        ImGui::Separator();

        // ── Pad Grid ─────────────────────────────────────────────────────
        ImGui::Text("Pads:");
        const float padSize = 90.f;
        const int cols = 4;
        static bool padHeld[xpad::samples::kPadCount]{};

        for (int i = 0; i < xpad::samples::kPadCount; ++i) {
            if (i > 0 && (i % cols) != 0) ImGui::SameLine(0, 6);

            const bool active    = scheduler.isPadActive(i);
            const bool scheduled = scheduler.isPadScheduled(i);

            ImVec4 col{0.25f, 0.25f, 0.25f, 1.f};
            if (active)    col = {0.1f, 0.8f, 0.2f, 1.f};
            if (scheduled) col = {0.8f, 0.7f, 0.1f, 1.f};

            ImGui::PushStyleColor(ImGuiCol_Button,        col);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {col.x+0.1f, col.y+0.1f, col.z+0.1f, 1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.1f, 1.f, 0.3f, 1.f});

            const std::string label = "Pad " + std::to_string(i + 1) + "##pad" + std::to_string(i);
            const bool pressed = ImGui::Button(label.c_str(), {padSize, padSize});

            ImGui::PopStyleColor(3);

            if (pressed && !padHeld[i]) {
                padHeld[i] = true;
                if (handlers_.onPadPress) handlers_.onPadPress(i, 1.0f);
            }
            if (!ImGui::IsItemActive() && padHeld[i]) {
                padHeld[i] = false;
                if (handlers_.onPadRelease) handlers_.onPadRelease(i);
            }

            // Small quantization selector under each pad
            const std::string qId = "##q" + std::to_string(i);
            int qv = cfg.padQuantization[i];
            ImGui::PushItemWidth(padSize);
            if (ImGui::Combo(qId.c_str(), &qv, kQuantLabels, 6)) {
                cfg.padQuantization[i] = qv;
                if (handlers_.onPadQuantChange) handlers_.onPadQuantChange(i, qv);
            }
            ImGui::PopItemWidth();
        }

        ImGui::Separator();

        // ── MIDI Port ────────────────────────────────────────────────────
        ImGui::Text("MIDI Port:");
        ImGui::SameLine();
        ImGui::PushItemWidth(300.f);
        static int midiPortIdx = 0;
        std::vector<const char*> midiC;
        for (auto& s : midiPorts) midiC.push_back(s.c_str());
        if (!midiC.empty() && ImGui::Combo("##midi", &midiPortIdx, midiC.data(), static_cast<int>(midiC.size()))) {
            if (handlers_.onMidiPortSelect) handlers_.onMidiPortSelect(midiPorts[midiPortIdx]);
        }
        ImGui::PopItemWidth();

        // ── Audio Device ─────────────────────────────────────────────────
        ImGui::Text("Audio Out:"); ImGui::SameLine();
        ImGui::PushItemWidth(300.f);
        static int audioDevIdx = 0;
        std::vector<const char*> audioC;
        for (auto& s : audioDevices) audioC.push_back(s.c_str());
        if (!audioC.empty() && ImGui::Combo("##audio", &audioDevIdx, audioC.data(), static_cast<int>(audioC.size()))) {
            if (handlers_.onAudioDeviceSelect) handlers_.onAudioDeviceSelect(audioDevices[audioDevIdx]);
        }
        ImGui::PopItemWidth();

        ImGui::Separator();

        if (ImGui::Button("Save Config")) {
            if (handlers_.onSaveConfig) handlers_.onSaveConfig();
        }

        ImGui::End();

        // Render
        ImGui::Render();
        glClearColor(0.12f, 0.12f, 0.13f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(impl_->window);
    }
}

} // namespace xpad::gui

