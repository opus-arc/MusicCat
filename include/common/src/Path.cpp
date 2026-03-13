//
// Created by opus arc on 2026/3/7.
//

#include <Path.h>
#include <filesystem>

#include "FileManager.h"

std::filesystem::path Path::getProjectPath() {
    return std::filesystem::current_path();
}

std::filesystem::path Path::getCachePath() {
    return getProjectPath().parent_path() / "include" / "common" / "cache" ;
}

std::filesystem::path Path::getCacheTxtPath() {
    return getProjectPath().parent_path() / "include" / "common" / "cache" / "cache.txt";
}

std::filesystem::path Path::getDataPath() {
    return getProjectPath().parent_path() / "include" / "common" / "data";
}

std::filesystem::path Path::getLogPath() {
    return getProjectPath().parent_path() / "include" / "common" / "log";
}

std::filesystem::path Path::getLogoPath() {
    return getProjectPath().parent_path() / "logo" / "Mcat_cli_logo.txt";
}

std::filesystem::path Path::getDefaultOutputPath() {
    return getProjectPath().parent_path() / "output";
}


