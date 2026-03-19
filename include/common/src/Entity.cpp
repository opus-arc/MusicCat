//
// Created by opus arc on 2026/3/12.
//

#include "../Entity.h"

#include "FileManager.h"
#include "Logger.h"
#include "Path.h"

void Entity::init() {

}

std::filesystem::path Entity::getOutputFolderPath() {

    return FileManager::txt_kvPair_reader(Path::getCacheTxtPath(), "currOutputFolderPath");
}

std::string Entity::getCurrVirtualDevice() {
    return FileManager::txt_kvPair_reader(Path::getCacheTxtPath(), "currVirtualDevice");
}

void Entity::setCurrOutputFolderPath(const std::string &opf) {
    Logger::info("Mcat: Set outputPath: " + opf);
    FileManager::txt_kvPair_writer(Path::getCacheTxtPath(),
        "currOutputFolderPath", opf);
}

void Entity::setCurrVirtualDevice(const std::string& vd) {
    Logger::info("Mcat: Set Virtual Device: " + vd);
    FileManager::txt_kvPair_writer(Path::getCacheTxtPath(),
        "currVirtualDevice", vd);
}



// TODO
void Entity::testCurrOutputFolderPath() {

}

// TODO
void Entity::testCurrVirtualDevice(){

}




