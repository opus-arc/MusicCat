//
// Created by opus arc on 2026/3/7.
//

#include <MyAppleMusic.h>

#include <array>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

#include <Logger.h>
#include <regex>

#include "Cmd.h"

#include <Public.h>

#include "Entity.h"

namespace {
    struct Device {
        int index;
        std::string name;
    };

    pid_t g_recordingPid = -1;
    std::optional<std::chrono::steady_clock::time_point> g_recordingStartTime;

    std::string runCommand(const std::string& cmd) {
        std::array<char, 4096> buffer{};
        std::string result;

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            throw std::runtime_error("popen() failed");
        }

        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            result += buffer.data();
        }

        pclose(pipe);
        return result;
    }

    std::vector<Device> listAudioDevices() {
        const std::string cmd = R"(ffmpeg -f avfoundation -list_devices true -i "" 2>&1)";
        const std::string output = runCommand(cmd);

        std::vector<Device> devices;
        std::stringstream ss(output);
        std::string line;

        bool inAudioSection = false;
        std::regex deviceLineRegex(R"(\[AVFoundation indev @ .*?\] \[([0-9]+)\] (.+))");

        while (std::getline(ss, line)) {
            if (line.find("AVFoundation audio devices:") != std::string::npos) {
                inAudioSection = true;
                continue;
            }

            if (line.find("AVFoundation video devices:") != std::string::npos) {
                inAudioSection = false;
                continue;
            }

            if (!inAudioSection) {
                continue;
            }

            std::smatch match;
            if (std::regex_search(line, match, deviceLineRegex)) {
                devices.push_back(Device{
                    std::stoi(match[1].str()),
                    match[2].str()
                });
            }
        }

        return devices;
    }

    std::string trimTrailingNewlines(std::string value) {
        while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
            value.pop_back();
        }
        return value;
    }


    std::string trimWhitespace(const std::string& value) {
        const auto first = value.find_first_not_of(" \t\n\r");
        if (first == std::string::npos) {
            return "";
        }
        const auto last = value.find_last_not_of(" \t\n\r");
        return value.substr(first, last - first + 1);
    }

    std::string escapeForSingleQuotedShell(const std::string& value) {
        std::string escaped;
        escaped.reserve(value.size() + 8);
        for (const char ch : value) {
            if (ch == '\'') {
                escaped += "'\\''";
            } else {
                escaped += ch;
            }
        }
        return escaped;
    }

    std::optional<int> parseOptionalInt(const std::string& rawValue) {
        const std::string value = trimWhitespace(rawValue);
        if (value.empty() || value == "<unavailable>" || value == "not provided") {
            return std::nullopt;
        }

        try {
            size_t pos = 0;
            const double parsed = std::stod(value, &pos);
            if (pos != value.size()) {
                return std::nullopt;
            }
            return static_cast<int>(parsed + (parsed >= 0.0 ? 0.5 : -0.5));
        } catch (...) {
            return std::nullopt;
        }
    }

    std::optional<std::string> parseOptionalString(const std::string& rawValue) {
        const std::string value = trimWhitespace(rawValue);
        if (value.empty() || value == "<unavailable>") {
            return std::nullopt;
        }
        return value;
    }

    std::optional<int> parseKeyedOptionalInt(const std::unordered_map<std::string, std::string>& values,
                                             const std::string& key) {
        const auto it = values.find(key);
        if (it == values.end()) {
            return std::nullopt;
        }
        return parseOptionalInt(it->second);
    }

    std::optional<std::string> parseKeyedOptionalString(const std::unordered_map<std::string, std::string>& values,
                                                        const std::string& key) {
        const auto it = values.find(key);
        if (it == values.end()) {
            return std::nullopt;
        }
        return parseOptionalString(it->second);
    }

    std::string sanitizeFilenameComponent(const std::string& value) {
        std::string result;
        result.reserve(value.size());

        for (const unsigned char ch : value) {
            if (std::isalnum(ch)) {
                result.push_back(static_cast<char>(ch));
                continue;
            }

            switch (ch) {
                case ' ':
                case '-':
                case '_':
                case '.':
                case '(':
                case ')':
                case '[':
                case ']':
                    result.push_back(static_cast<char>(ch));
                    break;
                default:
                    result.push_back('_');
                    break;
            }
        }

        result = trimWhitespace(result);
        if (result.empty()) {
            return "unknown";
        }
        return result;
    }

    bool fileExistsAndNotEmpty(const std::filesystem::path& path) {
        std::error_code ec;
        return std::filesystem::exists(path, ec) && std::filesystem::is_regular_file(path, ec) &&
               std::filesystem::file_size(path, ec) > 0;
    }

    std::optional<std::string> detectArtworkExtension(const std::filesystem::path& filePath) {
        try {
            const std::string cmd =
                "file -b --mime-type '" +
                escapeForSingleQuotedShell(filePath.string()) +
                "'";
            const std::string mime = trimWhitespace(runCommand(cmd));

            if (mime == "image/jpeg") {
                return std::string(".jpg");
            }
            if (mime == "image/png") {
                return std::string(".png");
            }
            if (mime == "image/tiff") {
                return std::string(".tiff");
            }
            if (mime == "image/heic") {
                return std::string(".heic");
            }
            if (mime == "image/gif") {
                return std::string(".gif");
            }
        } catch (...) {
        }

        return std::nullopt;
    }
}

