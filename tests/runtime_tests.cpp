#include "runtime_trace.hpp"
#include <fstream>
#include <iostream>
#include <thread>
#include <windows.h>
int main() {
    using namespace pb;
    const auto directory =
        std::filesystem::current_path() / ("runtime-test-" + std::to_string(GetCurrentProcessId()));
    try {
        {
            RuntimeMonitor monitor(directory, std::chrono::milliseconds(100));
            {
                RuntimeStage ui(RuntimeThread::Ui, "ui.test-stall");
                RuntimeStage pdf(RuntimeThread::Pdf, "pdf.test-render");
                std::this_thread::sleep_for(std::chrono::milliseconds(350));
            }
            for (int i = 0; i < 8; ++i) {
                runtimeHeartbeat(0x113);
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
        }
        bool detected = false;
        for (auto &entry : std::filesystem::directory_iterator(directory)) {
            std::ifstream input(entry.path());
            const std::string log{std::istreambuf_iterator<char>(input), {}};
            detected = log.find("UI=STALLED") != std::string::npos &&
                       log.find("ui.test-stall") != std::string::npos &&
                       log.find("pdf.test-render") != std::string::npos &&
                       log.find("UI=responsive") != std::string::npos &&
                       log.find("STOP") != std::string::npos;
            input.close();
            std::filesystem::remove(entry.path());
        }
        std::filesystem::remove(directory);
        if (!detected)
            throw std::runtime_error("watchdog did not record stalled stages and recovery");
        std::cout << "PASS: background watchdog records blocked UI and recovery\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
