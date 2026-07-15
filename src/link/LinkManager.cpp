#include "link/LinkManager.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#if defined(XPAD_HAS_ABLETON_LINK)
#include <ableton/Link.hpp>
#endif

namespace xpad::link {

struct LinkManager::Impl {
#if defined(XPAD_HAS_ABLETON_LINK)
    explicit Impl(double initialTempoBpm)
        : link(initialTempoBpm) {}

    ableton::Link link;
#endif
};

LinkManager::LinkManager()
    : LinkManager(Config{}) {
}

LinkManager::LinkManager(Config config)
    : config_(config) {
    const auto now = Clock::now();
    transport_.tempoBpm = config_.initialTempoBpm;
    transport_.originTime = now;
    transport_.beatAtOrigin = 0.0;
}

LinkManager::~LinkManager() = default;

void LinkManager::start() {
    std::scoped_lock lock(mutex_);
    if (running_) {
        return;
    }

    running_ = true;
    transport_.originTime = Clock::now();
    transport_.beatAtOrigin = 0.0;
    transport_.tempoBpm = config_.initialTempoBpm;
    transport_.isPlaying = true;

#if defined(XPAD_HAS_ABLETON_LINK)
    if (config_.preferAbletonLink) {
        impl_ = std::make_unique<Impl>(config_.initialTempoBpm);
        impl_->link.enableStartStopSync(true);
        impl_->link.enable(true);
        activeMode_ = LinkMode::AbletonLink;
        transport_.connected = impl_->link.isEnabled() && impl_->link.numPeers() > 0;
        transport_.peerCount = static_cast<std::int64_t>(impl_->link.numPeers());
        return;
    }
#endif

    activeMode_ = LinkMode::Simulation;
    transport_.connected = false;
    transport_.peerCount = 0;
}

void LinkManager::stop() {
    std::scoped_lock lock(mutex_);
#if defined(XPAD_HAS_ABLETON_LINK)
    if (impl_ && impl_->link.isEnabled()) {
        impl_->link.enable(false);
    }
#endif
    running_ = false;
    activeMode_ = LinkMode::Simulation;
    impl_.reset();
}

LinkSnapshot LinkManager::snapshot() const {
    const auto now = Clock::now();

    if (mode() == LinkMode::AbletonLink) {
        return readAbletonSnapshot(now);
    }

    return readSimulationSnapshot(now);
}

double LinkManager::beatAt(TimePoint timePoint) const {
    std::scoped_lock lock(mutex_);
    return beatAtLocked(timePoint);
}

void LinkManager::setTempoBpm(double tempoBpm) {
    if (!(tempoBpm > 0.0) || !std::isfinite(tempoBpm)) {
        throw std::invalid_argument("tempoBpm must be a finite value greater than zero");
    }

    std::scoped_lock lock(mutex_);
    const auto now = Clock::now();

#if defined(XPAD_HAS_ABLETON_LINK)
    if (activeMode_ == LinkMode::AbletonLink && impl_) {
        auto sessionState = impl_->link.captureAppSessionState();
        const auto linkTimeMicros = impl_->link.clock().micros();
        const double currentBeat = sessionState.beatAtTime(linkTimeMicros, config_.quantum);
        sessionState.setTempo(tempoBpm, linkTimeMicros);
        impl_->link.commitAppSessionState(sessionState);

        transport_.beatAtOrigin = currentBeat;
        transport_.originTime = now;
        transport_.tempoBpm = tempoBpm;
        return;
    }
#endif

    transport_.beatAtOrigin = beatAtLocked(now);
    transport_.originTime = now;
    transport_.tempoBpm = tempoBpm;
}

LinkMode LinkManager::mode() const noexcept {
    std::scoped_lock lock(mutex_);
    return activeMode_;
}

const char* LinkManager::modeName() const noexcept {
    return mode() == LinkMode::AbletonLink ? "Ableton Link" : "Simulation";
}

double LinkManager::beatAtLocked(TimePoint timePoint) const {
    const auto elapsed = std::chrono::duration<double>(timePoint - transport_.originTime).count();
    return transport_.beatAtOrigin + (elapsed * transport_.tempoBpm / 60.0);
}

LinkSnapshot LinkManager::readSimulationSnapshot(TimePoint now) const {
    std::scoped_lock lock(mutex_);
    return LinkSnapshot{
        .mode = LinkMode::Simulation,
        .running = running_,
        .connected = transport_.connected,
        .isPlaying = transport_.isPlaying,
        .peerCount = transport_.peerCount,
        .tempoBpm = transport_.tempoBpm,
        .beat = beatAtLocked(now),
        .capturedAt = now,
    };
}

LinkSnapshot LinkManager::readAbletonSnapshot(TimePoint now) const {
#if defined(XPAD_HAS_ABLETON_LINK)
    std::scoped_lock lock(mutex_);
    if (!impl_) {
        return LinkSnapshot{
            .mode = LinkMode::Simulation,
            .running = running_,
            .connected = transport_.connected,
            .isPlaying = transport_.isPlaying,
            .peerCount = transport_.peerCount,
            .tempoBpm = transport_.tempoBpm,
            .beat = beatAtLocked(now),
            .capturedAt = now,
        };
    }

    auto sessionState = impl_->link.captureAppSessionState();
    const auto linkTimeMicros = impl_->link.clock().micros();
    const double tempoBpm = sessionState.tempo();
    const double beat = sessionState.beatAtTime(linkTimeMicros, config_.quantum);
    const auto peerCount = static_cast<std::int64_t>(impl_->link.numPeers());
    const bool linkEnabled = impl_->link.isEnabled();

    transport_.tempoBpm = tempoBpm;
    transport_.originTime = now;
    transport_.beatAtOrigin = beat;
    transport_.connected = linkEnabled && peerCount > 0;
    transport_.peerCount = peerCount;
    transport_.isPlaying = sessionState.isPlaying();

    return LinkSnapshot{
        .mode = LinkMode::AbletonLink,
        .running = running_,
        .connected = transport_.connected,
        .isPlaying = transport_.isPlaying,
        .peerCount = transport_.peerCount,
        .tempoBpm = transport_.tempoBpm,
        .beat = transport_.beatAtOrigin,
        .capturedAt = now,
    };
#else
    (void) now;
    return readSimulationSnapshot(Clock::now());
#endif
}

} // namespace xpad::link




