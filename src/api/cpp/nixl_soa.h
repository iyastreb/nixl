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
#ifndef _NIXL_SOA_H
#define _NIXL_SOA_H

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include "nixl_descriptors.h"


namespace nixlTag {
    template <typename Tag, typename... Tags>
    constexpr size_t index() {
        size_t index = 0;
        (void)((std::is_same_v<Tag, Tags> ? false : (++index, true)) && ...);
        return index;
    }

    struct addr {
        using valueType = uintptr_t;
        static constexpr const char* name = "addr";
        static constexpr bool required = true;
        static constexpr bool allowScalar = false;
    };

    struct length {
        using valueType = size_t;
        static constexpr const char* name = "length";
        static constexpr bool required = true;
        static constexpr bool allowScalar = false;
    };

    struct devId {
        using valueType = uint64_t;
        static constexpr const char* name = "devId";
        static constexpr bool required = true;
        static constexpr bool allowScalar = true;
    };

    struct stride {
        using valueType = size_t;
        static constexpr const char* name = "stride";
        static constexpr bool required = false;
        static constexpr bool allowScalar = true;
        static constexpr size_t defaultValue = 0;
    };

    struct strideCount {
        using valueType = uint64_t;
        static constexpr const char* name = "strideCount";
        static constexpr bool required = false;
        static constexpr bool allowScalar = true;
        static constexpr size_t defaultValue = 1;
    };
}

enum nixlArrayFlags : uint8_t {
    NIXL_ARRAY_NONE    = 0,
    NIXL_ARRAY_VIEW    = 1 << 1,
    NIXL_ARRAY_SCALAR  = 1 << 2,
    NIXL_ARRAY_INVALID = 0xFF
};

template <typename Tag>
class nixlArray {
public:
    using T = typename Tag::valueType;
    static constexpr bool allowScalar = Tag::allowScalar;

    nixlArray() = default;

    nixlArray(const T* ptr, size_t size, uint8_t flags = NIXL_ARRAY_VIEW)
        : ptr_(ptr), size_(size), flags_(flags) {
        if (!isView()) {
            data_.assign(ptr, ptr + size);
            sync();
        }
    }

    template <bool A = allowScalar, std::enable_if_t<A, int> = 0>
    nixlArray(T scalar)
        : size_(1), scalar_(std::move(scalar)), flags_(NIXL_ARRAY_SCALAR), mask_(0) {
        sync();
    }

    nixlArray(size_t count, T val)
        : size_(count) {
        data_.assign(count, std::move(val));
        sync();
    }

    nixlArray(std::vector<T>&& data) : data_(std::move(data)) {
        sync();
    }

    nixlArray(const nixlArray& other)
        : ptr_(other.ptr_), size_(other.size_), data_(other.data_),
          scalar_(other.scalar_), flags_(other.flags_), mask_(other.mask_) {
        sync();
    }

    nixlArray(nixlArray&& other) noexcept {
        swap(*this, other);
        sync();
    }

    nixlArray& operator=(nixlArray other) noexcept {
        swap(*this, other);
        sync();
        return *this;
    }

    static void swap(nixlArray& a, nixlArray& b) noexcept {
        std::swap(a.ptr_, b.ptr_);
        std::swap(a.size_, b.size_);
        std::swap(a.data_, b.data_);
        std::swap(a.scalar_, b.scalar_);
        std::swap(a.flags_, b.flags_);
        std::swap(a.mask_, b.mask_);
    }

    const T& operator[](size_t i) const {
        if constexpr (allowScalar) {
            return ptr_[i & mask_];
        }
        return ptr_[i];
    }

    size_t size() const { return size_; }

    const T* data() const { return ptr_; }

    const bool empty() const { return size_ == 0; }

    bool isView() const { return flags_ & NIXL_ARRAY_VIEW; }

    bool isScalar() const { return flags_ & NIXL_ARRAY_SCALAR; }

    void add(T val) {
        if (!checkMutable(val)) {
            return;
        }
        data_.push_back(std::move(val));
        sync();
    }

    void remove(size_t i) {
        if (!checkMutable()) {
            return;
        }
        data_.erase(data_.begin() + i);
        sync();
    }

    void reserve(size_t size) {
        if (!checkMutable()) {
            return;
        }
        data_.reserve(size);
    }

