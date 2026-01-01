#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fgd_converter {

template <typename T>
class span {
   public:
    span() : data_(nullptr), size_(0) {}
    span(T* data, size_t size) : data_(data), size_(size) {}
    span(std::vector<T>& vec) : data_(vec.data()), size_(vec.size()) {}

    T* data() const { return data_; }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    T& operator[](size_t idx) const { return data_[idx]; }
    T* begin() const { return data_; }
    T* end() const { return data_ + size_; }

   private:
    T* data_;
    size_t size_;
};

struct Metadata {
    std::string mesh_code;
    std::string file_name;
    std::string type;
    double lower_corner_x{};
    double lower_corner_y{};
    double upper_corner_x{};
    double upper_corner_y{};
    int x_length{};
    int y_length{};
    double start_x{};
    double start_y{};
};

struct BoundsLatLng {
    double min_lat{};
    double max_lat{};
    double min_lng{};
    double max_lng{};
};

class Dem {
   public:
    explicit Dem(std::filesystem::path import_path, bool sea_at_zero = true);

    [[nodiscard]] auto contents_to_array() const -> std::vector<std::vector<double>>;
    void get_xml_content();

    [[nodiscard]] auto get_metadata_list() const noexcept -> span<const Metadata> {
        return span<const Metadata>(meta_data_list.data(), meta_data_list.size());
    }
    [[nodiscard]] auto get_np_array_list() const noexcept
        -> span<const std::vector<std::vector<double>>> {
        return span<const std::vector<std::vector<double>>>(np_array_list.data(),
                                                            np_array_list.size());
    }
    [[nodiscard]] auto get_bounds_latlng() const noexcept -> const BoundsLatLng& {
        return bounds_latlng;
    }
    [[nodiscard]] auto get_mesh_code_list() const noexcept -> span<const std::string> {
        return span<const std::string>(mesh_code_list.data(), mesh_code_list.size());
    }

   private:
    void unzip_dem();
    [[nodiscard]] auto get_xml_paths() -> std::vector<std::filesystem::path>;
    [[nodiscard]] auto format_metadata(std::string_view xml_content,
                                       std::string_view mesh_code) -> Metadata;
    void check_mesh_codes();
    void populate_metadata_list();
    void store_bounds_latlng();
    [[nodiscard]] auto get_np_array(std::string_view xml_content)
        -> std::vector<std::vector<double>>;
    void store_np_array_list();

    std::filesystem::path import_path;
    std::vector<std::filesystem::path> xml_paths;
    std::vector<std::string> all_content_list;
    std::vector<std::string> mesh_code_list;
    std::vector<Metadata> meta_data_list;
    bool sea_at_zero;
    std::vector<std::vector<std::vector<double>>> np_array_list;
    BoundsLatLng bounds_latlng{};
};

}  // namespace fgd_converter
