//
// Created by opus arc on 2026/3/11.
//

#include "../Cmd.h"
#include <array>
#include <cstdio>
#include <memory>
#include <string>

#include <stdexcept>
#include <unistd.h>
#include <sys/fcntl.h>
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

// pid_t Cmd::runCmdAsync(const std::string& cmd) {
//     pid_t pid = fork();
//
//     if (pid < 0) {
//         throw std::runtime_error("fork failed");
//     }
//
//     if (pid == 0) {
//         // 子进程
//         execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
//
//         // 如果 exec 失败
//         _exit(127);
//     }
//
//     // 父进程：返回子进程 PID
//     return pid;
// }

pid_t Cmd::runCmdAsync(const std::string& cmd) {
    pid_t pid = fork();

    if (pid < 0) {
        throw std::runtime_error("fork failed");
    }

    if (pid == 0) {
        // 子进程

        // 1️⃣ 脱离控制终端（关键）
        if (setsid() < 0) {
            _exit(127);
        }

        // 2️⃣ 重定向标准输出/错误到 /dev/null
        int fd = open("/dev/null", O_RDWR);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            dup2(fd, STDIN_FILENO);
            close(fd);
        }

        // 3️⃣ 执行命令
        execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);

        _exit(127); // exec 失败
    }

    // 父进程：直接返回 PID
    return pid;
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
