#include "musiccat.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>

#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace musiccat {
namespace {

constexpr char kFieldSeparator = 30;
constexpr double kAlignmentSafetySeconds = 0.150;
constexpr double kPositionAlignedTailSeconds = 0.050;

struct CommandResult {
    int status = -1;
    std::string output;
};

std::filesystem::path home_path() {
    const char* value = std::getenv("HOME");
    if (!value || !*value) {
        throw std::runtime_error("HOME is not set");
    }
    return value;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<std::string> split(const std::string& value, char separator) {
    std::vector<std::string> fields;
    std::size_t begin = 0;
    while (true) {
        const std::size_t end = value.find(separator, begin);
        if (end == std::string::npos) {
            fields.push_back(value.substr(begin));
            break;
        }
        fields.push_back(value.substr(begin, end - begin));
        begin = end + 1;
    }
    return fields;
}

double parse_double(const std::string& value) {
    if (trim(value).empty()) return 0.0;
    try {
        std::size_t used = 0;
        const double parsed = std::stod(trim(value), &used);
        return used == trim(value).size() && std::isfinite(parsed) ? parsed : 0.0;
    } catch (...) {
        return 0.0;
    }
}

int parse_int(const std::string& value) {
    return static_cast<int>(std::lround(parse_double(value)));
}

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_r(&time, &local);
    std::ostringstream out;
    out << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

std::string file_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_r(&time, &local);
    std::ostringstream out;
    out << std::put_time(&local, "%Y%m%d-%H%M%S");
    return out.str();
}

CommandResult run_process(const std::vector<std::string>& arguments, bool require_success = true,
                          std::chrono::milliseconds timeout = std::chrono::minutes(10)) {
    if (arguments.empty()) throw std::runtime_error("empty process invocation");

    int pipe_fds[2]{};
    if (pipe(pipe_fds) != 0) {
        throw std::runtime_error("pipe failed: " + std::string(std::strerror(errno)));
    }

    const pid_t child = fork();
    if (child < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        throw std::runtime_error("fork failed: " + std::string(std::strerror(errno)));
    }

    if (child == 0) {
        setpgid(0, 0);
        sigset_t empty_mask;
        sigemptyset(&empty_mask);
        pthread_sigmask(SIG_SETMASK, &empty_mask, nullptr);
        close(pipe_fds[0]);
        const int null_fd = open("/dev/null", O_RDONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            if (null_fd != STDIN_FILENO) close(null_fd);
        }
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[1]);

        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1);
        for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(pipe_fds[1]);
    const int original_flags = fcntl(pipe_fds[0], F_GETFL, 0);
    fcntl(pipe_fds[0], F_SETFL, original_flags | O_NONBLOCK);
    std::string output;
    std::array<char, 4096> buffer{};
    int wait_status = 0;
    bool child_finished = false;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        while (true) {
            const ssize_t count = read(pipe_fds[0], buffer.data(), buffer.size());
            if (count > 0) {
                output.append(buffer.data(), static_cast<std::size_t>(count));
                continue;
            }
            if (count < 0 && errno == EINTR) continue;
            break;
        }
        if (!child_finished) {
            const pid_t result = waitpid(child, &wait_status, WNOHANG);
            if (result == child || (result < 0 && errno == ECHILD)) child_finished = true;
        }
        if (child_finished) {
            const ssize_t final_count = read(pipe_fds[0], buffer.data(), buffer.size());
            if (final_count > 0) {
                output.append(buffer.data(), static_cast<std::size_t>(final_count));
                continue;
            }
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(-child, SIGKILL);
            kill(child, SIGKILL);
            while (waitpid(child, &wait_status, 0) < 0 && errno == EINTR) {}
            close(pipe_fds[0]);
            throw std::runtime_error(arguments.front() + " timed out");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    close(pipe_fds[0]);
    const int status = WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : 128;
    if (require_success && status != 0) {
        throw std::runtime_error(arguments.front() + " failed (exit " + std::to_string(status) + "): " + trim(output));
    }
    return {status, output};
}

bool executable_exists(const std::string& name) {
    const char* raw_path = std::getenv("PATH");
    if (!raw_path) return false;
    for (const auto& folder : split(raw_path, ':')) {
        const auto candidate = std::filesystem::path(folder) / name;
        if (access(candidate.c_str(), X_OK) == 0) return true;
    }
    return false;
}

void move_file(const std::filesystem::path& source, const std::filesystem::path& destination) {
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    if (!error) return;
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, error);
    if (error) throw std::runtime_error("cannot move " + source.string() + " to " + destination.string() + ": " + error.message());
    std::filesystem::remove(source, error);
}

std::filesystem::path unique_path(const std::filesystem::path& requested) {
    if (!std::filesystem::exists(requested)) return requested;
    for (int index = 1; index < 10000; ++index) {
        const auto candidate = requested.parent_path() /
            (requested.stem().string() + " (" + std::to_string(index) + ")" + requested.extension().string());
        if (!std::filesystem::exists(candidate)) return candidate;
    }
    throw std::runtime_error("cannot allocate a unique path for " + requested.string());
}

std::string metadata_argument(const std::string& key, const std::string& value) {
    return key + "=" + value;
}

void append_metadata(std::vector<std::string>& args, const Metadata& metadata) {
    const auto add = [&args](const std::string& key, const std::string& value) {
        if (!value.empty()) {
            args.push_back("-metadata");
            args.push_back(metadata_argument(key, value));
        }
    };
    const auto add_number = [&add](const std::string& key, int value) {
        if (value > 0) add(key, std::to_string(value));
    };

    add("comment", "Captured and organized by MusicCat");
    add("title", metadata.title);
    add("artist", metadata.artist);
    add("album", metadata.album);
    add("album_artist", metadata.album_artist);
    add("composer", metadata.composer);
    add("genre", metadata.genre);
    add("lyrics", metadata.lyrics);
    add_number("date", metadata.year);
    add_number("bpm", metadata.bpm);
    if (metadata.track_number > 0) {
        add("track", std::to_string(metadata.track_number) +
            (metadata.track_count > 0 ? "/" + std::to_string(metadata.track_count) : ""));
    }
    if (metadata.disc_number > 0) {
        add("disc", std::to_string(metadata.disc_number) +
            (metadata.disc_count > 0 ? "/" + std::to_string(metadata.disc_count) : ""));
    }
}

double audio_duration(const std::filesystem::path& file) {
    const auto result = run_process({"ffprobe", "-v", "error", "-show_entries", "format=duration",
                                     "-of", "default=noprint_wrappers=1:nokey=1", file.string()});
    const double duration = parse_double(result.output);
    if (duration <= 0.0) throw std::runtime_error("ffprobe returned an invalid duration for " + file.string());
    return duration;
}

std::pair<double, double> audio_levels(const std::filesystem::path& file, double trim_seconds) {
    std::vector<std::string> args = {"ffmpeg", "-hide_banner"};
    if (trim_seconds > 0.0) {
        args.insert(args.end(), {"-ss", std::to_string(trim_seconds)});
    }
    args.insert(args.end(), {"-i", file.string(), "-af", "volumedetect", "-f", "null", "-"});
    const auto result = run_process(args, false);
    auto parse_level = [&result](const std::string& label) {
        const auto position = result.output.rfind(label);
        if (position == std::string::npos) return -std::numeric_limits<double>::infinity();
        const auto begin = position + label.size();
        const auto end = result.output.find(" dB", begin);
        if (end == std::string::npos) return -std::numeric_limits<double>::infinity();
        const std::string value = trim(result.output.substr(begin, end - begin));
        if (value == "-inf") return -std::numeric_limits<double>::infinity();
        return parse_double(value);
    };
    return {parse_level("mean_volume: "), parse_level("max_volume: ")};
}

struct SourceAlignment {
    double leading_seconds = 0.0;
    double envelope_score = 0.0;
    double waveform_score = 0.0;
};

