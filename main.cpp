#include "src/musiccat.hpp"

#include <cstdlib>
#include <exception>
#include <getopt.h>
#include <iostream>
#include <optional>
#include <string>

namespace {

enum class Action {
    none, help, help_zh, help_ja, version, record, record_once, output, device,
    status, list_devices, test, log
};

struct CliOptions {
    Action action = Action::none;
    std::optional<std::string> value;
};

bool select_action(CliOptions& options, Action action,
                   std::optional<std::string> value = std::nullopt) {
    if (options.action != Action::none) return false;
    options.action = action;
    options.value = std::move(value);
    return true;
}

int cli_entry(int argc, char* argv[]) {
    CliOptions options;

    // Compatibility with the original command-shaped interface.
    if (argc == 2) {
        const std::string legacy = argv[1];
        if (legacy == "ready") options.action = Action::record;
        else if (legacy == "log") options.action = Action::log;
        else if (legacy == "help") options.action = Action::help;
        else if (legacy == "zh") options.action = Action::help_zh;
        else if (legacy == "ja" || legacy == "japan") options.action = Action::help_ja;
    }

    if (options.action == Action::none) {
        enum LongOnly { help_zh = 1000, help_ja, list_devices, record_once };
        static const option long_options[] = {
            {"help", no_argument, nullptr, 'h'},
            {"version", no_argument, nullptr, 'v'},
            {"record", no_argument, nullptr, 'r'},
            {"record-once", no_argument, nullptr, record_once},
            {"output", required_argument, nullptr, 'o'},
            {"device", required_argument, nullptr, 'd'},
            {"status", no_argument, nullptr, 's'},
            {"test", no_argument, nullptr, 't'},
            {"log", no_argument, nullptr, 'l'},
            {"zh", no_argument, nullptr, help_zh},
            {"ja", no_argument, nullptr, help_ja},
            {"list-devices", no_argument, nullptr, list_devices},
            {nullptr, 0, nullptr, 0}
        };

        opterr = 0;
        int option = 0;
        while ((option = getopt_long(argc, argv, "hvro:d:stl", long_options, nullptr)) != -1) {
            bool selected = false;
            switch (option) {
                case 'h': selected = select_action(options, Action::help); break;
                case 'v': selected = select_action(options, Action::version); break;
                case 'r': selected = select_action(options, Action::record); break;
                case record_once: selected = select_action(options, Action::record_once); break;
                case 'o': selected = select_action(options, Action::output, optarg); break;
                case 'd': selected = select_action(options, Action::device, optarg); break;
                case 's': selected = select_action(options, Action::status); break;
                case 't': selected = select_action(options, Action::test); break;
                case 'l': selected = select_action(options, Action::log); break;
                case help_zh: selected = select_action(options, Action::help_zh); break;
                case help_ja: selected = select_action(options, Action::help_ja); break;
                case list_devices: selected = select_action(options, Action::list_devices); break;
                default:
                    std::cerr << "mcat: unknown or incomplete command\n";
                    return EXIT_FAILURE;
            }
            if (!selected) {
                std::cerr << "mcat: specify exactly one command per invocation\n";
                return EXIT_FAILURE;
            }
        }
        if (optind != argc) {
            std::cerr << "mcat: unexpected positional argument: " << argv[optind] << '\n';
            return EXIT_FAILURE;
        }
    }

    if (options.action == Action::none) {
        musiccat::print_logo_and_version();
        return EXIT_FAILURE;
    }
    if (options.action == Action::help) {
        musiccat::print_help();
        return EXIT_SUCCESS;
    }
    if (options.action == Action::help_zh) {
        musiccat::print_help_zh();
        return EXIT_SUCCESS;
    }
    if (options.action == Action::help_ja) {
        musiccat::print_help_ja();
        return EXIT_SUCCESS;
    }
    if (options.action == Action::version) {
        musiccat::print_logo_and_version();
        return EXIT_SUCCESS;
    }

    musiccat::Config config = musiccat::load_config();
    musiccat::Logger logger(musiccat::log_path());
    switch (options.action) {
        case Action::record:
            musiccat::run_service(config, logger, false);
            break;
        case Action::record_once:
            musiccat::run_service(config, logger, true);
            break;
        case Action::output:
            config.output = *options.value;
            musiccat::save_config(config);
            std::cout << "Output: " << config.output << '\n';
            break;
        case Action::device:
            config.device = *options.value;
            musiccat::save_config(config);
            std::cout << "Device: " << config.device << '\n';
            break;
        case Action::status:
            musiccat::print_status(config);
            break;
        case Action::list_devices:
            musiccat::list_audio_devices();
            break;
        case Action::test:
            musiccat::run_diagnostics(config, logger);
            break;
        case Action::log:
            logger.print_tail();
            break;
        default:
            return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        return cli_entry(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "mcat: " << error.what() << '\n';
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "mcat: unknown fatal error.\n";
        return EXIT_FAILURE;
    }
}
