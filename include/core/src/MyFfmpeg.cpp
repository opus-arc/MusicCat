//
// Created by opus arc on 2026/3/7.
//

#include "../MyFfmpeg.h"


#include <filesystem>
#include <sstream>
#include <stdexcept>

#include "Entity.h"
#include "Cmd.h"
#include "Logger.h"

namespace {
    std::string shellQuote(const std::string &s) {
        std::string out = "'";
        for (char ch: s) {
            if (ch == '\'') {
                out += "'\\''";
            } else {
                out += ch;
            }
        }
        out += "'";
        return out;
    }

    std::string trimCopy(const std::string &s) {
        const std::string whitespace = " \t\r\n";
        const auto begin = s.find_first_not_of(whitespace);
        if (begin == std::string::npos) {
            return "";
        }
        const auto end = s.find_last_not_of(whitespace);
        return s.substr(begin, end - begin + 1);
    }

    std::string sanitizeFolderName(const std::string &name) {
        std::string out;
        out.reserve(name.size());

        for (char ch: name) {
            if (ch == '/' || ch == ':' || ch == '\\') {
                out += '_';
            } else {
                out += ch;
            }
        }

        out = trimCopy(out);
        return out.empty() ? "Unknown Album" : out;
    }

    std::string getM4aAlbumName(const std::filesystem::path &m4aPath) {
        std::ostringstream cmd;
        cmd << "ffprobe -v error "
                << "-show_entries format_tags=album "
                << "-of default=noprint_wrappers=1:nokey=1 "
                << shellQuote(m4aPath.string());

        const std::string output = trimCopy(Cmd::runCmdCapture(cmd.str()));
        return output.empty() ? "Unknown Album" : output;
    }

    int getAudioBitDepth(const std::filesystem::path &audioPath) {
        std::ostringstream cmd;
        cmd << "ffprobe -v error "
                << "-select_streams a:0 "
                << "-show_entries stream=bits_per_raw_sample,bits_per_sample "
                << "-of default=noprint_wrappers=1:nokey=1 "
                << shellQuote(audioPath.string());

        const std::string output = trimCopy(Cmd::runCmdCapture(cmd.str()));
        if (output.empty()) {
            throw std::runtime_error("ffprobe returned empty bit depth for: " + audioPath.string());
        }

        std::istringstream iss(output);
        std::string line;
        while (std::getline(iss, line)) {
            line = trimCopy(line);
            if (line.empty() || line == "N/A") {
                continue;
            }

            try {
                return std::stoi(line);
            } catch (const std::exception &) {
            }
        }

        throw std::runtime_error("Failed to parse bit depth from ffprobe output: " + output);
    }

    int getFlacBitDepth(const std::filesystem::path &flacPath) {
        return getAudioBitDepth(flacPath);
    }

    bool extractCoverFromM4a(const std::filesystem::path &m4aPath, const std::filesystem::path &coverPath) {
        if (std::filesystem::exists(coverPath)) {
            std::filesystem::remove(coverPath);
        }

        std::ostringstream cmd;
        cmd << "ffmpeg -y -i " << shellQuote(m4aPath.string())
                << " -an -map 0:v:0 -c:v copy "
                << shellQuote(coverPath.string());

        try {
            Cmd::runCmdCapture(cmd.str());
            return std::filesystem::exists(coverPath);
        } catch (const std::exception &) {
            return false;
        }
    }

    void applyMacFolderIcon(const std::filesystem::path &folderPath, const std::filesystem::path &imagePath) {
        // fileicon set
        // "/Users/opusarc/CLionProjects/MusicCat/output/Mcat Library/A Moment in Time"
        // "/Users/opusarc/CLionProjects/MusicCat/output/Mcat Library/A Moment in Time/Folder.jpg"
        const std::string cmd = "fileicon set \"" + folderPath.string() + "\" \"" + imagePath.string() + "\"";
        Cmd::runCmdCapture(cmd);
    }

    std::filesystem::path makeUniqueDestination(const std::filesystem::path &targetPath) {
        if (!std::filesystem::exists(targetPath)) {
            return targetPath;
        }

        const std::filesystem::path parent = targetPath.parent_path();
        const std::string stem = targetPath.stem().string();
        const std::string ext = targetPath.extension().string();

        for (int i = 1; i <= 9999; ++i) {
            const std::filesystem::path candidate = parent / (stem + " (" + std::to_string(i) + ")" + ext);
            if (!std::filesystem::exists(candidate)) {
                return candidate;
            }
        }

        throw std::runtime_error("Failed to allocate unique destination path for: " + targetPath.string());
    }
}

