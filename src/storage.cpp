#include "storage.hpp"
#include "app_paths.hpp"
#include <fstream>
#include <future>
#include <iomanip>
#include <sstream>
#include <windows.h>

namespace pb {
Storage::Storage(std::filesystem::path root) : root_(std::move(root)) {
    if (root_.empty())
        root_ = portableDataDirectory() / L"recovery";
    std::filesystem::create_directories(root_);
    worker_ = std::thread([this] {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [&] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty())
                    break;
                if (!stop_ && std::chrono::steady_clock::now() < tasks_.front().due) {
                    const auto due = tasks_.front().due;
                    cv_.wait_until(lock, due);
                    continue; // Re-evaluate after a new stroke replaces the pending snapshot.
                }
                task = std::move(tasks_.front().run);
                tasks_.pop_front();
            }
            try {
                task();
            } catch (...) {
                std::lock_guard lock(mutex_);
                error_ = L"恢复记录写入失败，请及时手动保存 PDF。";
            }
        }
    });
}
Storage::~Storage() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    cv_.notify_one();
    if (worker_.joinable())
        worker_.join();
}
std::filesystem::path Storage::fileFor(const std::filesystem::path &path) const {
    auto key = std::filesystem::absolute(path).lexically_normal().wstring();
    uint64_t hash = 14695981039346656037ull;
    for (wchar_t c : key) {
        c = towlower(c);
        hash ^= uint16_t(c);
        hash *= 1099511628211ull;
    }
    return root_ / (std::to_string(hash) + ".pbrecovery");
}
std::optional<Recovery> Storage::read(const std::filesystem::path &source) const {
    std::error_code ec;
    auto path = fileFor(source);
    auto length = std::filesystem::file_size(path, ec);
    if (ec || length > 64 * 1024 * 1024)
        return {};
    std::ifstream in(path, std::ios::binary);
    std::string magic;
    Recovery r;
    size_t count = 0;
    if (!(in >> magic >> r.sourceStamp >> r.sourceSize >> count) || magic != "PBREC1" || count > 100000)
        return {};
    std::string line;
    std::getline(in, line);
    r.path = source;
    for (size_t i = 0; i < count; ++i) {
        Stroke s;
        if (!std::getline(in, line) || !decode(line, s))
            return {};
        r.strokes.push_back(std::move(s));
    }
    return r;
}
void Storage::post(std::function<void()> task, std::filesystem::path key, std::chrono::milliseconds debounce) {
    std::deque<Task> retired;
    const auto now = std::chrono::steady_clock::now();
    auto first = now;
    {
        std::lock_guard lock(mutex_);
        if (!key.empty())
            for (auto i = tasks_.begin(); i != tasks_.end();)
                if (i->key == key) {
                    first = std::min(first, i->first);
                    retired.push_back(std::move(*i));
                    i = tasks_.erase(i);
                } else
                    ++i;
        const auto due = debounce.count() ? std::min(now + debounce, first + std::chrono::seconds(2)) : now;
        tasks_.push_back({std::move(key), std::move(task), first, due});
    }
    cv_.notify_one();
}
void Storage::write(Recovery r) {
    auto key = fileFor(r.path);
    post(
        [this, r = std::move(r)] {
            std::ostringstream output;
            output << "PBREC1 " << r.sourceStamp << ' ' << r.sourceSize << ' ' << r.strokes.size() << '\n';
            for (const auto &s : r.strokes)
                output << encode(s) << '\n';
            auto bytes = output.str();
            persist(fileFor(r.path), bytes);
            recoveryWrites_.fetch_add(1, std::memory_order_relaxed);
        },
        key, std::chrono::milliseconds(400));
}
void Storage::persist(const std::filesystem::path &path, const std::string &bytes) {
    auto temp = path;
    temp += L".tmp";
    HANDLE handle =
        CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("recovery open");
    DWORD written = 0;
    bool ok = bytes.size() < MAXDWORD &&
              WriteFile(handle, bytes.data(), DWORD(bytes.size()), &written, nullptr) &&
              written == bytes.size() && FlushFileBuffers(handle);
    CloseHandle(handle);
    if (!ok || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("recovery write");
}
void Storage::erase(const std::filesystem::path &path) {
    post(
        [this, path] {
            std::error_code ec;
            std::filesystem::remove(fileFor(path), ec);
            if (ec)
                throw std::runtime_error("recovery remove");
        },
        fileFor(path));
}
void Storage::flush() {
    auto signal = std::make_shared<std::promise<void>>();
    auto done = signal->get_future();
    {
        std::lock_guard lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        for (auto &task : tasks_)
            task.due = now;
        tasks_.push_back({{}, [signal] { signal->set_value(); }, now, now});
    }
    cv_.notify_one();
    done.wait();
}
std::wstring Storage::lastError() const {
    std::lock_guard lock(mutex_);
    return error_;
}
std::string Storage::readSettings() const {
    auto file = root_.parent_path() / "settings.txt";
    std::error_code ec;
    if (std::filesystem::file_size(file, ec) > 16384 || ec)
        return {};
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void Storage::writeSettings(std::string text) {
    auto path = root_.parent_path() / "settings.txt";
    post([this, path, text = std::move(text)] { persist(path, text); }, path);
}
std::optional<ViewState> Storage::readView(const std::filesystem::path &path) const {
    auto file = root_.parent_path() / "reading-state" / fileFor(path).filename();
    file.replace_extension(".pbview");
    std::ifstream in(file);
    ViewState view;
    std::string magic;
    if (!(in >> magic >> view.page >> view.scale >> view.scroll >> view.horizontal >> view.fit >>
          view.hidden) ||
        magic != "PBVIEW1" || view.page < 0 || !std::isfinite(view.scale) || view.scale < .1 ||
        view.scale > 4 || !std::isfinite(view.scroll) || view.scroll < 0 || !std::isfinite(view.horizontal) ||
        view.horizontal < 0)
        return {};
    return view;
}
void Storage::writeView(const std::filesystem::path &path, ViewState view) {
    auto file = root_.parent_path() / "reading-state" / fileFor(path).filename();
    file.replace_extension(".pbview");
    post(
        [this, file, view] {
            std::filesystem::create_directories(file.parent_path());
            std::ostringstream out;
            out << std::setprecision(12) << "PBVIEW1 " << view.page << ' ' << view.scale << ' ' << view.scroll
                << ' ' << view.horizontal << ' ' << view.fit << ' ' << view.hidden;
            persist(file, out.str());
        },
        file);
}
std::vector<std::filesystem::path> Storage::readRecent() const {
    const auto file = root_.parent_path() / "recent.txt";
    std::error_code error;
    const auto size = std::filesystem::file_size(file, error);
    if (error || size > 256 * 1024)
        return {};
    std::ifstream input(file, std::ios::binary);
    std::string magic;
    size_t count{};
    if (!(input >> magic >> count) || magic != "PBRECENT1" || count > 20)
        return {};
    std::vector<std::filesystem::path> result;
    for (size_t i = 0; i < count; ++i) {
        std::string value;
        if (!(input >> std::quoted(value)) || value.empty() || value.size() > 32768)
            return {};
        std::u8string utf8(value.begin(), value.end());
        auto path = std::filesystem::path(utf8);
        if (!path.is_absolute())
            return {};
        result.push_back(std::move(path));
    }
    return result;
}
void Storage::writeRecent(const std::vector<std::filesystem::path> &paths) {
    std::ostringstream output;
    const size_t count = std::min<size_t>(paths.size(), 20);
    output << "PBRECENT1 " << count << '\n';
    for (size_t i = 0; i < count; ++i) {
        const auto utf8 = paths[i].u8string();
        output << std::quoted(std::string(utf8.begin(), utf8.end())) << '\n';
    }
    const auto file = root_.parent_path() / "recent.txt";
    post([this, file, text = output.str()] { persist(file, text); }, file);
}
} // namespace pb
