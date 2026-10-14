/*
 * SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "ucx_sgl.h"

#ifdef HAVE_UCX_SGL_API

#include "common/configuration.h"
#include "common/nixl_log.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <limits>
#include <mutex>
#include <sstream>
#include <type_traits>
#include <utility>
#include <vector>
#include <unistd.h>

namespace nixl::ucx {

namespace {
struct SglProfileState {
    std::atomic<bool> disabled{false};
    std::mutex mutex;
    std::array<uint64_t, 64> shapes{};
    size_t count = 0;
};

SglProfileState &
profileState() {
    static SglProfileState state;
    return state;
}

bool
profileEnabled() noexcept {
    static const bool enabled = []() noexcept {
        try {
            return nixl::config::getValueDefaulted("NIXL_UCX_SGL_PROFILE", false);
        }
        catch (...) {
            return false;
        }
    }();
    try {
        return enabled && !profileState().disabled.load(std::memory_order_relaxed);
    }
    catch (...) {
        return false;
    }
}

struct SglProfileGeometry {
    uint64_t hash = 14695981039346656037ULL;
    size_t bytes = 0;
    size_t lengthRuns = 0;
};

void
profileSgl(const size_t *lengths,
           size_t count,
           size_t worker_id,
           size_t posted_runs,
           bool split_requested,
           const char *decision) noexcept {
    SglProfileState *state_ptr = nullptr;
    try {
        auto &state = profileState();
        state_ptr = &state;
        if (state.disabled.load(std::memory_order_relaxed)) {
            return;
        }
        SglProfileGeometry geometry;
        for (size_t i = 0; i < count; ++i) {
            geometry.bytes += lengths[i];
            geometry.lengthRuns += i == 0 || lengths[i] != lengths[i - 1];
            geometry.hash = (geometry.hash ^ lengths[i]) * 1099511628211ULL;
        }
        std::lock_guard lock(state.mutex);
        if (state.disabled.load(std::memory_order_relaxed)) {
            return;
        }
        uint64_t shape_hash = geometry.hash;
        for (const char *p = decision; *p; ++p) {
            shape_hash = (shape_hash ^ static_cast<unsigned char>(*p)) * 1099511628211ULL;
        }
        shape_hash = (shape_hash ^ posted_runs) * 1099511628211ULL;
        if (std::find(state.shapes.begin(), state.shapes.begin() + state.count, shape_hash) !=
            state.shapes.begin() + state.count) {
            return;
        }
        std::array<std::pair<size_t, size_t>, 16> histogram{};
        size_t bins = 0;
        bool truncated = false;
        for (size_t i = 0; i < count; ++i) {
            size_t j = 0;
            while (j < bins && histogram[j].first != lengths[i]) {
                ++j;
            }
            if (j < bins) {
                ++histogram[j].second;
            } else if (bins < histogram.size()) {
                histogram[bins++] = {lengths[i], 1};
            } else {
                truncated = true;
            }
        }
        std::sort(histogram.begin(), histogram.begin() + bins);
        std::ostringstream line;
        line.exceptions(std::ios::badbit | std::ios::failbit);
        line << "NIXL_UCX_SGL_PROFILE {\"representative\":true,\"pid\":" << getpid()
             << ",\"worker_id\":" << worker_id << ",\"n_desc\":" << count
             << ",\"bytes\":" << geometry.bytes << ",\"adjacent_length_runs\":"
             << geometry.lengthRuns << ",\"posted_runs\":" << posted_runs
             << ",\"split_requested\":" << (split_requested ? "true" : "false")
             << ",\"decision\":\"" << decision << "\",\"length_sequence_hash\":\""
             << std::hex << geometry.hash << std::dec << "\",\"length_hist\":[";
        for (size_t i = 0; i < bins; ++i) {
            line << (i ? ",[" : "[") << histogram[i].first << ',' << histogram[i].second << ']';
        }
        line << "],\"length_hist_truncated\":" << (truncated ? "true" : "false") << '}';
        if (std::fprintf(stderr, "%s\n", line.str().c_str()) < 0) {
            state.disabled.store(true, std::memory_order_relaxed);
            return;
        }
        state.shapes[state.count++] = shape_hash;
        if (state.count == state.shapes.size()) {
            state.disabled.store(true, std::memory_order_relaxed);
            std::fprintf(stderr, "NIXL_UCX_SGL_PROFILE {\"saturated\":true,\"limit\":64}\n");
        }
    }
    catch (...) {
        if (state_ptr) {
            state_ptr->disabled.store(true, std::memory_order_relaxed);
        }
    }
}

template<typename Address>
bool
nonOverlapping(const Address *addresses, const size_t *lengths, size_t count) {
    std::vector<std::pair<uint64_t, uint64_t>> ranges;
    ranges.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const uint64_t start = [&] {
            if constexpr (std::is_pointer_v<Address>) {
                return reinterpret_cast<uint64_t>(addresses[i]);
            } else {
                return addresses[i];
            }
        }();
        if (lengths[i] > std::numeric_limits<uint64_t>::max() - start) {
            return false;
        }
        if (lengths[i] != 0) {
            ranges.emplace_back(start, start + lengths[i]);
        }
    }
    std::sort(ranges.begin(), ranges.end());
    for (size_t i = 1; i < ranges.size(); ++i) {
        if (ranges[i].first < ranges[i - 1].second) {
            return false;
        }
    }
    return true;
}
} // namespace

sglXfer::sglXfer(const nixl_meta_dlist_t &local,
                 const nixl_meta_dlist_t &remote,
                 size_t worker_id,
                 size_t start_idx,
                 size_t end_idx,
                 bool allow_split) {
    NIXL_ASSERT(end_idx > start_idx);
    size_ = end_idx - start_idx;
    conn_ = static_cast<nixlUcxPublicMetadata *>(remote[start_idx].metadataP)->conn;

    static_assert(sizeof(void *) == sizeof(uint64_t) && sizeof(size_t) == sizeof(uint64_t) &&
                      sizeof(ucp_mem_h) == sizeof(uint64_t) &&
                      sizeof(ucp_rkey_h) == sizeof(uint64_t),
                  "sglXfer assumes all field types are 8 bytes wide");
    constexpr size_t field_bytes = sizeof(uint64_t);
    const size_t array_bytes = size_ * field_bytes;

    storage_ = std::make_unique_for_overwrite<std::byte[]>(num_fields * array_bytes);
    std::byte *const base = storage_.get();
    localAddrs_ = reinterpret_cast<void **>(base + 0 * array_bytes);
    remoteAddrs_ = reinterpret_cast<uint64_t *>(base + 1 * array_bytes);
    lengths_ = reinterpret_cast<size_t *>(base + 2 * array_bytes);
    memhs_ = reinterpret_cast<ucp_mem_h *>(base + 3 * array_bytes);
    rkeys_ = reinterpret_cast<ucp_rkey_h *>(base + 4 * array_bytes);

    for (size_t i = start_idx; i < end_idx; ++i) {
        const size_t out = i - start_idx;
        const auto lmd = static_cast<nixlUcxPrivateMetadata *>(local[i].metadataP);
        const auto rmd = static_cast<nixlUcxPublicMetadata *>(remote[i].metadataP);
        NIXL_ASSERT(local[i].len == remote[i].len);
        NIXL_ASSERT(rmd->conn == conn_);

        localAddrs_[out] = reinterpret_cast<void *>(local[i].addr);
        remoteAddrs_[out] = static_cast<uint64_t>(remote[i].addr);
        lengths_[out] = local[i].len;
        memhs_[out] = lmd->getMem().getMemh();
        rkeys_[out] = rmd->getRkey(worker_id).get();
    }
    runOffsets_[1] = size_;
    const bool split_requested =
        allow_split && nixl::config::getValueDefaulted("NIXL_UCX_SGL_SPLIT_BY_SIZE", false);
    const char *decision = allow_split ? "disabled" : "local_transfer";
    if (split_requested) {
        decision = prepareRuns();
    }
    if (profileEnabled()) {
        profileSgl(lengths_, size_, worker_id, runCount_, split_requested, decision);
    }
}

const char *
sglXfer::prepareRuns() {
    std::array<size_t, max_runs + 1> offsets{};
    size_t runs = 1;
    for (size_t i = 1; i < size_; ++i) {
        if (lengths_[i] != lengths_[i - 1]) {
            if (runs == max_runs) {
                return "too_many_runs";
            }
            offsets[runs++] = i;
        }
    }
    if (runs == 1) {
        return "homogeneous";
    }
    if (!nonOverlapping(localAddrs_, lengths_, size_)) {
        return "local_overlap_or_overflow";
    }
    if (!nonOverlapping(remoteAddrs_, lengths_, size_)) {
        return "remote_overlap_or_overflow";
    }
    offsets[runs] = size_;
    runOffsets_ = offsets;
    runCount_ = runs;
    NIXL_DEBUG << "UCX SGL split " << size_ << " descriptors into " << runs
               << " equal-length runs";
    return "split";
}

nixl_status_t
sglXfer::post(nixlUcxEp &ep, size_t run, nixlUcxReq &req) const {
    NIXL_ASSERT(run < runCount_);
    const size_t offset = runOffsets_[run];
    return ep.postSgl(localView(offset), remoteView(offset),
                       runOffsets_[run + 1] - offset, req);
}

} // namespace nixl::ucx

#endif // HAVE_UCX_SGL_API
