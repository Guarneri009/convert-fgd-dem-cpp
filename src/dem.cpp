#include "dem.hpp"

#include <tbb/parallel_for_each.h>

#include <algorithm>
#include <iostream>
#include <mutex>
#include <numeric>
#include <set>
#include <sstream>
#include <thread>

#include "flat_array_2d.hpp"
#include "memory_mapped_file.hpp"
#include "tbb_pipeline.hpp"
#include "xml_parser.hpp"
#include "zip_handler.hpp"

namespace fgd_converter {

Dem::Dem(std::filesystem::path import_path, bool sea_at_zero)
    : import_path(std::move(import_path)), sea_at_zero(sea_at_zero) {
    if (!std::filesystem::exists(this->import_path)) {
        std::stringstream ss;
        ss << "ファイルが見つかりません: " << this->import_path.string();
        throw std::runtime_error(ss.str());
    }
}

auto Dem::contents_to_array() const -> std::vector<std::vector<double>> {
    std::vector<std::vector<double>> result;

    for (const auto &array : np_array_list) {
        result.insert(result.end(), array.begin(), array.end());
    }

    return result;
}

void Dem::get_xml_content() {
    unzip_dem();
    xml_paths = get_xml_paths();

    if (xml_paths.empty()) {
        throw std::runtime_error("アーカイブ内にXMLファイルが見つかりません");
    }

    TBBPipeline<std::string> pipeline(
        [](std::string_view content) { return std::string(content); });

    all_content_list = pipeline.process_files(xml_paths);

    check_mesh_codes();
    populate_metadata_list();
    store_bounds_latlng();
    store_np_array_list();
}

void Dem::unzip_dem() {
    // ZIPファイル名に基づいてユニークな展開フォルダを作成
    std::filesystem::path extract_to = std::filesystem::path("extracted") / import_path.stem();
    extract_to = std::filesystem::absolute(extract_to).make_preferred();
    std::filesystem::create_directories(extract_to);

    if (zip::is_zip_file(import_path)) {
        zip::ZipHandler handler(import_path);

        // すべてのXMLファイルを展開
        std::error_code ec;
        auto result = handler.extract(extract_to, ec);
        if (!result) {
            std::stringstream ss;
            ss << "展開に失敗しました: " << import_path.string();
            throw std::runtime_error(ss.str());
        }

        // ネストされたZIPファイルも確認
        for (const auto &file : *result) {
            if (zip::is_zip_file(file)) {
                zip::ZipHandler nested_handler(file);
                std::error_code nested_ec;
                auto nested_result = nested_handler.extract(extract_to, nested_ec);
                if (!nested_result) {
                    std::cerr << "ネストZIPの展開に失敗: " << file.string() << "\n";
                }
            }
        }
    }
}

auto Dem::get_xml_paths() -> std::vector<std::filesystem::path> {
    std::vector<std::filesystem::path> xml_files;
    // 同じユニークな展開フォルダを使用
    std::filesystem::path extract_dir = std::filesystem::path("extracted") / import_path.stem();
    extract_dir = std::filesystem::absolute(extract_dir).make_preferred();

    if (!std::filesystem::exists(extract_dir)) {
        return xml_files;
    }

    for (const auto &entry : std::filesystem::recursive_directory_iterator(extract_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".xml") {
            xml_files.push_back(entry.path());
        }
    }

    // ファイル名でソート
    std::sort(xml_files.begin(), xml_files.end(),
              [](const auto &a, const auto &b) { return a.filename() < b.filename(); });

    return xml_files;
}

auto Dem::format_metadata(std::string_view xml_content, std::string_view mesh_code) -> Metadata {
    xml::XmlParser parser(xml_content);
    Metadata metadata;

    metadata.mesh_code = std::string(mesh_code);

    // 境界を取得
    if (auto lower = parser.get_lower_corner()) {
        metadata.lower_corner_x = lower->x;
        metadata.lower_corner_y = lower->y;
    }

    if (auto upper = parser.get_upper_corner()) {
        metadata.upper_corner_x = upper->x;
        metadata.upper_corner_y = upper->y;
    }

    // グリッドエンベロープを取得
    if (auto envelope = parser.get_grid_envelope()) {
        metadata.x_length = envelope->high_x - envelope->low_x + 1;
        metadata.y_length = envelope->high_y - envelope->low_y + 1;
    }

    // 開始点を取得
    if (auto start = parser.get_start_point()) {
        metadata.start_x = start->x;
        metadata.start_y = start->y;
    }

    // DEM種別を取得
    if (auto type = parser.get_dem_type()) {
        metadata.type = *type;
    }

    return metadata;
}

void Dem::check_mesh_codes() {
    for (const auto &xml_content : all_content_list) {
        xml::XmlParser parser(xml_content);

        if (auto mesh_code = parser.get_mesh_code()) {
            mesh_code_list.push_back(*mesh_code);
        }
    }

    // 重複を確認
    auto sorted_codes = mesh_code_list;
    std::sort(sorted_codes.begin(), sorted_codes.end());
    auto last = std::unique(sorted_codes.begin(), sorted_codes.end());

    if (std::distance(sorted_codes.begin(), last) !=
        static_cast<std::ptrdiff_t>(mesh_code_list.size())) {
        std::cerr << "警告: 重複するメッシュコードが見つかりました\n";
    }
}

void Dem::populate_metadata_list() {
    // スレッドセーフな並列アクセスのため事前割り当て
    size_t size = std::min(all_content_list.size(), mesh_code_list.size());
    meta_data_list.resize(size);

    // TBBを使用してメタデータを並列処理 (クロスプラットフォーム)
    std::vector<size_t> indices(size);
    std::iota(indices.begin(), indices.end(), 0);

    tbb::parallel_for_each(indices, [this](size_t i) {
        meta_data_list[i] = format_metadata(all_content_list[i], mesh_code_list[i]);
    });
}

void Dem::store_bounds_latlng() {
    if (meta_data_list.empty())
        return;

    bounds_latlng.min_lat = std::numeric_limits<double>::max();
    bounds_latlng.max_lat = std::numeric_limits<double>::lowest();
    bounds_latlng.min_lng = std::numeric_limits<double>::max();
    bounds_latlng.max_lng = std::numeric_limits<double>::lowest();

    for (const auto &metadata : meta_data_list) {
        bounds_latlng.min_lat = std::min(bounds_latlng.min_lat, metadata.lower_corner_x);
        bounds_latlng.max_lat = std::max(bounds_latlng.max_lat, metadata.upper_corner_x);
        bounds_latlng.min_lng = std::min(bounds_latlng.min_lng, metadata.lower_corner_y);
        bounds_latlng.max_lng = std::max(bounds_latlng.max_lng, metadata.upper_corner_y);
    }
}

auto Dem::get_np_array(std::string_view xml_content) -> std::vector<std::vector<double>> {
    xml::XmlParser parser(xml_content);

    std::error_code ec;
    auto tuple_result = parser.get_tuple_list(ec);
    if (!tuple_result || tuple_result->empty()) {
        return {};
    }

    const auto &elevation = (*tuple_result)[0];

    // グリッド寸法と開始点を取得
    auto envelope = parser.get_grid_envelope();
    auto start = parser.get_start_point();

    if (!envelope || !start) {
        return {};
    }

    int x_length = envelope->high_x - envelope->low_x + 1;
    int y_length = envelope->high_y - envelope->low_y + 1;
    int start_x = start->x;
    int start_y = start->y;

    FlatArray2D<double> array(y_length, x_length, -9999.0);

    size_t index = 0;
    int current_start_x = start_x;

    for (int y = start_y; y < y_length && index < elevation.size(); ++y) {
        for (int x = current_start_x; x < x_length && index < elevation.size(); ++x) {
            array(y, x) = elevation[index];
            ++index;
        }
        current_start_x = 0;  // 最初の行の後はx=0から開始
    }

    // 互換性のためvector<vector<double>>に変換
    return array.to_2d_vector();
}

void Dem::store_np_array_list() {
    // スレッドセーフな並列アクセスのため事前割り当て
    np_array_list.resize(all_content_list.size());

    // TBBを使用して配列を並列処理 (クロスプラットフォーム)
    std::vector<size_t> indices(all_content_list.size());
    std::iota(indices.begin(), indices.end(), 0);

    tbb::parallel_for_each(
        indices, [this](size_t i) { np_array_list[i] = get_np_array(all_content_list[i]); });
}

}  // namespace fgd_converter