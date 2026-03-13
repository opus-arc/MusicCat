//
// Created by opus arc on 2026/3/7.
//

#include <CommonsInit.h>

#include <Logger.h>
#include <FileManager.h>
#include <Path.h>
#include <TypeParser.h>

#include "Entity.h"


/**
 * 测试 Entity 模块
 */
void TestEntity() {
    // Entity::init();
}

/**
 * 测试 Logger 模块
 */
void TestLogger(const bool full) {
    Logger::init();
    if (full)
        Logger::printLastUse();
}

/**
 * 测试 PathManager 模块
 */
void TestPathManager() {
    const std::string McatFolder = Path::getProjectPath().parent_path().parent_path();
    // Logger::info("Mcat is in this folder " + McatFolder);
}

/**
 * 测试 FileManager 模块
 */
void TestFileManager() {
    FileManager::ensure_path(Path::getCacheTxtPath());

    const std::string logFilePath = Path::getLogPath();
    // Logger::info("Current logFile is in " + logFilePath);
}


/**
  * 测试 TypeParser 模块
  */
void TestTypeParser() {
}

/**
  * 测试 TestCmdManager 模块
  */
void TestCmdManager() {

}


/**
 * 测试所有常用模块
 */
void CommonsInit::TestAllCommons(const bool full) {
    TestEntity();
    TestLogger(full);
    TestPathManager();
    TestFileManager();
    TestTypeParser();
}
