//
// Created by opus arc on 2026/3/7.
//

#ifndef MUSICCAT_PATHMANAGER_H
#define MUSICCAT_PATHMANAGER_H

#include <filesystem>
#include <iostream>

class Path {
public:
    static std::filesystem::path getProjectPath();

    static std::filesystem::path getDefaultOutputPath();

    static std::filesystem::path getCachePath();

    static std::filesystem::path getCacheTxtPath();

    static std::filesystem::path getDataPath();

    static std::filesystem::path getLogPath();

    static std::filesystem::path getLogoPath();
};


#endif //MUSICCAT_PATHMANAGER_H