bool MyAppleMusic::isRunning() {
    try {
        const std::string cmd =
            "osascript "
            "-e 'tell application \"System Events\"' "
            "-e 'return exists process \"Music\"' "
            "-e 'end tell'";

        const std::string output = Cmd::runCmdCapture(cmd);
        return output == "true";
    } catch (const std::exception& e) {
        Logger::error(std::string("[MyAppleMusic]: Failed to determine whether Music is running: ") + e.what());
        return false;
    }
}

bool MyAppleMusic::isPlaying() {
    try {
        const std::string cmd =
            "osascript "
            "-e 'tell application \"Music\"' "
            "-e 'if it is running then' "
            "-e 'return (player state is playing)' "
            "-e 'else' "
            "-e 'return false' "
            "-e 'end if' "
            "-e 'end tell'";

        const std::string output = Cmd::runCmdCapture(cmd);

        return output == "true";
    } catch (const std::exception& e) {
        Logger::error(std::string("[MyAppleMusic]: Failed to determine whether Music is playing: ") + e.what());
        return false;
    }
}

std::string MyAppleMusic::currentTrackId() {
    try {
        const std::string cmd =
            "osascript "
            "-e 'tell application \"Music\"' "
            "-e 'if it is running then' "
            "-e 'if player state is playing then' "
            "-e 'set currentTrack to current track' "
            "-e 'set trackName to name of currentTrack as string' "
            "-e 'set trackArtist to artist of currentTrack as string' "
            "-e 'set trackAlbum to album of currentTrack as string' "
            "-e 'set trackDuration to duration of currentTrack as string' "
            "-e 'return trackName & \"||\" & trackArtist & \"||\" & trackAlbum & \"||\" & trackDuration' "
            "-e 'else' "
            "-e 'return \"\"' "
            "-e 'end if' "
            "-e 'else' "
            "-e 'return \"\"' "
            "-e 'end if' "
            "-e 'end tell'";

        return Cmd::runCmdCapture(cmd);
    } catch (const std::exception& e) {
        Logger::error(std::string("[MyAppleMusic]: Failed to read current track id: ") + e.what());
        return "";
    }
}

bool MyAppleMusic::isCurrentTrackNearBeginning(const int toleranceMs) {
    if (toleranceMs <= 0) {
        Logger::warn(std::string("[MyAppleMusic]: Invalid toleranceMs=") + std::to_string(toleranceMs) + ", fallback to 1000ms.");
    }

    const int allowedLeadInMs = toleranceMs > 0 ? toleranceMs : 1000;

    if (!isPlaying()) {
        return false;
    }

    try {
        const std::string cmd =
            "osascript "
            "-e 'tell application \"Music\"' "
            "-e 'if it is running then' "
            "-e 'if player state is playing then' "
            "-e 'return player position as string' "
            "-e 'else' "
            "-e 'return \"\"' "
            "-e 'end if' "
            "-e 'else' "
            "-e 'return \"\"' "
            "-e 'end if' "
            "-e 'end tell'";

        const std::string output = Cmd::runCmdCapture(cmd);
        if (output.empty()) {
            return false;
        }

        const double playerPositionSeconds = std::stod(output);
        return playerPositionSeconds >= 0.0 && playerPositionSeconds * 1000.0 <= static_cast<double>(allowedLeadInMs);
    } catch (const std::exception& e) {
        Logger::error(std::string("[MyAppleMusic]: Failed to determine whether current track is near beginning: ") + e.what());
        return false;
    }
}