std::vector<float> decode_alignment_audio(const std::filesystem::path& file,
                                          std::optional<double> maximum_seconds = std::nullopt) {
    std::vector<std::string> args = {"ffmpeg", "-v", "error", "-i", file.string(),
                                     "-map", "0:a:0", "-ac", "1", "-ar", "8000"};
    if (maximum_seconds && *maximum_seconds > 0.0) {
        args.insert(args.end(), {"-t", std::to_string(*maximum_seconds)});
    }
    args.insert(args.end(), {"-f", "f32le", "-"});
    const auto decoded = run_process(args, true, std::chrono::minutes(3)).output;
    if (decoded.size() < sizeof(float) * 8000 || decoded.size() % sizeof(float) != 0) {
        throw std::runtime_error("decoded alignment audio is missing or malformed");
    }
    std::vector<float> samples(decoded.size() / sizeof(float));
    std::memcpy(samples.data(), decoded.data(), decoded.size());
    return samples;
}

std::vector<double> energy_envelope(const std::vector<float>& samples) {
    constexpr std::size_t frame_samples = 80; // 10 ms at 8 kHz.
    std::vector<double> result;
    result.reserve(samples.size() / frame_samples);
    for (std::size_t begin = 0; begin + frame_samples <= samples.size(); begin += frame_samples) {
        double energy = 0.0;
        for (std::size_t index = begin; index < begin + frame_samples; ++index) {
            energy += static_cast<double>(samples[index]) * samples[index];
        }
        result.push_back(std::log(std::sqrt(energy / frame_samples) + 1e-8));
    }
    return result;
}

SourceAlignment align_to_downloaded_source(const std::filesystem::path& source,
                                            const std::filesystem::path& capture,
                                            double expected_duration) {
    if (!std::filesystem::is_regular_file(source)) {
        throw std::runtime_error("downloaded source file is unavailable: " + source.string());
    }
    const auto reference_pcm = decode_alignment_audio(source);
    const auto capture_pcm = decode_alignment_audio(capture, expected_duration + 25.0);
    const auto reference = energy_envelope(reference_pcm);
    const auto observed = energy_envelope(capture_pcm);
    if (observed.size() < reference.size() || reference.size() < 100) {
        throw std::runtime_error("capture is too short to align with the downloaded source");
    }

    const double reference_mean = std::accumulate(reference.begin(), reference.end(), 0.0) /
                                  static_cast<double>(reference.size());
    double reference_energy = 0.0;
    for (const double value : reference) {
        reference_energy += (value - reference_mean) * (value - reference_mean);
    }
    if (reference_energy <= 0.0) throw std::runtime_error("downloaded source has no usable dynamics");

    const std::size_t maximum_lag = std::min<std::size_t>(
        observed.size() - reference.size(), 2000); // At most 20 seconds of armed pre-roll.
    double best_envelope_score = -std::numeric_limits<double>::infinity();
    std::size_t best_lag_frames = 0;
    for (std::size_t lag = 0; lag <= maximum_lag; ++lag) {
        double observed_mean = 0.0;
        for (std::size_t index = 0; index < reference.size(); ++index) {
            observed_mean += observed[lag + index];
        }
        observed_mean /= static_cast<double>(reference.size());
        double numerator = 0.0;
        double observed_energy = 0.0;
        for (std::size_t index = 0; index < reference.size(); ++index) {
            const double a = reference[index] - reference_mean;
            const double b = observed[lag + index] - observed_mean;
            numerator += a * b;
            observed_energy += b * b;
        }
        if (observed_energy <= 0.0) continue;
        const double score = numerator / std::sqrt(reference_energy * observed_energy);
        if (score > best_envelope_score) {
            best_envelope_score = score;
            best_lag_frames = lag;
        }
    }
    if (!std::isfinite(best_envelope_score) || best_envelope_score < 0.90) {
        throw std::runtime_error("captured output does not correlate with the downloaded source");
    }

    const std::size_t window_samples = std::min<std::size_t>(80000, reference_pcm.size());
    std::size_t reference_begin = 0;
    double strongest_energy = -1.0;
    const std::size_t window_step = 8000;
    for (std::size_t begin = 0; begin + window_samples <= reference_pcm.size();
         begin += window_step) {
        double energy = 0.0;
        for (std::size_t index = begin; index < begin + window_samples; ++index) {
            energy += static_cast<double>(reference_pcm[index]) * reference_pcm[index];
        }
        if (energy > strongest_energy) {
            strongest_energy = energy;
            reference_begin = begin;
        }
        if (reference_pcm.size() - begin < window_samples + window_step) break;
    }

    const std::size_t coarse_lag_samples = best_lag_frames * 80;
    const std::size_t first_lag = coarse_lag_samples > 240 ? coarse_lag_samples - 240 : 0;
    const std::size_t last_possible_lag =
        capture_pcm.size() - reference_begin - window_samples;
    const std::size_t last_lag = std::min(coarse_lag_samples + 240, last_possible_lag);
    double best_waveform_score = -std::numeric_limits<double>::infinity();
    std::size_t best_lag_samples = coarse_lag_samples;
    for (std::size_t lag = first_lag; lag <= last_lag; ++lag) {
        double numerator = 0.0;
        double reference_window_energy = 0.0;
        double observed_window_energy = 0.0;
        for (std::size_t index = 0; index < window_samples; ++index) {
            const double a = reference_pcm[reference_begin + index];
            const double b = capture_pcm[lag + reference_begin + index];
            numerator += a * b;
            reference_window_energy += a * a;
            observed_window_energy += b * b;
        }
        if (reference_window_energy <= 0.0 || observed_window_energy <= 0.0) continue;
        const double score = numerator /
            std::sqrt(reference_window_energy * observed_window_energy);
        if (score > best_waveform_score) {
            best_waveform_score = score;
            best_lag_samples = lag;
        }
    }
    if (!std::isfinite(best_waveform_score) || best_waveform_score < 0.70) {
        throw std::runtime_error("captured waveform cannot be precisely aligned to the downloaded source");
    }

    return {static_cast<double>(best_lag_samples) / 8000.0,
            best_envelope_score, best_waveform_score};
}

std::string reason_slug(std::string value) {
    for (char& character : value) {
        if (!std::isalnum(static_cast<unsigned char>(character))) character = '-';
    }
    while (value.find("--") != std::string::npos) value.replace(value.find("--"), 2, "-");
    return sanitize_component(value, 60);
}

const char* kLiveSnapshotScript = R"APPLESCRIPT(
on safeText(v)
    try
        if v is missing value then return ""
        return v as text
    on error
        return ""
    end try
end safeText

on safePath(v)
    try
        if v is missing value then return ""
        return POSIX path of v
    on error
        return ""
    end try
end safePath

on joinFields(valuesList)
    set oldDelimiters to AppleScript's text item delimiters
    set AppleScript's text item delimiters to (ASCII character 30)
    set joined to valuesList as text
    set AppleScript's text item delimiters to oldDelimiters
    return joined
end joinFields

tell application "Music"
    if not running then return my joinFields({"1", "0", "stopped"})
    set stateValue to player state as text
    set positionValue to "0"
    try
        set positionValue to player position as text
    end try
    if stateValue is not "playing" and stateValue is not "paused" then
        return my joinFields({"1", "1", stateValue, positionValue})
    end if
    set t to current track
    set idValue to ""
    try
        set idValue to my safeText(persistent ID of t)
    end try
    set titleValue to my safeText(name of t)
    set artistValue to my safeText(artist of t)
    set albumValue to my safeText(album of t)
    set durationValue to "0"
    try
        set durationValue to duration of t as text
    end try
    set locationValue to ""
    try
        set locationValue to my safePath(location of t)
    end try
    -- Read position last. Metadata access can take hundreds of milliseconds;
    -- pairing the final position with the query-completion timestamp keeps
    -- pre-roll alignment independent of that variable delay.
    try
        set positionValue to player position as text
    end try
    return my joinFields({"1", "1", stateValue, positionValue, idValue, titleValue, artistValue, albumValue, durationValue, locationValue})
