#include "runtime_trace.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <windows.h>

namespace pb {
namespace {
struct StageState {
    std::atomic<const char *> name{"idle"};
    std::atomic<ULONGLONG> since{};
};
std::array<StageState, 2> stages;
std::atomic<ULONGLONG> heartbeat{};
std::atomic<unsigned> lastMessage{};
ULONGLONG elapsed(ULONGLONG now, ULONGLONG then) {
    // A heartbeat may be published just after the monitor reads its own clock.
    return now >= then ? now - then : 0;
}
} // namespace
void runtimeHeartbeat(unsigned message) {
    lastMessage.store(message, std::memory_order_relaxed);
    heartbeat.store(GetTickCount64(), std::memory_order_relaxed);
}
RuntimeStage::RuntimeStage(RuntimeThread thread, const char *name) : thread_(thread) {
    auto &state = stages[size_t(thread_)];
    since_ = state.since.exchange(GetTickCount64());
    previous_ = state.name.exchange(name);
}
RuntimeStage::~RuntimeStage() {
    auto &state = stages[size_t(thread_)];
    state.name.store(previous_);
    state.since.store(since_);
}
struct RuntimeMonitor::Impl {
    std::mutex mutex;
    std::condition_variable cv;
    bool stop{};
    std::thread worker;
    Impl(const std::filesystem::path &directory, std::chrono::milliseconds threshold) {
        std::filesystem::create_directories(directory);
        SYSTEMTIME now{};
        GetLocalTime(&now);
        wchar_t filename[96]{};
        swprintf_s(filename, L"runtime-%04u%02u%02u-%02u%02u%02u-%lu.log", now.wYear, now.wMonth, now.wDay,
                   now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId());
        std::ofstream output(directory / filename, std::ios::binary);
        if (!output)
            throw std::runtime_error("Cannot create portable runtime diagnostics");
        output << "InkHorn runtime diagnostics; no PDF contents or paths\nSTART\n";
        output.flush();
        runtimeHeartbeat(0);
        worker = std::thread([this, threshold, output = std::move(output)]() mutable {
            const auto started = GetTickCount64();
            const auto interval =
                std::clamp(threshold / 4, std::chrono::milliseconds(10), std::chrono::milliseconds(250));
            bool uiStalled = false, pdfSlow = false;
            ULONGLONG lastReport{};
            while (true) {
                {
                    std::unique_lock lock(mutex);
                    if (cv.wait_for(lock, interval, [&] { return stop; }))
                        break;
                }
                const auto now = GetTickCount64();
                const auto beat = heartbeat.load();
                const auto pdfSince = stages[1].since.load();
                const auto uiAge = elapsed(now, beat), pdfAge = pdfSince ? elapsed(now, pdfSince) : 0;
                const bool blocked = uiAge >= ULONGLONG(threshold.count());
                const bool slow = pdfSince && pdfAge >= ULONGLONG(threshold.count());
                if (((blocked || slow) && ((!uiStalled && !pdfSlow) || now - lastReport >= 5000)) ||
                    (!blocked && uiStalled) || (!slow && pdfSlow)) {
                    if (output.tellp() < std::streampos(1024 * 1024)) {
                        output << "t=" << now - started << "ms UI=" << (blocked ? "STALLED" : "responsive")
                               << " heartbeat_age=" << uiAge << "ms msg=" << lastMessage.load()
                               << " ui_stage=" << stages[0].name.load()
                               << " pdf_stage=" << stages[1].name.load() << " pdf_age=" << pdfAge << "ms\n";
                        output.flush();
                    }
                    lastReport = now;
                }
                uiStalled = blocked;
                pdfSlow = slow;
            }
            output << "STOP\n";
        });
    }
    ~Impl() {
        {
            std::lock_guard lock(mutex);
            stop = true;
        }
        cv.notify_one();
        worker.join();
    }
};
RuntimeMonitor::RuntimeMonitor(const std::filesystem::path &directory, std::chrono::milliseconds threshold)
    : impl_(std::make_unique<Impl>(directory, threshold)) {}
RuntimeMonitor::~RuntimeMonitor() = default;
} // namespace pb
