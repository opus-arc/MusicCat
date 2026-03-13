//
// Created by opus arc on 2026/3/6.
//

#include "Mcat.h"

#include <future>
#include <iostream>
#include <thread>

#include <common/FileManager.h>

#include "Entity.h"
#include "Listener.h"
#include "Logger.h"
#include "MyFfmpeg.h"
#include "MySox.h"

#include <string>
#include <iomanip>


void l_isRunning_true();

void l_isRunning_false();

void l_isPlaying_true();

void l_isPlaying_false();

void l_isChanged_true();

void l_isChanged_false();

Listener Mcat::l_isRunning(
    MyAppleMusic::isRunning,
    l_isRunning_true,
    l_isRunning_false,
    1500
);
Listener Mcat::l_isPlaying(
    MyAppleMusic::isPlaying,
    l_isPlaying_true,
    l_isPlaying_false,
    500
);
Listener Mcat::l_isChanged(
    MyAppleMusic::isNextTrack,
    l_isChanged_true,
    l_isChanged_false,
    500
);

bool firstRunningCheck = true;

void l_isRunning_true() {
    if (firstRunningCheck) {
        Logger::info("Apple Music 正在运行");
        firstRunningCheck = false;
    } else {
        Logger::info("打开了 Apple Music");
    }
    Mcat::l_isPlaying.on();
}

void l_isRunning_false() {
    if (firstRunningCheck) {
        Logger::info("Apple Music 未在运行");
        firstRunningCheck = false;
    } else {
        Logger::info("关闭了 Apple Music");
    }

    Mcat::l_isPlaying.off();
    if (Mcat::isRecording)
        Mcat::action_2();
}

bool firstPlayingCheck = true;

void l_isPlaying_true() {
    if (firstPlayingCheck) {
        Logger::info("正在播放音乐");
        firstPlayingCheck = false;
    } else {
        Logger::info("播放了一首作品");
    }

    MyAppleMusic::printAppleMusicMetadata(MyAppleMusic::readAppleMusicMetadata().value());

    Mcat::l_isChanged.on();

    if (MyAppleMusic::isCurrentTrackNearBeginning(900))
        Mcat::action_3();
}

void l_isPlaying_false() {
    if (firstPlayingCheck) {
        Logger::info("但还没有播放音乐");
        firstPlayingCheck = false;
    } else {
        std::thread([] {
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
            Logger::info("停止了播放音乐");
        }).detach();
    }

    Mcat::l_isChanged.off();
    if (Mcat::isRecording)
        Mcat::action_2();
}

void l_isChanged_true() {
    // Logger::info("检测到歌曲切换！ ");
    MyAppleMusic::printAppleMusicMetadata(MyAppleMusic::readAppleMusicMetadata().value());
    if (Mcat::isRecording) {
        Mcat::action_2();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        Mcat::action_3();
    } else {
        Mcat::action_3();
    }
}

void l_isChanged_false() {
}

bool Mcat::isRecording = false;
std::optional<AppleMusicMetadata> Mcat::currMeta;
AppleMusicMetadata Mcat::recordingMeta;


void Mcat::setOutput(const std::string &opf) {
    Entity::setCurrOutputFolderPath(opf);
}

void Mcat::setVirtualDevice(const std::string &vd) {
    Entity::setCurrVirtualDevice(vd);
}

void Mcat::printLog() {
    Logger::printLog();
}


bool Mcat::action_1() {
    currMeta = MyAppleMusic::readAppleMusicMetadata();
    if (!currMeta.has_value()) {
        return false;
    }

    recordingMeta = currMeta.value();
    recordingMeta.title = sanitizeFileName(recordingMeta.title);
    return true;
}

void Mcat::action_2() {
    MySox::stopRecording();
    isRecording = false;
}

void eraseLinesFromBottom(const int startFrom, const int count) {
    std::cout << "\r"; // 回到当前行行首

    // 移动到目标起始行
    for (int i = 1; i < startFrom; ++i) {
        std::cout << "\033[1A"; // 上移一行
    }

    // 开始清理
    for (int i = 0; i < count; ++i) {
        std::cout << "\033[2K"; // 清当前行
        if (i < count - 1) {
            std::cout << "\033[1A"; // 上移一行
        }
    }

    std::cout.flush();
}

void Mcat::action_3() {
    if (!action_1()) return;

    const AppleMusicMetadata metadata = recordingMeta;
    const std::string virtualDevice = Entity::getCurrVirtualDevice();


    std::thread t([metadata, virtualDevice] {
        MyAppleMusic::scrapingCover(recordingMeta.title);
        MySox::startRecording(virtualDevice, metadata.title);
        bool testing = true;
        if (testing
            || (
                std::abs(metadata.durationSeconds.value() -
                         MyFfmpeg::getFlacDurationSecondsByFilename(metadata.title)) <= 3
            ) || (
                MyFfmpeg::getFlacDurationSecondsByFilename(metadata.title) -
                metadata.durationSeconds.value() >= 0
                &&
                MyFfmpeg::getFlacDurationSecondsByFilename(metadata.title) -
                metadata.durationSeconds.value() <= 5
            )
        ) {
            MyFfmpeg::applyMetadataToFlacByFilename(metadata.title, metadata);
            MyFfmpeg::flacConvertedToM4aByFilename(metadata.title);
            MyFfmpeg::applyMetadataToM4aByFilename(metadata.title, metadata);
            MyFfmpeg::applyCover(metadata.title);
        } else
            FileManager::deleteFlacByName(metadata.title);

        FileManager::deleteJpg();
        MyFfmpeg::organizeAlbums();
    });
    t.detach();
    isRecording = true;
}


void turnOn_l_isPlaying() {
}


void Mcat::ready() {
    l_isRunning.start();
    l_isPlaying.start();
    l_isChanged.start();

    l_isRunning.on();


    while (true) {
    }
}
