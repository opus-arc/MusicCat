#include "musiccat.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using Clock = std::chrono::steady_clock;

musiccat::Snapshot playing(double position, Clock::time_point at, std::string id = "track-a") {
    musiccat::Snapshot value;
    value.query_ok = true;
    value.music_running = true;
    value.state = musiccat::PlayerState::playing;
    value.position_seconds = position;
    value.observed_at = at;
    value.metadata.id = std::move(id);
    value.metadata.title = "Track";
    value.metadata.artist = "Artist";
    value.metadata.album = "Album";
    value.metadata.duration_seconds = 60.0;
    value.metadata.source_path = "/tmp/downloaded-track.m4a";
    return value;
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    const auto origin = Clock::now();

    {
        const std::string long_unicode = std::string(99, 'a') + "猫";
        const std::string sanitized = musiccat::sanitize_component(long_unicode, 100);
        require(sanitized == std::string(99, 'a'),
                "UTF-8 truncation never writes a partial code point");
        require(musiccat::sanitize_component("Album/Track:  One") == "Album - Track - One",
                "filesystem separators are replaced and whitespace is normalized");
    }

    {
        musiccat::CaptureStateMachine state;
        require(state.observe(playing(0.4, origin)).kind == musiccat::DecisionKind::start,
                "a clean beginning starts recording");
        require(state.observe(playing(10.4, origin + std::chrono::seconds(10))).kind == musiccat::DecisionKind::none,
                "normal playback continues");
        require(state.observe(playing(58.2, origin + std::chrono::seconds(58))).kind == musiccat::DecisionKind::none,
                "near-end playback remains active");
        const auto next_track = playing(0.2, origin + std::chrono::seconds(59), "track-b");
        require(state.observe(next_track).kind == musiccat::DecisionKind::complete,
                "a near-end track change completes the capture");
        require(state.observe(next_track).kind == musiccat::DecisionKind::start,
                "the same boundary snapshot immediately starts the next track");
    }

    {
        musiccat::CaptureStateMachine state;
        require(state.observe(playing(1.0, origin)).kind == musiccat::DecisionKind::none,
                "a late start is ignored");
    }

    {
        musiccat::CaptureStateMachine state;
        auto streaming = playing(0.2, origin);
        streaming.metadata.source_path.clear();
        require(state.observe(streaming).kind == musiccat::DecisionKind::start,
                "a clean streaming track beginning is accepted without a local source");
    }

    {
        musiccat::CaptureStateMachine state;
        require(state.observe(playing(0.2, origin)).kind == musiccat::DecisionKind::start,
                "recording starts before a seek test");
        require(state.observe(playing(12.0, origin + std::chrono::seconds(2))).kind == musiccat::DecisionKind::none,
                "one forward position anomaly waits for confirmation");
        require(state.observe(playing(12.4, origin + std::chrono::milliseconds(2400))).kind == musiccat::DecisionKind::reject,
                "a confirmed forward seek rejects the capture");
    }

    {
        musiccat::CaptureStateMachine state;
        require(state.observe(playing(0.2, origin)).kind == musiccat::DecisionKind::start,
                "recording starts before a pause test");
        auto paused = playing(20.0, origin + std::chrono::seconds(20));
        paused.state = musiccat::PlayerState::paused;
        require(state.observe(paused).kind == musiccat::DecisionKind::none,
                "one paused sample is tolerated");
        paused.observed_at += std::chrono::milliseconds(350);
        require(state.observe(paused).kind == musiccat::DecisionKind::reject,
                "confirmed early pause rejects the capture");
    }

    {
        musiccat::CaptureStateMachine state;
        require(state.observe(playing(0.2, origin)).kind == musiccat::DecisionKind::start,
                "recording starts before an outage test");
        musiccat::Snapshot unavailable;
        unavailable.observed_at = origin;
        require(state.observe(unavailable).kind == musiccat::DecisionKind::none,
                "an Apple Music query outage starts a grace window");
        unavailable.observed_at = origin + std::chrono::seconds(20);
        require(state.observe(unavailable).kind == musiccat::DecisionKind::none,
                "a short Apple Music query outage is tolerated by elapsed time");
        unavailable.observed_at = origin + std::chrono::seconds(31);
        require(state.observe(unavailable).kind == musiccat::DecisionKind::reject,
                "a sustained Apple Music outage rejects the capture");
    }

    {
        musiccat::CaptureStateMachine state;
        require(state.observe(playing(0.0, origin)).kind == musiccat::DecisionKind::start,
                "recording starts while Apple Music is initially buffering");
        require(state.observe(playing(0.0, origin + std::chrono::seconds(10))).kind == musiccat::DecisionKind::none,
                "initial streaming buffer delay is tolerated");
        require(state.observe(playing(1.0, origin + std::chrono::seconds(11))).kind == musiccat::DecisionKind::none,
                "playback advancement ends startup buffering");
        require(state.observe(playing(1.0, origin + std::chrono::seconds(14))).kind == musiccat::DecisionKind::reject,
                "a later sustained stall rejects the capture");
    }

    {
        musiccat::CaptureStateMachine state;
        require(state.observe(playing(0.2, origin)).kind == musiccat::DecisionKind::start,
                "recording starts before a backward seek test");
        require(state.observe(playing(10.0, origin + std::chrono::seconds(10))).kind == musiccat::DecisionKind::none,
                "normal playback reaches the seek point");
        require(state.observe(playing(3.0, origin + std::chrono::seconds(11))).kind == musiccat::DecisionKind::none,
                "one backward position anomaly waits for confirmation");
        require(state.observe(playing(3.4, origin + std::chrono::milliseconds(11400))).kind == musiccat::DecisionKind::reject,
                "a confirmed backward seek rejects the capture");
    }

    {
        musiccat::CaptureStateMachine state;
        require(state.observe(playing(0.2, origin)).kind == musiccat::DecisionKind::start,
                "recording starts before a transient position anomaly");
        require(state.observe(playing(10.0, origin + std::chrono::seconds(10))).kind == musiccat::DecisionKind::none,
                "normal playback reaches the anomaly point");
        require(state.observe(playing(0.0, origin + std::chrono::milliseconds(10350))).kind == musiccat::DecisionKind::none,
                "a single zero position sample is tolerated");
        require(state.observe(playing(10.7, origin + std::chrono::milliseconds(10700))).kind == musiccat::DecisionKind::none,
                "normal progress clears a transient anomaly");
    }

    {
        musiccat::CaptureStateMachine state;
        require(state.observe(playing(0.2, origin)).kind == musiccat::DecisionKind::start,
                "recording starts before repeat-at-end test");
        require(state.observe(playing(58.5, origin + std::chrono::seconds(58))).kind == musiccat::DecisionKind::none,
                "playback reaches the repeat window");
        require(state.observe(playing(0.1, origin + std::chrono::seconds(60))).kind == musiccat::DecisionKind::complete,
                "same-track repeat at the natural end completes the capture");
    }

    {
        musiccat::CaptureStateMachine state;
        require(state.observe(playing(0.2, origin)).kind == musiccat::DecisionKind::start,
                "recording starts before a natural-end pause");
        require(state.observe(playing(58.5, origin + std::chrono::seconds(58))).kind == musiccat::DecisionKind::none,
                "playback reaches the end window");
        auto paused = playing(59.0, origin + std::chrono::seconds(59));
        paused.state = musiccat::PlayerState::paused;
        require(state.observe(paused).kind == musiccat::DecisionKind::none,
                "one end pause sample is tolerated");
        paused.observed_at += std::chrono::milliseconds(350);
        require(state.observe(paused).kind == musiccat::DecisionKind::complete,
                "a confirmed pause near the natural end completes the capture");
    }

    std::cout << "MusicCat state-machine tests passed.\n";
    return EXIT_SUCCESS;
}
