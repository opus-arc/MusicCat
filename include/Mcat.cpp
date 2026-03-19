//
// Created by opus arc on 2026/3/6.
//

#include "Mcat.h"

#include <future>
#include <thread>

#include <common/FileManager.h>

#include "Entity.h"
#include "Listener.h"
#include "Logger.h"
#include "MyFfmpeg.h"
#include "MySox.h"

#include <string>
#include <iomanip>


constexpr bool needPure = true;
bool Mcat::isRecording = false;
std::queue<AppleMusicMetadata> Mcat::tasks{};


extern std::atomic<bool> g_shouldExit;

void l_isRunning_true();

void l_isRunning_false();

void l_isPlaying_true();

void l_isPlaying_false();

void l_isChanged_true();

void l_isChanged_false();

void l_isPlayingPrue_true();

void l_isPlayingPrue_false();

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
    100
);
Listener Mcat::l_isChanged(
    MyAppleMusic::isNextTrack,
    l_isChanged_true,
    l_isChanged_false,
    100
);

Listener Mcat::l_isPlayingPrue(
    MyAppleMusic::isPlayingPrue,
    l_isPlayingPrue_true,
    l_isPlayingPrue_false,
    300
);


bool firstRunningCheck = true;
bool firstPlayingPrueCheck = true;


void l_isRunning_true() {
    if (firstRunningCheck) {
        Logger::info("Apple Music is running.");
        firstRunningCheck = false;
    } else {
        Logger::info("You ran Apple Music.");
    }
    Mcat::l_isPlaying.on();
}

void l_isRunning_false() {
    if (firstRunningCheck) {
        Logger::info("No running was detected.");
        firstRunningCheck = false;
    } else {
        Logger::info("Apple Music has been shut down.");
    }

    Mcat::l_isPlaying.off();

    // if (Mcat::isRecording)
    //     Mcat::action_2();
}

bool firstPlayingCheck = true;

void l_isPlaying_true() {
    if (firstPlayingCheck) {
        Logger::info("Is playing music.");
        firstPlayingCheck = false;
    } else {
        Logger::info("A piece of music was played.");
    }

    Mcat::tasks.push(Mcat::action_1());

    if (!Mcat::l_isChanged.status())
        Mcat::l_isChanged.on();

    MyAppleMusic::isRestart = true;
    firstPlayingPrueCheck = true;

    if (!Mcat::l_isPlayingPrue.status())
        Mcat::l_isPlayingPrue.on();
}

void l_isPlaying_false() {
    if (firstPlayingCheck) {
        Logger::info("But no music has been played yet.");
        firstPlayingCheck = false;
    } else
        Logger::info("The music seems to have been paused.");

    if (Mcat::isRecording)
        Mcat::action_2();

    if (Mcat::l_isChanged.status())
        Mcat::l_isChanged.off();

    if (Mcat::l_isPlayingPrue.status())
        Mcat::l_isPlayingPrue.off();

    // 暂停后让 l_isChanged 认为已经切换但是不采取 l_isChanged_true() 函数的内容
    // 因为暂停后无论是切歌还是继续播放都不涉及 l_isChanged_true() 里面的行为
    // 上次的关键 bug 就出现在这里 如果不在这里重置下一首的监听器，在上一首曲目暂停并切换到下一首播放的时候
    // 会同时触发 l_isPlaying_true 和 l_isChange_true
    MyAppleMusic::_lastTrackId = MyAppleMusic::currentTrackId();

    MyAppleMusic::isRestart = true;
    firstPlayingPrueCheck = true;

}

bool needToWaitForChange = false;
void l_isChanged_true() {
    testLog("!!!!!!!!!!!!!      Track switching detected.");
    // Logger::info("检测到歌曲切换！ ");

    // 停顿一下 l_isPlaying 监听器
    Mcat::l_isPlaying.sendFalseSignal();
}

void l_isChanged_false() {
}

void l_isPlayingPrue_true() {
    // Logger::info("Mcat: 歌曲纯净播放");
    // 等乐曲缓存结束、正常播放之后检查是否在开头的容忍范围之内
    if (MyAppleMusic::getCurrentPlaybackTime() < 2) {
        Mcat::action_3(Mcat::tasks.back());
    } else {
        Logger::info("This track was not recorded as it was not played from the beginning.");
        needToWaitForChange = true;
        Mcat::l_isPlayingPrue.off();
    }
}

