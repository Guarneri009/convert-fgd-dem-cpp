#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

namespace fgd_converter {

class GeoTiff {
   public:
    struct Config {
        std::array<double, 6> geo_transform;
        std::span<const std::vector<double>> np_array;
        int x_length;
        int y_length;
        std::filesystem::path output_path;
    };

    explicit GeoTiff(Config config);
    ~GeoTiff();

    // ムーブのみ可能な型
    GeoTiff(const GeoTiff&) = delete;
    GeoTiff& operator=(const GeoTiff&) = delete;
    GeoTiff(GeoTiff&&) noexcept;
    GeoTiff& operator=(GeoTiff&&) noexcept;

    [[nodiscard]] bool create(std::string_view output_epsg, bool rgbify, std::error_code& ec);
    [[nodiscard]] bool resampling(std::string_view output_epsg, std::error_code& ec);

   private:
    class Impl;
    std::unique_ptr<Impl> pImpl;

    void write_raster_bands(bool rgbify);
};

// TIFファイルをマージするユーティリティ関数
struct MergeConfig {
    std::filesystem::path input_folder;
    std::string dem_type;               // "1A", "5A", "5B", "5C", "10A", "10B" など
    double resolution;                  // 出力解像度（メートル）
    std::filesystem::path output_file;  // 空の場合は自動生成
};

[[nodiscard]] bool merge_tif_files(const MergeConfig& config, std::error_code& ec);

}  // namespace fgd_converter