void MyFfmpeg::flacConvertedToM4aByFilename(const std::string &title) {
    const std::filesystem::path flacPath = Entity::getOutputFolderPath() / (title + ".flac");
    const std::filesystem::path m4aPath = Entity::getOutputFolderPath() / (title + ".m4a");

    if (!std::filesystem::exists(flacPath)) {
        throw std::runtime_error("FLAC file does not exist: " + flacPath.string());
    }

    std::ostringstream cmd;
    cmd << "ffmpeg -y -i " << shellQuote(flacPath.string())
            << " -vn -c:a aac -b:a 256k "
            << shellQuote(m4aPath.string());

    Cmd::runCmdCapture(cmd.str());
}

void MyFfmpeg::applyMetadataToM4aByFilename(const std::string &title, AppleMusicMetadata meta) {
    const std::filesystem::path m4aPath = Entity::getOutputFolderPath() / (title + ".m4a");
    const std::filesystem::path tempPath = Entity::getOutputFolderPath() / (title + ".metadata.tmp.m4a");

    if (!std::filesystem::exists(m4aPath)) {
        throw std::runtime_error("M4A file does not exist: " + m4aPath.string());
    }

    std::ostringstream cmd;
    cmd << "ffmpeg -y -i " << shellQuote(m4aPath.string()) << ' ';

    const bool hasArtwork = meta.artworkPath && !meta.artworkPath->empty() &&
                            std::filesystem::exists(*meta.artworkPath);
    if (hasArtwork) {
        cmd << "-i " << shellQuote(*meta.artworkPath) << ' ';
    }

    cmd << "-map 0:a:0 ";
    if (hasArtwork) {
        cmd << "-map 1:v:0 -disposition:v:0 attached_pic ";
    }

    cmd << "-c:a copy ";
    if (hasArtwork) {
        cmd << "-c:v copy ";
    }

    cmd << "-metadata comment=" << shellQuote("Processed by opus arc") << ' '
            << "-metadata title=" << shellQuote(meta.title_o) << ' '
            << "-metadata artist=" << shellQuote(meta.artist) << ' '
            << "-metadata album=" << shellQuote(meta.album) << ' ';

    if (meta.albumArtist) {
        cmd << "-metadata album_artist=" << shellQuote(*meta.albumArtist) << ' ';
    }
    if (meta.composer) {
        cmd << "-metadata composer=" << shellQuote(*meta.composer) << ' ';
    }
    if (meta.genre) {
        cmd << "-metadata genre=" << shellQuote(*meta.genre) << ' ';
    }
    if (meta.lyrics) {
        cmd << "-metadata lyrics=" << shellQuote(*meta.lyrics) << ' ';
    }
    if (meta.year) {
        cmd << "-metadata date=" << shellQuote(std::to_string(*meta.year)) << ' ';
    }
    if (meta.bpm) {
        cmd << "-metadata bpm=" << shellQuote(std::to_string(*meta.bpm)) << ' ';
    }
    if (meta.rating) {
        cmd << "-metadata rating=" << shellQuote(std::to_string(*meta.rating)) << ' ';
    }
    if (meta.playCount) {
        cmd << "-metadata play_count=" << shellQuote(std::to_string(*meta.playCount)) << ' ';
    }
    if (meta.trackNumber) {
        std::string trackValue = std::to_string(*meta.trackNumber);
        if (meta.trackCount) {
            trackValue += "/" + std::to_string(*meta.trackCount);
        }
        cmd << "-metadata track=" << shellQuote(trackValue) << ' ';
    }
    if (meta.discNumber) {
        std::string discValue = std::to_string(*meta.discNumber);
        if (meta.discCount) {
            discValue += "/" + std::to_string(*meta.discCount);
        }
        cmd << "-metadata disc=" << shellQuote(discValue) << ' ';
    }

    cmd << shellQuote(tempPath.string());

    Cmd::runCmdCapture(cmd.str());

    std::filesystem::rename(tempPath, m4aPath);
}