    nixlArray view() const {
        if (isScalar() || isView()) {
            return *this;
        }
        return nixlArray(ptr_, size_);
    }

private:
    void sync() {
        if (isScalar()) {
            ptr_ = &scalar_;
        } else if (!isView()) {
            ptr_ = data_.data();
            size_ = data_.size();
        }
    }

    bool checkMutable(std::optional<T> val = std::nullopt) {
        if (isScalar()) {
            if (val != std::nullopt && val != scalar_) {
                throw std::runtime_error("scalar value mismatch");
            }
            return false;
        }
        if (isView()) {
            throw std::runtime_error("can't modify a view array");
        }
        return true;
    }

    const T* ptr_ = nullptr;
    size_t size_ = 0;
    std::vector<T> data_;
    T scalar_ = {};
    uint8_t flags_ = 0;
    size_t mask_ = ~(size_t)0;
};

template <typename Container>
class nixlIterator {
public:
    nixlIterator(const Container* parent, size_t index) : parent_(parent), index_(index) {}

    nixlIterator& operator++() {
        ++index_;
        return *this;
    }

    bool operator!=(const nixlIterator& other) const {
        return index_ != other.index_;
    }

    auto operator*() const {
        return (*parent_)[index_];
    }

private:
    const Container* parent_;
    size_t index_;
};

template <typename... Tags>
class nixlSoABuilder;

template <typename... Tags>
class nixlSoA {
public:
    using iterator = nixlIterator<nixlSoA>;
    using builder = nixlSoABuilder<Tags...>;

    template <typename... NewTags>
    using extend = nixlSoA<Tags..., NewTags...>;

    explicit nixlSoA(nixl_mem_t mem_type) : mem_type_(mem_type) {}

    nixlSoA(nixl_mem_t mem_type, nixlArray<Tags>... arrs)
        : mem_type_(mem_type), arrays_(std::move(arrs)...) {}

    template <typename... OtherTags>
    nixlSoA(const nixlSoA<OtherTags...>& other)
        : mem_type_(other.getType()), arrays_(extract<Tags>(other)...) {}

    template <typename... OtherTags>
    nixlSoA(nixlSoA<OtherTags...>&& other)
        : mem_type_(other.getType()), arrays_(extract<Tags>(std::move(other))...) {}

    template <typename TagToCheck>
    static constexpr bool hasTag() {
        return (std::is_same_v<TagToCheck, Tags> || ...);
    }

    template <typename Tag>
    auto& get() {
        constexpr size_t idx = nixlTag::index<Tag, Tags...>();
        return std::get<idx>(arrays_);
    }

    template <typename Tag>
    const auto& get() const {
        constexpr size_t idx = nixlTag::index<Tag, Tags...>();
        return std::get<idx>(arrays_);
    }

    size_t size() const {
        return std::get<0>(arrays_).size();
    }

    bool empty() const {
        return std::get<0>(arrays_).empty();
    }

    nixl_mem_t getType() const { return mem_type_; }

    void add(const typename Tags::valueType&... args) {
        std::apply([&](auto&... arrs) { (arrs.add(args), ...); }, arrays_);
    }

    void remove(size_t i) {
        std::apply([i](auto&... arrs) { (arrs.remove(i), ...); }, arrays_);
    }

    void reserve(size_t size) {
        std::apply([size](auto&... arrs) { (arrs.reserve(size), ...); }, arrays_);
    }

    template <size_t Count = sizeof...(Tags)>
    auto getRow(size_t i) const {
        return getRowImpl(i, std::make_index_sequence<Count>{});
    }

    auto operator[](size_t i) const {
        return getRow(i);
    }

    nixlBasicDescRef getDesc(size_t i) const {
        auto [addr, length, devId] = getRow<3>(i);
        return nixlBasicDescRef{addr, length, devId};
    }

    nixlSoA view() const {
        return std::apply([this](const auto&... arrs) {
            return nixlSoA(mem_type_, arrs.view()...);
        }, arrays_);
    }

    iterator begin() const { return {this, 0}; }
    iterator end() const { return {this, size()}; }

private:
    template <typename TargetTag, typename OtherSoA>
    static nixlArray<TargetTag> extract(OtherSoA&& other) {
        using DecayedSoA = std::decay_t<OtherSoA>;

        if constexpr (DecayedSoA::template hasTag<TargetTag>()) {
            return std::forward<OtherSoA>(other).template get<TargetTag>();
        } else {
            if constexpr (TargetTag::allowScalar) {
                return nixlArray<TargetTag>(TargetTag::defaultValue);
            } else {
                return nixlArray<TargetTag>(other.size(), TargetTag::defaultValue);
            }
        }
    }

