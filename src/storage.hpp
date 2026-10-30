#pragma once
#include "core.hpp"
#include <condition_variable>
#include <chrono>
#include <atomic>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace pb {
struct Recovery {
    std::filesystem::path path;
    int64_t sourceStamp{};
    uintmax_t sourceSize{};
    std::vector<Stroke> strokes;
};
struct ViewState {
    int page{};
    double scale{1}, scroll{}, horizontal{};
    bool fit{true}, hidden{};
};
class Storage {
  public:
    explicit Storage(std::filesystem::path root = {});
    ~Storage();
    std::optional<Recovery> read(const std::filesystem::path &) const;
    void write(Recovery recovery);
    void erase(const std::filesystem::path &);
    void flush();
    std::wstring lastError() const;
    size_t recoveryWrites() const { return recoveryWrites_.load(std::memory_order_relaxed); }
    std::string readSettings() const;
    void writeSettings(std::string);
    std::optional<ViewState> readView(const std::filesystem::path &) const;
    void writeView(const std::filesystem::path &, ViewState);
    std::vector<std::filesystem::path> readRecent() const;
    void writeRecent(const std::vector<std::filesystem::path> &);

  private:
    std::filesystem::path root_;
    std::filesystem::path fileFor(const std::filesystem::path &) const;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    struct Task {
        std::filesystem::path key;
        std::function<void()> run;
        std::chrono::steady_clock::time_point first{}, due{};
    };
    std::deque<Task> tasks_;
    std::thread worker_;
    bool stop_{};
    std::wstring error_;
    std::atomic_size_t recoveryWrites_{};
    void post(std::function<void()>, std::filesystem::path key = {},
              std::chrono::milliseconds debounce = std::chrono::milliseconds(0));
    void persist(const std::filesystem::path &, const std::string &);
};
} // namespace pb
