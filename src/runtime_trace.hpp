#pragma once
#include <chrono>
#include <filesystem>
#include <memory>

namespace pb {
// Stage names must have static storage; no document text or paths are recorded.
enum class RuntimeThread { Ui, Pdf };
void runtimeHeartbeat(unsigned message);
class RuntimeStage {
    RuntimeThread thread_;
    const char *previous_;
    unsigned long long since_;

  public:
    RuntimeStage(RuntimeThread thread, const char *name);
    ~RuntimeStage();
};
class RuntimeMonitor {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit RuntimeMonitor(const std::filesystem::path &directory,
                            std::chrono::milliseconds threshold = std::chrono::seconds(1));
    ~RuntimeMonitor();
};
} // namespace pb
