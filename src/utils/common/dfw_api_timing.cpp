// SPDX-License-Identifier: Apache-2.0
#include "dfw_api_timing.h"

#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <limits.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace nixl::dfw_api {

struct Writer {
    int fd = -1;
    long pid = getpid(), tid = syscall(SYS_gettid);
    uint64_t sequence = 0;
    char host[256]{};

    explicit Writer(const char *dir) noexcept {
        gethostname(host, sizeof(host) - 1);
        for (char *p = host; *p; ++p) {
            if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                  (*p >= '0' && *p <= '9') || *p == '.' || *p == '-' || *p == '_'))
                *p = '_';
        }
        char path[PATH_MAX];
        const int size = snprintf(path, sizeof(path), "%s/nixl-api-%s-%ld-%ld.jsonl",
                                  dir, host, pid, tid);
        if (size > 0 && static_cast<size_t>(size) < sizeof(path))
            fd = open(path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0600);
        if (fd < 0) fprintf(stderr, "NIXL API timing: cannot open output in %s\n", dir);
    }
    ~Writer() {
        if (fd >= 0) close(fd);
    }
};

Writer *writer() noexcept {
    static const char *dir = getenv("NIXL_DFW_API_TIMING_DIR");
    if (!dir || !*dir) return nullptr;
    thread_local Writer output(dir);
    return output.fd >= 0 ? &output : nullptr;
}

void emit(Writer *output, const Record &r) noexcept {
    char line[2048];
    const int size = snprintf(
        line, sizeof(line),
        "{\"event\":\"%s\",\"host\":\"%s\",\"pid\":%ld,\"tid\":%ld,"
        "\"sequence\":%" PRIu64 ",\"start_ns\":%" PRIu64 ",\"end_ns\":%" PRIu64 ","
        "\"handle\":%" PRIu64 ",\"input_descs\":%" PRIu64 ",\"output_descs\":%" PRIu64 ","
        "\"bytes\":%" PRIu64 ",\"status\":%d,\"returned\":%s,"
        "\"alloc_start_ns\":%" PRIu64 ",\"alloc_end_ns\":%" PRIu64 ","
        "\"descs_start_ns\":%" PRIu64 ",\"descs_end_ns\":%" PRIu64 ","
        "\"backend_start_ns\":%" PRIu64 ",\"backend_end_ns\":%" PRIu64 "}\n",
        r.event, output->host, output->pid, output->tid, ++output->sequence,
        r.start_ns, r.end_ns, r.handle, r.input_descs, r.output_descs, r.bytes,
        r.status, r.returned ? "true" : "false", r.alloc_start_ns, r.alloc_end_ns,
        r.descs_start_ns, r.descs_end_ns, r.backend_start_ns, r.backend_end_ns);
    if (size <= 0 || static_cast<size_t>(size) >= sizeof(line)) return;
    size_t written = 0;
    while (written < static_cast<size_t>(size)) {
        const ssize_t n = write(output->fd, line + written, size - written);
        if (n > 0) {
            written += static_cast<size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            fprintf(stderr, "NIXL API timing: output write failed\n");
            close(output->fd);
            output->fd = -1;
            return;
        }
    }
}

} // namespace nixl::dfw_api