bool MyAppleMusic::isNextTrack() {
    static std::string lastTrackId;
    // std::cout << "!!!!!!    " << lastTrackId << std::endl;

    if (!isPlaying()) {
        return false;
    }

    const std::string currentId = currentTrackId();
    if (currentId.empty()) {
        return false;
    }

    // 第一次记录当前曲目
    if (lastTrackId.empty()) {
        lastTrackId = currentId;
        return false;
    }

    // 检测是否切歌
    if (currentId != lastTrackId) {
        lastTrackId = currentId;
        return true;
    }

    return false;
}

std::optional<AppleMusicMetadata> MyAppleMusic::readAppleMusicMetadata() {
    try {
        MyAppleMusic appleMusic;

        // AppleScript to get metadata lines as "key: value" and attempt artwork export or availability
        const std::string cmd =
            "osascript "
            "-e 'tell application \"Music\"' "
            "-e 'set currentTrack to current track' "
            "-e 'set outputLines to {}' "
            "-e 'set end of outputLines to \"title: \" & (name of currentTrack as string)' "
            "-e 'set end of outputLines to \"artist: \" & (artist of currentTrack as string)' "
            "-e 'set end of outputLines to \"album: \" & (album of currentTrack as string)' "
            "-e 'set trackDuration to duration of currentTrack as string' "
            "-e 'set end of outputLines to \"duration: \" & trackDuration' "
            "-e 'try' "
            "-e 'set end of outputLines to \"album artist: \" & (album artist of currentTrack as string)' "
            "-e 'on error' "
            "-e 'set end of outputLines to \"album artist: <unavailable>\"' "
            "-e 'end try' "
            "-e 'try' "
            "-e 'set end of outputLines to \"composer: \" & (composer of currentTrack as string)' "
            "-e 'on error' "
            "-e 'set end of outputLines to \"composer: <unavailable>\"' "
            "-e 'end try' "
            "-e 'try' "
            "-e 'set end of outputLines to \"genre: \" & (genre of currentTrack as string)' "
            "-e 'on error' "
            "-e 'set end of outputLines to \"genre: <unavailable>\"' "
            "-e 'end try' "
            "-e 'try' "
            "-e 'set end of outputLines to \"year: \" & (year of currentTrack as string)' "
            "-e 'on error' "
            "-e 'set end of outputLines to \"year: <unavailable>\"' "
            "-e 'end try' "
            "-e 'try' "
            "-e 'set end of outputLines to \"track number: \" & (track number of currentTrack as string)' "
            "-e 'on error' "
            "-e 'set end of outputLines to \"track number: <unavailable>\"' "
            "-e 'end try' "
            "-e 'try' "
            "-e 'set end of outputLines to \"track count: \" & (track count of currentTrack as string)' "
            "-e 'on error' "
            "-e 'set end of outputLines to \"track count: <unavailable>\"' "
            "-e 'end try' "
            "-e 'try' "
            "-e 'set end of outputLines to \"disc number: \" & (disc number of currentTrack as string)' "
            "-e 'on error' "
            "-e 'set end of outputLines to \"disc number: <unavailable>\"' "
            "-e 'end try' "
            "-e 'try' "
            "-e 'set end of outputLines to \"disc count: \" & (disc count of currentTrack as string)' "
            "-e 'on error' "
            "-e 'set end of outputLines to \"disc count: <unavailable>\"' "
            "-e 'end try' "
            "-e 'try' "
            "-e 'set bitRateValue to bit rate of currentTrack as string' "
            "-e 'if bitRateValue is missing value or bitRateValue is \"\" then' "
            "-e 'set end of outputLines to \"bit rate: not provided\"' "
            "-e 'else' "
            "-e 'set end of outputLines to \"bit rate: \" & bitRateValue' "
            "-e 'end if' "
            "-e 'on error' "
            "-e 'set end of outputLines to \"bit rate: not provided\"' "
            "-e 'end try' "
            "-e 'try' "
            "-e 'set sampleRateValue to sample rate of currentTrack as string' "
            "-e 'if sampleRateValue is missing value or sampleRateValue is \"\" then' "
            "-e 'set end of outputLines to \"sample rate: not provided\"' "
            "-e 'else' "
            "-e 'set end of outputLines to \"sample rate: \" & sampleRateValue' "
            "-e 'end if' "
            "-e 'on error' "
            "-e 'set end of outputLines to \"sample rate: not provided\"' "
            "-e 'end try' "
            "-e 'try' "
            "-e 'set end of outputLines to \"bpm: \" & (bpm of currentTrack as string)' "
            "-e 'on error' "
            "-e 'set end of outputLines to \"bpm: <unavailable>\"' "
            "-e 'end try' "
            "-e 'try' "
            "-e 'set end of outputLines to \"play count: \" & (played count of currentTrack as string)' "
            "-e 'on error' "
            "-e 'set end of outputLines to \"play count: <unavailable>\"' "
            "-e 'end try' "
            "-e 'try' "
            "-e 'set end of outputLines to \"rating: \" & (rating of currentTrack as string)' "
            "-e 'on error' "
            "-e 'set end of outputLines to \"rating: <unavailable>\"' "
            "-e 'end try' "
            "-e 'try' "
            "-e 'set trackLyrics to lyrics of currentTrack' "
            "-e 'if trackLyrics is missing value or trackLyrics is \"\" then' "
            "-e 'set end of outputLines to \"lyrics: <unavailable>\"' "
            "-e 'else' "
            "-e 'set end of outputLines to \"lyrics: \" & trackLyrics' "
            "-e 'end if' "
            "-e 'on error' "
            "-e 'set end of outputLines to \"lyrics: <unavailable>\"' "
            "-e 'end try' "
            "-e 'try' "
            "-e 'if (count of artworks of currentTrack) > 0 then' "
            "-e 'set end of outputLines to \"artwork path: available\"' "
            "-e 'else' "
            "-e 'set end of outputLines to \"artwork path: <unavailable>\"' "
            "-e 'end if' "
            "-e 'on error' "
            "-e 'set end of outputLines to \"artwork path: <unavailable>\"' "
            "-e 'end try' "
            "-e \"set oldDelimiters to AppleScript's text item delimiters\" "
            "-e \"set AppleScript's text item delimiters to linefeed\" "
            "-e 'set joinedOutput to outputLines as string' "
            "-e \"set AppleScript's text item delimiters to oldDelimiters\" "
            "-e 'return joinedOutput' "
            "-e 'end tell'";

        const std::string output = trimTrailingNewlines(Cmd::runCmdCapture(cmd));
        if (output.empty()) {
            Logger::warn("[MyFfmpeg]: Apple Music metadata output is empty.");
            return std::nullopt;
        }

        std::unordered_map<std::string, std::string> values;
        std::istringstream iss(output);
        std::string line;
        while (std::getline(iss, line)) {
            const auto colonPos = line.find(':');
            if (colonPos == std::string::npos) {
                continue;
            }
            const std::string key = trimWhitespace(line.substr(0, colonPos));
            const std::string val = trimWhitespace(line.substr(colonPos + 1));
            values[key] = val;
        }

        // Required fields
        const auto titleIt = values.find("title");
        const auto artistIt = values.find("artist");
        const auto albumIt = values.find("album");
        if (titleIt == values.end() || artistIt == values.end() || albumIt == values.end()) {
            Logger::warn("[MyFfmpeg]: Incomplete Apple Music metadata.");
            return std::nullopt;
        }
        const std::string& title = titleIt->second;
        const std::string& artist = artistIt->second;
        const std::string& album = albumIt->second;
        if (title.empty() || artist.empty() || album.empty()) {
            Logger::warn("[MyFfmpeg]: Incomplete Apple Music metadata.");
            return std::nullopt;
        }

        AppleMusicMetadata metadata;
        metadata.title = title;
        metadata.title_o = title;
        metadata.artist = artist;
        metadata.album = album;

        metadata.albumArtist = parseKeyedOptionalString(values, "album artist");
        metadata.composer = parseKeyedOptionalString(values, "composer");
        metadata.genre = parseKeyedOptionalString(values, "genre");
        metadata.lyrics = parseKeyedOptionalString(values, "lyrics");

        metadata.durationSeconds = parseKeyedOptionalInt(values, "duration");
        if (!metadata.durationSeconds) {
            const std::string trackId = appleMusic.currentTrackId();
            if (!trackId.empty()) {
                const std::string delimiter = "||";
                const size_t p1 = trackId.find(delimiter);
                if (p1 != std::string::npos) {
                    const size_t p2 = trackId.find(delimiter, p1 + delimiter.size());
                    if (p2 != std::string::npos) {
                        const size_t p3 = trackId.find(delimiter, p2 + delimiter.size());
                        if (p3 != std::string::npos) {
                            metadata.durationSeconds = parseOptionalInt(trackId.substr(p3 + delimiter.size()));
                        }
                    }
                }
            }
        }

        metadata.year = parseKeyedOptionalInt(values, "year");
        metadata.trackNumber = parseKeyedOptionalInt(values, "track number");
        metadata.trackCount = parseKeyedOptionalInt(values, "track count");
        metadata.discNumber = parseKeyedOptionalInt(values, "disc number");
        metadata.discCount = parseKeyedOptionalInt(values, "disc count");
        metadata.bitRate = parseKeyedOptionalInt(values, "bit rate");
        metadata.sampleRate = parseKeyedOptionalInt(values, "sample rate");
        metadata.bpm = parseKeyedOptionalInt(values, "bpm");
        metadata.playCount = parseKeyedOptionalInt(values, "play count");
        metadata.rating = parseKeyedOptionalInt(values, "rating");

        metadata.artworkPath = parseKeyedOptionalString(values, "artwork path");
        if (metadata.artworkPath && *metadata.artworkPath != "available") {
            metadata.artworkPath = std::nullopt;
        }

        return metadata;
    } catch (const std::exception& e) {
        std::string err = e.what();
        Logger::error("[MyFfmpeg]: Failed to read Apple Music metadata: " + err);
        return std::nullopt;
    }
}

