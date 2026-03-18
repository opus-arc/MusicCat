//
// Created by opus arc on 2026/3/12.
//

#ifndef MUSICCAT_PUBLIC_H
#define MUSICCAT_PUBLIC_H
#include <iomanip>
#include <regex>
#include <string>
#include <iostream>


struct PlaybackClockTime {
    int totalSeconds = 0;
    std::string timeString{};

    [[nodiscard]] int minutes() const { return totalSeconds / 60; }
    [[nodiscard]] int seconds() const { return totalSeconds % 60; }

    [[nodiscard]] std::string toDisplayString() const;

    [[nodiscard]] int operator-(const PlaybackClockTime &other) const { return totalSeconds - other.totalSeconds; }

    [[nodiscard]] bool operator==(const PlaybackClockTime &other) const { return totalSeconds == other.totalSeconds; }
    [[nodiscard]] bool operator!=(const PlaybackClockTime &other) const { return totalSeconds != other.totalSeconds; }
    [[nodiscard]] bool operator<(const PlaybackClockTime &other) const { return totalSeconds < other.totalSeconds; }
    [[nodiscard]] bool operator<=(const PlaybackClockTime &other) const { return totalSeconds <= other.totalSeconds; }
    [[nodiscard]] bool operator>(const PlaybackClockTime &other) const { return totalSeconds > other.totalSeconds; }
    [[nodiscard]] bool operator>=(const PlaybackClockTime &other) const { return totalSeconds >= other.totalSeconds; }
};

struct AppleMusicMetadata {
    std::string title;
    std::string title_o;

    std::string artist;
    std::string album;

    std::optional<std::string> albumArtist;
    std::optional<std::string> composer;
    std::optional<std::string> genre;
    std::optional<std::string> lyrics;

    std::optional<int> durationSeconds;
    std::optional<int> year;
    std::optional<int> trackNumber;
    std::optional<int> trackCount;
    std::optional<int> discNumber;
    std::optional<int> discCount;
    std::optional<int> bitRate;
    std::optional<int> sampleRate;
    std::optional<int> bpm;
    std::optional<int> playCount;
    std::optional<int> rating;

    std::optional<std::string> artworkPath;

    AppleMusicMetadata &operator=(const AppleMusicMetadata &) = default;

    static void printOptionalField(const std::string &label, const std::optional<std::string> &value) {
        std::cout << label << ": " << (value ? *value : "not provided") << '\n';
    }

    static void printOptionalField(const std::string &label, const std::optional<int> &value) {
        if (value) {
            std::cout << label << ": " << *value << '\n';
        } else {
            std::cout << label << ": <unavailable>\n";
        }
    }
};

// enum MetadataState {
//     // 录制异常
//     未初始化,
//
//     // 录制失败
//     未从头播放,
//     存在缓存卡顿,
//     中途切歌,
//
//     // 录制成功
//     完整录制
// };
// struct Task {
//     AppleMusicMetadata metadata;
//     bool isProcessing;
//     bool finished;
// };
//
// struct MetadataProcessor {
//     std::queue<Task> tasks;
//
//     Task getTask() {
//
//     }
// };


inline std::string sanitizeFileName(const std::string &title, const std::size_t maxLen = 180) {
    std::string s = title;

    // 1) 把最危险、最常见的文件名问题字符替换掉
    // mac/Linux 不能含 '/'
    // Windows 也不喜欢 \ : * ? " < > |
    static const std::regex badChars(R"([\/\\:\*\?"<>\|])");
    s = std::regex_replace(s, badChars, " - ");

    // 2) 去掉控制字符
    static const std::regex ctrlChars(R"([\x00-\x1F\x7F])");
    s = std::regex_replace(s, ctrlChars, "");

    // 3) 把连续空白压成一个空格
    static const std::regex multiSpace(R"(\s+)");
    s = std::regex_replace(s, multiSpace, " ");

    // 4) 去掉首尾空格
    auto trim = [](std::string &str) {
        auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
        str.erase(str.begin(), std::find_if(str.begin(), str.end(), notSpace));
        str.erase(std::find_if(str.rbegin(), str.rend(), notSpace).base(), str.end());
    };
    trim(s);

    // 5) 避免文件名结尾是 '.' 或空格
    while (!s.empty() && (s.back() == '.' || s.back() == ' ')) {
        s.pop_back();
    }

    // 6) 空字符串兜底
    if (s.empty()) {
        s = "Untitled";
    }

    // 7) 控制长度，给扩展名留空间
    if (s.size() > maxLen) {
        s = s.substr(0, maxLen);
        while (!s.empty() && (s.back() == ' ' || s.back() == '.')) {
            s.pop_back();
        }
        if (s.empty()) {
            s = "Untitled";
        }
    }

    return s;
}

namespace TermFix {
    static inline std::string trim(const std::string &s) {
        const auto begin = s.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) return "";
        const auto end = s.find_last_not_of(" \t\r\n");
        return s.substr(begin, end - begin + 1);
    }

    static inline bool startsWith(const std::string &s, const std::string &prefix) {
        return s.rfind(prefix, 0) == 0;
    }

    struct SoxField {
        std::string key;
        std::string value;
    };

    static std::vector<SoxField> parseSoxHeader(const std::string &headerText) {
        std::vector<SoxField> fields;
        std::istringstream iss(headerText);
        std::string line;

        while (std::getline(iss, line)) {
            line = trim(line);
            if (line.empty()) continue;

            auto pos = line.find(':');
            if (pos == std::string::npos) continue;

            std::string key = trim(line.substr(0, pos));
            std::string value = trim(line.substr(pos + 1));

            // 去掉 Input File
            if (key == "Input File") continue;

            fields.push_back({key, value});
        }

        return fields;
    }

    static void eraseCurrentLine() {
        std::cout << "\r\033[2K";
    }

    static void moveCursorUp(int n) {
        if (n > 0) {
            std::cout << "\033[" << n << "A";
        }
    }

    static void rewriteSoxHeaderAligned(const std::string &headerText) {
        auto fields = parseSoxHeader(headerText);
        if (fields.empty()) return;

        // 计算 key 最大宽度，让冒号对齐
        std::size_t maxKeyLen = 0;
        for (const auto &f: fields) {
            maxKeyLen = std::max(maxKeyLen, f.key.size());
        }

        // sox 原先通常打印 5 行头部 + 1 个空行，然后下面是实时状态行
        // 我们要删掉上面的 6 行，再按新格式重写 4 行 + 1 空行
        moveCursorUp(6);

        for (int i = 0; i < 6; ++i) {
            eraseCurrentLine();
            if (i != 5) std::cout << '\n';
        }

        moveCursorUp(5);

        for (const auto &f: fields) {
            std::cout << std::setw(static_cast<int>(maxKeyLen))
                    << std::right
                    << f.key
                    << ": "
                    << f.value
                    << '\n';
        }

        std::cout << '\n';
        std::cout.flush();
    }
} // namespace TermFix


#endif //MUSICCAT_PUBLIC_H
