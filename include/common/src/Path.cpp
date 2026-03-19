//
// Created by opus arc on 2026/3/7.
//

#include <Path.h>
#include <filesystem>
#include <stdexcept>
#include <fstream>

bool Path::isTestingInClion = false;

namespace {
    void ensureCacheTxtInitialized(const std::filesystem::path& cachePath) {
        const auto cacheTxtPath = cachePath / "cache.txt";

        if (std::filesystem::exists(cacheTxtPath) && std::filesystem::file_size(cacheTxtPath) > 0) {
            return;
        }

        std::ofstream cacheFile(cacheTxtPath, std::ios::out | std::ios::trunc);
        if (!cacheFile.is_open()) {
            throw std::runtime_error("Failed to initialize cache.txt at " + cacheTxtPath.string());
        }

        const auto defaultOutputPath = Path::getHomePath() / "Desktop" / "MusicCatOutput";
        std::filesystem::create_directories(defaultOutputPath);

        cacheFile << "currOutputFolderPath=" << defaultOutputPath.string() << "\n";
        cacheFile << "currVirtualDevice=Apple Music Virtual Device\n";
    }

    void ensureLogoFileInitialized(const std::filesystem::path& logoPath) {
        const auto logoParentPath = logoPath.parent_path();
        std::filesystem::create_directories(logoParentPath);

        if (std::filesystem::exists(logoPath) && std::filesystem::file_size(logoPath) > 0) {
            return;
        }

        std::ofstream logoFile(logoPath, std::ios::out | std::ios::trunc);
        if (!logoFile.is_open()) {
            throw std::runtime_error("Failed to initialize logo file at " + logoPath.string());
        }

        logoFile << " ╔╦╗┌─┐┌─┐┌┬┐    /\\_/\\\n";
        logoFile << " ║║║│  ├─┤ │    ( •.• )\n";
        logoFile << " ╩ ╩└─┘┴ ┴ ┴     / >🍪\n";
        logoFile << "for mac and apple music\n";
        logoFile << "  background recorder\n";
    }
}

std::filesystem::path Path::getProjectPath() {
    return std::filesystem::current_path();
}

std::filesystem::path Path::getHomePath() {
    const char* home = std::getenv("HOME");
    if (!home || std::string(home).empty()) {
        throw std::runtime_error("HOME environment variable is not set");
    }
    return std::filesystem::path(home);
}


std::filesystem::path Path::getCachePath() {
    if (isTestingInClion) {
        const auto cachePath = getProjectPath().parent_path() / "include" / "common" / "cache";
        std::filesystem::create_directories(cachePath);
        ensureCacheTxtInitialized(cachePath);
        return cachePath;
    }

    const auto cachePath = getHomePath() / "Library" / "Caches" / "MusicCat";
    std::filesystem::create_directories(cachePath);
    ensureCacheTxtInitialized(cachePath);
    return cachePath;
}

std::filesystem::path Path::getCacheTxtPath() {
    const auto cachePath = getCachePath();
    std::filesystem::create_directories(cachePath);
    ensureCacheTxtInitialized(cachePath);
    return cachePath / "cache.txt";
}

std::filesystem::path Path::getLogPath() {
    if (isTestingInClion) {
        const auto logPath = getProjectPath().parent_path() / "include" / "common" / "log";
        std::filesystem::create_directories(logPath);
        return logPath;
    }

    const auto logPath = getHomePath() / "Library" / "Logs" / "MusicCat";
    std::filesystem::create_directories(logPath);
    return logPath;
}

std::filesystem::path Path::getLogFilePath() {
    const auto logPath = getLogPath();
    std::filesystem::create_directories(logPath);
    return logPath / "musiccat.log";
}

std::filesystem::path Path::getLogoPath() {
    std::filesystem::path logoPath;

    if (isTestingInClion) {
        logoPath = getProjectPath().parent_path() / "logo" / "Mcat_cli_logo.txt";
    } else {
        logoPath = getHomePath() / "Library" / "Application Support" / "MusicCat" / "logo" / "Mcat_cli_logo.txt";
    }

    ensureLogoFileInitialized(logoPath);
    return logoPath;
}

std::filesystem::path Path::getDefaultOutputPath() {
    if (isTestingInClion) {
        const auto outputPath = getProjectPath().parent_path() / "output";
        std::filesystem::create_directories(outputPath);
        return outputPath;
    }

    const auto outputPath = getHomePath() / "Music" / "MusicCat";
    std::filesystem::create_directories(outputPath);
    return outputPath;
}