void MyAppleMusic::printAppleMusicMetadata(const AppleMusicMetadata& metadata) {

    std::cout << std::endl;
                                        std::cout << "               title: " << metadata.title_o << '\n';
                                        std::cout << "              artist: " << metadata.artist << '\n';
                                        std::cout << "               album: " << metadata.album << '\n';

      AppleMusicMetadata::printOptionalField("        album artist", metadata.albumArtist);
      AppleMusicMetadata::printOptionalField("            composer", metadata.composer);
      AppleMusicMetadata::printOptionalField("               genre", metadata.genre);
      AppleMusicMetadata::printOptionalField("              lyrics", metadata.lyrics);

      AppleMusicMetadata::printOptionalField("     durationSeconds", metadata.durationSeconds);
      AppleMusicMetadata::printOptionalField("                year", metadata.year);
      AppleMusicMetadata::printOptionalField("         trackNumber", metadata.trackNumber);
      AppleMusicMetadata::printOptionalField("          trackCount", metadata.trackCount);
      AppleMusicMetadata::printOptionalField("          discNumber", metadata.discNumber);
      AppleMusicMetadata::printOptionalField("           discCount", metadata.discCount);
      // AppleMusicMetadata::printOptionalField("             bitRate", metadata.bitRate);
      // AppleMusicMetadata::printOptionalField("          sampleRate", metadata.sampleRate);
      // AppleMusicMetadata::printOptionalField("                 bpm", metadata.bpm);
      // AppleMusicMetadata::printOptionalField("           playCount", metadata.playCount);
      // AppleMusicMetadata::printOptionalField("              rating", metadata.rating);

      AppleMusicMetadata::printOptionalField("         artworkPath", metadata.artworkPath);

    std::cout << std::endl;
}

