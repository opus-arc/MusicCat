//
// Created by opus arc on 2026/3/11.
//

#ifndef MUSICCAT_CMD_H
#define MUSICCAT_CMD_H
#include <string>


class Cmd {
public:
    static std::string runCmdCapture(const std::string& cmd);
    static int runCmdInteractive(const std::string& cmd);
    static void stopRecording();
};


#endif //MUSICCAT_CMD_H