end tell
)APPLESCRIPT";

const char* kMetadataScript = R"APPLESCRIPT(
on safeText(v)
    try
        if v is missing value then return ""
        set s to v as text
        set oldDelimiters to AppleScript's text item delimiters
        set AppleScript's text item delimiters to (ASCII character 30)
        set pieces to text items of s
        set AppleScript's text item delimiters to " "
        set s to pieces as text
        set AppleScript's text item delimiters to oldDelimiters
        return s
    on error
        return ""
    end try
end safeText

on joinFields(valuesList)
    set oldDelimiters to AppleScript's text item delimiters
    set AppleScript's text item delimiters to (ASCII character 30)
    set joined to valuesList as text
    set AppleScript's text item delimiters to oldDelimiters
    return joined
end joinFields

tell application "Music"
    if not running then return my joinFields({"1", "0", "stopped"})
    set stateValue to player state as text
    set positionValue to "0"
    try
        set positionValue to player position as text
    end try
    if stateValue is not "playing" and stateValue is not "paused" then
        return my joinFields({"1", "1", stateValue, positionValue})
    end if
    set t to current track
    set idValue to ""
    set titleValue to ""
    set artistValue to ""
    set albumValue to ""
    set albumArtistValue to ""
    set composerValue to ""
    set genreValue to ""
    set lyricsValue to ""
    set durationValue to "0"
    set yearValue to "0"
    set trackNumberValue to "0"
    set trackCountValue to "0"
    set discNumberValue to "0"
    set discCountValue to "0"
    set bpmValue to "0"
    try
        set idValue to my safeText(persistent ID of t)
    end try
    try
        set titleValue to my safeText(name of t)
    end try
    try
        set artistValue to my safeText(artist of t)
    end try
    try
        set albumValue to my safeText(album of t)
    end try
    try
        set albumArtistValue to my safeText(album artist of t)
    end try
    try
        set composerValue to my safeText(composer of t)
    end try
    try
        set genreValue to my safeText(genre of t)
    end try
    try
        set lyricsValue to my safeText(lyrics of t)
    end try
    try
        set durationValue to duration of t as text
    end try
    try
        set yearValue to year of t as text
    end try
    try
        set trackNumberValue to track number of t as text
    end try
    try
        set trackCountValue to track count of t as text
    end try
    try
        set discNumberValue to disc number of t as text
    end try
    try
        set discCountValue to disc count of t as text
    end try
    try
        set bpmValue to bpm of t as text
    end try
    return my joinFields({"1", "1", stateValue, positionValue, idValue, titleValue, artistValue, albumValue, albumArtistValue, composerValue, genreValue, lyricsValue, durationValue, yearValue, trackNumberValue, trackCountValue, discNumberValue, discCountValue, bpmValue})
end tell
)APPLESCRIPT";

const char* kArtworkScript = R"APPLESCRIPT(
on run argv
    set targetPath to item 1 of argv
    set expectedId to item 2 of argv
    set targetFile to POSIX file targetPath
    tell application "Music"
        if not running then error "Music is not running"
        set t to current track
        set observedId to ""
        try
            set observedId to persistent ID of t as text
        end try
        if expectedId is not "" and observedId is not "" and observedId is not expectedId then error "track changed before artwork export"
        if (count of artworks of t) is 0 then error "track has no artwork"
        try
            close access targetFile
        end try
        try
            set fileRef to open for access targetFile with write permission
            set eof of fileRef to 0
            try
                set artworkBytes to raw data of artwork 1 of t
            on error
                set artworkBytes to data of artwork 1 of t
            end try
            write artworkBytes to fileRef starting at eof
            close access fileRef
        on error errorMessage number errorNumber
            try
                close access targetFile
            end try
            error errorMessage number errorNumber
        end try
        return targetPath
    end tell
end run
)APPLESCRIPT";

Decision finish_decision(DecisionKind kind, const std::string& reason, const std::optional<Metadata>& metadata) {
    return {kind, reason, metadata};
}

} // namespace

CaptureStateMachine::CaptureStateMachine(Policy policy) : policy_(policy) {}

Decision CaptureStateMachine::observe(const Snapshot& snapshot) {
    if (!snapshot.query_ok) {
        if (!unavailable_since_) unavailable_since_ = snapshot.observed_at;
        const double unavailable_seconds =
            std::chrono::duration<double>(snapshot.observed_at - *unavailable_since_).count();
        if (current_ && unavailable_seconds > policy_.unavailable_grace_seconds) {
            const auto metadata = current_;
            reset();
            return finish_decision(DecisionKind::reject, "Apple Music became unavailable", metadata);
        }
        return {};
    }
    unavailable_since_.reset();

    if (!current_) {
        if (!snapshot.music_running || snapshot.state != PlayerState::playing) return {};
        const auto& metadata = snapshot.metadata;
        if (metadata.title.empty() || metadata.artist.empty() || metadata.album.empty() ||
            metadata.duration_seconds <= 5.0 ||
            snapshot.position_seconds < 0.0 ||
            snapshot.position_seconds > policy_.start_window_seconds) {
            return {};
        }
        current_ = metadata;
        last_good_ = snapshot;
        stalled_seconds_ = 0.0;
        playback_advanced_ = snapshot.position_seconds > 0.2;
        paused_samples_ = 0;
        return finish_decision(DecisionKind::start, "clean track beginning detected", current_);
    }

    const auto complete_or_reject = [&](const std::string& rejection_reason) {
        const auto metadata = current_;
        const double last_position = last_good_ ? last_good_->position_seconds : 0.0;
        const bool reached_end = metadata->duration_seconds > 0.0 &&
            last_position >= metadata->duration_seconds - policy_.end_window_seconds;
        reset();
        return finish_decision(reached_end ? DecisionKind::complete : DecisionKind::reject,
                               reached_end ? "track reached its natural end" : rejection_reason,
                               metadata);
    };

    if (!snapshot.music_running) return complete_or_reject("Music was closed before the track ended");
    if (!snapshot.metadata.id.empty() && snapshot.metadata.id != current_->id) {
        return complete_or_reject("track changed before the previous track ended");
    }

    if (snapshot.state != PlayerState::playing) {
        ++paused_samples_;
        if (paused_samples_ >= policy_.paused_grace_samples) {
            return complete_or_reject("playback paused or stopped before the track ended");
        }
        return {};
    }
    paused_samples_ = 0;

    if (last_good_) {
        const double wall_delta = std::chrono::duration<double>(snapshot.observed_at - last_good_->observed_at).count();
        const double position_delta = snapshot.position_seconds - last_good_->position_seconds;
        const bool backward_seek = position_delta < -policy_.backward_seek_tolerance_seconds;
        const bool forward_seek = wall_delta > 0.0 &&
            position_delta - wall_delta > policy_.forward_seek_tolerance_seconds;
        if (backward_seek || forward_seek) {
            if (backward_seek && last_good_->position_seconds >=
                    current_->duration_seconds - policy_.end_window_seconds &&
                snapshot.position_seconds <= policy_.start_window_seconds) {
                const auto metadata = current_;
                reset();
                return finish_decision(DecisionKind::complete,
                                       "track reached its natural end and restarted", metadata);
            }

            const int direction = backward_seek ? -1 : 1;
            if (pending_seek_direction_ == direction) {
                const auto metadata = current_;
                reset();
                return finish_decision(DecisionKind::reject,
                                       backward_seek ? "backward seek detected"
                                                     : "forward seek detected",
                                       metadata);
            }
            pending_seek_direction_ = direction;
            return {};
        }
        pending_seek_direction_ = 0;
        if (wall_delta > 0.0 && position_delta < std::min(0.1, wall_delta * 0.2)) {
            stalled_seconds_ += wall_delta;
        } else {
            stalled_seconds_ = 0.0;
        }
        if (position_delta > 0.1 || snapshot.position_seconds > 0.3) playback_advanced_ = true;
        const double stall_limit = playback_advanced_ ? policy_.stall_grace_seconds
                                                      : policy_.startup_buffer_grace_seconds;
        if (stalled_seconds_ > stall_limit) {
            const auto metadata = current_;
            reset();
            return finish_decision(DecisionKind::reject, "playback stalled beyond the grace period", metadata);
        }
    }

    last_good_ = snapshot;
    return {};
}

