#pragma once

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace fgd_converter {

template <typename T>
class FlatArray2D {
   public:
    /**
     * @brief 指定した次元で2D配列を構築
     * @param height 行数
     * @param width 列数
     * @param init_value 全要素の初期値 (デフォルト: 0)
     */
    FlatArray2D(size_t height, size_t width, T init_value = T{})
        : data_(height * width, init_value), width_(width), height_(height) {}

    /**
     * @brief (row, col)の要素にアクセス
     * @param row 行インデックス
     * @param col 列インデックス
     * @return 要素への参照
     */
    T& operator()(size_t row, size_t col) { return data_[row * width_ + col]; }

    /**
     * @brief (row, col)の要素にアクセス (const版)
     */
    const T& operator()(size_t row, size_t col) const { return data_[row * width_ + col]; }

    /**
     * @brief 行の先頭へのポインタを取得
     * @param row 行インデックス
     * @return 行の最初の要素へのポインタ
     *
     * memcpyなどの操作に便利:
     * @code
     * memcpy(array.row(y), source, width * sizeof(T));
     * @endcode
     */
    T* row(size_t row) { return &data_[row * width_]; }

    const T* row(size_t row) const { return &data_[row * width_]; }

    /**
     * @brief 基礎データへの生ポインタを取得
     */
    T* data() { return data_.data(); }

    const T* data() const { return data_.data(); }

    /**
     * @brief 次元を取得
     */
    size_t width() const { return width_; }
    size_t height() const { return height_; }
    size_t size() const { return data_.size(); }

    /**
     * @brief 全要素を指定値で埋める
     */
    void fill(T value) { std::fill(data_.begin(), data_.end(), value); }

    /**
     * @brief vector<vector<T>>に変換 (互換性のため)
     * 注意: コピーを作成するため低速 - 可能な限り避けること
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
