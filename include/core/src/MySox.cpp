//
// Created by opus arc on 2026/3/11.
//

#include "../MySox.h"

#include <iostream>

#include "Cmd.h"
#include "Entity.h"

/**
 *   用 SoX 通过 macOS 的 CoreAudio，
 *   打开名为 Apple Music Virtual Device 的输入设备，
 *   把其中的音频录下来，
 *   以 24-bit、双声道方式，
 *   并启用削波保护，
 *   最终保存成一个名为 record.flac 的无损 FLAC 文件。
 */
void MySox::startRecording(const std::string& vd, const std::string& audioName) {
    namespace fs = std::filesystem;

    const fs::path outputFolder = Entity::getOutputFolderPath();
    const fs::path flacPath = outputFolder / (audioName + ".flac");

    const std::string quotedDevice = "\"" + vd + "\"";
    const std::string quotedOutput = "\"" + flacPath.string() + "\"";

    // sox -t coreaudio "Apple Music Virtual Device" -b 24 -c 2 -G "/Users/opusarc/CLionProjects/MusicCat/output/xxx.flac"
    const std::string cmd =
        "sox -t coreaudio " + quotedDevice +
        " -b 24 -c 2 -G " + quotedOutput;

    // std::cout << "cmd = " << cmd << std::endl;

    Cmd::runCmdInteractive(cmd);
}

void MySox::stopRecording() {
    Cmd::runCmdCapture("killall -INT sox");
}
