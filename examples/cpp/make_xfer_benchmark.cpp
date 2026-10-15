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

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <random>
#include <string_view>
#include <utility>
#include <vector>

#include "make_xfer_benchmark_backend.h"
#include "nixl.h"
#include "plugin_manager.h"
#include "transfer_request.h"

namespace {

void
require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void
check(nixl_status_t status, const char *operation) {
    if (status != NIXL_SUCCESS) {
        std::cerr << operation << " failed with status " << status << '\n';
        std::exit(EXIT_FAILURE);
    }
}

struct referenceDesc {
    nixlMetaDesc desc;
    size_t run;
    bool dense;
};

std::vector<referenceDesc>
expand(const nixl_meta_stride_dlist_t &descriptors) {
    std::vector<referenceDesc> result;
    for (int run = 0; run < descriptors.descCount(); ++run) {
        const auto &stride = descriptors[run];
        for (size_t i = 0; i < stride.count; ++i) {
            result.push_back(
                {nixlMetaDesc(
                     stride.addr + i * stride.stride, stride.len, stride.devId, stride.metadataP),
                 static_cast<size_t>(run),
                 stride.stride == stride.len});
        }
    }
    return result;
}

size_t
validate(nixlAgent &agent,
         const std::string &agent_name,
         uintptr_t base_addr,
         const nixl_opt_args_t &options) {
    size_t cases = 0;
    std::mt19937 random(20260327);
    const auto release_dlist = [&agent](nixlDlistH *handle) {
        if (handle) {
            check(agent.releasedDlistH(handle), "releasedDlistH");
        }
    };

    for (int layout = 0; layout < 4; ++layout) {
        nixl_stride_dlist_t local_input(DRAM_SEG), remote_input(DRAM_SEG);
        if (layout == 1 || layout == 3) {
            local_input.emplace(base_addr, 64, 0, 64, 127);
            local_input.emplace(
                base_addr + 65536, layout == 3 ? 128 : 64, 0, layout == 3 ? 128 : 64, 130);
            remote_input.emplace(base_addr + 131072, 64, 0, 64, 37);
            remote_input.emplace(base_addr + 196608, 64, 0, 64, 220);
        } else {
            local_input.emplace(base_addr, 64, 0, layout == 2 ? 128 : 64, 257);
            remote_input.emplace(base_addr + 131072, 64, 0, 64, 257);
        }

        nixlDlistH *raw_local = nullptr, *raw_remote = nullptr;
        check(agent.prepXferDlist(local_input, raw_local, &options), "prepXferDlist(local)");
        std::unique_ptr<nixlDlistH, decltype(release_dlist)> local(raw_local, release_dlist);
        check(agent.prepXferDlist(agent_name, remote_input, raw_remote, &options),
              "prepXferDlist(remote)");
        std::unique_ptr<nixlDlistH, decltype(release_dlist)> remote(raw_remote, release_dlist);
        const auto &backend = *static_cast<makeXferBenchmarkBackend *>(local->descs.begin()->first);
        const auto local_flat = expand(*local->descs.begin()->second);
        const auto remote_flat = expand(*remote->descs.begin()->second);

        const auto compare = [&](const std::vector<int> &local_indices,
                                 const std::vector<int> &remote_indices,
                                 bool skip_merge = false) {
            ++cases;
            nixl_status_t expected_status = NIXL_SUCCESS;
            std::vector<nixlMetaDesc> expected_local, expected_remote;
            if (local_indices.empty() || local_indices.size() != remote_indices.size()) {
                expected_status = NIXL_ERR_INVALID_PARAM;
            } else {
                for (size_t i = 0; i < local_indices.size(); ++i) {
                    const size_t local_idx = static_cast<size_t>(local_indices[i]);
                    const size_t remote_idx = static_cast<size_t>(remote_indices[i]);
                    if (local_idx >= local_flat.size() || remote_idx >= remote_flat.size() ||
                        local_flat[local_idx].desc.len != remote_flat[remote_idx].desc.len) {
                        expected_status = NIXL_ERR_INVALID_PARAM;
                        break;
                    }
                    const auto &loc = local_flat[local_idx];
                    const auto &rem = remote_flat[remote_idx];
                    const bool extend = !skip_merge && i > 0 && loc.dense && rem.dense &&
                        local_indices[i] == local_indices[i - 1] + 1 &&
                        remote_indices[i] == remote_indices[i - 1] + 1 &&
                        loc.run == local_flat[local_indices[i - 1]].run &&
                        rem.run == remote_flat[remote_indices[i - 1]].run;
                    if (extend) {
                        expected_local.back().len += loc.desc.len;
                        expected_remote.back().len += rem.desc.len;
                    } else {
                        expected_local.push_back(loc.desc);
                        expected_remote.push_back(rem.desc);
                    }
                }
            }

            nixl_opt_args_t case_options = options;
            case_options.skipDescMerge = skip_merge;
            backend.localDescs = backend.remoteDescs = nullptr;
            nixlXferReqH *request = nullptr;
            const auto status = agent.makeXferReq(
                NIXL_WRITE, *local, local_indices, *remote, remote_indices, request, &case_options);
            if (status != expected_status) {
                std::cerr << "Validation case " << cases << ": expected status " << expected_status
                          << ", got " << status << '\n';
                std::exit(EXIT_FAILURE);
            }
            if (status != NIXL_SUCCESS) {
                require(request == nullptr, "Failed request returned a handle");
                require(backend.localDescs == nullptr, "Invalid indices reached the backend");
                return;
            }
            require(request != nullptr, "Expected a request handle");
            const auto compare_descriptors = [&](const nixl_meta_dlist_t *actual,
                                                 const std::vector<nixlMetaDesc> &expected) {
                require(actual != nullptr, "Backend did not receive descriptors");
                require(static_cast<size_t>(actual->descCount()) == expected.size(),
                        "Merged descriptor count differs from scalar reference");
                for (size_t i = 0; i < expected.size(); ++i) {
                    require((*actual)[i] == expected[i],
                            "Merged descriptor differs from scalar reference");
                }
            };
            compare_descriptors(backend.localDescs, expected_local);
            compare_descriptors(backend.remoteDescs, expected_remote);
            check(agent.releaseXferReq(request), "releaseXferReq");
        };

        if (layout == 3) {
            compare({125, 126, 127}, {0, 1, 2});
            std::vector<int> indices(128);
            std::iota(indices.begin(), indices.end(), 0);
            compare(indices, indices);
            continue;
        }
        for (int count = 1; count <= 257; ++count) {
            std::vector<int> indices(count);
            std::iota(indices.begin(), indices.end(), 0);
            compare(indices, indices);
            compare(indices, indices, true);
        }
        for (size_t sample = 0; sample < 400; ++sample) {
            const size_t count = 1 + random() % 1024;
            std::vector<int> local_indices(count), remote_indices(count);
            int local_idx = random() % 257, remote_idx = random() % 257;
            for (size_t i = 0; i < count; ++i) {
                local_indices[i] = local_idx;
                remote_indices[i] = remote_idx;
                switch (sample % 4) {
                case 0:
                    local_idx = (local_idx + 1 + (random() % 19 == 0)) % 257;
                    remote_idx = (remote_idx + 1 + (random() % 23 == 0)) % 257;
                    break;
                case 1:
                    local_idx = random() % 257;
                    remote_idx = random() % 257;
                    break;
                case 2:
                    local_idx = (local_idx + 256) % 257;
                    remote_idx = (remote_idx + 256) % 257;
                    break;
                default:
                    local_idx = (local_idx + random() % 2) % 257;
                    remote_idx = (remote_idx + random() % 2) % 257;
                }
            }
            compare(local_indices, remote_indices);
            compare(local_indices, remote_indices, true);
        }
        compare({}, {});
        compare({0, 1}, {0});
        compare({-1}, {0});
        compare({0}, {-1});
        compare({0, 1, 257}, {0, 1, 2});
        compare({0, 1, 2}, {0, 1, 257});
        if (layout == 0) {
            for (int prefix : {16, 128}) {
                std::vector<int> valid(prefix + 1);
                std::iota(valid.begin(), valid.end(), 0);
                auto invalid = valid;
                for (int index : {-1, 257}) {
                    invalid.back() = index;
                    compare(invalid, valid);
                    compare(valid, invalid);
                }
            }
        }
    }
    return cases;
}

} // namespace

