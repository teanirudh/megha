#pragma once

#include <fstream>
#include <mutex>
#include <string>

namespace kumo {

class TraceLogger {
public:
    static void init(const std::string& path) {
        std::lock_guard<std::mutex> lock(mu_);
        if (enabled_) {
            return;
        }
        ofs_.open(path, std::ios::out | std::ios::trunc);
        if (!ofs_) {
            // If we can't open, just disable tracing
            enabled_ = false;
            return;
        }
        enabled_ = true;
    }

    static void shutdown() {
        std::lock_guard<std::mutex> lock(mu_);
        if (ofs_.is_open()) {
            ofs_.close();
        }
        enabled_ = false;
    }

    template <typename... Args>
    static void log(Args&&... args) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!enabled_ || !ofs_) return;
        (ofs_ << ... << args) << '\n';
    }

    static bool enabled() {
        std::lock_guard<std::mutex> lock(mu_);
        return enabled_;
    }

private:
    static inline std::ofstream ofs_;
    static inline bool enabled_ = false;
    static inline std::mutex mu_;
};

} // namespace kumo
