// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <ctime>

namespace nixl::dfw_api {

struct Record {
    const char *event;
    uint64_t start_ns = 0, end_ns = 0;
    uint64_t alloc_start_ns = 0, alloc_end_ns = 0;
    uint64_t descs_start_ns = 0, descs_end_ns = 0;
    uint64_t backend_start_ns = 0, backend_end_ns = 0;
    uint64_t handle = 0, input_descs = 0, output_descs = 0, bytes = 0;
    int status = 0;
    bool returned = false;
};

struct Writer;
Writer *writer() noexcept;
void emit(Writer *, const Record &) noexcept;

inline uint64_t now() noexcept {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ULL + ts.tv_nsec;
}

class Scope : public Record {
    Writer *writer_;

  public:
    explicit Scope(const char *event, uint64_t input_descs = 0) noexcept
        : Record{event}, writer_(writer()) {
        this->input_descs = input_descs;
        mark(start_ns);
    }
    ~Scope() {
        if (writer_) {
            end_ns = now();
            emit(writer_, *this);
        }
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

    void mark(uint64_t &stamp) const noexcept {
        if (writer_) stamp = now();
    }
    template <typename Status> Status result(Status value) noexcept {
        status = static_cast<int>(value);
        returned = true;
        return value;
    }
};

} // namespace nixl::dfw_api
