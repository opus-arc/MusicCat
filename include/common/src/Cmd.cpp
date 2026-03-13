//
// Created by opus arc on 2026/3/11.
//

#include "../Cmd.h"
#include <array>
#include <cstdio>
#include <memory>
#include <string>

#include <stdexcept>
#include <cstdlib>
#include <sys/wait.h>



std::string Cmd::runCmdCapture(const std::string& cmd) {
    std::array<char, 4096> buffer{};
    std::string result;

    const std::string fullCmd = cmd + " 2>&1";

    std::unique_ptr<FILE, decltype(&pclose)> pipe(
        popen(fullCmd.c_str(), "r"), pclose
    );

    if (!pipe) {
        throw std::runtime_error("popen failed");
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        result += buffer.data();
    }

    const int status = pclose(pipe.release());
    if (status == -1) {
        throw std::runtime_error("pclose failed");
    }

    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }

    return result;
}
int Cmd::runCmdInteractive(const std::string& cmd) {
    const int rc = std::system(cmd.c_str());
    if (rc == -1) {
        throw std::runtime_error("system failed");
    }

    if (WIFEXITED(rc)) {
        return WEXITSTATUS(rc);
    }

    if (WIFSIGNALED(rc)) {
        return 128 + WTERMSIG(rc);
    }

    return rc;
}


// sox -t coreaudio "Apple Music Virtual Device" \
// -b 24 \
// -c 2 \
// -G \
// record.flac
