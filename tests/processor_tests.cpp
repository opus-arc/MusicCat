#include "musiccat.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

std::string quote(const std::filesystem::path& value) {
    std::string result = "'";
    for (const char character : value.string()) {
        if (character == '\'') result += "'\\''";
        else result.push_back(character);
    }
    return result + "'";
}

std::string command_output(const std::string& command) {
    FILE* pipe = popen(command.c_str(), "r");
    require(pipe != nullptr, "open command output pipe");
    std::string output;
    std::array<char, 256> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) output += buffer.data();
    require(pclose(pipe) == 0, "command producing test metadata succeeds");
    return output;
}

} // namespace

int main() {
    setenv("MUSICCAT_DISABLE_TRANSKUN", "1", 1);
    const auto root = std::filesystem::temp_directory_path() /
        ("musiccat-processor-test-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    const auto source = root / "downloaded-source.flac";
    const auto raw = root / "candidate.flac";
    const std::string generate_source =
        "ffmpeg -v error -y -f lavfi -i sine=frequency=440:sample_rate=48000:duration=4 "
        "-af \"volume='0.2+0.8*t/4':eval=frame\" -c:a flac -sample_fmt s32 " + quote(source);
    require(std::system(generate_source.c_str()) == 0, "generate the downloaded reference FLAC");
    const std::string generate_capture =
        "ffmpeg -v error -y -i " + quote(source) +
        " -af 'adelay=1000:all=1,apad=pad_dur=1' -c:a flac -sample_fmt s32 " + quote(raw);
    require(std::system(generate_capture.c_str()) == 0,
            "generate a synthetic capture with head and tail padding");

    musiccat::Logger logger(root / "test.log");
    musiccat::Config config{root / "output", "unused"};
    musiccat::Processor processor(config, logger);
    musiccat::ProcessingJob job;
    job.metadata.id = "synthetic-id";
    job.metadata.title = "Synthetic Track";
    job.metadata.artist = "MusicCat Tests";
    job.metadata.album = "Synthetic Album";
    job.metadata.duration_seconds = 4.0;
    job.metadata.source_path = source.string();
    job.raw_flac = raw;
    job.enrichment = std::async(std::launch::async, [] {
        musiccat::ProcessingJob::Enrichment result;
        result.metadata.id = "synthetic-id";
        result.metadata.title = "Synthetic Track";
        result.metadata.artist = "MusicCat Tests";
        result.metadata.album = "Synthetic Album";
        result.metadata.composer = "Background Metadata";
        result.metadata.duration_seconds = 4.0;
        return result;
    });
    job.candidate_complete = true;
    job.disposition_reason = "test";
    processor.enqueue(std::move(job));
    processor.finish();

    const auto album = config.output / "Mcat Library" / "Synthetic Album";
    require(std::filesystem::is_regular_file(album / "Synthetic Track.m4a"),
            "publish the M4A listening copy");
    require(std::filesystem::is_regular_file(album / "flac" / "Synthetic Track.flac"),
            "publish the FLAC archive");
    const auto composer = command_output(
        "ffprobe -v error -show_entries format_tags=composer -of default=nk=1:nw=1 " +
        quote(album / "flac" / "Synthetic Track.flac"));
    require(composer.find("Background Metadata") != std::string::npos,
            "apply metadata returned by the background enrichment task");

    const auto streaming_raw = root / "streaming-candidate.flac";
    require(std::system(("ffmpeg -v error -y -i " + quote(source) +
                         " -af 'adelay=1000:all=1,apad=pad_dur=1' -c:a flac -sample_fmt s32 " +
                         quote(streaming_raw)).c_str()) == 0,
            "generate a capture without a local source reference");
    musiccat::Processor streaming_processor(config, logger);
    musiccat::ProcessingJob streaming;
    streaming.metadata.id = "streaming-id";
    streaming.metadata.title = "Position Aligned Track";
    streaming.metadata.artist = "MusicCat Tests";
    streaming.metadata.album = "Synthetic Album";
    streaming.metadata.duration_seconds = 4.0;
    streaming.raw_flac = streaming_raw;
    streaming.candidate_complete = true;
    streaming.leading_trim_seconds = 1.0;
    streaming_processor.enqueue(std::move(streaming));
    streaming_processor.finish();
    require(std::filesystem::is_regular_file(album / "Position Aligned Track.m4a"),
            "publish streaming playback using player-position alignment");
    require(std::filesystem::is_regular_file(album / "flac" / "Position Aligned Track.flac"),
            "archive position-aligned streaming playback as FLAC");

    const auto rejected_raw = root / "rejected.flac";
    const auto rejected_log = std::filesystem::path(rejected_raw.string() + ".sox.log");
    std::filesystem::copy_file(album / "flac" / "Synthetic Track.flac", rejected_raw);
    {
        std::ofstream log(rejected_log);
        log << "synthetic recorder diagnostics\n";
    }
    musiccat::Processor rejection_processor(config, logger);
    musiccat::ProcessingJob rejected;
    rejected.metadata.title = "Interrupted Track";
    rejected.raw_flac = rejected_raw;
    rejected.candidate_complete = false;
    rejected.disposition_reason = "paused before completion";
    rejection_processor.enqueue(std::move(rejected));
    rejection_processor.finish();

    const auto rejected_folder = config.output / "Mcat Library" / ".Rejected";
    const auto retained_audio = rejected_folder / "Interrupted Track--paused-before-completion.flac";
    require(std::filesystem::is_regular_file(retained_audio),
            "retain an interrupted capture outside the album library");
    require(std::filesystem::is_regular_file(retained_audio.string() + ".sox.log"),
            "retain matching recorder diagnostics beside rejected audio");
    require(!std::filesystem::exists(rejected_log),
            "move rather than orphan recorder diagnostics in the work directory");

    const auto short_raw = root / "too-short.flac";
    std::filesystem::copy_file(album / "flac" / "Synthetic Track.flac", short_raw);
    musiccat::Processor duration_processor(config, logger);
    musiccat::ProcessingJob too_short;
    too_short.metadata.title = "Duration Mismatch";
    too_short.metadata.artist = "MusicCat Tests";
    too_short.metadata.album = "Synthetic Album";
    too_short.metadata.duration_seconds = 6.0;
    too_short.metadata.source_path = short_raw.string();
    too_short.raw_flac = short_raw;
    too_short.candidate_complete = true;
    duration_processor.enqueue(std::move(too_short));
    duration_processor.finish();
    bool retained_mismatch = false;
    for (const auto& entry : std::filesystem::directory_iterator(rejected_folder)) {
        if (entry.path().filename().string().starts_with("Duration Mismatch--duration-mismatch")) {
            retained_mismatch = true;
        }
    }
    require(retained_mismatch, "reject a candidate that is more than 1.25 seconds short");

    std::filesystem::remove_all(root);
    std::cout << "MusicCat processor tests passed.\n";
    return EXIT_SUCCESS;
}
