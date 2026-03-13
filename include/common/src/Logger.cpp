//
// Created by opus arc on 2026/3/7.
//

#include <Logger.h>
#include <Path.h>
#include <FileManager.h>

#include <filesystem>
#include <vector>
#include <fstream>
#include <string>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include "Entity.h"

std::shared_ptr<spdlog::logger> Logger::logger_ = nullptr;

void Logger::init() {
    if (logger_) {
        return;
    }

    // Ensure log directory exists
    const auto logDir = Path::getLogPath();
    FileManager::ensure_path(logDir);

    auto logFile = (logDir / "musiccat.log").string();

    // Console output
    const auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

    // File output with rotation
    const auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        logFile,
        5 * 1024 * 1024,   // 5MB per file
        3                  // keep 3 old log files
    );

    std::vector<spdlog::sink_ptr> sinks { console_sink, file_sink };

    logger_ = std::make_shared<spdlog::logger>("musiccat", sinks.begin(), sinks.end());

    logger_->set_pattern("[%Y-%m-%d %H:%M:%S] [%^%l%$] %v");
    logger_->set_level(spdlog::level::debug);

    spdlog::set_default_logger(logger_);
}

std::shared_ptr<spdlog::logger> Logger::get() {
    return logger_;
}

void Logger::debug(const std::string_view message) {
    if (logger_) {
        logger_->debug(message);
    }
}

void Logger::info(const std::string_view message) {
    if (logger_) {
        logger_->info(message);
    }
}

void Logger::warn(const std::string_view message) {
    if (logger_) {
        logger_->warn(message);
    }
}

void Logger::error(const std::string_view message) {
    if (logger_) {
        logger_->error(message);
    }
}

void Logger::fatal(const std::string_view message) {
    if (logger_) {
        logger_->critical(message);
    }
}

void printLogo() {
    const std::string path = Path::getLogoPath();

    std::ifstream in(path, std::ios::in);
    if (!in) {
        std::cerr << "Failed to open file: " << path << "\n";
        return;
    }

    std::string line;
    while (std::getline(in, line)) {
        std::cout << line << '\n';
    }
}


void Logger::printLastUse() {

    printLogo();

    std::cout << " vd: " << Entity::getCurrVirtualDevice() << std::endl;
    std::cout << "opf: "<< Entity::getOutputFolderPath() << std::endl;

    std::cout << std::endl;

    const auto logDir = Path::getLogPath();
    const auto logFile = logDir / "musiccat.log";

    std::ifstream file(logFile);
    if (!file.is_open()) {
        Logger::info("Welcome to MusicCat");
        return;
    }

    std::string line;
    std::string lastLine;

    while (std::getline(file, line)) {
        if (!line.empty()) {
            lastLine = line;
        }
    }

    if (lastLine.empty()) {
        Logger::info("Welcome to MusicCat");
        return;
    }

    auto start = lastLine.find('[');
    auto end = lastLine.find(']');

    if (start == std::string::npos || end == std::string::npos || end <= start + 1) {
        Logger::info("Welcome to MusicCat");
        return;
    }

    std::string time = lastLine.substr(start + 1, end - start - 1);

    std::string message = "Last time used: " + time;
    Logger::info(message);
}

void Logger::printLog() {
    const auto logDir = Path::getLogPath();
    const auto logFile = logDir / "musiccat.log";

    std::ifstream file(logFile);
    if (!file.is_open()) {
        warn("Failed to open log file");
        return;
    }

    std::vector<std::string> lines;
    std::string line;

    while (std::getline(file, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }

    if (lines.empty()) {
        info("Log file is empty");
        return;
    }

    constexpr std::size_t maxLines = 20;
    const std::size_t start = lines.size() > maxLines ? lines.size() - maxLines : 0;

    for (std::size_t i = start; i < lines.size(); ++i) {
        std::cout << lines[i] << '\n';
    }
}


