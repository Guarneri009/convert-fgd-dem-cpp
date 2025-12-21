#include "xml_parser.hpp"

#include <filesystem>

#include "fast_fgd_parser.hpp"

namespace fgd_converter::xml {

class XmlParser::Impl {
   public:
    explicit Impl(std::string_view xml_content) {
        // 超高速1パスパーサーを使用
        auto parsed = FastFGDParser::parse_all(xml_content, true);
        if (parsed) {
            data = *parsed;
        } else {
            // パースに失敗した場合は例外をスロー
            throw std::runtime_error("XMLコンテンツの解析に失敗しました");
        }
    }

    FastFGDParser::ParsedData data;
};

XmlParser::XmlParser(std::string_view xml_content) : pImpl(std::make_unique<Impl>(xml_content)) {}

XmlParser::~XmlParser() = default;
XmlParser::XmlParser(XmlParser &&) noexcept = default;
XmlParser &XmlParser::operator=(XmlParser &&) noexcept = default;

auto XmlParser::get_lower_corner() const -> std::optional<TuplePoint> {
    if (pImpl->data.has_lower_corner) {
        return TuplePoint{pImpl->data.lower_corner_x, pImpl->data.lower_corner_y};
    }
    return std::nullopt;
}

auto XmlParser::get_upper_corner() const -> std::optional<TuplePoint> {
    if (pImpl->data.has_upper_corner) {
        return TuplePoint{pImpl->data.upper_corner_x, pImpl->data.upper_corner_y};
    }
    return std::nullopt;
}

auto XmlParser::get_grid_envelope() const -> std::optional<GridEnvelope> {
    if (pImpl->data.has_grid_envelope) {
        return GridEnvelope{pImpl->data.grid_low_x, pImpl->data.grid_low_y, pImpl->data.grid_high_x,
                            pImpl->data.grid_high_y};
    }
    return std::nullopt;
}

auto XmlParser::get_start_point() const -> std::optional<StartPoint> {
    if (pImpl->data.has_start_point) {
        return StartPoint{pImpl->data.start_x, pImpl->data.start_y};
    }
    return std::nullopt;
}

auto XmlParser::get_tuple_list(std::error_code &ec) const
    -> std::optional<std::vector<std::vector<double>>> {
    if (pImpl->data.has_tuple_list) {
        // 互換性のため単一行の2Dベクターとして返す
        return std::vector<std::vector<double>>{pImpl->data.elevation_list};
    }
    ec = std::make_error_code(std::errc::invalid_argument);
    return std::nullopt;
}

auto XmlParser::get_mesh_code() const -> std::optional<std::string> {
    if (pImpl->data.has_mesh_code) {
        return pImpl->data.mesh_code;
    }
    return std::nullopt;
}

auto XmlParser::get_dem_type() const -> std::optional<std::string> {
    if (pImpl->data.has_dem_type) {
        return pImpl->data.dem_type;
    }
    return std::nullopt;
}

auto XmlParser::extract_file_name(std::string_view xml_path) -> std::string {
    std::filesystem::path path(xml_path);
    return path.stem().string();
}

auto XmlParser::validate_xml(std::string_view xml_content) -> bool {
    // FastFGDParserを使用して検証
    auto result = FastFGDParser::parse_all(xml_content, true);
    return result.has_value();
}

}  // namespace fgd_converter::xml