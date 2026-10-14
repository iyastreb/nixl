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
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace nixl::ucx {

namespace {
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
    if (allow_split && nixl::config::getValueDefaulted("NIXL_UCX_SGL_SPLIT_BY_SIZE", false)) {
        prepareRuns();
    }
}

void
sglXfer::prepareRuns() {
    std::array<size_t, max_runs + 1> offsets{};
    size_t runs = 1;
    for (size_t i = 1; i < size_; ++i) {
        if (lengths_[i] != lengths_[i - 1]) {
            if (runs == max_runs) {
                return;
            }
            offsets[runs++] = i;
        }
    }
    if (runs == 1 || !nonOverlapping(localAddrs_, lengths_, size_) ||
        !nonOverlapping(remoteAddrs_, lengths_, size_)) {
        return;
    }
    offsets[runs] = size_;
    runOffsets_ = offsets;
    runCount_ = runs;
    NIXL_DEBUG << "UCX SGL split " << size_ << " descriptors into " << runs
               << " equal-length runs";
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