void l_isPlayingPrue_false() {
    if (firstPlayingPrueCheck) {
        firstPlayingPrueCheck = false;
        return ;
    }

    if (Mcat::isRecording) {
        Mcat::action_2();

        Logger::info("Recording has been stopped due to playback stuttering or other abnormal behavior. The recording will wait for the next recording window.");
        needToWaitForChange = true;
        Mcat::l_isPlayingPrue.off();
    } else {
        Logger::info("Playback was found to be choppy or exhibiting other abnormal playback behavior.");
    }
}


void Mcat::setOutput(const std::string &opf) {
    Entity::setCurrOutputFolderPath(opf);
}

void Mcat::setVirtualDevice(const std::string &vd) {
    Entity::setCurrVirtualDevice(vd);
}

void Mcat::printLog() {
    Logger::printLog();
}


inline void enqueue_meta_queue(const std::function<void(AppleMusicMetadata)>& processor) {

    static std::queue<AppleMusicMetadata> queue;
    static std::mutex mtx;
    static std::condition_variable cv;
    static std::once_flag init_flag;
    static std::function<void(AppleMusicMetadata)> stored_processor;
    static std::thread worker;

    // testLog("enqueue_meta_queue: 被调用");

    std::call_once(init_flag, [&] {
        // testLog("enqueue_meta_queue: 初始化 worker");
        stored_processor = processor;

        worker = std::thread([] {
            // testLog("enqueue_meta_queue.worker: 线程已启动");

            while (true) {
                AppleMusicMetadata task;

                {
                    std::unique_lock<std::mutex> lock(mtx);

                    // testLog("enqueue_meta_queue.worker: 等待任务");
                    cv.wait(lock, [] {
                        return !queue.empty();
                    });

                    // testLog("enqueue_meta_queue.worker: 检测到任务，internal queue size(before pop) = "
                    //         + std::to_string(queue.size()));

                    task = queue.front();
                    queue.pop();

                    // testLog("enqueue_meta_queue.worker: 取出任务 -> " + task.title);
                    // testLog("enqueue_meta_queue.worker: internal queue size(after pop) = "
                            // + std::to_string(queue.size()));
                }

                // testLog("enqueue_meta_queue.worker: 开始执行 processor -> " + task.title);
                stored_processor(task);
                // testLog("enqueue_meta_queue.worker: processor 执行完毕 -> " + task.title);
            }
        });

        worker.detach();
    });

    {
        std::lock_guard<std::mutex> lock(mtx);

        // testLog("enqueue_meta_queue: 开始搬运任务");
        // testLog("enqueue_meta_queue: Mcat::tasks size(before) = " + std::to_string(Mcat::tasks.size()));
        // testLog("enqueue_meta_queue: internal queue size(before) = " + std::to_string(queue.size()));

        while (!Mcat::tasks.empty()) {
            // testLog("enqueue_meta_queue: 搬运 task -> " + Mcat::tasks.front().title);
            queue.push(Mcat::tasks.front());
            Mcat::tasks.pop();

            // testLog("enqueue_meta_queue: internal queue size(now) = " + std::to_string(queue.size()));
            // testLog("enqueue_meta_queue: Mcat::tasks size(now) = " + std::to_string(Mcat::tasks.size()));
        }

        // testLog("enqueue_meta_queue: 搬运结束");
        // testLog("enqueue_meta_queue: Mcat::tasks size(after) = " + std::to_string(Mcat::tasks.size()));
        // testLog("enqueue_meta_queue: internal queue size(after) = " + std::to_string(queue.size()));
    }

    // testLog("enqueue_meta_queue: notify_one()");
    cv.notify_one();
}

