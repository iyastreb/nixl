// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <ctime>

namespace nixl::dfw {

bool enabled() noexcept;
void flush() noexcept;

inline uint64_t now() noexcept {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ULL + ts.tv_nsec;
}

inline uint64_t cpuNow() noexcept {
    timespec ts{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ULL + ts.tv_nsec;
}

struct Event {
    const char *name;
    uint64_t start = 0, end = 0;
    uint64_t handle = 0, peer = 0, descs = 0, bytes = 0;
    uint64_t count = 0, value = 0, lock_ns = 0;
    uint64_t cpu_ns = 0;
    int status = 0;
};

void emit(const Event &event) noexcept;

struct Scope : Event {
    bool flush_after = false;
    uint64_t cpu_start = 0;
    explicit Scope(const char *name, const void *handle = nullptr, bool cpu = false) noexcept
        : Event{name} {
        if (enabled()) {
            if (cpu) cpu_start = cpuNow();
            start = now();
            this->handle = reinterpret_cast<uint64_t>(handle);
        }
    }
    ~Scope() { finish(); }
    void finish() noexcept {
        if (start) {
            end = now();
            if (cpu_start) cpu_ns = cpuNow() - cpu_start;
            emit(*this);
            start = 0;
            if (flush_after) flush();
        }
    }
};

struct PollStats {
    uint64_t count = 0, wall_ns = 0, lock_ns = 0, progress_ns = 0;
};

} // namespace nixl::dfw