void MyAppleMusic::scrapingCover(std::string& title) {
    try {
        std::filesystem::path opf = Entity::getOutputFolderPath();
        std::error_code ec;
        std::filesystem::create_directories(opf, ec);
        if (ec) {
            Logger::error(std::string("[MyAppleMusic]: Failed to create output folder: ") + ec.message());
            return;
        }

        const std::string& baseName = title;

        const std::filesystem::path tempBase = opf / ".apple_music_cover_export";
        const std::filesystem::path& tempFile = tempBase;

        std::filesystem::remove(tempFile, ec);
        std::filesystem::remove(tempFile.string() + ".jpg", ec);
        std::filesystem::remove(tempFile.string() + ".png", ec);
        std::filesystem::remove(tempFile.string() + ".tiff", ec);
        std::filesystem::remove(tempFile.string() + ".heic", ec);
        std::filesystem::remove(tempFile.string() + ".gif", ec);

        const std::string tempPathEscaped = escapeForSingleQuotedShell(tempFile.string());

        const std::string cmd =
            "osascript "
            "-e 'tell application \"Music\"' "
            "-e 'if not (it is running) then error \"Music app is not running\"' "
            "-e 'if player state is not playing then error \"Nothing is playing\"' "
            "-e 'set currentTrack to current track' "
            "-e 'if (count of artworks of currentTrack) is 0 then error \"Current track has no artwork\"' "
            "-e 'set coverData to data of artwork 1 of currentTrack' "
            "-e 'set targetFile to POSIX file \"'" + tempPathEscaped + "'\"' "
            "-e 'try' "
            "-e 'close access targetFile' "
            "-e 'end try' "
            "-e 'set fileRef to open for access targetFile with write permission' "
            "-e 'set eof of fileRef to 0' "
            "-e 'write coverData to fileRef starting at eof' "
            "-e 'close access fileRef' "
            "-e 'return POSIX path of targetFile' "
            "-e 'end tell'";

        const std::string exportResult = trimWhitespace(Cmd::runCmdCapture(cmd));
        if (exportResult.empty()) {
            Logger::error("[MyAppleMusic]: Artwork export returned empty result.");
            return;
        }

        std::filesystem::path exportedFile = tempFile;
        if (!fileExistsAndNotEmpty(exportedFile)) {
            const std::array<std::filesystem::path, 5> candidates = {
                std::filesystem::path(tempFile.string() + ".jpg"),
                std::filesystem::path(tempFile.string() + ".png"),
                std::filesystem::path(tempFile.string() + ".tiff"),
                std::filesystem::path(tempFile.string() + ".heic"),
                std::filesystem::path(tempFile.string() + ".gif")
            };

            for (const auto& candidate : candidates) {
                if (fileExistsAndNotEmpty(candidate)) {
                    exportedFile = candidate;
                    break;
                }
            }
        }

        if (!fileExistsAndNotEmpty(exportedFile)) {
            Logger::error("[MyAppleMusic]: Artwork export did not produce a readable image file.");
            return;
        }

        std::string extension = exportedFile.extension().string();
        if (extension.empty()) {
            const auto detectedExtension = detectArtworkExtension(exportedFile);
            extension = detectedExtension.value_or(".img");
        }

        const std::filesystem::path finalPath = opf / (baseName + extension);
        std::filesystem::remove(finalPath, ec);
        std::filesystem::rename(exportedFile, finalPath, ec);
        if (ec) {
            std::filesystem::copy_file(exportedFile, finalPath, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                Logger::error(std::string("[MyAppleMusic]: Failed to move exported cover to output folder: ") + ec.message());
                return;
            }
            std::filesystem::remove(exportedFile, ec);
        }

        // Logger::info(std::string("[MyAppleMusic]: Cover exported to ") + finalPath.string());
        // Logger::info("[MyAppleMusic]: This saves the highest-quality artwork currently exposed by the local Music app for the playing track.");
    } catch (const std::exception& e) {
        Logger::error(std::string("[MyAppleMusic]: Failed to scrape cover: ") + e.what());
    }
}



