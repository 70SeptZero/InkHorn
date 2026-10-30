#pragma once
#include "core.hpp"
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace pb {
struct DocumentInfo {
    uint64_t id{};
    std::filesystem::path path;
    std::vector<Size> pages;
    std::vector<Stroke> strokes;
    int64_t sourceStamp{};
    uintmax_t sourceSize{};
};
struct Bitmap {
    uint64_t document{};
    int page{}, width{}, height{};
    double scale{};
    std::vector<uint8_t> pixels;
};
struct Result {
    enum class Type { Opened, Rendered, Saved, Failed } type{Type::Failed};
    uint64_t document{};
    DocumentInfo info;
    Bitmap bitmap;
    std::string saveFingerprint;
    std::filesystem::path path;
    int64_t sourceStamp{};
    uintmax_t sourceSize{};
    std::wstring error;
    uint64_t renderTicket{};
};
class PdfEngine {
  public:
    using Callback = std::function<void(std::shared_ptr<Result>)>;
    explicit PdfEngine(Callback);
    ~PdfEngine();
    void open(uint64_t, const std::filesystem::path &);
    void render(uint64_t, int, double, bool includeSavedAnnotations = false, uint64_t ticket = 0);
    void cancelRender(uint64_t);
    void save(uint64_t, std::vector<Stroke>, std::string, const std::filesystem::path &target = {});
    void close(uint64_t);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace pb
