//
// Created by opus arc on 2026/3/12.
//

#ifndef MUSICCAT_ENTITY_H
#define MUSICCAT_ENTITY_H
#include <filesystem>
#include <string>


class Entity {
    std::string currVirtualDevice;
    std::filesystem::path currOutputFolderPath;

public:
    static void init();

    static void setCurrVirtualDevice(const std::string& vd);

    static void setCurrOutputFolderPath(const std::string &opf);

    static std::string getCurrVirtualDevice();

    static std::filesystem::path getOutputFolderPath();

    static void testCurrVirtualDevice();

    static void testCurrOutputFolderPath();
};


#endif //MUSICCAT_ENTITY_H