    template <size_t... Is>
    auto getRowImpl(size_t i, std::index_sequence<Is...>) const {
        return std::forward_as_tuple(std::get<Is>(arrays_)[i]...);
    }

    nixl_mem_t mem_type_;
    std::tuple<nixlArray<Tags>...> arrays_;
};

template <typename... Tags>
class nixlSoABuilder {
public:
    nixlSoABuilder() = default;

    nixlSoABuilder& setSize(size_t size) {
        size_ = size;
        return *this;
    }

    nixlSoABuilder& setMemType(nixl_mem_t mem_type) {
        mem_type_ = mem_type;
        return *this;
    }

    template <typename Tag>
    nixlSoABuilder& setArrayPtr(const typename Tag::valueType* ptr) {
        constexpr size_t idx = nixlTag::index<Tag, Tags...>();
        std::get<idx>(cols_).ptr = ptr;
        std::get<idx>(cols_).flags = NIXL_ARRAY_VIEW;
        return *this;
    }

    template <typename Tag>
    nixlSoABuilder& setArrayCopy(const typename Tag::valueType* ptr) {
        constexpr size_t idx = nixlTag::index<Tag, Tags...>();
        std::get<idx>(cols_).ptr = ptr;
        std::get<idx>(cols_).flags = NIXL_ARRAY_NONE;
        return *this;
    }

    template <typename Tag>
    nixlSoABuilder& setScalar(typename Tag::valueType val) {
        constexpr size_t idx = nixlTag::index<Tag, Tags...>();
        static_assert(Tag::allowScalar, "scalar not allowed");

        std::get<idx>(cols_).scalar = std::move(val);
        std::get<idx>(cols_).flags = NIXL_ARRAY_SCALAR;
        return *this;
    }

    template <typename Target>
    Target make() const {
        return Target(makeImpl(std::index_sequence_for<Tags...>{}));
    }

private:
    template <typename T>
    struct data {
        const T* ptr = nullptr;
        T scalar = {};
        nixlArrayFlags flags = NIXL_ARRAY_INVALID;
    };

    template <typename Tag>
    static nixlArray<Tag> makeArray(
        const data<typename Tag::valueType>& data, size_t size) {
        using ArrayType = nixlArray<Tag>;

        if (data.flags == NIXL_ARRAY_INVALID) {
            if constexpr (Tag::required) {
                throw std::runtime_error(std::string("required column '") +
                                         Tag::name + "' was not set");
            } else {
                if constexpr (Tag::allowScalar) {
                    return ArrayType(Tag::defaultValue);
                } else {
                    return ArrayType(size, Tag::defaultValue);
                }
            }
        }

        if (data.flags == NIXL_ARRAY_SCALAR) {
            if constexpr (ArrayType::allowScalar) {
                return ArrayType(data.scalar);
            } else {
                throw std::runtime_error("unreachable");
            }
        } else {
            return ArrayType(data.ptr, size, data.flags);
        }
    }

    template <size_t... Is>
    nixlSoA<Tags...> makeImpl(std::index_sequence<Is...>) const {
        if (size_ == 0) {
            throw std::runtime_error("size must be > 0");
        }
        return nixlSoA<Tags...>(
            mem_type_,
            makeArray<std::tuple_element_t<Is, std::tuple<Tags...>>>(
                std::get<Is>(cols_), size_)...);
    }

    size_t size_ = 0;
    nixl_mem_t mem_type_ = DRAM_SEG;
    std::tuple<data<typename Tags::valueType>...> cols_;
};

using nixlXferArrayBase = nixlSoA<
    nixlTag::addr,
    nixlTag::length,
    nixlTag::devId,
    nixlTag::stride,
    nixlTag::strideCount
>;

class nixlXferArray : public nixlXferArrayBase {
public:
    using builder = nixlXferArrayBase::builder;

    nixlXferArray(nixl_mem_t mem_type) : nixlXferArrayBase(mem_type) {}
    nixlXferArray(nixlXferArrayBase&& base) : nixlXferArrayBase(std::move(base)) {}
};

using nixl_xfer_array_t = nixlXferArray;

#endif
