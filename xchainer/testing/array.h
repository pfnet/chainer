#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <functional>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

#include <gsl/gsl>

#include "xchainer/array.h"
#include "xchainer/dtype.h"
#include "xchainer/shape.h"
#include "xchainer/strides.h"

namespace xchainer {
namespace testing {

class ArrayBuilder {
public:
    ArrayBuilder(const Shape& shape) : shape_(shape) {}

    operator Array() const { return array(); }

    Array operator*() const { return array(); }

    template <typename T, typename InputIter>
    ArrayBuilder& WithData(InputIter first, InputIter last) {
        Expects(create_array_ == nullptr);
        std::vector<T> data(first, last);

        Ensures(data.size() == static_cast<size_t>(shape_.GetTotalSize()));

        // Define create_array_ here to type-erase T of `data`.
        // Note: ArrayBuilder must be specified as an argument instead of capturing `this` pointer, because the ArrayBuilder instance could
        // be copied and thus `this` pointer could be invalidated at the moment the function is called.
        create_array_ = [data](const ArrayBuilder& builder) -> Array {
            Dtype dtype = TypeToDtype<T>;
            const Shape& shape = builder.shape_;
            Expects(static_cast<size_t>(shape.GetTotalSize()) == data.size());
            Strides strides = builder.GetStrides<T>();
            int64_t total_size = shape.GetTotalSize();
            size_t total_bytes = strides.GetTotalBytes(shape);
            auto ptr = std::make_unique<uint8_t[]>(total_bytes);
            std::fill(ptr.get(), ptr.get() + total_bytes, uint8_t{0xff});

            if (total_size > 0) {
                // Copy the data to buffer, respecting strides
                auto* raw_ptr = ptr.get();
                std::vector<int64_t> counter(shape.begin(), shape.end());
                for (const T& value : data) {
                    // Copy a single value
                    assert((raw_ptr - ptr.get()) < static_cast<ptrdiff_t>(total_bytes));
                    *reinterpret_cast<T*>(raw_ptr) = value;
                    // Advance the counter and the pointer
                    int8_t i_dim = shape.ndim() - 1;
                    while (i_dim >= 0) {
                        raw_ptr += strides[i_dim];
                        counter[i_dim]--;
                        if (counter[i_dim] > 0) {
                            break;
                        }
                        counter[i_dim] = shape[i_dim];
                        raw_ptr -= strides[i_dim] * shape[i_dim];
                        i_dim--;
                    }
                }
            }
            return Array::FromBuffer(shape, dtype, std::move(ptr), std::move(strides));
        };
        return *this;
    }

    template <typename T, size_t N>
    ArrayBuilder& WithData(const std::array<T, N>& data) {
        Expects(static_cast<size_t>(shape_.GetTotalSize()) == N);
        return WithData<T>(data.begin(), data.end());
    }

    template <typename T>
    ArrayBuilder& WithData(std::initializer_list<T> data) {
        Expects(static_cast<size_t>(shape_.GetTotalSize()) == data.size());
        return WithData<T>(data.begin(), data.end());
    }

    template <typename T>
    ArrayBuilder& WithLinearData(T start = T{0}, T step = T{1}) {
        int64_t total_size = shape_.GetTotalSize();
        std::vector<T> data;
        data.reserve(total_size);
        T value = start;
        for (int64_t i = 0; i < total_size; ++i) {
            data.push_back(value);
            value += step;
        }
        return WithData<T>(data.begin(), data.end());
    }

    ArrayBuilder& WithPadding(std::vector<int64_t> padding) {
        Expects(padding_.empty());
        Expects(padding.size() == shape_.size());
        padding_ = std::move(padding);
        return *this;
    }

    ArrayBuilder& WithPadding(int64_t padding) {
        Expects(padding_.empty());
        std::fill_n(std::back_inserter(padding_), shape_.size(), padding);
        return *this;
    }

    Array array() const {
        Expects(create_array_ != nullptr);
        return create_array_(*this);
    }

private:
    template <typename T>
    Strides GetStrides() const {
        std::vector<int64_t> padding = padding_;
        if (padding.empty()) {
            std::fill_n(std::back_inserter(padding), shape_.size(), int64_t{0});
        }

        // Create strides with extra space specified by `padding`.
        Expects(padding.size() == shape_.size());
        std::vector<int64_t> rev_strides;
        rev_strides.reserve(shape_.size());
        int64_t st = sizeof(T);
        for (int8_t i = shape_.ndim() - 1; i >= 0; --i) {
            st += padding[i];
            rev_strides.push_back(st);
            st *= shape_[i];
        }
        return {rev_strides.rbegin(), rev_strides.rend()};
    }

    Shape shape_;

    // Padding bytes to each dimension.
    // TODO(niboshi): Support negative strides
    std::vector<int64_t> padding_;

    // Using std::function to type-erase data type T
    std::function<Array(const ArrayBuilder&)> create_array_;
};

inline ArrayBuilder MakeArray(const Shape& shape) { return {shape}; }

template <typename T, size_t N>
ArrayBuilder MakeArray(const Shape& shape, const std::array<T, N>& data) {
    return ArrayBuilder{shape}.WithData<T>(data.begin(), data.end());
}

template <typename T>
ArrayBuilder MakeArray(const Shape& shape, std::initializer_list<T> data) {
    return ArrayBuilder{shape}.WithData<T>(data.begin(), data.end());
}

}  // namespace testing
}  // namespace xchainer
