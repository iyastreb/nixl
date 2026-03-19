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

#include <gtest/gtest.h>

#include "common.h"
#include "nixl.h"
#include "nixl_soa.h"


namespace nixlTag {
    struct dummyScalar {
        using valueType = int;
        static constexpr const char* name = "dummyScalar";
        static constexpr bool required = false;
        static constexpr bool allowScalar = true;
        static constexpr int defaultValue = 99;
    };

    struct dummyArray {
        using valueType = std::string;
        static constexpr const char* name = "dummyString";
        static constexpr bool required = false;
        static constexpr bool allowScalar = false;
        static constexpr const char defaultValue[] = "dummy";
    };
}

namespace gtest {
namespace soa {

TEST(nixlSoATest, XferArrayBuilderAndView) {
    uintptr_t addrs[] = {0x1000, 0x2000, 0x3000};
    size_t lengths[] = {256, 512, 1024};

    nixlXferArray arr = nixlXferArray::builder()
        .setSize(3)
        .setArrayPtr<nixlTag::addr>(addrs)
        .setArrayPtr<nixlTag::length>(lengths)
        .setScalar<nixlTag::devId>(42)
        .make<nixlXferArray>();

    EXPECT_EQ(arr.size(), 3);

    for (size_t i = 0; i < arr.size(); ++i) {
        auto [addr, len, dev, stride, strideCount] = arr[i];
        EXPECT_EQ(addr, addrs[i]);
        EXPECT_EQ(len, lengths[i]);
        EXPECT_EQ(dev, 42);
        EXPECT_EQ(stride, 0);
        EXPECT_EQ(strideCount, 1);
    }
}

TEST(nixlSoATest, XferArrayMutableCopies) {
    uintptr_t addrs[] = {0x1000, 0x2000};
    size_t lengths[] = {256, 512};
    uint64_t devIds[] = {0, 1};

    nixlXferArray arr = nixlXferArray::builder()
        .setSize(2)
        .setArrayCopy<nixlTag::addr>(addrs)
        .setArrayCopy<nixlTag::length>(lengths)
        .setArrayCopy<nixlTag::devId>(devIds)
        .make<nixlXferArray>();

    arr.add(0x3000, 1024, 2, 0, 1);

    EXPECT_EQ(arr.size(), 3);
    EXPECT_EQ(std::get<0>(arr[2]), 0x3000);
    EXPECT_EQ(std::get<1>(arr[2]), 1024);
    EXPECT_EQ(std::get<2>(arr[2]), 2);

    arr.remove(1);
    EXPECT_EQ(arr.size(), 2);
    EXPECT_EQ(std::get<0>(arr[1]), 0x3000);
}

TEST(nixlSoATest, ViewMutationThrows) {
    uintptr_t addrs[] = {0x1000};
    size_t lengths[] = {256};

    nixlXferArray arr = nixlXferArray::builder()
        .setSize(1)
        .setArrayPtr<nixlTag::addr>(addrs)
        .setArrayPtr<nixlTag::length>(lengths)
        .setScalar<nixlTag::devId>(42)
        .make<nixlXferArray>();

    EXPECT_THROW(arr.add(0x2000, 512, 42, 0, 1), std::runtime_error);
}

using nixlDummyArrayBase = nixlXferArrayBase::extend<
    nixlTag::dummyScalar,
    nixlTag::dummyArray
>;

class nixlDummyArray : public nixlDummyArrayBase {
public:
    using builder = nixlDummyArrayBase::builder;

    nixlDummyArray(nixl_mem_t mem_type) : nixlDummyArrayBase(mem_type) {}
    nixlDummyArray(nixlDummyArrayBase&& base) : nixlDummyArrayBase(std::move(base)) {}
};

TEST(nixlSoATest, ArrayExtensionWithDefaults) {
    uintptr_t addrs[] = {0x10, 0x20, 0x30};
    size_t lengths[] = {1, 2, 3};

    nixlXferArray arr = nixlXferArray::builder()
        .setSize(3)
        .setArrayPtr<nixlTag::addr>(addrs)
        .setArrayPtr<nixlTag::length>(lengths)
        .setScalar<nixlTag::devId>(7)
        .make<nixlXferArray>();

    nixlDummyArray dummy(std::move(arr));
    auto &strings = dummy.get<nixlTag::dummyArray>();
    strings = nixlArray<nixlTag::dummyArray>();
    strings.reserve(3);
    strings.add("dummy1");
    strings.add("dummy2");
    strings.add("dummy3");

    for (size_t i = 0; i < dummy.size(); ++i) {
        auto [addr, len, dev, stride, strideCount, scalar, string] = dummy[i];
        EXPECT_EQ(addr, addrs[i]);
        EXPECT_EQ(len, lengths[i]);
        EXPECT_EQ(dev, 7);
        EXPECT_EQ(scalar, 99);
        EXPECT_EQ(string, "dummy" + std::to_string(i + 1));
    }
}

} // namespace soa
} // namespace gtest
