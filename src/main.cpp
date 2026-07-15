#include "app/App.hpp"
#include "core/Logger.hpp"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>

namespace {

void printUsage() {
    xpad::core::Logger::info(
        "Uso: xpad_link [--tempo <bpm>] [--headless] [--duration-seconds <n>] "
        "[--print-ms <n>] [--simulation-only] [--config <path>]");
}

bool isOption(std::string_view arg, std::string_view shortName, std::string_view longName) {
    return arg == shortName || arg == longName;
}

} // namespace

int main(int argc, char** argv) {
    xpad::app::App::Config config;

    try {
        for (int index = 1; index < argc; ++index) {
            const std::string arg = argv[index];

            if (isOption(arg, "-h", "--help"))    { printUsage(); return 0; }

            if (isOption(arg, "-t", "--tempo") && index + 1 < argc) {
                config.initialTempoBpm = std::stod(argv[++index]); continue;
            }
            if (arg == "--duration-seconds" && index + 1 < argc) {
                config.runFor = std::chrono::seconds(std::stoi(argv[++index])); continue;
            }
            if (arg == "--print-ms" && index + 1 < argc) {
                config.printEvery = std::chrono::milliseconds(std::stoi(argv[++index])); continue;
            }
            if (arg == "--headless")        { config.headless = true; continue; }
            if (arg == "--simulation-only") { config.preferAbletonLink = false; continue; }
            if (arg == "--config" && index + 1 < argc) {
                config.configFile = argv[++index]; continue;
            }

            xpad::core::Logger::error("Argumento desconhecido: " + arg);
            printUsage();
            return 1;
        }

        xpad::app::App app(config);
        return app.run();
    } catch (const std::exception& exception) {
        xpad::core::Logger::error(std::string("Falha: ") + exception.what());
        return 1;
    }
}
