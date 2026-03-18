//
// Created by opus arc on 2026/3/11.
//

#include "../MySox.h"

#include <iostream>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

#include "Cmd.h"
#include "Entity.h"
#include "Logger.h"
#include "Public.h"
pid_t MySox::sox_pid_t = 0;
/**
 *   用 SoX 通过 macOS 的 CoreAudio，
 *   打开名为 Apple Music Virtual Device 的输入设备，
 *   把其中的音频录下来，
 *   以 24-bit、双声道方式，
 *   并启用削波保护，
 *   最终保存成一个名为 record.flac 的无损 FLAC 文件。
 */
void MySox::startRecording(const std::string& audioName) {
    testLog("recording: 开始录音");
    testLog("recording: 线程" + audioName);

    const std::string virtualDevice = Entity::getCurrVirtualDevice();
    namespace fs = std::filesystem;

    const fs::path outputFolder = Entity::getOutputFolderPath();
    const fs::path flacPath = outputFolder / (audioName + ".flac");

    const std::string quotedDevice = "\"" + virtualDevice + "\"";
    const std::string quotedOutput = "\"" + flacPath.string() + "\"";

    // sox -t coreaudio "Apple Music Virtual Device" -b 24 -c 2 -G "/Users/opusarc/CLionProjects/MusicCat/output/xxx.flac"
    const std::string cmd =
        "sox -t coreaudio " + quotedDevice +
        " -b 24 -c 2 -G " + quotedOutput;

    // std::cout << "cmd = " << cmd << std::endl;

    sox_pid_t = Cmd::runCmdAsync(cmd);
    testLog("recording: 进程" + fmt::to_string(sox_pid_t));

}

void MySox::stopRecording() {
    testLog("recording: 停止录音");
    testLog("recording: 进程: " + std::to_string(sox_pid_t));

    if (sox_pid_t <= 0) {
        Logger::error("没有可以结束的sox进程或pid未正确保存");
        return;
    }

    const pid_t pid = sox_pid_t;

    if (kill(pid, SIGINT) != 0) {
        Logger::error("发送 SIGINT 失败, errno=" + std::to_string(errno));
        sox_pid_t = 0;
        return;
    }

    int status = 0;
    while (true) {
        const pid_t result = waitpid(pid, &status, 0);

        if (result == pid) {
            break;
        }

        if (result == -1) {
            if (errno == EINTR) {
                continue;
            }

            Logger::error("等待 sox 进程退出失败, errno=" + std::to_string(errno));
            break;
        }
    }

    sox_pid_t = 0;
}
