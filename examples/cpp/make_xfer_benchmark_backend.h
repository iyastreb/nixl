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

#ifndef NIXL_EXAMPLES_CPP_MAKE_XFER_BENCHMARK_BACKEND_H
#define NIXL_EXAMPLES_CPP_MAKE_XFER_BENCHMARK_BACKEND_H

#include "backend/backend_engine.h"

class makeXferBenchmarkBackend final : public nixlBackendEngine {
public:
    using nixlBackendEngine::nixlBackendEngine;

    bool
    supportsRemote() const override {
        return false;
    }

    bool
    supportsLocal() const override {
        return true;
    }

    bool
    supportsNotif() const override {
        return false;
    }

    nixl_mem_list_t
    getSupportedMems() const override {
        return {DRAM_SEG};
    }

    nixl_status_t
    registerMem(const nixlBlobDesc &, const nixl_mem_t &, nixlBackendMD *&out) override {
        out = nullptr;
        return NIXL_SUCCESS;
    }

    nixl_status_t
    deregisterMem(nixlBackendMD *) override {
        return NIXL_SUCCESS;
    }

    nixl_status_t
    connect(const std::string &) override {
        return NIXL_SUCCESS;
    }

    nixl_status_t
    disconnect(const std::string &) override {
        return NIXL_SUCCESS;
    }

    nixl_status_t
    loadLocalMD(nixlBackendMD *, nixlBackendMD *&out) override {
        out = nullptr;
        return NIXL_SUCCESS;
    }

    nixl_status_t
    unloadMD(nixlBackendMD *) override {
        return NIXL_SUCCESS;
    }

    nixl_status_t
    prepXfer(const nixl_xfer_op_t &,
             const nixl_meta_dlist_t &local,
             const nixl_meta_dlist_t &remote,
             const std::string &,
             nixlBackendReqH *&handle,
             const nixl_opt_b_args_t *) const override {
        localDescs = &local;
        remoteDescs = &remote;
        handle = nullptr;
        return NIXL_SUCCESS;
    }

    nixl_status_t
    postXfer(const nixl_xfer_op_t &,
             const nixl_meta_dlist_t &,
             const nixl_meta_dlist_t &,
             const std::string &,
             nixlBackendReqH *&,
             const nixl_opt_b_args_t *) const override {
        return NIXL_ERR_NOT_SUPPORTED;
    }

    nixl_status_t
    checkXfer(nixlBackendReqH *) const override {
        return NIXL_ERR_NOT_SUPPORTED;
    }

    nixl_status_t
    releaseReqH(nixlBackendReqH *) const override {
        return NIXL_ERR_NOT_SUPPORTED;
    }

    mutable const nixl_meta_dlist_t *localDescs = nullptr;
    mutable const nixl_meta_dlist_t *remoteDescs = nullptr;
};

#endif
