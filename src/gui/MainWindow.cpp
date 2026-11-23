#include "gui/MainWindow.hpp"
#include "core/Logger.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace xpad::gui {

static const char* kRollLabels[] = {"1/8", "1/4", "1/2", "1/1", "2/1"};

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
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

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
    ImGui::StyleColorsDark();
    {
        ImGuiStyle& st = ImGui::GetStyle();
        st.FrameRounding = 3.0f;
        st.PopupRounding = 3.0f;
        st.WindowBorderSize = 0.0f;
        st.Colors[ImGuiCol_WindowBg]       = ImVec4(0.07f, 0.07f, 0.08f, 1.0f);
        st.Colors[ImGuiCol_FrameBg]        = ImVec4(0.13f, 0.13f, 0.14f, 1.0f);
        st.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.22f, 1.0f);
        st.Colors[ImGuiCol_PopupBg]        = ImVec4(0.10f, 0.10f, 0.11f, 1.0f);
        st.Colors[ImGuiCol_CheckMark]      = ImVec4(0.35f, 0.85f, 0.25f, 1.0f);
        st.Colors[ImGuiCol_Button]         = ImVec4(0.16f, 0.16f, 0.18f, 1.0f);
        st.Colors[ImGuiCol_ButtonHovered]  = ImVec4(0.24f, 0.24f, 0.27f, 1.0f);
        st.Colors[ImGuiCol_ButtonActive]   = ImVec4(0.30f, 0.30f, 0.34f, 1.0f);
    }

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

namespace {

constexpr float kPi = 3.14159265358979f;

ImU32 col(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

void textCentered(ImDrawList* dl, ImVec2 center, float size, ImU32 c, const char* text) {
    ImFont* font = ImGui::GetFont();
    const ImVec2 ts = font->CalcTextSizeA(size, 10000.0f, 0.0f, text);
    dl->AddText(font, size, {center.x - ts.x * 0.5f, center.y - ts.y * 0.5f}, c, text);
}

void panelLabel(ImDrawList* dl, ImVec2 center, const char* text, ImU32 c = IM_COL32(205, 205, 210, 255)) {
    textCentered(dl, center, 12.0f, c, text);
}

} // namespace

void MainWindow::run(const xpad::link::LinkManager& linkManager,
                     const xpad::audio::AudioScheduler& scheduler,
                     xpad::config::XPadConfig& cfg,
                     const std::vector<std::string>& midiPorts,
                     const std::vector<std::string>& audioDevices,
                     const std::vector<std::string>& sampleNames,
                     int& selectedSampleIndex,
                     int& activeRollButton,
                     bool& midiLearnMode,
                     std::string& pendingMidiLearnControl,
                     const std::unordered_map<std::string, std::string>& midiBindings) {
    (void)scheduler;
    auto bindingLabel = [&midiBindings](const std::string& id) -> std::string {
        auto it = midiBindings.find(id);
        return it == midiBindings.end() ? "" : it->second;
    };

    auto makeComboItems = [](const std::string& defaultLabel,
                             const std::vector<std::string>& values,
                             std::vector<std::string>& storage,
                             std::vector<const char*>& cstrStorage) {
        storage.clear();
        cstrStorage.clear();
        storage.push_back(defaultLabel);
        storage.insert(storage.end(), values.begin(), values.end());
        cstrStorage.reserve(storage.size());
        for (const auto& item : storage) cstrStorage.push_back(item.c_str());
    };

    auto indexFromValue = [](const std::vector<std::string>& values, const std::string& current) {
        if (current.empty()) return 0;
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (values[i] == current) return static_cast<int>(i + 1);
        }
        return 0;
    };

    auto beginLearn = [&](const std::string& id) {
        pendingMidiLearnControl = id;
        if (handlers_.onBeginMidiLearn) handlers_.onBeginMidiLearn(id);
    };