bool CaptureStateMachine::recording() const noexcept { return current_.has_value(); }
const std::optional<Metadata>& CaptureStateMachine::current_metadata() const noexcept { return current_; }

void CaptureStateMachine::reset() noexcept {
    current_.reset();
    last_good_.reset();
    stalled_seconds_ = 0.0;
    playback_advanced_ = false;
    unavailable_since_.reset();
    paused_samples_ = 0;
    pending_seek_direction_ = 0;
}

Logger::Logger(std::filesystem::path path) : path_(std::move(path)) {
    std::filesystem::create_directories(path_.parent_path());
}

void Logger::info(const std::string& message) { write("info", message); }
void Logger::warn(const std::string& message) { write("warn", message); }
void Logger::error(const std::string& message) { write("error", message); }

void Logger::write(const char* level, const std::string& message) {
    std::lock_guard lock(mutex_);
    const std::string line = "[" + timestamp() + "] [" + level + "] " + message;
    std::ofstream output(path_, std::ios::app);
    output << line << '\n';
    if (std::string(level) == "info") std::cout << message << '\n';
    else std::cerr << "[" << level << "] " << message << '\n';
}

void Logger::print_tail(std::size_t lines) const {
    std::lock_guard lock(mutex_);
    std::ifstream input(path_);
    std::vector<std::string> content;
    std::string line;
    while (std::getline(input, line)) content.push_back(line);
    const std::size_t first = content.size() > lines ? content.size() - lines : 0;
    for (std::size_t index = first; index < content.size(); ++index) std::cout << content[index] << '\n';
}

Snapshot AppleMusicClient::snapshot() const {
    Snapshot snapshot;
    try {
        const auto result = run_process({"/usr/bin/osascript", "-e", kLiveSnapshotScript}, true,
                                        std::chrono::seconds(3));
        snapshot.observed_at = std::chrono::steady_clock::now();
        const auto fields = split(trim(result.output), kFieldSeparator);
        if (fields.size() < 3 || fields[0] != "1") return snapshot;
        snapshot.query_ok = true;
        snapshot.music_running = fields[1] == "1";
        const std::string state = fields[2];
        snapshot.state = state == "playing" ? PlayerState::playing :
                         state == "paused" ? PlayerState::paused : PlayerState::stopped;
        if (fields.size() > 3) snapshot.position_seconds = parse_double(fields[3]);
        if (fields.size() < 10) return snapshot;
        auto& metadata = snapshot.metadata;
        metadata.id = fields[4];
        metadata.title = fields[5];
        metadata.artist = fields[6];
        metadata.album = fields[7];
        metadata.duration_seconds = parse_double(fields[8]);
        metadata.source_path = fields[9];
        if (metadata.id.empty()) {
            metadata.id = metadata.title + "\x1f" + metadata.artist + "\x1f" +
                          metadata.album + "\x1f" + std::to_string(static_cast<int>(metadata.duration_seconds));
        }
    } catch (...) {
        snapshot.observed_at = std::chrono::steady_clock::now();
        snapshot.query_ok = false;
    }
    return snapshot;
}

Metadata AppleMusicClient::enrich_metadata(const Metadata& base) const {
    try {
        const auto result = run_process({"/usr/bin/osascript", "-e", kMetadataScript}, true,
                                        std::chrono::seconds(10));
        const auto fields = split(trim(result.output), kFieldSeparator);
        if (fields.size() < 19 || fields[0] != "1" || fields[1] != "1") return base;
        Metadata metadata = base;
        const std::string observed_id = fields[4];
        if (!base.id.empty() && !observed_id.empty() && base.id != observed_id) return base;
        metadata.id = observed_id.empty() ? base.id : observed_id;
        metadata.title = fields[5].empty() ? base.title : fields[5];
        metadata.artist = fields[6].empty() ? base.artist : fields[6];
        metadata.album = fields[7].empty() ? base.album : fields[7];
        metadata.album_artist = fields[8];
        metadata.composer = fields[9];
        metadata.genre = fields[10];
        metadata.lyrics = fields[11];
        const double duration = parse_double(fields[12]);
        if (duration > 0.0) metadata.duration_seconds = duration;
        metadata.year = parse_int(fields[13]);
        metadata.track_number = parse_int(fields[14]);
        metadata.track_count = parse_int(fields[15]);
        metadata.disc_number = parse_int(fields[16]);
        metadata.disc_count = parse_int(fields[17]);
        metadata.bpm = parse_int(fields[18]);
        return metadata;
    } catch (...) {
        return base;
    }
}

