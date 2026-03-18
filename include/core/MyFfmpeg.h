//
// Created by opus arc on 2026/3/7.
//

#ifndef MUSICCAT_MYFFMPEG_H
#define MUSICCAT_MYFFMPEG_H
#include <string>

#include "Mcat.h"
#include "MyAppleMusic.h"


class MyFfmpeg {
public:

    static void flacConvertedToM4aByFilename(const std::string& title);

    static void applyMetadataToM4aByFilename(const std::string& title, AppleMusicMetadata meta);

    static void applyMetadataToFlacByFilename(const std::string &title, AppleMusicMetadata meta);

    static int getFlacDurationSecondsByFilename(const std::string& title);

    static void applyCover(const std::string& title);

    static int getFlacBitDepthByFilename(const std::string &title);

    static void organizeAlbums(const std::string &title);

};


#endif //MUSICCAT_MYFFMPEG_H
