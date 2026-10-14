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

#include <array>
#include <cstddef>
#include <memory>
#include <utility>

/**
 * @brief Bounded per-thread object cache shared by pool instances.
 *
 * Objects must release external resources before put(); cached storage lives until thread exit.
 */
template<class T, size_t max_size = 64> class nixlMemoryPool {
public:
    nixlMemoryPool() = default;

    nixlMemoryPool(const nixlMemoryPool &) = delete;
    nixlMemoryPool &
    operator=(const nixlMemoryPool &) = delete;

    template<class... Args>
    std::unique_ptr<T>
    get(Args &&...args) {
        auto &fl = freeList();
        if (fl.size != 0) {
            auto obj = std::move(fl.objects[--fl.size]);
            obj->reset(std::forward<Args>(args)...);
            return obj;
        }
        return std::make_unique<T>(std::forward<Args>(args)...);
    }

    void
    put(std::unique_ptr<T> obj) noexcept {
        if (!obj) {
            return;
        }
        auto &fl = freeList();
        if (fl.size == max_size) {
            return;
        }
        fl.objects[fl.size++] = std::move(obj);
    }

private:
    struct cache {
        size_t size = 0;
        std::array<std::unique_ptr<T>, max_size> objects;
    };

    static cache &
    freeList() {
        static thread_local cache fl;
        return fl;
    }
};

#endif // NIXL_SRC_UTILS_COMMON_NIXL_MEMORY_POOL_H
