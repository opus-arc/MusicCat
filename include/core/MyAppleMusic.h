//
// Created by opus arc on 2026/3/7.
//

#ifndef MUSICCAT_MYAPPLEMUSIC_H
#define MUSICCAT_MYAPPLEMUSIC_H

#include <iostream>

#include "Mcat.h"
#include "Public.h"

class MyAppleMusic {
public:

    static bool isRestart;
    static double lastTime;
    static std::chrono::time_point<std::chrono::steady_clock> lastCheck;
    static std::string lastTrackId;
    static std::string _lastTrackId;

    static bool isRunning();

    static bool isPlaying();

    static std::string currentTrackId();

    static double getCurrentPlaybackTime();

    static bool isNextTrack();

    static bool isPlayingPrue();

    static bool isCurrentTrackNearBeginning(int toleranceMs);

    static std::optional<AppleMusicMetadata> readAppleMusicMetadata();

    static void printAppleMusicMetadata(const AppleMusicMetadata& metadata);

    static void scrapingCover(std::string title);
};


#endif //MUSICCAT_MYAPPLEMUSIC_H