std::optional<std::filesystem::path> AppleMusicClient::export_artwork(
    const Metadata& expected, const std::filesystem::path& destination_base,
    std::string* error_message) const {
    std::error_code error;
    std::string last_error = "unknown artwork export error";
    std::filesystem::remove(destination_base, error);
    for (int attempt = 0; attempt < 5; ++attempt) {
        try {
            run_process({"/usr/bin/osascript", "-e", kArtworkScript,
                         destination_base.string(),
                         expected.id.find('\x1f') == std::string::npos ? expected.id : ""},
                        true, std::chrono::seconds(10));
            if (!std::filesystem::is_regular_file(destination_base) ||
                std::filesystem::file_size(destination_base) == 0) {
                throw std::runtime_error("Apple Music returned empty artwork");
            }

            const auto mime = trim(run_process({"/usr/bin/file", "-b", "--mime-type", destination_base.string()}).output);
            std::string extension = ".img";
            if (mime == "image/jpeg") extension = ".jpg";
            else if (mime == "image/png") extension = ".png";
            else if (mime == "image/tiff") extension = ".tiff";
            else if (mime == "image/gif") extension = ".gif";
            const auto final_path = destination_base.string() + extension;
            move_file(destination_base, final_path);
            return std::filesystem::path(final_path);
        } catch (const std::exception& exception) {
            last_error = exception.what();
            std::filesystem::remove(destination_base, error);
            if (attempt < 4) std::this_thread::sleep_for(std::chrono::milliseconds(500));
        } catch (...) {
            last_error = "unknown artwork export error";
            std::filesystem::remove(destination_base, error);
            if (attempt < 4) std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    {
        std::filesystem::remove(destination_base, error);
        if (error_message) *error_message = last_error;
        return std::nullopt;
    }
}

Recorder::Recorder(Logger& logger) : logger_(logger) {}

Recorder::~Recorder() {
    try { stop(); } catch (...) {}
}

void Recorder::start(const std::string& device, const std::filesystem::path& output) {
    if (pid_ > 0) throw std::runtime_error("recorder is already running");
    std::filesystem::create_directories(output.parent_path());
    const auto log = output.string() + ".sox.log";
    const pid_t child = fork();
    if (child < 0) throw std::runtime_error("cannot start SoX: " + std::string(std::strerror(errno)));
    if (child == 0) {
        setpgid(0, 0);
        sigset_t empty_mask;
        sigemptyset(&empty_mask);
        pthread_sigmask(SIG_SETMASK, &empty_mask, nullptr);
        const int input_fd = open("/dev/null", O_RDONLY);
        const int null_fd = open("/dev/null", O_WRONLY);
        const int log_fd = open(log.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (input_fd >= 0) dup2(input_fd, STDIN_FILENO);
        if (null_fd >= 0) dup2(null_fd, STDOUT_FILENO);
        if (log_fd >= 0) dup2(log_fd, STDERR_FILENO);
        execlp("sox", "sox", "--buffer", "131072", "-V1", "-t", "coreaudio", device.c_str(),
               "-b", "24", "-c", "2", "-G", output.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    pid_ = child;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    int status = 0;
    const pid_t result = waitpid(pid_, &status, WNOHANG);
    if (result == pid_) {
        pid_ = -1;
        throw std::runtime_error("SoX could not open the configured device; see " + log);
    }
}

void Recorder::stop() {
    if (pid_ <= 0) return;
    const pid_t child = pid_;
    kill(child, SIGINT);
    int status = 0;
    for (int attempt = 0; attempt < 100; ++attempt) {
        const pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child || (result < 0 && errno == ECHILD)) {
            pid_ = -1;
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    kill(child, SIGTERM);
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (waitpid(child, &status, WNOHANG) == child) {
            pid_ = -1;
            logger_.warn("SoX required SIGTERM to stop");
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    kill(child, SIGKILL);
    waitpid(child, &status, 0);
    pid_ = -1;
    throw std::runtime_error("SoX did not stop cleanly and was killed");
}

bool Recorder::running() {
    if (pid_ <= 0) return false;
    int status = 0;
    const pid_t result = waitpid(pid_, &status, WNOHANG);
    if (result == 0) return true;
    pid_ = -1;
    return false;
}

int Recorder::pid() const noexcept { return pid_; }

Processor::Processor(Config config, Logger& logger)
    : config_(std::move(config)), logger_(logger), worker_([this](std::stop_token stop) { run(stop); }) {}

Processor::~Processor() {
    try { finish(); } catch (...) {}
}

void Processor::enqueue(ProcessingJob job) {
    std::lock_guard lock(mutex_);
    if (!accepting_) throw std::runtime_error("processor is no longer accepting jobs");
    if (jobs_.size() >= 8) throw std::runtime_error("post-processing queue is full");
    jobs_.push(std::move(job));
    cv_.notify_one();
}

void Processor::rethrow_if_failed() {
    std::lock_guard lock(mutex_);
    if (failure_) std::rethrow_exception(failure_);
}

void Processor::finish() {
    {
        std::lock_guard lock(mutex_);
        if (!accepting_ && !worker_.joinable()) {
            if (failure_) std::rethrow_exception(failure_);
            return;
        }
        accepting_ = false;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    rethrow_if_failed();
}

void Processor::run(std::stop_token stop) {
    while (true) {
        ProcessingJob job;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, stop, [this] { return !jobs_.empty() || !accepting_; });
            if (jobs_.empty()) {
                if (!accepting_ || stop.stop_requested()) return;
                continue;
            }
            job = std::move(jobs_.front());
            jobs_.pop();
        }
        try {
            process(std::move(job));
        } catch (...) {
            std::lock_guard lock(mutex_);
            if (!failure_) failure_ = std::current_exception();
            accepting_ = false;
            return;
        }
    }
}

void Processor::process(ProcessingJob job) {
    if (!std::filesystem::is_regular_file(job.raw_flac)) {
        throw std::runtime_error("recording file is missing: " + job.raw_flac.string());
    }
    if (job.enrichment.valid()) {
        const std::string source_path = job.metadata.source_path;
        auto enrichment = job.enrichment.get();
        job.metadata = std::move(enrichment.metadata);
        if (job.metadata.source_path.empty()) job.metadata.source_path = source_path;
        job.artwork = std::move(enrichment.artwork);
        if (!job.artwork && !enrichment.artwork_error.empty()) {
            logger_.warn("Artwork unavailable: " + enrichment.artwork_error);
        }
    }

    auto quarantine = [&](const std::string& reason) {
        const auto folder = config_.output / "Mcat Library" / ".Rejected";
        std::filesystem::create_directories(folder);
        const std::string stem = sanitize_component(job.metadata.title) + "--" + reason_slug(reason);
        const auto destination = unique_path(folder / (stem + ".flac"));
        move_file(job.raw_flac, destination);
        const auto recorder_log = std::filesystem::path(job.raw_flac.string() + ".sox.log");
        if (std::filesystem::exists(recorder_log)) {
            move_file(recorder_log, destination.string() + ".sox.log");
        }
        if (job.artwork && std::filesystem::exists(*job.artwork)) {
            move_file(*job.artwork, destination.string() + job.artwork->extension().string());
        }
        logger_.warn("Rejected recording retained for inspection: " + destination.string() + " (" + reason + ")");
    };

    try {
    if (!job.candidate_complete) {
        quarantine(job.disposition_reason);
        return;
    }

    double leading_trim = 0.0;
    double target_duration = job.metadata.duration_seconds;
    bool source_correlated = false;
    if (std::filesystem::is_regular_file(job.metadata.source_path)) {
        try {
            const SourceAlignment alignment = align_to_downloaded_source(
                job.metadata.source_path, job.raw_flac, job.metadata.duration_seconds);
            leading_trim = alignment.leading_seconds;
            source_correlated = true;
            logger_.info("Source correlation: trim " + std::to_string(alignment.leading_seconds) +
                         " s, envelope " + std::to_string(alignment.envelope_score) +
                         ", waveform " + std::to_string(alignment.waveform_score));
        } catch (const std::exception& exception) {
            logger_.warn("Local source could not be decoded for correlation; using player-position alignment: " +
                         std::string(exception.what()));
        }
    }
    if (!source_correlated && job.leading_trim_seconds) {
        leading_trim = *job.leading_trim_seconds;
        target_duration += kPositionAlignedTailSeconds;
        logger_.info("Player-position alignment: trim " + std::to_string(leading_trim) +
                     " s with conservative tail padding");
    } else if (!source_correlated) {
        quarantine("playback beginning could not be aligned");
        return;
    }

    const double actual_duration = audio_duration(job.raw_flac);
    leading_trim = std::clamp(leading_trim, 0.0, std::max(0.0, actual_duration - 0.1));
    const double effective_duration = actual_duration - leading_trim;
    const double shortfall = job.metadata.duration_seconds - effective_duration;
    const double excess = effective_duration - job.metadata.duration_seconds;
    if (shortfall > 1.25 || excess > 5.0) {
        quarantine("duration mismatch: expected " + std::to_string(job.metadata.duration_seconds) +
                   "s, captured " + std::to_string(effective_duration) + "s after alignment");
        return;
    }
    const auto [mean_db, peak_db] = audio_levels(job.raw_flac, leading_trim);
    if (!std::isfinite(peak_db) || peak_db < -70.0) {
        quarantine("captured audio is silent or below -70 dBFS");
        return;
    }
    logger_.info("Audio QC: " + std::to_string(effective_duration) + " s, mean " +
                 std::to_string(mean_db) + " dBFS, peak " + std::to_string(peak_db) + " dBFS");

    const auto album_folder = config_.output / "Mcat Library" / sanitize_component(job.metadata.album);
    const auto flac_folder = album_folder / "flac";
    std::filesystem::create_directories(flac_folder);

    const std::string requested_name = sanitize_component(job.metadata.title);
    std::string final_name = requested_name;
    for (int suffix = 0; ; ++suffix) {
        if (suffix > 0) final_name = requested_name + " (" + std::to_string(suffix) + ")";
        if (!std::filesystem::exists(album_folder / (final_name + ".m4a")) &&
            !std::filesystem::exists(flac_folder / (final_name + ".flac"))) break;
    }

    const auto processed_flac = job.raw_flac.parent_path() / (job.raw_flac.stem().string() + ".metadata.flac");
    const auto processed_m4a = job.raw_flac.parent_path() / (job.raw_flac.stem().string() + ".metadata.m4a");
    const bool has_artwork = job.artwork && std::filesystem::is_regular_file(*job.artwork);

    std::vector<std::string> flac_args = {"ffmpeg", "-v", "error", "-y"};
    if (leading_trim > 0.0) flac_args.insert(flac_args.end(), {"-ss", std::to_string(leading_trim)});
    flac_args.insert(flac_args.end(), {"-i", job.raw_flac.string()});
    if (has_artwork) {
        flac_args.insert(flac_args.end(), {"-i", job.artwork->string(), "-map", "0:a:0", "-map", "1:v:0"});
    } else {
        flac_args.insert(flac_args.end(), {"-map", "0:a:0"});
    }
    flac_args.insert(flac_args.end(), {"-t", std::to_string(target_duration)});
    flac_args.insert(flac_args.end(), {"-c:a", "flac"});
    if (has_artwork) flac_args.insert(flac_args.end(), {"-c:v", "mjpeg", "-disposition:v:0", "attached_pic"});
    append_metadata(flac_args, job.metadata);
    flac_args.push_back(processed_flac.string());
    run_process(flac_args);

    std::vector<std::string> m4a_args = {"ffmpeg", "-v", "error", "-y"};
    if (leading_trim > 0.0) m4a_args.insert(m4a_args.end(), {"-ss", std::to_string(leading_trim)});
    m4a_args.insert(m4a_args.end(), {"-i", job.raw_flac.string()});
    if (has_artwork) {
        m4a_args.insert(m4a_args.end(), {"-i", job.artwork->string(), "-map", "0:a:0", "-map", "1:v:0"});
    } else {
        m4a_args.insert(m4a_args.end(), {"-map", "0:a:0"});
    }
    m4a_args.insert(m4a_args.end(), {"-t", std::to_string(target_duration)});
    m4a_args.insert(m4a_args.end(), {"-c:a", "aac", "-b:a", "256k"});
    if (has_artwork) m4a_args.insert(m4a_args.end(), {"-c:v", "mjpeg", "-disposition:v:0", "attached_pic"});
    append_metadata(m4a_args, job.metadata);
    m4a_args.push_back(processed_m4a.string());
    run_process(m4a_args);

    const auto final_flac = flac_folder / (final_name + ".flac");
    const auto final_m4a = album_folder / (final_name + ".m4a");
    move_file(processed_flac, final_flac);
    move_file(processed_m4a, final_m4a);

    if (has_artwork) {
        const auto cover = album_folder / "Cover.jpg";
        if (!std::filesystem::exists(cover)) {
            const auto temp_cover = album_folder / ".Cover.mcat-tmp.jpg";
            run_process({"ffmpeg", "-v", "error", "-y", "-i", job.artwork->string(),
                         "-frames:v", "1", temp_cover.string()});
            move_file(temp_cover, cover);
            if (executable_exists("fileicon")) {
                const auto icon_result = run_process({"fileicon", "set", album_folder.string(), cover.string()}, false);
                if (icon_result.status != 0) logger_.warn("Could not apply the album folder icon");
            }
        }
    }

    const char* disable_transkun = std::getenv("MUSICCAT_DISABLE_TRANSKUN");
    if ((!disable_transkun || std::string(disable_transkun) != "1") &&
        executable_exists("transkun")) {
        const auto transcription_wav =
            job.raw_flac.parent_path() / (job.raw_flac.stem().string() + ".transkun.wav");
        const auto transcription_midi =
            job.raw_flac.parent_path() / (job.raw_flac.stem().string() + ".transkun.mid");
        try {
            run_process({"ffmpeg", "-v", "error", "-y", "-i", final_flac.string(),
                         "-map", "0:a:0", "-ar", "44100", "-ac", "2",
                         "-c:a", "pcm_s16le", transcription_wav.string()});
            run_process({"transkun", transcription_wav.string(), transcription_midi.string()},
                        true, std::chrono::hours(1));
            if (!std::filesystem::is_regular_file(transcription_midi) ||
                std::filesystem::file_size(transcription_midi) == 0) {
                throw std::runtime_error("Transkun did not create a MIDI file");
            }
            const auto midi_folder = album_folder / "midi";
            std::filesystem::create_directories(midi_folder);
            const auto final_midi = unique_path(midi_folder / (final_name + ".mid"));
            move_file(transcription_midi, final_midi);
            const auto cover = album_folder / "Cover.jpg";
            if (std::filesystem::is_regular_file(cover) && executable_exists("fileicon")) {
                const auto icon_result = run_process(
                    {"fileicon", "set", midi_folder.string(), cover.string()}, false);
                if (icon_result.status != 0) {
                    logger_.warn("Could not apply the album cover to the MIDI folder icon");
                }
            }
            logger_.info("MIDI transcription: " + final_midi.string());
        } catch (const std::exception& exception) {
            logger_.warn("Optional Transkun transcription failed: " + std::string(exception.what()));
        }
        std::error_code transcription_error;
        std::filesystem::remove(transcription_wav, transcription_error);
        std::filesystem::remove(transcription_midi, transcription_error);
    }

    std::error_code error;
    std::filesystem::remove(job.raw_flac, error);
    std::filesystem::remove(job.raw_flac.string() + ".sox.log", error);
    if (job.artwork) std::filesystem::remove(*job.artwork, error);
    logger_.info("Saved: " + final_m4a.string());
    logger_.info("Lossless archive: " + final_flac.string());
    } catch (const std::exception& exception) {
        if (std::filesystem::is_regular_file(job.raw_flac)) {
            try {
                quarantine("post-processing failed: " + std::string(exception.what()));
            } catch (...) {
                logger_.error("Post-processing failed and the raw capture could not be moved out of .mcat-work");
            }
        }
        throw;
    } catch (...) {
        if (std::filesystem::is_regular_file(job.raw_flac)) {
            try {
                quarantine("unknown post-processing failure");
            } catch (...) {
                logger_.error("Post-processing failed and the raw capture could not be moved out of .mcat-work");
            }
        }
        throw;
    }
}

std::filesystem::path config_path() {
    return home_path() / "Library" / "Application Support" / "MusicCat" / "config";
}

std::filesystem::path log_path() {
    return home_path() / "Library" / "Logs" / "MusicCat" / "musiccat.log";
}

Config load_config() {
    Config config{home_path() / "Music" / "MusicCat", "Apple Music Virtual Device"};
    std::filesystem::path source = config_path();
    if (!std::filesystem::exists(source)) {
        const auto legacy = home_path() / "Library" / "Caches" / "MusicCat" / "cache.txt";
        if (std::filesystem::exists(legacy)) source = legacy;
    }
    std::ifstream input(source);
    std::string line;
    while (std::getline(input, line)) {
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));
        if ((key == "output" || key == "currOutputFolderPath") && !value.empty()) config.output = value;
        if ((key == "device" || key == "currVirtualDevice") && !value.empty()) config.device = value;
    }
    return config;
}

void save_config(const Config& config) {
    const auto path = config_path();
    std::filesystem::create_directories(path.parent_path());
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) throw std::runtime_error("cannot write MusicCat config");
        output << "output=" << config.output.string() << '\n';
        output << "device=" << config.device << '\n';
    }
    move_file(temporary, path);
}

std::filesystem::path make_work_path(const Config& config, const Metadata& metadata) {
    const auto folder = config.output / ".mcat-work";
    std::filesystem::create_directories(folder);
    return unique_path(folder / (file_timestamp() + "-" + std::to_string(getpid()) + "-" +
                                 sanitize_component(metadata.title, 100) + ".flac"));
}

std::string sanitize_component(const std::string& value, std::size_t max_bytes) {
    std::string result;
    result.reserve(std::min(value.size(), max_bytes));
    bool previous_space = false;
    const auto append = [&](std::string_view piece) {
        if (result.size() + piece.size() > max_bytes) return false;
        result.append(piece);
        return true;
    };
    for (std::size_t index = 0; index < value.size();) {
        const unsigned char character = static_cast<unsigned char>(value[index]);
        if (character < 0x80) {
            ++index;
            if (character < 32 || character == 127) continue;
            if (character == '/' || character == '\\' || character == ':' || character == '*' ||
                character == '?' || character == '"' || character == '<' || character == '>' || character == '|') {
                if (!result.empty() && result.back() != ' ' && !append(" - ")) break;
                previous_space = true;
                continue;
            }
            const bool space = std::isspace(character);
            if (space) {
                if (!previous_space && !result.empty() && !append(" ")) break;
            } else if (!append(std::string_view(value).substr(index - 1, 1))) {
                break;
            }
            previous_space = space;
            continue;
        }

        std::size_t sequence_length = character >= 0xF0 ? 4 : character >= 0xE0 ? 3 : 2;
        if ((sequence_length == 2 && character < 0xC2) || index + sequence_length > value.size()) {
            ++index;
            continue;
        }
        bool valid = true;
        for (std::size_t offset = 1; offset < sequence_length; ++offset) {
            const unsigned char continuation = static_cast<unsigned char>(value[index + offset]);
            if ((continuation & 0xC0) != 0x80) valid = false;
        }
        if (!valid) {
            ++index;
            continue;
        }
        if (!append(std::string_view(value).substr(index, sequence_length))) break;
        index += sequence_length;
        previous_space = false;
    }
    result = trim(result);
    while (!result.empty() && (result.back() == '.' || result.back() == ' ')) result.pop_back();
    return result.empty() ? "Untitled" : result;
}

void print_logo_and_version() {
    std::cout << R"( ╔╦╗┌─┐┌─┐┌┬┐    /\_/\
 ║║║│  ├─┤ │    ( •.• )
 ╩ ╩└─┘┴ ┴ ┴     / >🍪
for mac and apple music
  background recorder
)";
    std::cout << "mcat " << kVersion << "\n\n";
}

void print_help() {
    std::cout << R"(
    mcat - Apple Music recording and dataset archiving CLI

    Usage:
      mcat <command>

    Recording:
      -r, --record              Monitor Apple Music and record clean tracks
          --record-once         Stop after one completed or rejected attempt

    Configuration:
      -o, --output <folder>     Set the output root
      -d, --device <name>       Set the CoreAudio capture device
      -s, --status              Show current configuration and player state
          --list-devices        List AVFoundation audio input devices

    Diagnostics:
      -t, --test                Test tools, output, Apple Music access, and device
      -l, --log                 Print the recent MusicCat log

    General:
      -h, --help                Show this help and exit
      -v, --version             Show logo and version
          --zh                  Show Chinese help
          --ja                  Show Japanese help

    Compatibility:
      mcat ready, mcat log, mcat zh, and mcat ja remain supported.

    Notes:
      Specify exactly one command per invocation.
      Downloading first is recommended, but not required. MusicCat does not
      repair or splice network interruptions.
      Press Ctrl-C to stop the recording service cleanly.
      Short observation failures are tolerated. Incomplete, paused, stalled,
      or seeked captures are isolated in Mcat Library/.Rejected rather than
      mixed into the usable dataset.

)";
}

