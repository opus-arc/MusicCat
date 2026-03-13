//
// Created by opus arc on 2026/3/6.
//

#ifndef MUSICCAT_MCAT_H
#define MUSICCAT_MCAT_H
#include <iostream>
#include <string>

#include "Listener.h"
#include "Public.h"

#define OUTPUT_PATH "/output"


/**
 * # Mcat Backend Interface
 *
 * EN:
 * Core backend class for the MusicCat CLI application.
 * This class contains the real implementation of recording logic,
 * Apple Music monitoring, metadata handling, and configuration.
 *
 * CN:
 * MusicCat 的核心后端类。
 * 该类负责实际业务逻辑，包括：
 * - Apple Music 播放状态监听
 * - 录音控制
 * - 元数据处理
 * - 配置管理
 */
class Mcat {
public:
    static Listener l_isRunning;
    static Listener l_isPlaying;
    static Listener l_isChanged;

    static bool isRecording;
    static std::optional<AppleMusicMetadata> currMeta;
    static AppleMusicMetadata recordingMeta;

    static bool action_1();

    static void action_2();

    static void action_3();


    /**
     * ## ready
     *
     * EN:
     * Start the listener that monitors Apple Music playback and
     * triggers recording when playback begins.
     *
     * CN:
     * 启动监听器，监控 Apple Music 的播放行为，
     * 一旦检测到播放开始就触发录音。
     */
    static void ready();

    /**
     * ## setOutput
     *
     * EN:
     * Set the default output directory for recordings.
     *
     * CN:
     * 设置录音文件默认输出目录。
     */
    static void setOutput(const std::string &opf);

    /**
     * ## setOutput
     *
     * EN:
     * Set the default output directory for recordings.
     *
     * CN:
     * 设置录音文件默认输出目录。
     */
    static void setVirtualDevice(const std::string &vd);


    /**
     * ## printLog
     *
     * EN:
     * Print log
     *
     * CN:
     * 打印 log
     */
    static void printLog();
};


inline std::string PlaybackClockTime::toDisplayString() const {
    return timeString;
}

#endif //MUSICCAT_MCAT_H
