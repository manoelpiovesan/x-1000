#pragma once

#include <chrono>
#include <cstdint>

namespace xpad::link {

enum class LinkMode {
    Simulation,
    AbletonLink
};

struct LinkSnapshot {
    LinkMode mode{LinkMode::Simulation};
    bool running{false};
    bool connected{false};
    bool isPlaying{true};
    std::int64_t peerCount{0};
    double tempoBpm{120.0};
    double beat{0.0};
    std::chrono::steady_clock::time_point capturedAt{};
};

} // namespace xpad::link