void print_help_zh() {
    std::cout << R"(
    mcat - Apple Music 录音与 DSP 数据集归档工具

    用法:
      mcat <命令>

    录制:
      -r, --record              监听 Apple Music 并录制完整、连续的曲目
          --record-once         完成或隔离一次录音后自动退出

    配置:
      -o, --output <目录>       设置输出根目录
      -d, --device <设备名>     设置 CoreAudio 录音设备
      -s, --status              显示配置与当前播放状态
          --list-devices        列出 AVFoundation 音频输入设备

    诊断:
      -t, --test                检查工具、目录、Apple Music 权限与录音设备
      -l, --log                 显示最近日志

    通用:
      -h, --help                显示英文帮助
      -v, --version             显示 logo 与版本
          --zh                  显示中文帮助
          --ja                  显示日文帮助

    建议先在 Apple Music 下载曲目，但并非强制；程序不会修复或拼接网络中断。
    每次只能指定一个命令。Ctrl-C 会安全结束服务。
    短暂查询失败会容忍；暂停、卡顿、跳播或不完整录音会被隔离到
    Mcat Library/.Rejected，不会混入可用数据集。

)";
}

void print_help_ja() {
    std::cout << R"(
    mcat - Apple Music 録音・DSP データセット整理 CLI

    使い方:
      mcat <command>

    -r, --record              Apple Music を監視して完全な曲を録音
    -o, --output <folder>     出力フォルダを設定
    -d, --device <name>       CoreAudio 録音デバイスを設定
    -s, --status              設定と再生状態を表示
        --list-devices        オーディオ入力デバイスを表示
    -t, --test                環境と録音デバイスを診断
    -l, --log                 最近のログを表示
    -h, --help                英語ヘルプを表示
    -v, --version             logo とバージョンを表示

    事前ダウンロードを推奨しますが必須ではありません。
    ネットワーク中断の修復や音声の継ぎ合わせは行いません。

)";
}