    // Draws the MIDI binding (or the pending-learn marker) under a control.
    auto drawBinding = [&](ImDrawList* dl, ImVec2 center, const std::string& id, bool onLight = false) {
        const std::string label = bindingLabel(id);
        if (pendingMidiLearnControl == id) {
            textCentered(dl, center, 11.0f, col(255, 190, 60), "...");
        } else if (!label.empty()) {
            textCentered(dl, center, 11.0f, onLight ? col(10, 60, 130) : col(120, 200, 255), label.c_str());
        } else if (midiLearnMode) {
            textCentered(dl, center, 11.0f, onLight ? col(40, 40, 46) : col(110, 110, 120), "MAP");
        }
    };

    // Rotary knob: vertical drag changes the value, double click resets it,
    // and in MIDI learn mode a click arms the control for mapping.
    auto knob = [&](const char* id, const char* label, float& value, float vMin, float vMax,
                    float vDefault, const char* fmt, ImVec2 c, float radius,
                    const std::string& bindId) -> bool {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::SetCursorScreenPos({c.x - radius, c.y - radius});
        ImGui::InvisibleButton(id, {radius * 2.0f, radius * 2.0f});
        const bool hovered = ImGui::IsItemHovered();
        bool changed = false;

        if (midiLearnMode) {
            if (ImGui::IsItemClicked()) beginLearn(bindId);
        } else {
            if (ImGui::IsItemActive() && ImGui::GetIO().MouseDelta.y != 0.0f) {
                const float speed = ImGui::GetIO().KeyShift ? 600.0f : 180.0f;
                value = std::clamp(value - ImGui::GetIO().MouseDelta.y * (vMax - vMin) / speed, vMin, vMax);
                changed = true;
            }
            if (hovered && ImGui::IsMouseDoubleClicked(0)) {
                value = vDefault;
                changed = true;
            }
            if (hovered) {
                const float wheel = ImGui::GetIO().MouseWheel;
                if (wheel != 0.0f) {
                    value = std::clamp(value + wheel * (vMax - vMin) / 50.0f, vMin, vMax);
                    changed = true;
                }
            }
        }

        const float a0 = 0.75f * kPi;
        const float a1 = 2.25f * kPi;
        const float t = (value - vMin) / (vMax - vMin);
        const float ang = a0 + (a1 - a0) * t;
        const bool bipolar = vMin < 0.0f && vMax > 0.0f;
        const float tZero = bipolar ? (0.0f - vMin) / (vMax - vMin) : 0.0f;
        const float angZero = a0 + (a1 - a0) * tZero;

        // tick ring
        const float rTick = radius + 1.0f;
        dl->PathArcTo(c, rTick, a0, a1, 40);
        dl->PathStroke(col(70, 70, 76), 0, 2.0f);
        dl->PathArcTo(c, rTick, std::min(ang, angZero), std::max(ang, angZero), 40);
        const bool armed = pendingMidiLearnControl == bindId;
        dl->PathStroke(armed ? col(255, 190, 60) : col(70, 170, 255), 0, 3.0f);

        // body
        const float rBody = radius * 0.82f;
        dl->AddCircleFilled({c.x + 1.5f, c.y + 3.0f}, rBody + 3.0f, col(0, 0, 0, 120), 40);
        dl->AddCircleFilled(c, rBody + 3.0f, col(22, 22, 24), 40);
        dl->AddCircleFilled(c, rBody, hovered ? col(48, 48, 52) : col(38, 38, 42), 40);
        dl->AddCircle(c, rBody, col(92, 92, 98), 40, 1.5f);
        dl->AddCircleFilled({c.x - rBody * 0.25f, c.y - rBody * 0.3f}, rBody * 0.55f, col(255, 255, 255, 10), 30);

        const ImVec2 dir{std::cos(ang), std::sin(ang)};
        dl->AddLine({c.x + dir.x * rBody * 0.35f, c.y + dir.y * rBody * 0.35f},
                    {c.x + dir.x * rBody * 0.9f,  c.y + dir.y * rBody * 0.9f},
                    col(245, 245, 245), 3.0f);

        panelLabel(dl, {c.x, c.y - radius - 12.0f}, label, col(18, 18, 20));

        char buf[32];
        std::snprintf(buf, sizeof(buf), fmt, value);
        if (midiLearnMode || !bindingLabel(bindId).empty() || armed) {
            drawBinding(dl, {c.x, c.y + radius + 14.0f}, bindId, true);
        } else {
            textCentered(dl, {c.x, c.y + radius + 14.0f}, 11.0f, col(45, 45, 50), buf);
        }
        return changed;
    };