void MyFfmpeg::applyMetadataToFlacByFilename(const std::string &title, AppleMusicMetadata meta) {
    const std::filesystem::path flacPath = Entity::getOutputFolderPath() / (title + ".flac");
    const std::filesystem::path tempPath = Entity::getOutputFolderPath() / (title + ".metadata.tmp.flac");

    if (!std::filesystem::exists(flacPath)) {
        throw std::runtime_error("FLAC file does not exist: " + flacPath.string());
    }

    const int bitDepth = getFlacBitDepth(flacPath);

    std::ostringstream cmd;
    cmd << "ffmpeg -y -i " << shellQuote(flacPath.string()) << ' ';

    const bool hasArtwork = meta.artworkPath && !meta.artworkPath->empty() &&
                            std::filesystem::exists(*meta.artworkPath);
    if (hasArtwork) {
        cmd << "-i " << shellQuote(*meta.artworkPath) << ' ';
    }

    cmd << "-map 0:a:0 ";
    if (hasArtwork) {
        cmd << "-map 1:v:0 ";
    }

    cmd << "-c:a flac ";

    if (bitDepth > 0) {
        cmd << "-sample_fmt s" << bitDepth << ' ';
    }

    if (hasArtwork) {
        cmd << "-c:v mjpeg "
               "-disposition:v:0 attached_pic ";
    }

    cmd << "-metadata comment=" << shellQuote("Processed by opus arc") << ' '
            << "-metadata title=" << shellQuote(meta.title_o) << ' '
            << "-metadata artist=" << shellQuote(meta.artist) << ' '
            << "-metadata album=" << shellQuote(meta.album) << ' ';

    if (meta.albumArtist) {
        cmd << "-metadata album_artist=" << shellQuote(*meta.albumArtist) << ' ';
    }
    if (meta.composer) {
        cmd << "-metadata composer=" << shellQuote(*meta.composer) << ' ';
    }
    if (meta.genre) {
        cmd << "-metadata genre=" << shellQuote(*meta.genre) << ' ';
    }
    if (meta.lyrics) {
        cmd << "-metadata lyrics=" << shellQuote(*meta.lyrics) << ' ';
    }
    if (meta.year) {
        cmd << "-metadata date=" << shellQuote(std::to_string(*meta.year)) << ' ';
    }
    if (meta.bpm) {
        cmd << "-metadata bpm=" << shellQuote(std::to_string(*meta.bpm)) << ' ';
    }
    if (meta.rating) {
        cmd << "-metadata rating=" << shellQuote(std::to_string(*meta.rating)) << ' ';
    }
    if (meta.playCount) {
        cmd << "-metadata play_count=" << shellQuote(std::to_string(*meta.playCount)) << ' ';
    }
    if (meta.trackNumber) {
        std::string trackValue = std::to_string(*meta.trackNumber);
        if (meta.trackCount) {
            trackValue += "/" + std::to_string(*meta.trackCount);
        }
        cmd << "-metadata track=" << shellQuote(trackValue) << ' ';
    }
    if (meta.discNumber) {
        std::string discValue = std::to_string(*meta.discNumber);
        if (meta.discCount) {
            discValue += "/" + std::to_string(*meta.discCount);
        }
        cmd << "-metadata disc=" << shellQuote(discValue) << ' ';
    }

    cmd << shellQuote(tempPath.string());

    Cmd::runCmdCapture(cmd.str());

    std::filesystem::rename(tempPath, flacPath);
}

int MyFfmpeg::getFlacDurationSecondsByFilename(const std::string &title) {
    const std::filesystem::path flacPath = Entity::getOutputFolderPath() / (title + ".flac");

    if (!std::filesystem::exists(flacPath)) {
        throw std::runtime_error("FLAC file does not exist: " + flacPath.string());
    }

    std::ostringstream cmd;
    cmd << "ffprobe -v error "
            << "-show_entries format=duration "
            << "-of default=noprint_wrappers=1:nokey=1 "
            << shellQuote(flacPath.string());

    const std::string output = Cmd::runCmdCapture(cmd.str());

    if (output.empty()) {
        throw std::runtime_error("ffprobe returned empty duration for: " + flacPath.string());
    }

    try {
        const double seconds = std::stod(output);
        return static_cast<int>(seconds + 0.5);
    } catch (const std::exception &) {
        throw std::runtime_error("Failed to parse duration from ffprobe output: " + output);
    }
}