void print_status(const Config& config) {
    AppleMusicClient client;
    const Snapshot value = client.snapshot();
    std::cout << "Output: " << config.output << '\n';
    std::cout << "Device: " << config.device << '\n';
    std::cout << "Log:    " << log_path() << '\n';
    if (!value.query_ok) {
        std::cout << "Music:  unavailable (Music may be initializing, or Automation permission is missing)\n";
    } else if (!value.music_running) {
        std::cout << "Music:  not running\n";
    } else {
        const char* state = value.state == PlayerState::playing ? "playing" :
                            value.state == PlayerState::paused ? "paused" : "stopped";
        std::cout << "Music:  " << state << '\n';
        if (!value.metadata.title.empty()) {
            std::cout << "Track:  " << value.metadata.title << " — " << value.metadata.artist << '\n';
            std::cout << "At:     " << std::fixed << std::setprecision(2) << value.position_seconds
                      << " / " << value.metadata.duration_seconds << " s\n";
            std::cout << "Local source reference: "
                      << (value.metadata.source_path.empty() ? "no (position alignment)" : "yes") << '\n';
        }
    }
}

void list_audio_devices() {
    const auto result = run_process({"ffmpeg", "-hide_banner", "-f", "avfoundation",
                                     "-list_devices", "true", "-i", ""}, false);
    std::istringstream lines(result.output);
    std::string line;
    bool audio = false;
    while (std::getline(lines, line)) {
        if (line.find("AVFoundation audio devices") != std::string::npos) {
            audio = true;
            continue;
        }
        if (line.find("AVFoundation video devices") != std::string::npos) audio = false;
        if (audio && line.find("] [") != std::string::npos) std::cout << line << '\n';
    }
}

void preflight(const Config& config, Logger& logger, bool probe_device,
               bool require_music_access) {
    for (const std::string tool : {"sox", "ffmpeg", "ffprobe"}) {
        if (!executable_exists(tool)) throw std::runtime_error("required tool is not executable: " + tool);
    }
    std::filesystem::create_directories(config.output);
    const auto probe = config.output / ".mcat-write-test";
    {
        std::ofstream output(probe);
        if (!output) throw std::runtime_error("output directory is not writable: " + config.output.string());
        output << "ok";
    }
    std::filesystem::remove(probe);

    AppleMusicClient client;
    if (require_music_access && !client.snapshot().query_ok) {
        throw std::runtime_error(
            "cannot query Apple Music; wait for Music to finish initializing and grant Automation permission to the app that launches mcat");
    }

    if (probe_device) {
        Metadata metadata;
        metadata.title = "device-probe";
        const auto test_file = make_work_path(config, metadata);
        Recorder recorder(logger);
        recorder.start(config.device, test_file);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        recorder.stop();
        if (!std::filesystem::is_regular_file(test_file) || std::filesystem::file_size(test_file) < 64) {
            throw std::runtime_error("device probe did not create a valid FLAC file");
        }
        static_cast<void>(audio_duration(test_file));
        std::error_code error;
        std::filesystem::remove(test_file, error);
        std::filesystem::remove(test_file.string() + ".sox.log", error);
    }
}

void run_diagnostics(const Config& config, Logger& logger) {
    print_logo_and_version();
    logger.info("Running MusicCat diagnostics...");
    preflight(config, logger, true, true);
    logger.info("Diagnostics passed: dependencies, output directory, Apple Music access, and capture device are usable.");
}

