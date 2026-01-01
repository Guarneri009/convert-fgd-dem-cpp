#include "converter.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>  // for memcpy
#include <iostream>
#include <numeric>
#include <set>
#include <sstream>

#include "geotiff.hpp"

// SIMDイントリンシクスのプラットフォーム検出
#if defined(__x86_64__) || defined(_M_X64)
#    if defined(__AVX2__)
#        define HAS_AVX2 1
#        include <immintrin.h>
#    endif
#elif defined(__aarch64__) || defined(_M_ARM64)
#    define HAS_NEON 1
#    include <arm_neon.h>
#endif

namespace fgd_converter {

Converter::Converter(Config config) : config_(std::move(config)) {
    if (!std::filesystem::exists(config_.import_path)) {
        std::stringstream ss;
        ss << "Import path does not exist: " << config_.import_path.string();
        throw std::runtime_error(ss.str());
    }

    if (!std::filesystem::exists(config_.output_path)) {
        std::filesystem::create_directories(config_.output_path);
    }

    dem_ = std::make_unique<Dem>(config_.import_path, config_.sea_at_zero);
}

auto Converter::calc_image_size(span<const Metadata> meta_data_list) const noexcept
    -> std::pair<int, int> {
    if (meta_data_list.empty()) {
        return {0, 0};
    }

    auto bounds = dem_->get_bounds_latlng();

    // 最初のメタデータからピクセルサイズを計算
    double pixel_size_x = (meta_data_list[0].upper_corner_y - meta_data_list[0].lower_corner_y) /
                          meta_data_list[0].x_length;
    double pixel_size_y = (meta_data_list[0].lower_corner_x - meta_data_list[0].upper_corner_x) /
                          meta_data_list[0].y_length;

    // 境界とピクセルサイズに基づいて総画像サイズを計算
    int x_length =
        static_cast<int>(std::round(std::abs((bounds.max_lng - bounds.min_lng) / pixel_size_x)));
    int y_length =
        static_cast<int>(std::round(std::abs((bounds.max_lat - bounds.min_lat) / pixel_size_y)));

    return {x_length, y_length};
}

auto Converter::combine_meta_data_and_contents(
    span<const Metadata> meta_data_list, span<const std::vector<std::vector<double>>> np_array_list)
    const -> std::tuple<std::vector<std::vector<double>>, int, int> {
    auto [total_x, total_y] = calc_image_size(meta_data_list);
    auto bounds = dem_->get_bounds_latlng();

    // 出力配列を初期化
    std::vector<std::vector<double>> combined_array(total_y, std::vector<double>(total_x, -9999.0));

    // ピクセルサイズを計算
    double pixel_size_x = (meta_data_list[0].upper_corner_y - meta_data_list[0].lower_corner_y) /
                          meta_data_list[0].x_length;
    double pixel_size_y = (meta_data_list[0].lower_corner_x - meta_data_list[0].upper_corner_x) /
                          meta_data_list[0].y_length;

    // 各メッシュデータを結合配列に配置
    for (size_t i = 0; i < meta_data_list.size() && i < np_array_list.size(); ++i) {
        const auto &metadata = meta_data_list[i];
        const auto &np_array = np_array_list[i];

        // 左下角からの距離を計算
        double lat_distance = metadata.lower_corner_x - bounds.min_lat;
        double lon_distance = metadata.lower_corner_y - bounds.min_lng;

        // 配列上の座標を取得 (誤差除去のため四捨五入)
        int x_coordinate = static_cast<int>(std::round(lon_distance / pixel_size_x));
        int y_coordinate = static_cast<int>(std::round(lat_distance / (-pixel_size_y)));

        int x_len = metadata.x_length;
        int y_len = metadata.y_length;

        // 行と列の位置を計算
        int row_start = total_y - (y_coordinate + y_len);
        int column_start = x_coordinate;

        // 配列データをコピー - 最適化版
        for (int y = 0; y < y_len && y < static_cast<int>(np_array.size()); ++y) {
            int target_row = row_start + y;
            if (target_row < 0 || target_row >= total_y)
                continue;

            // 安全なコピー範囲を計算
            int src_start = 0;
            int dst_start = column_start;
            int copy_len = x_len;

            // コピー先が配列境界より前から始まる場合は調整
            if (dst_start < 0) {
                src_start = -dst_start;
                copy_len += dst_start;
                dst_start = 0;
            }

            // コピー先が配列境界を超える場合は調整
            if (dst_start + copy_len > total_x) {
                copy_len = total_x - dst_start;
            }

            // コピー元が利用可能データを超える場合は調整
            int src_available = static_cast<int>(np_array[y].size()) - src_start;
            if (copy_len > src_available) {
                copy_len = src_available;
            }

            // memcpyを使用した一括コピー (要素ごとより大幅に高速)
            if (copy_len > 0) {
                std::memcpy(&combined_array[target_row][dst_start], &np_array[y][src_start],
                            copy_len * sizeof(double));
            }
        }
    }

    return {combined_array, total_x, total_y};
}

bool Converter::make_data_for_geotiff(std::vector<std::vector<double>> &np_array,
                                      std::array<double, 6> &geo_transform, int &x_length,
                                      int &y_length, std::error_code &ec) {
    if (!dem_) {
        ec = std::make_error_code(std::errc::invalid_argument);
        return false;
    }

    dem_->get_xml_content();

    auto meta_data_list = dem_->get_metadata_list();
    auto np_array_list = dem_->get_np_array_list();

    if (meta_data_list.empty() || np_array_list.empty()) {
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return false;
    }

    auto [combined_array, combined_x_length, combined_y_length] =
        combine_meta_data_and_contents(meta_data_list, np_array_list);

    // ジオ変換を計算
    auto bounds = dem_->get_bounds_latlng();

    double pixel_width = (bounds.max_lng - bounds.min_lng) / combined_x_length;
    double pixel_height = -(bounds.max_lat - bounds.min_lat) / combined_y_length;

    geo_transform = {
        bounds.min_lng,  // 左上X
        pixel_width,     // ピクセル幅
        0.0,             // 回転
        bounds.max_lat,  // 左上Y
        0.0,             // 回転
        pixel_height     // ピクセル高さ (負の値)
    };

    np_array = std::move(combined_array);
    x_length = combined_x_length;
    y_length = combined_y_length;

    return true;
}

bool Converter::run(std::error_code &ec) {
    std::vector<std::vector<double>> np_array;
    std::array<double, 6> geo_transform;
    int x_length, y_length;

    if (!make_data_for_geotiff(np_array, geo_transform, x_length, y_length, ec)) {
        return false;
    }

    // 出力ファイル名を決定
    std::filesystem::path output_file;
    if (config_.file_name) {
        output_file = config_.output_path / *config_.file_name;
    } else {
        output_file = config_.output_path / config_.import_path.stem();
        output_file.replace_extension(".tif");
    }

    // GeoTIFFを作成
    GeoTiff::Config geotiff_config{.geo_transform = geo_transform,
                                   .np_array = np_array,
                                   .x_length = x_length,
                                   .y_length = y_length,
                                   .output_path = output_file};

    GeoTiff geotiff(geotiff_config);

    if (!geotiff.create(config_.output_epsg, config_.rgbify, ec)) {
        return false;
    }

    // 必要に応じてリサンプリング
    if (config_.output_epsg != "EPSG:4326") {
        std::error_code resample_ec;
        if (!geotiff.resampling(config_.output_epsg, resample_ec)) {
            std::cerr << "警告: リサンプリングに失敗しました\n";
        }
    }

    std::cout << "出力先: " << output_file.string() << "\n";
    return true;
}

}  // namespace fgd_converter