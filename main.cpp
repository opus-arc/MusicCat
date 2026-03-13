/**
 * # MusicCat CLI Entry
 */

#include <iostream>
#include <string>
#include <string_view>
#include <Mcat.h>
#include <CommonsInit.h>
#define MCAT_VERSION "version 0.1.0"

void help();

namespace {
    constexpr std::string_view kAppName = "MusicCat";


    void printUnknownCommand(const std::string_view command) {
        std::cerr << "Unknown command: " << command << "\n\n";
        help();
    }

    void readyCommand(const int argc, char *argv[]) {
        CommonsInit::TestAllCommons(true);
        if (argc < 3) {
            Mcat::ready();
            return;
        }

        const std::string outputPath = argv[2];

        Mcat::ready();
    }

    void outputCommand(const int argc, char *argv[]) {
        CommonsInit::TestAllCommons(false);
        if (argc < 3) {
            std::cerr << "Mcat: Usage: -o <outputPath>\n";
            return;
        }
        Mcat::setVirtualDevice(argv[2]);
    }

    void virtualDeviceCommand(const int argc, char *argv[]) {
        CommonsInit::TestAllCommons(false);
        if (argc < 3) {
            std::cerr << "Mcat: Usage: -vd <available virtual device>\n";
            return;
        }
        Mcat::setOutput(argv[2]);
    }

    void versionCommand() {
        CommonsInit::TestAllCommons(false);
        const std::string version = MCAT_VERSION;
        std::cout << kAppName << " " << version << std::endl;
    }

    void logCommand() {
        CommonsInit::TestAllCommons(false);
        Mcat::printLog();
    }

} // namespace


void help() {
    std::cout << "MusicCat - Apple Music recording CLI\n\n";

    std::cout << "Usage:\n";
    std::cout << "  MusicCat <command> [options]\n\n";

    std::cout << "Commands:\n";
    std::cout << "  ready                Start the Apple Music recording listener\n";
    std::cout << "  log                  Print runtime logs\n";
    std::cout << "  help                 Show this help message\n";
    std::cout << "  zh                   Show Chinese help\n";
    std::cout << "  ja                   Show Japanese help\n\n";

    std::cout << "Options:\n";
    std::cout << "  -o <path>            Set output directory\n";
    std::cout << "  -vd <device>         Set virtual audio device\n";
    std::cout << "  -v, -version         Show program version\n";
}

void helpZh() {
    std::cout << "MusicCat - Apple Music 自动录音工具\n\n";

    std::cout << "用法:\n";
    std::cout << "  MusicCat <命令> [参数]\n\n";

    std::cout << "命令:\n";
    std::cout << "  ready                启动 Apple Music 监听并自动录音\n";
    std::cout << "  log                  查看运行日志\n";
    std::cout << "  help                 显示英文帮助\n";
    std::cout << "  zh                   显示中文帮助\n";
    std::cout << "  ja                   显示日文帮助\n\n";

    std::cout << "参数:\n";
    std::cout << "  -o <路径>            设置输出目录\n";
    std::cout << "  -vd <设备名>         设置虚拟音频设备\n";
    std::cout << "  -v, -version         显示版本号\n";
}

void helpJa() {
    std::cout << "MusicCat - Apple Music 自動録音ツール\n\n";

    std::cout << "使い方:\n";
    std::cout << "  MusicCat <コマンド> [オプション]\n\n";

    std::cout << "コマンド:\n";
    std::cout << "  ready                Apple Music を監視して自動録音を開始\n";
    std::cout << "  log                  実行ログを表示\n";
    std::cout << "  help                 英語ヘルプを表示\n";
    std::cout << "  zh                   中国語ヘルプを表示\n";
    std::cout << "  ja                   日本語ヘルプを表示\n\n";

    std::cout << "オプション:\n";
    std::cout << "  -o <path>            出力ディレクトリを設定\n";
    std::cout << "  -vd <device>         仮想オーディオデバイスを設定\n";
    std::cout << "  -v, -version         バージョンを表示\n";
}


int main(const int argc, char *argv[]) {

    if (argc < 2) {
        // help();
        return 0;
    }

    const std::string cmd = argv[1];

    if (cmd == "help"
        || cmd == "-h"
        || cmd == "--help"
        || cmd == "--zh"
        || cmd == "zh"
        || cmd == "ja"
        || cmd == "--ja"
    ) {
        if (cmd == "zh" || cmd == "--zh")
            helpZh();
        else if (cmd == "ja" || cmd == "--ja" || cmd == "japan")
            helpJa();
        else
            help();
        return 0;
    }

    if (cmd == "ready") {
        readyCommand(argc, argv);
        return 0;
    }


    if (cmd == "-o" || cmd == "-output" || cmd == "-opf") {
        outputCommand(argc, argv);
        return 0;
    }

    if (cmd == "-vd" || cmd == "-virtualDevice" || cmd == "-d") {
        virtualDeviceCommand(argc, argv);
        return 0;
    }

    if (cmd == "-ver" || cmd == "-version" || cmd == "-v") {
        versionCommand();
        return 0;
    }

    if (cmd == "log") {
        logCommand();
        return 0;
    }

    printUnknownCommand(cmd);
    return 1;
}

