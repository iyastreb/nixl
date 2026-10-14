// SPDX-License-Identifier: Apache-2.0
#include "dfw_trace.h"

#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace nixl::dfw {
namespace {
const char *directory() noexcept {
    static const char *path = std::getenv("NIXL_DFW_TRACE_DIR");
    return path;
}

struct Writer {
    FILE *file = nullptr;
    char buffer[65536];
    long pid = getpid(), tid = syscall(SYS_gettid);
    uint64_t count = 0, last_flush = 0;
    Writer() noexcept {
        const char *path = directory();
        if (!path || !*path) return;
        mkdir(path, 0755);
        char filename[4096];
        std::snprintf(filename, sizeof(filename), "%s/nixl-%ld-%ld.jsonl", path, pid, tid);
        file = std::fopen(filename, "a");
        if (!file) {
            std::fprintf(stderr, "NIXL_DFW_TRACE cannot open %s\n", filename);
            return;
        }
        std::setvbuf(file, buffer, _IOFBF, sizeof(buffer));
        last_flush = now();
    }
    ~Writer() { if (file) std::fclose(file); }
};

Writer &writer() noexcept {
    thread_local Writer state;
    return state;
}
} // namespace

bool enabled() noexcept {
    const char *path = directory();
    return path && *path;
}

void flush() noexcept {
    if (!enabled()) return;
    auto &out = writer();
    if (out.file) {
        const auto start = now();
        std::fflush(out.file);
        out.last_flush = now();
        std::fprintf(out.file,
            "{\"event\":\"trace.flush\",\"pid\":%ld,\"tid\":%ld,"
            "\"start_ns\":%llu,\"end_ns\":%llu,\"handle\":0,\"peer\":0,"
            "\"descs\":0,\"bytes\":0,\"count\":0,\"value\":0,"
            "\"lock_ns\":0,\"cpu_ns\":0,\"completed_ns\":0,\"status\":0}\n",
            out.pid, out.tid, (unsigned long long)start,
            (unsigned long long)out.last_flush);
    }
}

void emit(const Event &e) noexcept {
    auto &out = writer();
    if (!out.file) return;
    timespec real{};
    clock_gettime(CLOCK_REALTIME, &real);
    const uint64_t real_ns = uint64_t(real.tv_sec) * 1000000000ULL + real.tv_nsec;
    std::fprintf(out.file,
        "{\"event\":\"%s\",\"pid\":%ld,\"tid\":%ld,\"start_ns\":%llu,"
        "\"end_ns\":%llu,\"real_ns\":%llu,\"handle\":%llu,\"peer\":%llu,"
        "\"descs\":%llu,\"bytes\":%llu,\"count\":%llu,\"value\":%llu,"
        "\"lock_ns\":%llu,\"cpu_ns\":%llu,\"completed_ns\":%llu,\"status\":%d}\n",
        e.name, out.pid, out.tid, (unsigned long long)e.start, (unsigned long long)e.end,
        (unsigned long long)real_ns, (unsigned long long)e.handle, (unsigned long long)e.peer,
        (unsigned long long)e.descs, (unsigned long long)e.bytes, (unsigned long long)e.count,
        (unsigned long long)e.value, (unsigned long long)e.lock_ns,
        (unsigned long long)e.cpu_ns, (unsigned long long)e.completed_ns, e.status);
    if ((++out.count % 32 == 0) ||
        (e.end >= out.last_flush && e.end - out.last_flush >= 100000000ULL)) {
        flush();
    }
}
} // namespace nixl::dfw
