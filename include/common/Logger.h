//
// Created by opus arc on 2026/3/7.
//

#ifndef MUSICCAT_LOGMANAGER_H
#define MUSICCAT_LOGMANAGER_H

#include <memory>
#include <string_view>

#include <spdlog/logger.h>


class Logger {
public:
    static void init();

    static std::shared_ptr<spdlog::logger> get();

    static void debug(std::string_view message);

    static void info(std::string_view message);

    static void warn(std::string_view message);

    static void error(std::string_view message);

    static void fatal(std::string_view message);

    static void printLog();

    static void printLastUse();

private:
    static std::shared_ptr<spdlog::logger> logger_;
};

inline bool isTesting = false;


inline void testLog(const std::string &message) {
    if (isTesting)
        Logger::debug(message);
}


#endif // MUSICCAT_LOGMANAGER_H
