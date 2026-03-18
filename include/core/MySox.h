//
// Created by opus arc on 2026/3/11.
//

#ifndef MUSICCAT_MYSOX_H
#define MUSICCAT_MYSOX_H
#include <string>
#include <filesystem>


class MySox {
    static pid_t sox_pid_t;
public:
    // 用 active cmd 命令 sox 开始录音
    // sox -t coreaudio "Apple Music Virtual Device" -b 24 -c 2 -G record.flac
    // sox -t coreaudio [Virtual Device] -b 24 -c 2 -G [Audio Output Path]
    static void startRecording(const std::string& audioName);
    static void stopRecording();

};


#endif //MUSICCAT_MYSOX_H