// Measures makeXferReq with no-op backend preparation; release is outside the timer.
int
main(int argc, char **argv) {
    const bool validate_only = argc == 2 && std::string_view(argv[1]) == "--validate";
    size_t index_count = 20000;
    size_t block_count = 128;
    size_t iterations = 10000;
    if (!validate_only) {
        for (int i = 1; i < argc; i += 2) {
            const std::string_view option(argv[i]);
            require(i + 1 < argc &&
                        (option == "--indices" || option == "--blocks" || option == "--iterations"),
                    "Usage: make_xfer_benchmark [--indices N] [--blocks N] [--iterations N] | "
                    "--validate");
            const std::string_view argument(argv[i + 1]);
            int value = 0;
            const auto [end, error] =
                std::from_chars(argument.data(), argument.data() + argument.size(), value);
            require(error == std::errc{} && end == argument.data() + argument.size() && value > 0,
                    "Counts must be positive integers no greater than INT_MAX");
            if (option == "--indices") {
                index_count = value;
            } else if (option == "--blocks") {
                block_count = value;
            } else {
                iterations = value;
            }
        }
    }
    require(block_count <= index_count, "Block count must not exceed index count");
    constexpr size_t block_bytes = 4096;
    const size_t prepared_count = index_count + block_count - 1;
    require(prepared_count - 1 <= static_cast<size_t>(std::numeric_limits<int>::max()),
            "Generated indices exceed INT_MAX");
    require(prepared_count <= std::numeric_limits<size_t>::max() / block_bytes,
            "Prepared allocation size overflows size_t");
    constexpr size_t warmup_count = 100;
    constexpr size_t repeats = 20;

    require(setenv("NIXL_TELEMETRY_ENABLE", "0", 1) == 0, "Cannot disable telemetry");
    require(setenv("NIXL_TRACE_BACKENDS", "", 1) == 0, "Cannot disable tracing");

    auto memory = std::make_unique_for_overwrite<std::byte[]>(prepared_count * block_bytes);
    const auto base_addr = reinterpret_cast<uintptr_t>(memory.get());
    const std::string agent_name = "make_xfer_benchmark";
    nixlAgent agent(agent_name, nixlAgentConfig{});

    nixlPluginManager::getInstance().addPluginDirectory(BENCHMARK_PLUGIN_DIR);
    nixlBackendH *backend_handle = nullptr;
    check(agent.createBackend("MAKE_XFER_BENCHMARK", {}, backend_handle), "createBackend");
    nixl_opt_args_t options;
    options.backends.push_back(backend_handle);

    nixl_reg_dlist_t registration(DRAM_SEG);
    registration.emplace(base_addr, prepared_count * block_bytes, 0);
    check(agent.registerMem(registration, &options), "registerMem");

    if (validate_only) {
        const auto cases = validate(agent, agent_name, base_addr, options);
        std::cout << "[ make-xfer-bench ] validation passed: " << cases << " cases\n";
        check(agent.deregisterMem(registration, &options), "deregisterMem");
        return EXIT_SUCCESS;
    }

    nixl_stride_dlist_t descriptors(DRAM_SEG);
    descriptors.emplace(base_addr, block_bytes, 0, block_bytes, prepared_count);

    const auto release_dlist = [&agent](nixlDlistH *handle) {
        if (handle) {
            check(agent.releasedDlistH(handle), "releasedDlistH");
        }
    };
    nixlDlistH *raw_local = nullptr;
    const auto local_status = agent.prepXferDlist(descriptors, raw_local, &options);
    std::unique_ptr<nixlDlistH, decltype(release_dlist)> local(raw_local, release_dlist);
    check(local_status, "prepXferDlist(local)");

    nixlDlistH *raw_remote = nullptr;
    const auto remote_status = agent.prepXferDlist(agent_name, descriptors, raw_remote, &options);
    std::unique_ptr<nixlDlistH, decltype(release_dlist)> remote(raw_remote, release_dlist);
    check(remote_status, "prepXferDlist(remote)");
    require(local->descs.begin()->second->descCount() == 1, "Expected one local prepared stride");
    require(remote->descs.begin()->second->descCount() == 1, "Expected one remote prepared stride");
    const auto &backend = *static_cast<makeXferBenchmarkBackend *>(local->descs.begin()->first);

    std::vector<int> local_indices;
    local_indices.reserve(index_count);
    std::vector<nixlBasicDesc> expected;
    expected.reserve(block_count);
    size_t start = 0;
    for (size_t block = 0; block < block_count; ++block) {
        const size_t count = index_count / block_count + (block < index_count % block_count);
        expected.emplace_back(base_addr + start * block_bytes, count * block_bytes, 0);
        for (size_t i = 0; i < count; ++i) {
            local_indices.push_back(static_cast<int>(start + i));
        }
        // A gap ends each merged block inside the single prepared stride.
        start += count + 1;
    }
    const auto remote_indices = local_indices;
    require(local_indices.size() == index_count, "Unexpected number of input indices");

    const auto release_request = [&agent](nixlXferReqH *handle) {
        if (handle) {
            check(agent.releaseXferReq(handle), "releaseXferReq");
        }
    };
    std::unique_ptr<nixlXferReqH, decltype(release_request)> request(nullptr, release_request);
    const auto make_request = [&] {
        nixlXferReqH *raw_request = nullptr;
        const auto begin = std::chrono::steady_clock::now();
        const auto status = agent.makeXferReq(
            NIXL_WRITE, *local, local_indices, *remote, remote_indices, raw_request, &options);
        const auto end = std::chrono::steady_clock::now();
        request.reset(raw_request);
        return std::pair{status, end - begin};
    };

    check(make_request().first, "makeXferReq");
    require(request != nullptr, "Expected a request handle");
    require(backend.localDescs != nullptr && backend.remoteDescs != nullptr,
            "Backend did not receive descriptors");
    require(static_cast<size_t>(backend.localDescs->descCount()) == block_count,
            "Unexpected number of local blocks");
    require(static_cast<size_t>(backend.remoteDescs->descCount()) == block_count,
            "Unexpected number of remote blocks");
    for (size_t i = 0; i < block_count; ++i) {
        for (const auto *actual : {backend.localDescs, backend.remoteDescs}) {
            require((*actual)[i].addr == expected[i].addr && (*actual)[i].len == expected[i].len &&
                        (*actual)[i].devId == expected[i].devId,
                    "Unexpected merged descriptor");
        }
    }
    request.reset();

    for (size_t i = 0; i < warmup_count; ++i) {
        check(make_request().first, "makeXferReq");
        request.reset();
    }

    std::array<double, repeats> samples;
    for (size_t repeat = 0; repeat < repeats; ++repeat) {
        std::chrono::steady_clock::duration elapsed{};
        for (size_t i = 0; i < iterations; ++i) {
            const auto [status, duration] = make_request();
            check(status, "makeXferReq");
            elapsed += duration;
            request.reset();
        }
        samples[repeat] = std::chrono::duration<double, std::micro>(elapsed).count() / iterations;
    }
    std::ranges::sort(samples);
    std::cout << "[ make-xfer-bench ] indices=" << index_count << " merged=" << block_count
              << " iterations=" << iterations << " repeats=" << repeats
              << " makeXferReq us/call: min=" << samples.front()
              << " median=" << samples[repeats / 2] << " max=" << samples.back() << '\n';

    remote.reset();
    local.reset();
    check(agent.deregisterMem(registration, &options), "deregisterMem");
    return EXIT_SUCCESS;
}