int MyFfmpeg::getFlacBitDepthByFilename(const std::string &title) {
    const std::filesystem::path flacPath = Entity::getOutputFolderPath() / (title + ".flac");

    if (!std::filesystem::exists(flacPath)) {
        throw std::runtime_error("FLAC file does not exist: " + flacPath.string());
    }

    return getFlacBitDepth(flacPath);
}

void MyFfmpeg::applyCover(const std::string &title) {
    const std::filesystem::path opf = Entity::getOutputFolderPath();
    const std::filesystem::path m4aPath = opf / (title + ".m4a");
    const std::filesystem::path coverPath = opf / (title + ".jpg");
    const std::filesystem::path tempPath = opf / (title + ".cover.tmp.m4a");

    if (!std::filesystem::exists(m4aPath)) {
        Logger::info(
            std::string("[MyFfmpeg]: Skip applying cover because M4A file does not exist: ") + m4aPath.string());
        return;
    }

    if (!std::filesystem::exists(coverPath)) {
        Logger::info(
            std::string("[MyFfmpeg]: Skip applying cover because cover image does not exist: ") + coverPath.string());
        return;
    }

    if (std::filesystem::exists(tempPath)) {
        std::filesystem::remove(tempPath);
    }

    std::ostringstream cmd;
    cmd << "ffmpeg -y "
            << "-i " << shellQuote(m4aPath.string()) << ' '
            << "-i " << shellQuote(coverPath.string()) << ' '
            << "-map 0:a:0 -map 1:v:0 "
            << "-c:a copy "
            << "-c:v mjpeg "
            << "-disposition:v:0 attached_pic "
            << "-metadata:s:v:0 title=" << shellQuote("Cover") << ' '
            << "-metadata:s:v:0 comment=" << shellQuote("Cover (front)") << ' '
            << shellQuote(tempPath.string());

    Cmd::runCmdCapture(cmd.str());

    std::filesystem::rename(tempPath, m4aPath);
    // Logger::info(std::string("[MyFfmpeg]: Attached front cover artwork to ") + m4aPath.string());
}

void MyFfmpeg::organizeAlbums() {
    const std::filesystem::path opf = Entity::getOutputFolderPath();
    const std::filesystem::path libraryRoot = opf / "Mcat Library";

    if (!std::filesystem::exists(opf) || !std::filesystem::is_directory(opf)) {
        throw std::runtime_error("Output folder does not exist or is not a directory: " + opf.string());
    }

    if (!std::filesystem::exists(libraryRoot)) {
        std::filesystem::create_directory(libraryRoot);
    }

    for (const auto &entry: std::filesystem::directory_iterator(opf)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const std::filesystem::path m4aPath = entry.path();
        if (m4aPath.extension() != ".m4a") {
            continue;
        }

        const std::string rawAlbumName = getM4aAlbumName(m4aPath);
        const std::string albumFolderName = sanitizeFolderName(rawAlbumName);
        const std::filesystem::path albumFolderPath = libraryRoot / albumFolderName;
        const bool albumFolderAlreadyExists = std::filesystem::exists(albumFolderPath);

        if (!albumFolderAlreadyExists) {
            std::filesystem::create_directory(albumFolderPath);
        }

        if (!albumFolderAlreadyExists) {
            const std::filesystem::path folderCoverPath = albumFolderPath / "Cover.jpg";
            if (extractCoverFromM4a(m4aPath, folderCoverPath)) {
                applyMacFolderIcon(albumFolderPath, folderCoverPath);
            }
            // std::filesystem::remove(folderCoverPath);
        }

        const std::filesystem::path destinationPath = makeUniqueDestination(albumFolderPath / m4aPath.filename());
        std::filesystem::rename(m4aPath, destinationPath);

        const std::filesystem::path flacPath = opf / (m4aPath.stem().string() + ".flac");
        if (std::filesystem::exists(flacPath) && std::filesystem::is_regular_file(flacPath)) {
            const std::filesystem::path flacFolderPath = albumFolderPath / "flac";
            if (!std::filesystem::exists(flacFolderPath)) {
                std::filesystem::create_directory(flacFolderPath);
            }

            const std::filesystem::path flacDestinationPath = makeUniqueDestination(flacFolderPath / flacPath.filename());
            std::filesystem::rename(flacPath, flacDestinationPath);
        }
    }
}
