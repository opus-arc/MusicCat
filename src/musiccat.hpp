#pragma once

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <queue>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace musiccat {

inline constexpr const char* kVersion = "0.2.4";

struct Metadata {
    std::string id;
    std::string title;
    std::string artist;
    std::string album;
    std::string album_artist;
    std::string composer;
    std::string genre;
    std::string lyrics;
    double duration_seconds = 0.0;
    int year = 0;
    int track_number = 0;
    int track_count = 0;
    int disc_number = 0;
    int disc_count = 0;
    int bpm = 0;
    std::string source_path;
};

enum class PlayerState { unavailable, stopped, paused, playing };

struct Snapshot {
    bool query_ok = false;
    bool music_running = false;
    PlayerState state = PlayerState::unavailable;
    double position_seconds = 0.0;
    Metadata metadata;
    std::chrono::steady_clock::time_point observed_at{};
};

enum class DecisionKind { none, start, complete, reject };

struct Decision {
    DecisionKind kind = DecisionKind::none;
    std::string reason;
    std::optional<Metadata> metadata;
};

struct Policy {
    double restart_window_seconds = 5.0;
    double end_window_seconds = 3.0;
    double stall_grace_seconds = 2.5;
    double startup_buffer_grace_seconds = 30.0;
    double backward_seek_tolerance_seconds = 0.75;
    double forward_seek_tolerance_seconds = 2.0;
    double unavailable_grace_seconds = 30.0;
    int paused_grace_samples = 2;
};

bool capture_covers_track_beginning(double available_preroll_seconds,
                                    double player_position_seconds) noexcept;

class CaptureStateMachine {
public:
    explicit CaptureStateMachine(Policy policy = {});
    Decision observe(const Snapshot& snapshot);
    [[nodiscard]] bool recording() const noexcept;
    [[nodiscard]] const std::optional<Metadata>& current_metadata() const noexcept;
    void reset() noexcept;

private:
    Policy policy_;
    std::optional<Metadata> current_;
    std::optional<Snapshot> last_good_;
    double stalled_seconds_ = 0.0;
    bool playback_advanced_ = false;
    std::optional<std::chrono::steady_clock::time_point> unavailable_since_;
    int paused_samples_ = 0;
    int pending_seek_direction_ = 0;
};

struct Config {
    std::filesystem::path output;
    std::string device;
};

class Logger {
public:
    explicit Logger(std::filesystem::path path);
    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);
    void print_tail(std::size_t lines = 30) const;

private:
    void write(const char* level, const std::string& message);
    std::filesystem::path path_;
    mutable std::mutex mutex_;
};

class AppleMusicClient {
public:
    Snapshot snapshot() const;
    Metadata enrich_metadata(const Metadata& base) const;
    std::optional<std::filesystem::path> export_artwork(
        const Metadata& expected,
        const std::filesystem::path& destination_base,
        std::string* error_message = nullptr) const;
};

class Recorder {
public:
    explicit Recorder(Logger& logger);
    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    void start(const std::string& device, const std::filesystem::path& output);
    void stop();
    [[nodiscard]] bool running();
    [[nodiscard]] int pid() const noexcept;

private:
    Logger& logger_;
    int pid_ = -1;
};

struct ProcessingJob {
    struct Enrichment {
        Metadata metadata;
        std::optional<std::filesystem::path> artwork;
        std::string artwork_error;
    };

    Metadata metadata;
    std::filesystem::path raw_flac;
    std::optional<std::filesystem::path> artwork;
    std::future<Enrichment> enrichment;
    bool candidate_complete = false;
    std::optional<double> leading_trim_seconds;
    std::string disposition_reason;
};

class Processor {
public:
    Processor(Config config, Logger& logger);
    ~Processor();
    Processor(const Processor&) = delete;
    Processor& operator=(const Processor&) = delete;

    void enqueue(ProcessingJob job);
    void rethrow_if_failed();
    void finish();

private:
    void run(std::stop_token stop);
    void process(ProcessingJob job);

    Config config_;
    Logger& logger_;
    std::mutex mutex_;
    std::condition_variable_any cv_;
    std::queue<ProcessingJob> jobs_;
    std::exception_ptr failure_;
    bool accepting_ = true;
    std::jthread worker_;
};

Config load_config();
void save_config(const Config& config);
std::filesystem::path config_path();
std::filesystem::path log_path();
std::filesystem::path make_work_path(const Config& config, const Metadata& metadata);

void print_logo_and_version();
void print_help();
void print_help_zh();
void print_help_ja();
void print_status(const Config& config);
void list_audio_devices();
void preflight(const Config& config, Logger& logger, bool probe_device,
               bool require_music_access);
void run_service(const Config& config, Logger& logger, bool stop_after_first_attempt = false);
void run_diagnostics(const Config& config, Logger& logger);

std::string sanitize_component(const std::string& value, std::size_t max_bytes = 180);

} // namespace musiccat
