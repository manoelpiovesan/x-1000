#pragma once

#include "link/LinkTypes.hpp"

#include <chrono>
#include <memory>
#include <mutex>

namespace xpad::link {

class LinkManager {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    struct Config {
        double initialTempoBpm{126.0};
        double quantum{4.0};
        bool preferAbletonLink{true};
    };

    LinkManager();
    explicit LinkManager(Config config);
    ~LinkManager();

    void start();
    void stop();

    [[nodiscard]] LinkSnapshot snapshot() const;
    [[nodiscard]] double beatAt(TimePoint timePoint) const;

    void setTempoBpm(double tempoBpm);

    [[nodiscard]] LinkMode mode() const noexcept;
    [[nodiscard]] const char* modeName() const noexcept;

private:
    struct TransportState {
        double tempoBpm{126.0};
        double beatAtOrigin{0.0};
        TimePoint originTime{Clock::now()};
        bool connected{false};
        bool isPlaying{true};
        std::int64_t peerCount{0};
    };

    struct Impl;

    [[nodiscard]] double beatAtLocked(TimePoint timePoint) const;
    [[nodiscard]] LinkSnapshot readSimulationSnapshot(TimePoint now) const;
    [[nodiscard]] LinkSnapshot readAbletonSnapshot(TimePoint now) const;

    Config config_;
    mutable std::mutex mutex_;
    mutable TransportState transport_;
    LinkMode activeMode_{LinkMode::Simulation};
    bool running_{false};
    std::unique_ptr<Impl> impl_;
};

} // namespace xpad::link