AppleMusicMetadata Mcat::action_1() {
    testLog("开始执行 action_1");
    try {
        // 获取当前播放的作品的元数据，更新 currMeta
        std::optional<AppleMusicMetadata> currMeta = MyAppleMusic::readAppleMusicMetadata();
        if (!currMeta.has_value()) {
            Logger::warn("The reason why the metadata for this work could not be obtained may be because a music radio station was played or the system was being tested in other special scenarios. Works with incomplete metadata will not be recorded.");
        }
        MyAppleMusic::printAppleMusicMetadata(currMeta.value());

        // recordingMeta 是有别于从 Apple Music 上直接抓取到的 currMeta
        // 是需要考量工程因素，如文件名是否合乎规矩的、加工过的原数据
        currMeta.value().title = sanitizeFileName(currMeta.value().title);
        // std::cout << "文件名化的标题： " << recordingMeta.title << std::endl;

        if (currMeta.value().title.empty()) {
            throw std::runtime_error("No information was found for this track. Please check your network or Apple Music.");
        }

        AppleMusicMetadata _metadata = currMeta.value();

        return _metadata;

    } catch (std::exception &e) {
        Logger::error(e.what());
        AppleMusicMetadata _metadata = {};
        return _metadata;
    }
}

void Mcat::action_2() {
    testLog("开始执行 action_2");
    MySox::stopRecording();
    isRecording = false;

    // std::this_thread::sleep_for(std::chrono::seconds(1));
    enqueue_meta_queue(action_4);
}


void Mcat::action_3(const AppleMusicMetadata &metadata) {
    testLog("action_3: 开始执行");
    testLog("action_3: 线程: " + metadata.title);

    // Logger::info("开始录音");
    MySox::startRecording(metadata.title);
    // sox 录音改为了 后台异步 所以可以边录音边抓封面

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    MyAppleMusic::scrapingCover(metadata.title);
    isRecording = true;
}

// 录音结束之后的数据检查与加工
void Mcat::action_4(const AppleMusicMetadata &metadata) {
    testLog("action_4: 开始执行");
    testLog("action_4: 线程: " + metadata.title);
    // Logger::info("根据 action_5 的返回结果来加工数据");
    if (action_5(metadata)) {
        if (!FileManager::isFlacExist(metadata.title))
            Logger::warn("If too many operations are performed, or if the piece is too short, it will not be recorded until the next suitable recording window.");
        else {
            MyFfmpeg::applyMetadataToFlacByFilename(metadata.title, metadata);

            MyFfmpeg::flacConvertedToM4aByFilename(metadata.title);

            MyFfmpeg::applyMetadataToM4aByFilename(metadata.title, metadata);

            MyFfmpeg::applyCover(metadata.title);

            MyFfmpeg::organizeAlbums(metadata.title);
        }
    } else {
        FileManager::deleteFlacByName(metadata.title);
    }

    FileManager::deleteJpg(metadata.title);
    testLog("！！！！！线程:" + metadata.title + "已经处理完毕！！！");
}

// 检查数据是否纯净
bool Mcat::action_5(const AppleMusicMetadata &metadata) {
    testLog("action_5: 开始执行");
    testLog("action_5: 线程: " + metadata.title);
    // 用于以后可能用到的接口或暂时测试
    if constexpr (!needPure) return true;

    const bool condition_1 = std::abs(metadata.durationSeconds.value() -
                                      MyFfmpeg::getFlacDurationSecondsByFilename(metadata.title)) <= 3;
    const bool condition_2 = MyFfmpeg::getFlacDurationSecondsByFilename(metadata.title) -
                       metadata.durationSeconds.value() >= 0
                       &&
                       MyFfmpeg::getFlacDurationSecondsByFilename(metadata.title) -
                       metadata.durationSeconds.value() <= 5;

    if (condition_1 || condition_2) {
        testLog("action_5: 返回 true, 检测到数据纯净，录制较完整");
        return true;
    }

    testLog("action_5: 返回 false, 检测到数据不纯净，录制并不完整");
    return false;

}



void Mcat::ready() {
    l_isRunning.start();
    l_isPlaying.start();
    l_isChanged.start();
    l_isPlayingPrue.start();

    l_isRunning.on();

    while (!g_shouldExit.load()) {
        // 主循环
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // 退出前清理
    Logger::info("Logging out safely...");
    l_isRunning.stop();
    l_isPlaying.stop();
    l_isChanged.stop();
    l_isPlayingPrue.stop();
}
