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
#ifndef NIXL_SRC_UTILS_COMMON_NIXL_MEMORY_POOL_H
#define NIXL_SRC_UTILS_COMMON_NIXL_MEMORY_POOL_H

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

template <class T>
class nixlMemoryPool {
public:
    static constexpr size_t default_max_size = 64;

    explicit nixlMemoryPool(size_t max_size = default_max_size) : max_size_(max_size) {}

    nixlMemoryPool(const nixlMemoryPool &) = delete;
    nixlMemoryPool &
    operator=(const nixlMemoryPool &) = delete;

    template <class... Args>
    std::unique_ptr<T>
    get(Args &&...args) {
        std::vector<std::unique_ptr<T>> &fl = freeList();
        if (!fl.empty()) {
            std::unique_ptr<T> obj = std::move(fl.back());
            fl.pop_back();
            obj->reset(std::forward<Args>(args)...);
            return obj;
        }
        return std::make_unique<T>(std::forward<Args>(args)...);
    }

    void
    put(std::unique_ptr<T> obj) {
        if (!obj) return;
        std::vector<std::unique_ptr<T>> &fl = freeList();
        if ((max_size_ != 0) && (fl.size() >= max_size_)) {
            return;
        }
        fl.push_back(std::move(obj));
    }

private:
    static std::vector<std::unique_ptr<T>> &
    freeList() {
        static thread_local std::vector<std::unique_ptr<T>> fl;
        return fl;
    }

    size_t max_size_;
};

#endif // NIXL_SRC_UTILS_COMMON_NIXL_MEMORY_POOL_H
