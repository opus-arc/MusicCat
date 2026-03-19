//
// Created by opus arc on 2026/3/6.
//

#ifndef MUSICCAT_PERSISTENTSTORAGE_H
#define MUSICCAT_PERSISTENTSTORAGE_H
#include <fstream>
#include <sstream>


class FileManager {
public:

    // 确保一个路径绝对存在
    static bool ensure_path(const std::filesystem::path& p);

    // 删除一个 flac 文件
    static void deleteFlacByName(const std::string& flacName);

    // 侦测一个 flac 文件
    static bool isFlacExist(const std::string &flacName);

    // txt 文件键值对读取
    static std::string txt_kvPair_reader(const std::string &path, const std::string &k);

    // txt 文件键值对写入
    static void txt_kvPair_writer(const std::string& path, const std::string& k, const std::string& v);

    // 删除一个文件夹底下的所有 jpg 后缀的文件
    static void deleteJpg(const std::string& title);

};


#endif //MUSICCAT_PERSISTENTSTORAGE_H