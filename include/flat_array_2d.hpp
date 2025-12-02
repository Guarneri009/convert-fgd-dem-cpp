#pragma once

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace fgd_converter {

template <typename T>
class FlatArray2D {
   public:
    /**
     * @brief Construct a 2D array with specified dimensions
     * @param height Number of rows
     * @param width Number of columns
     * @param init_value Initial value for all elements (default: 0)
     */
    FlatArray2D(size_t height, size_t width, T init_value = T{})
        : data_(height * width, init_value), width_(width), height_(height) {}

    /**
     * @brief Access element at (row, col)
     * @param row Row index
     * @param col Column index
     * @return Reference to the element
     */
    T& operator()(size_t row, size_t col) { return data_[row * width_ + col]; }

    /**
     * @brief Access element at (row, col) (const version)
     */
    const T& operator()(size_t row, size_t col) const { return data_[row * width_ + col]; }

    /**
     * @brief Get pointer to the beginning of a row
     * @param row Row index
     * @return Pointer to the first element of the row
     *
     * This is useful for operations like memcpy:
     * @code
     * memcpy(array.row(y), source, width * sizeof(T));
     * @endcode
     */
    T* row(size_t row) { return &data_[row * width_]; }

    const T* row(size_t row) const { return &data_[row * width_]; }

    /**
     * @brief Get raw pointer to the underlying data
     */
    T* data() { return data_.data(); }

    const T* data() const { return data_.data(); }

    /**
     * @brief Get dimensions
     */
    size_t width() const { return width_; }
    size_t height() const { return height_; }
    size_t size() const { return data_.size(); }

    /**
     * @brief Fill all elements with a value
     */
    void fill(T value) { std::fill(data_.begin(), data_.end(), value); }

    /**
     * @brief Convert to vector<vector<T>> (for compatibility)
     * Note: This creates a copy and is slow - avoid if possible
     */
    std::vector<std::vector<T>> to_2d_vector() const {
        std::vector<std::vector<T>> result;
        result.reserve(height_);

        for (size_t y = 0; y < height_; ++y) {
            result.emplace_back(row(y), row(y) + width_);
        }

        return result;
    }

   private:
    std::vector<T> data_;
    size_t width_;
    size_t height_;
};

}  // namespace fgd_converter
