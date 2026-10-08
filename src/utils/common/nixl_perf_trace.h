/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef NIXL_SRC_UTILS_COMMON_NIXL_PERF_TRACE_H
#define NIXL_SRC_UTILS_COMMON_NIXL_PERF_TRACE_H

// Per-event JSONL timeline probes for transfer performance analysis.
// Enabled when NIXL_PERF_TRACE_DIR names a directory; one file per process.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <type_traits>

#include <sys/syscall.h>
#include <unistd.h>

namespace nixl::perf {

inline const char *
traceDir() noexcept {
    static const char *const dir = std::getenv("NIXL_PERF_TRACE_DIR");
    return dir;
}

inline bool
enabled() noexcept {
    const char *dir = traceDir();
    return dir != nullptr && dir[0] != '\0';
}

inline uint64_t
wallNs() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

inline uint64_t
monoNs() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

namespace detail {
inline std::mutex &
fileMutex() {
    static std::mutex mutex;
    return mutex;
}

inline FILE *
file() {
    static FILE *const fp = [] {
        char host[256] = "unknown";
        gethostname(host, sizeof(host) - 1);
        const std::string path = std::string(traceDir()) + "/nixl-" + host + "-" +
            std::to_string(getpid()) + ".jsonl";
        return std::fopen(path.c_str(), "a");
    }();
    return fp;
}

inline void
appendQuoted(std::string &out, const std::string &value) {
    out += '"';
    for (const char c : value) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += (static_cast<unsigned char>(c) < 0x20) ? ' ' : c;
    }
    out += '"';
}
} // namespace detail

class Event {
public:
    explicit Event(const char *name) : active_(enabled()) {
        if (!active_) {
            return;
        }
        line_ = "{\"ev\":\"";
        line_ += name;
        line_ += "\",\"t_ns\":";
        line_ += std::to_string(wallNs());
        line_ += ",\"tid\":";
        line_ += std::to_string(static_cast<long>(syscall(SYS_gettid)));
    }

    Event(const Event &) = delete;
    Event &
    operator=(const Event &) = delete;

    ~Event() {
        if (!active_) {
            return;
        }
        line_ += "}\n";
        std::lock_guard<std::mutex> lock(detail::fileMutex());
        FILE *fp = detail::file();
        if (fp != nullptr) {
            std::fputs(line_.c_str(), fp);
            std::fflush(fp);
        }
    }

    template<typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
    Event &
    add(const char *key, T value) {
        if (active_) {
            addKey(key);
            line_ += std::to_string(static_cast<long long>(value));
        }
        return *this;
    }

    Event &
    add(const char *key, double value) {
        if (active_) {
            addKey(key);
            line_ += std::to_string(value);
        }
        return *this;
    }

    Event &
    add(const char *key, const std::string &value) {
        if (active_) {
            addKey(key);
            detail::appendQuoted(line_, value);
        }
        return *this;
    }

    Event &
    add(const char *key, const char *value) {
        return add(key, std::string(value));
    }

    template<typename T>
    Event &
    add(const char *key, const T *ptr) {
        if (active_) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "\"%p\"", static_cast<const void *>(ptr));
            addKey(key);
            line_ += buf;
        }
        return *this;
    }

private:
    void
    addKey(const char *key) {
        line_ += ",\"";
        line_ += key;
        line_ += "\":";
    }

    bool active_;
    std::string line_;
};

} // namespace nixl::perf

#endif // NIXL_SRC_UTILS_COMMON_NIXL_PERF_TRACE_H
