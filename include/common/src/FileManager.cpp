//
// Created by opus arc on 2026/3/6.
//

#include "../FileManager.h"

#include "Entity.h"

bool FileManager::ensure_path(const std::filesystem::path& p) {
    std::filesystem::path dir = p.has_parent_path() ? p.parent_path() : std::filesystem::path();

    std::error_code ec;

    if (!dir.empty()) {
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            return false;
        }
    }

    if (!std::filesystem::exists(p) && p.has_filename()) {
        std::ofstream file(p);
        if (!file) {
            return false;
        }
    }

    return std::filesystem::exists(p);
}

void FileManager::deleteFlacByName(const std::string& flacName) {
    const std::string _flacName = flacName + ".flac";
    const std::filesystem::path flacPath = Entity::getOutputFolderPath() / _flacName;

    if (std::filesystem::exists(flacPath))
        std::filesystem::remove(flacPath);
}

std::string FileManager::txt_kvPair_reader(const std::string &path, const std::string &k) {
    ensure_path(path);

    std::ifstream file(path);
    if (!file.is_open())
        return "";

    std::string line;
    while (std::getline(file, line)) {
        auto pos = line.find('=');
        if (pos == std::string::npos)
            continue;

        std::string key = line.substr(0, pos);
        if (key != k)
            continue;

        return line.substr(pos + 1);
    }

    return "";
}

void FileManager::txt_kvPair_writer(const std::string &path, const std::string &k, const std::string &v) {
    ensure_path(path);

    std::unordered_map<std::string, std::string> kv;

    std::ifstream in(path);
    std::string line;

    while (std::getline(in, line)) {
        auto pos = line.find('=');
        if (pos == std::string::npos)
            continue;

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        kv[key] = value;
    }
    in.close();

    kv[k] = v;

    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open())
        return;

    for (const auto &[key, value]: kv) {
        out << key << "=" << value << "\n";
    }
}

void FileManager::deleteJpg() {
    const std::filesystem::path jpgFolderPath = Entity::getOutputFolderPath();

    if (!std::filesystem::exists(jpgFolderPath) || !std::filesystem::is_directory(jpgFolderPath))
        return;

    for (const auto& entry : std::filesystem::directory_iterator(jpgFolderPath)) {
        if (!entry.is_regular_file())
            continue;

        if (entry.path().extension() == ".jpg") {
            std::filesystem::remove(entry.path());
        }
    }
}