    // Small rounded momentary-style button used for "<" / ">" and similar.
    auto smallButton = [&](const char* id, const char* text, ImVec2 c, ImVec2 size) -> bool {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::SetCursorScreenPos({c.x - size.x * 0.5f, c.y - size.y * 0.5f});
        const bool clicked = ImGui::InvisibleButton(id, size);
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        const ImVec2 p0{c.x - size.x * 0.5f, c.y - size.y * 0.5f};
        const ImVec2 p1{c.x + size.x * 0.5f, c.y + size.y * 0.5f};
        dl->AddRectFilled(p0, p1, held ? col(60, 60, 66) : hovered ? col(44, 44, 50) : col(28, 28, 32), 4.0f);
        dl->AddRect(p0, p1, col(95, 95, 102), 4.0f, 0, 1.2f);
        textCentered(dl, c, 13.0f, col(225, 225, 230), text);
        return clicked;
    };

    while (!glfwWindowShouldClose(impl_->window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const auto snapshot = linkManager.snapshot();
        const double beatPhase = snapshot.beat - std::floor(snapshot.beat);
        const int beatInBar = static_cast<int>(std::floor(snapshot.beat)) & 3;

        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        int fbW{0}, fbH{0};
        glfwGetFramebufferSize(impl_->window, &fbW, &fbH);
        ImGui::SetNextWindowSize({static_cast<float>(fbW), static_cast<float>(fbH)}, ImGuiCond_Always);
        ImGui::Begin("X-1000 | @manoelpiovesan", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 o = ImGui::GetWindowPos();
        auto P = [&o](float x, float y) { return ImVec2{o.x + x, o.y + y}; };
        const float W = static_cast<float>(fbW);
        const float H = static_cast<float>(fbH);

        // ---- chassis ----
        dl->AddRectFilledMultiColor(P(0, 0), P(W, H), col(34, 34, 37), col(34, 34, 37), col(12, 12, 14), col(12, 12, 14));
        dl->AddRect(P(1, 1), P(W - 1, H - 1), col(70, 70, 76), 0.0f, 0, 2.0f);

        // brand
        dl->AddText(ImGui::GetFont(), 20.0f, P(W - 245, 8), col(240, 240, 242), "X-1000");
        dl->AddText(ImGui::GetFont(), 11.0f, P(W - 245, 31), col(150, 150, 158), "REMIX STATION");

        // ---- header: BPM LCD, beat LEDs, link ----
        // QUANTIZE on/off, grid selector
        {
            const ImVec2 sz{80.0f, 22.0f};
            ImGui::SetCursorScreenPos(P(30, 16));
            if (ImGui::InvisibleButton("##quantize", sz)) {
                if (midiLearnMode) {
                    beginLearn("quantize");
                } else {
                    cfg.quantizeEnabled = !cfg.quantizeEnabled;
                    if (handlers_.onQuantizeChange) handlers_.onQuantizeChange(cfg.quantizeEnabled);
                }
            }
            const ImVec2 q0 = P(30, 16), q1 = P(30 + sz.x, 16 + sz.y);
            const bool on = cfg.quantizeEnabled;
            if (on) dl->AddRectFilled({q0.x - 3, q0.y - 3}, {q1.x + 3, q1.y + 3}, col(255, 50, 50, 60), 5.0f);
            dl->AddRectFilled(q0, q1, on ? col(205, 40, 40) : col(70, 24, 24), 3.0f);
            dl->AddRect(q0, q1, col(215, 215, 220), 3.0f, 0, 1.5f);
            textCentered(dl, {(q0.x + q1.x) * 0.5f, (q0.y + q1.y) * 0.5f}, 12.0f,
                         on ? col(255, 255, 255) : col(190, 150, 150), "QUANTIZE");
            if (midiLearnMode || !bindingLabel("quantize").empty() || pendingMidiLearnControl == "quantize")
                drawBinding(dl, P(70, 50), "quantize");

            const int grid = std::clamp(cfg.quantizeGrid, 0, 4);
            char gl[24];
            std::snprintf(gl, sizeof(gl), "GRID %s", kRollLabels[grid]);
            if (smallButton("##qgrid", gl, P(70, 64), {80, 18})) {
                if (!midiLearnMode) {
                    cfg.quantizeGrid = (grid + 1) % 5;
                    if (handlers_.onQuantizeGridChange) handlers_.onQuantizeGridChange(cfg.quantizeGrid);
                }
            }
        }

        {
            const ImVec2 l0 = P(150, 14), l1 = P(290, 66);
            dl->AddRectFilled(l0, l1, col(8, 4, 4), 4.0f);
            dl->AddRect(l0, l1, col(80, 80, 86), 4.0f, 0, 2.0f);
            char bpm[16];
            std::snprintf(bpm, sizeof(bpm), "%3d", static_cast<int>(std::lround(snapshot.tempoBpm)));
            ImFont* f = ImGui::GetFont();
            dl->AddText(f, 44.0f, P(158, 14), col(255, 55, 45), bpm);
            dl->AddText(f, 11.0f, P(260, 46), col(200, 60, 55), "BPM");
        }

        for (int i = 0; i < 4; ++i) {
            const bool lit = snapshot.connected && i == beatInBar;
            const ImVec2 c = P(325.0f + i * 26.0f, 30.0f);
            dl->AddRectFilled({c.x - 9, c.y - 5}, {c.x + 9, c.y + 5},
                              lit ? col(70, 255, 90, 255) : col(24, 52, 28), 2.0f);
            if (lit) dl->AddRect({c.x - 11, c.y - 7}, {c.x + 11, c.y + 7}, col(70, 255, 90, 90), 3.0f, 0, 3.0f);
        }
        panelLabel(dl, P(363, 49), "BEAT", col(150, 150, 158));

        {
            const ImVec2 c = P(450, 30);
            dl->AddCircleFilled(c, 6.0f, snapshot.connected ? col(70, 255, 90) : col(120, 50, 40), 20);
            if (snapshot.connected) dl->AddCircle(c, 9.0f, col(70, 255, 90, 90), 20, 3.0f);
            panelLabel(dl, P(450, 49), snapshot.connected ? "LINK" : "NO LINK", col(150, 150, 158));
        }

        // TAP tempo and NUDGE (beat phase)
        {
            auto mapped = [&](const char* id, ImVec2 c, const char* text, ImVec2 size) {
                const bool clicked = smallButton(id, text, c, size);
                return clicked;
            };
            if (mapped("##tap", P(330, 70), "TAP", {50, 18})) {
                if (midiLearnMode) beginLearn("tap");
                else if (handlers_.onTap) handlers_.onTap();
            }
            if (mapped("##nudgem", P(388, 70), "-", {26, 18})) {
                if (midiLearnMode) beginLearn("nudge_minus");
                else if (handlers_.onNudge) handlers_.onNudge(-1);
            }
            if (mapped("##nudgep", P(492, 70), "+", {26, 18})) {
                if (midiLearnMode) beginLearn("nudge_plus");
                else if (handlers_.onNudge) handlers_.onNudge(1);
            }
            char nb[24];
            std::snprintf(nb, sizeof(nb), "NUDGE %+.2f", cfg.beatOffset);
            panelLabel(dl, P(440, 70), nb, col(150, 150, 158));
            if (midiLearnMode) {
                drawBinding(dl, P(330, 86), "tap");
                drawBinding(dl, P(388, 86), "nudge_minus");
                drawBinding(dl, P(492, 86), "nudge_plus");
            }
        }

        // ---- FX source arrow panel with the three main knobs ----
        {
            const float ay0 = 92.0f, ay1 = 232.0f;
            const float x0 = 12.0f, xBody = 440.0f, xTip = 500.0f;
            const ImVec2 pts[] = {P(x0, ay0), P(xBody, ay0), P(xTip, (ay0 + ay1) * 0.5f), P(xBody, ay1), P(x0, ay1)};
            dl->AddConvexPolyFilled(pts, 5, col(150, 152, 158));
            dl->AddRectFilledMultiColor(P(x0, ay0), P(xBody, ay1), col(176, 178, 184), col(176, 178, 184),
                                        col(104, 106, 112), col(104, 106, 112));
            dl->AddPolyline(pts, 5, col(205, 207, 212), ImDrawFlags_Closed, 1.5f);
            // dark tab on the left, like FX SOURCE
            dl->AddRectFilled(P(x0, ay0 + 22), P(x0 + 44, ay1 - 22), col(20, 20, 22), 3.0f);
            textCentered(dl, P(x0 + 22, (ay0 + ay1) * 0.5f - 12), 11.0f, col(220, 220, 225), "FX");
            textCentered(dl, P(x0 + 22, (ay0 + ay1) * 0.5f + 2), 11.0f, col(220, 220, 225), "SRC");
        }

        {
            float mv = cfg.masterVolume;
            if (knob("##master", "MASTER", mv, 0.0f, 1.0f, 1.0f, "%.2f", P(120, 164), 34.0f, "master")) {
                cfg.masterVolume = mv;
                if (handlers_.onMasterVolumeChange) handlers_.onMasterVolumeChange(mv);
            }
            float pitch = cfg.pitchSemitones;
            if (knob("##pitch", "PITCH", pitch, -12.0f, 12.0f, 0.0f, "%+.1f st", P(235, 164), 34.0f, "pitch")) {
                cfg.pitchSemitones = pitch;
                if (handlers_.onPitchChange) handlers_.onPitchChange(pitch);
            }
            float filter = cfg.filterAmount;
            if (knob("##filter", "FILTER", filter, -1.0f, 1.0f, 0.0f, "%+.2f", P(350, 164), 34.0f, "filter")) {
                cfg.filterAmount = filter;
                if (handlers_.onFilterChange) handlers_.onFilterChange(filter);
            }
        }

        // ---- ROLL pad row ----
        {
            const float by = 292.0f;
            panelLabel(dl, P(44, by - 54), "ROLL FX", col(150, 150, 158));
            const float bw = 78.0f, bh = 36.0f, gap = 10.0f, bx0 = 22.0f;
            for (int i = 0; i < 5; ++i) {
                const float x = bx0 + i * (bw + gap);
                const bool isActive = (activeRollButton == i);
                const std::string rollId = "roll_" + std::to_string(i);

                char num[4];
                std::snprintf(num, sizeof(num), "%d", i + 1);
                dl->AddRectFilled(P(x + bw * 0.5f - 7, by - 38), P(x + bw * 0.5f + 7, by - 22), col(20, 20, 22), 2.0f);
                dl->AddRect(P(x + bw * 0.5f - 7, by - 38), P(x + bw * 0.5f + 7, by - 22), col(190, 190, 196), 2.0f, 0, 1.0f);
                textCentered(dl, P(x + bw * 0.5f, by - 30), 11.0f, col(230, 230, 235), num);

                ImGui::SetCursorScreenPos(P(x, by - bh * 0.5f));
                const std::string btnId = "##roll" + std::to_string(i);
                if (ImGui::InvisibleButton(btnId.c_str(), {bw, bh})) {
                    if (midiLearnMode) {
                        beginLearn(rollId);
                    } else if (isActive) {
                        activeRollButton = -1;
                        cfg.globalQuantization = -1;
                        if (handlers_.onRulerChange) handlers_.onRulerChange(-1);
                    } else {
                        activeRollButton = i;
                        cfg.globalQuantization = i;
                        if (handlers_.onRulerChange) handlers_.onRulerChange(i);
                    }
                }
                const bool hov = ImGui::IsItemHovered();
                const bool held = ImGui::IsItemActive();
                const ImVec2 p0 = P(x, by - bh * 0.5f), p1 = P(x + bw, by + bh * 0.5f);
                dl->AddRectFilled({p0.x - 3, p0.y - 3}, {p1.x + 3, p1.y + 3}, col(0, 0, 0, 140), 6.0f);
                dl->AddRectFilled(p0, p1, held ? col(50, 50, 54) : hov ? col(40, 40, 44) : col(28, 28, 31), 5.0f);
                dl->AddRect(p0, p1, col(110, 110, 118), 5.0f, 0, 1.5f);
                // LED window
                const ImVec2 l0{p0.x + 8, p0.y + 5}, l1{p1.x - 8, p0.y + 15};
                if (isActive) {
                    dl->AddRectFilled({l0.x - 4, l0.y - 4}, {l1.x + 4, l1.y + 4}, col(90, 255, 70, 60), 5.0f);
                    dl->AddRectFilled(l0, l1, col(140, 255, 90), 3.0f);
                } else {
                    dl->AddRectFilled(l0, l1, col(30, 64, 30), 3.0f);
                }
                textCentered(dl, {(p0.x + p1.x) * 0.5f, p1.y - 10.0f}, 13.0f,
                             isActive ? col(255, 255, 255) : col(200, 200, 206), kRollLabels[i]);

                if (midiLearnMode || !bindingLabel(rollId).empty() || pendingMidiLearnControl == rollId) {
                    drawBinding(dl, P(x + bw * 0.5f, by + bh * 0.5f + 12.0f), rollId);
                }
            }
        }

        // ---- settings / routing strip ----
        {
            const float sy = 342.0f;
            dl->AddLine(P(14, sy - 12), P(500, sy - 12), col(70, 70, 76), 1.0f);

            std::vector<std::string> midiComboStorage;
            std::vector<const char*> midiComboItems;
            makeComboItems("Auto (first available)", midiPorts, midiComboStorage, midiComboItems);
            int midiSelected = indexFromValue(midiPorts, cfg.midiPortName);
            ImGui::SetCursorScreenPos(P(80, sy));
            ImGui::SetNextItemWidth(280.0f);
            if (ImGui::Combo("##midiin", &midiSelected, midiComboItems.data(), static_cast<int>(midiComboItems.size()))) {
                const std::string selectedPort = (midiSelected <= 0 || midiSelected > static_cast<int>(midiPorts.size()))
                    ? std::string{}
                    : midiPorts[static_cast<std::size_t>(midiSelected - 1)];
                if (handlers_.onMidiPortChange) handlers_.onMidiPortChange(selectedPort);
            }
            dl->AddText(ImGui::GetFont(), 12.0f, P(18, sy + 3), col(170, 170, 178), "MIDI IN");

            std::vector<std::string> audioComboStorage;
            std::vector<const char*> audioComboItems;
            makeComboItems("Default output", audioDevices, audioComboStorage, audioComboItems);
            int audioSelected = indexFromValue(audioDevices, cfg.audioDevice);
            ImGui::SetCursorScreenPos(P(80, sy + 30));
            ImGui::SetNextItemWidth(280.0f);
            if (ImGui::Combo("##audioout", &audioSelected, audioComboItems.data(), static_cast<int>(audioComboItems.size()))) {
                const std::string selectedDevice = (audioSelected <= 0 || audioSelected > static_cast<int>(audioDevices.size()))
                    ? std::string{}
                    : audioDevices[static_cast<std::size_t>(audioSelected - 1)];
                if (handlers_.onAudioDeviceChange) handlers_.onAudioDeviceChange(selectedDevice);
            }
            dl->AddText(ImGui::GetFont(), 12.0f, P(18, sy + 33), col(170, 170, 178), "AUDIO");

            // MIDI LEARN as an LED button
            {
                const ImVec2 sz{110.0f, 24.0f};
                ImGui::SetCursorScreenPos(P(380, sy - 1));
                if (ImGui::InvisibleButton("##learn", sz)) {
                    midiLearnMode = !midiLearnMode;
                    if (handlers_.onSetMidiLearnMode) handlers_.onSetMidiLearnMode(midiLearnMode);
                }
                const ImVec2 p0 = P(380, sy - 1), p1 = P(380 + sz.x, sy - 1 + sz.y);
                dl->AddRectFilled(p0, p1, midiLearnMode ? col(120, 70, 15) : col(28, 28, 32), 4.0f);
                dl->AddRect(p0, p1, midiLearnMode ? col(255, 170, 40) : col(95, 95, 102), 4.0f, 0, 1.5f);
                dl->AddCircleFilled({p0.x + 12, (p0.y + p1.y) * 0.5f}, 4.0f,
                                    midiLearnMode ? col(255, 190, 60) : col(80, 60, 30), 12);
                dl->AddText(ImGui::GetFont(), 12.0f, {p0.x + 24, p0.y + 5}, col(225, 225, 230), "MIDI LEARN");
            }

            // MIDI status
            if (handlers_.onGetMidiStatus) {
                const auto [portLabel, msgLabel] = handlers_.onGetMidiStatus();
                const bool midiConnected = !portLabel.empty() && portLabel != "(none)";
                const ImVec2 c = P(388, sy + 43);
                dl->AddCircleFilled(c, 4.0f, midiConnected ? col(70, 255, 90) : col(240, 110, 70), 12);
                const std::string status = midiConnected ? portLabel : std::string("No MIDI");
                dl->AddText(ImGui::GetFont(), 12.0f, P(398, sy + 36), col(190, 190, 198), status.substr(0, 14).c_str());
                dl->AddText(ImGui::GetFont(), 11.0f, P(18, sy + 66), col(120, 120, 128),
                            ("Last: " + msgLabel).c_str());
            }
            if (midiLearnMode) {
                const std::string hint = pendingMidiLearnControl.empty()
                    ? std::string("Click a control, then move your MIDI controller")
                    : "Waiting for MIDI: " + pendingMidiLearnControl;
                dl->AddText(ImGui::GetFont(), 11.0f, P(18, sy + 80), col(255, 190, 60), hint.c_str());
            }
        }

        // ---- scene FX style ring: sample selector ----
        {
            const ImVec2 c = P(740.0f, 220.0f);
            const float rOuter = 175.0f, rSegOut = 152.0f, rSegIn = 106.0f, rCore = 98.0f;
            const int nSeg = 12;
            const int nSamples = static_cast<int>(sampleNames.size());
            const int curSample = nSamples > 0 ? std::clamp(selectedSampleIndex, 0, nSamples - 1) : -1;

            dl->AddCircleFilled({c.x + 2, c.y + 5}, rOuter, col(0, 0, 0, 150), 80);
            dl->AddCircleFilled(c, rOuter, col(26, 26, 29), 80);
            dl->AddCircle(c, rOuter, col(120, 120, 128), 80, 3.0f);
            dl->AddCircle(c, rOuter - 4, col(10, 10, 12), 80, 2.0f);

            ImGui::SetCursorScreenPos({c.x - rOuter, c.y - rOuter});
            ImGui::InvisibleButton("##ring", {rOuter * 2.0f, rOuter * 2.0f});
            const bool ringHover = ImGui::IsItemHovered();
            const ImVec2 mp = ImGui::GetIO().MousePos;
            const float md = std::hypot(mp.x - c.x, mp.y - c.y);
            int hoverSeg = -1;
            if (ringHover && md >= rSegIn && md <= rSegOut) {
                float a = std::atan2(mp.y - c.y, mp.x - c.x);   // -pi..pi, 0 at +x
                float rel = a - kPi;                            // segments start on the left
                while (rel < 0.0f) rel += 2.0f * kPi;
                hoverSeg = std::min(nSeg - 1, static_cast<int>(rel / (2.0f * kPi / nSeg)));
            }
            if (ImGui::IsItemClicked() && hoverSeg >= 0 && !midiLearnMode && hoverSeg < nSamples) {
                selectedSampleIndex = hoverSeg;
                if (handlers_.onSampleSelectionChange) handlers_.onSampleSelectionChange(hoverSeg);
            }

            const float step = 2.0f * kPi / nSeg;
            const float gapA = 0.05f;
            const float rMid = (rSegIn + rSegOut) * 0.5f;
            const float thick = rSegOut - rSegIn;
            for (int i = 0; i < nSeg; ++i) {
                const bool top = i < nSeg / 2;
                const bool exists = i < nSamples;
                const bool sel = i == curSample;
                const float a0 = kPi + i * step + gapA;
                const float a1 = kPi + (i + 1) * step - gapA;
                ImU32 base = top ? (exists ? col(120, 24, 28) : col(58, 20, 22))
                                 : (exists ? col(26, 60, 128) : col(24, 34, 62));
                ImU32 lit  = top ? col(255, 70, 60) : col(70, 160, 255);
                if (sel) {
                    dl->PathArcTo(c, rMid, a0, a1, 12);
                    dl->PathStroke(top ? col(255, 70, 60, 70) : col(70, 160, 255, 70), 0, thick + 10.0f);
                }
                dl->PathArcTo(c, rMid, a0, a1, 12);
                dl->PathStroke(sel ? lit : (i == hoverSeg && exists) ? col(160, 160, 170, 255) : base, 0, thick - 4.0f);
                if (exists) {
                    const float am = (a0 + a1) * 0.5f;
                    char num[4];
                    std::snprintf(num, sizeof(num), "%d", i + 1);
                    textCentered(dl, {c.x + std::cos(am) * rMid, c.y + std::sin(am) * rMid}, 13.0f,
                                 sel ? col(20, 20, 22) : col(235, 235, 240), num);
                }
            }

            // inner core with glowing beat pulse
            dl->AddCircleFilled(c, rCore, col(8, 8, 10), 80);
            dl->AddCircle(c, rCore, col(70, 70, 76), 80, 2.0f);
            const float pulse = snapshot.connected ? static_cast<float>(1.0 - beatPhase) : 0.4f;
            const int glowA = static_cast<int>(50 + 90 * pulse);
            for (int k = 5; k >= 1; --k) {
                dl->AddCircleFilled(c, 50.0f + k * 5.0f, col(40, 120, 255, glowA / (k + 1)), 60);
            }
            dl->AddCircleFilled(c, 50.0f, col(14, 40, 110), 60);
            dl->AddCircle(c, 50.0f, col(90, 190, 255, 220), 60, 4.0f);
            dl->AddCircle(c, 44.0f, col(150, 220, 255, 120), 60, 1.5f);
            dl->AddCircleFilled(c, 38.0f, col(8, 18, 50), 60);

            panelLabel(dl, {c.x, c.y - 76.0f}, "SAMPLE", col(150, 160, 190));
            if (curSample >= 0) {
                std::string name = sampleNames[static_cast<std::size_t>(curSample)];
                if (name.size() > 12) name = name.substr(0, 11) + "~";
                textCentered(dl, c, 15.0f, col(235, 245, 255), name.c_str());
            } else {
                textCentered(dl, c, 11.0f, col(235, 245, 255), "no samples");
            }

            // prev / next
            if (nSamples > 0) {
                const ImVec2 prevC{c.x - 36.0f, c.y + 68.0f}, nextC{c.x + 36.0f, c.y + 68.0f};
                if (smallButton("##prev", "<", prevC, {40, 22})) {
                    if (midiLearnMode) {
                        beginLearn("sample_prev");
                    } else {
                        selectedSampleIndex = (curSample - 1 + nSamples) % nSamples;
                        if (handlers_.onSampleSelectionChange) handlers_.onSampleSelectionChange(selectedSampleIndex);
                    }
                }
                if (smallButton("##next", ">", nextC, {40, 22})) {
                    if (midiLearnMode) {
                        beginLearn("sample_next");
                    } else {
                        selectedSampleIndex = (curSample + 1) % nSamples;
                        if (handlers_.onSampleSelectionChange) handlers_.onSampleSelectionChange(selectedSampleIndex);
                    }
                }
                if (midiLearnMode || !bindingLabel("sample_prev").empty() || pendingMidiLearnControl == "sample_prev")
                    drawBinding(dl, {prevC.x, prevC.y + 20.0f}, "sample_prev");
                if (midiLearnMode || !bindingLabel("sample_next").empty() || pendingMidiLearnControl == "sample_next")
                    drawBinding(dl, {nextC.x, nextC.y + 20.0f}, "sample_next");
            }
        }

        ImGui::End();

        ImGui::Render();
        glClearColor(0.05f, 0.05f, 0.06f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(impl_->window);
    }
}

} // namespace xpad::gui
