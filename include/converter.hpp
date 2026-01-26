#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "dem.hpp"

namespace fgd_converter {

class Converter {
   public:
    struct Config {
        std::filesystem::path import_path;
        std::filesystem::path output_path;
        std::string output_epsg{"EPSG:4326"};
        std::optional<std::string> file_name;
        bool rgbify{false};
        bool sea_at_zero{true};
    };

    explicit Converter(Config config);

    [[nodiscard]] bool run(std::error_code& ec);

   private:
    [[nodiscard]] auto calc_image_size(span<const Metadata> meta_data_list) const noexcept
        -> std::pair<int, int>;

    [[nodiscard]] auto combine_meta_data_and_contents(
        span<const Metadata> meta_data_list,
        span<const std::vector<std::vector<double>>> np_array_list) const
        -> std::tuple<std::vector<std::vector<double>>, int, int>;

    [[nodiscard]] bool make_data_for_geotiff(std::vector<std::vector<double>>& np_array,
                                             std::array<double, 6>& geo_transform, int& x_length,
                                             int& y_length, std::error_code& ec);

    Config config_;
    std::unique_ptr<Dem> dem_;
    bool process_interrupted_{false};
};

}  // namespace fgd_converter