void run_service(const Config& config, Logger& logger, bool stop_after_first_attempt) {
    ::signal(SIGINT, SIG_DFL);
    ::signal(SIGTERM, SIG_DFL);
    ::signal(SIGUSR1, SIG_DFL);
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    sigaddset(&signals, SIGUSR1);
    if (pthread_sigmask(SIG_BLOCK, &signals, nullptr) != 0) {
        throw std::runtime_error("failed to block process signals");
    }

    preflight(config, logger, false, false);
    print_logo_and_version();
    logger.info("MusicCat is listening. Play an Apple Music track from its beginning (Ctrl-C exits).");
    logger.info("Output: " + config.output.string());
    logger.info("Device: " + config.device);

    std::stop_source service_stop;
    std::jthread signal_waiter([signals, &service_stop](std::stop_token thread_stop) {
        const pthread_t own_thread = pthread_self();
        std::stop_callback wake(thread_stop, [own_thread] { static_cast<void>(pthread_kill(own_thread, SIGUSR1)); });
        int received = 0;
        if (sigwait(&signals, &received) == 0 && received != SIGUSR1) service_stop.request_stop();
    });

    AppleMusicClient music;
    CaptureStateMachine state;
    Processor processor(config, logger);

    struct CaptureSlot {
        std::unique_ptr<Recorder> recorder;
        std::filesystem::path path;
        std::chrono::steady_clock::time_point started_at;
    };

    std::optional<CaptureSlot> capture;
    std::optional<CaptureSlot> reserve;
    std::optional<ProcessingJob> active_job;
    std::string advised_stream_id;
    std::exception_ptr service_error;

    const auto start_slot = [&]() {
        Metadata standby;
        standby.title = "standby";
        CaptureSlot slot;
        slot.recorder = std::make_unique<Recorder>(logger);
        slot.path = make_work_path(config, standby);
        slot.started_at = std::chrono::steady_clock::now();
        slot.recorder->start(config.device, slot.path);
        return slot;
    };

    const auto discard_slot = [](CaptureSlot& slot) {
        if (slot.recorder && slot.recorder->running()) slot.recorder->stop();
        std::error_code error;
        std::filesystem::remove(slot.path, error);
        std::filesystem::remove(slot.path.string() + ".sox.log", error);
    };

    const auto prepare_job = [&](const Decision& decision, const CaptureSlot& slot,
                                 const Snapshot& snapshot) {
        if (!decision.metadata) throw std::runtime_error("track-start decision has no metadata");
        ProcessingJob job;
        job.metadata = *decision.metadata;
        job.raw_flac = slot.path;
        if (snapshot.state == PlayerState::playing &&
            snapshot.metadata.id == job.metadata.id && snapshot.position_seconds >= 0.0 &&
            snapshot.position_seconds <= 5.0) {
            const double elapsed =
                std::chrono::duration<double>(snapshot.observed_at - slot.started_at).count();
            job.leading_trim_seconds = std::max(
                0.0, elapsed - snapshot.position_seconds - kAlignmentSafetySeconds);
        }
        const Metadata base_metadata = job.metadata;
        const auto artwork_path = std::filesystem::path(job.raw_flac.string() + ".artwork");
        job.enrichment = std::async(std::launch::async,
            [base_metadata, artwork_path] {
                AppleMusicClient background_music;
                ProcessingJob::Enrichment result;
                result.metadata = background_music.enrich_metadata(base_metadata);
                result.artwork = background_music.export_artwork(
                    result.metadata, artwork_path, &result.artwork_error);
                return result;
            });
        return job;
    };

    try {
        capture = start_slot();
        logger.info("Capture device armed; waiting for a track beginning.");
        while (!service_stop.stop_requested()) {
            processor.rethrow_if_failed();
            if (!capture || !capture->recorder->running()) {
                throw std::runtime_error("SoX exited unexpectedly while capture was armed");
            }
            if (reserve && !reserve->recorder->running()) {
                throw std::runtime_error("SoX exited unexpectedly while the transition reserve was armed");
            }
            const Snapshot snapshot = music.snapshot();
            if (active_job && capture &&
                snapshot.query_ok && snapshot.state == PlayerState::playing &&
                snapshot.metadata.id == active_job->metadata.id && snapshot.position_seconds >= 0.1 &&
                snapshot.position_seconds <= 5.0) {
                const double elapsed =
                    std::chrono::duration<double>(snapshot.observed_at - capture->started_at).count();
                const double candidate = std::max(
                    0.0, elapsed - snapshot.position_seconds - kAlignmentSafetySeconds);
                if (!active_job->leading_trim_seconds ||
                    candidate < *active_job->leading_trim_seconds) {
                    active_job->leading_trim_seconds = candidate;
                }
            }
            if (!active_job && snapshot.query_ok && snapshot.state == PlayerState::playing &&
                !snapshot.metadata.id.empty() && snapshot.metadata.source_path.empty() &&
                snapshot.metadata.id != advised_stream_id && snapshot.position_seconds <= 1.5) {
                advised_stream_id = snapshot.metadata.id;
                logger.warn("No local source reference for " + snapshot.metadata.title +
                            "; recording is allowed, but downloading first is recommended");
            } else if (!snapshot.metadata.source_path.empty()) {
                advised_stream_id.clear();
            }
            const Decision decision = state.observe(snapshot);

            if (decision.kind == DecisionKind::start && decision.metadata) {
                if (!capture) throw std::runtime_error("capture device was not armed at the track beginning");
                active_job = prepare_job(decision, *capture, snapshot);
                logger.info("Capturing: " + decision.metadata->title + " — " + decision.metadata->artist);
                reserve = start_slot();
            } else if ((decision.kind == DecisionKind::complete || decision.kind == DecisionKind::reject) && active_job) {
                if (!capture || !reserve) {
                    throw std::runtime_error("transition reserve is unavailable at the track boundary");
                }

                std::optional<ProcessingJob> following_job;
                std::optional<Metadata> following_metadata;
                if (!stop_after_first_attempt) {
                    const Decision following = state.observe(snapshot);
                    if (following.kind == DecisionKind::start && following.metadata) {
                        following_job = prepare_job(following, *reserve, snapshot);
                        following_metadata = following.metadata;
                    }
                }

                if (decision.kind == DecisionKind::complete) {
                    logger.info("Draining the CoreAudio output pipeline");
                    std::this_thread::sleep_for(std::chrono::milliseconds(600));
                }
                capture->recorder->stop();
                active_job->candidate_complete = decision.kind == DecisionKind::complete;
                active_job->disposition_reason = decision.reason;
                logger.info("Processing: " + active_job->metadata.title);
                processor.enqueue(std::move(*active_job));
                active_job.reset();

                capture = std::move(reserve);
                reserve.reset();

                if (stop_after_first_attempt) {
                    service_stop.request_stop();
                } else if (following_job && following_metadata) {
                    active_job = std::move(*following_job);
                    logger.info("Capturing: " + following_metadata->title + " — " +
                                following_metadata->artist);
                    reserve = start_slot();
                }
            } else if (!active_job && snapshot.query_ok &&
                       snapshot.state != PlayerState::playing && capture &&
                       snapshot.observed_at - capture->started_at > std::chrono::seconds(15)) {
                discard_slot(*capture);
                capture = start_slot();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(350));
        }
    } catch (...) {
        service_error = std::current_exception();
    }

    service_stop.request_stop();
    signal_waiter.request_stop();
    signal_waiter.join();

    try {
        if (active_job && capture) {
            if (capture->recorder->running()) capture->recorder->stop();
            active_job->candidate_complete = false;
            active_job->disposition_reason = "service stopped before track completion";
            processor.enqueue(std::move(*active_job));
            active_job.reset();
            capture.reset();
        } else if (capture) {
            discard_slot(*capture);
            capture.reset();
        }
        if (reserve) {
            discard_slot(*reserve);
            reserve.reset();
        }
        processor.finish();
    } catch (...) {
        if (!service_error) service_error = std::current_exception();
    }

    if (service_error) {
        logger.error("MusicCat stopped because recording or post-processing failed.");
        std::rethrow_exception(service_error);
    }
    logger.info("MusicCat stopped cleanly.");
}

} // namespace musiccat
