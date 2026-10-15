# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

nixl_example = executable('nixl_example',
        'nixl_example.cpp',
        dependencies: [nixl_dep, nixl_infra, nixl_common_deps, nixl_test_utils_dep],
        include_directories: [nixl_inc_dirs, utils_inc_dirs],
        link_with: [serdes_lib],
        install: true)

if etcd_dep.found()
    etcd_example = executable('nixl_etcd_example',
               'nixl_etcd_example.cpp',
               dependencies: [nixl_dep, nixl_infra, nixl_common_deps, nixl_test_utils_dep],
               include_directories: [nixl_inc_dirs, utils_inc_dirs],
               link_with: [serdes_lib],
               install: true)
endif

telemetry_reader = executable('telemetry_reader',
           'telemetry_reader.cpp',
           dependencies: [nixl_dep, nixl_common_deps],
           include_directories: [nixl_inc_dirs, utils_inc_dirs],
           install: true)

make_xfer_benchmark_plugin = shared_module('MAKE_XFER_BENCHMARK',
           'make_xfer_benchmark_plugin.cpp',
           dependencies: [nixl_infra, nixl_common_dep],
           include_directories: [nixl_inc_dirs, utils_inc_dirs],
           name_prefix: 'libplugin_',
           install: false)

make_xfer_benchmark = executable('make_xfer_benchmark',
           'make_xfer_benchmark.cpp',
           dependencies: [nixl_dep, nixl_infra, nixl_common_dep],
           include_directories: [nixl_inc_dirs, utils_inc_dirs],
           cpp_args: ['-DBENCHMARK_PLUGIN_DIR="' + meson.current_build_dir() + '"'],
           install: false)
