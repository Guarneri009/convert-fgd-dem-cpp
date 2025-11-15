#pragma once

#include <algorithm>
#include <charconv>
#include <concepts>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

namespace fgd_converter::xml {

// XML parsing concept
template <typename T>
concept XmlParsable = requires(T t, std::string_view xml) {
    { T::parse(xml) } -> std::convertible_to<std::optional<T>>;
};

struct TuplePoint {
    double x;
    double y;

    auto operator<=>(const TuplePoint&) const = default;
};

struct GridEnvelope {
    int low_x;
    int low_y;
    int high_x;
    int high_y;

    auto operator<=>(const GridEnvelope&) const = default;
};

struct StartPoint {
    double x;
    double y;

    auto operator<=>(const StartPoint&) const = default;
};

class XmlParser {
   public:
    explicit XmlParser(std::string_view xml_content);
    ~XmlParser();

    // Move-only type
    XmlParser(const XmlParser&) = delete;
    XmlParser& operator=(const XmlParser&) = delete;
    XmlParser(XmlParser&&) noexcept;
    XmlParser& operator=(XmlParser&&) noexcept;

    [[nodiscard]] auto get_lower_corner() const -> std::optional<TuplePoint>;
    [[nodiscard]] auto get_upper_corner() const -> std::optional<TuplePoint>;
    [[nodiscard]] auto get_grid_envelope() const -> std::optional<GridEnvelope>;
    [[nodiscard]] auto get_start_point() const -> std::optional<StartPoint>;
    [[nodiscard]] auto get_tuple_list(std::error_code& ec) const
        -> std::optional<std::vector<std::vector<double>>>;
    [[nodiscard]] auto get_mesh_code() const -> std::optional<std::string>;
    [[nodiscard]] auto get_dem_type() const -> std::optional<std::string>;

    [[nodiscard]] static auto extract_file_name(std::string_view xml_path) -> std::string;
    [[nodiscard]] static auto validate_xml(std::string_view xml_content) -> bool;

   private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

class FastTupleListParser {
   public:
    /**
     * @brief Parse tuple list from XML text
     *
     * @param text The tupleList XML content
     * @param sea_at_zero If true, replace -9999 with 0 for sea points
     * @return Vector of elevation values
     *
     * Input format example:
     *   "その他,13.90\nその他,13.50\n海水面,-9999.\n"
     *
     * Output:
     *   {13.90, 13.50, 0.0}  (if sea_at_zero = true)
     */
    static std::vector<double> parse(std::string_view text, bool sea_at_zero = true) {
        std::vector<double> elevation_list;

        // Pre-allocate based on estimated line count
        size_t estimated_lines = count_newlines(text);
        elevation_list.reserve(estimated_lines);

        const char* ptr = text.data();
        const char* end = text.data() + text.size();

        // Skip leading newline if exists
        if (ptr < end && *ptr == '\n') {
            ++ptr;
        }

        while (ptr < end) {
            // Find the comma separator between type and value
            const char* comma = ptr;
            while (comma < end && *comma != ',') {
                ++comma;
            }

            if (comma >= end)
                break;

            // Extract type (for sea detection)
            std::string_view type(ptr, comma - ptr);

            // Move past comma
            const char* value_start = comma + 1;

            // Find end of value (newline or end of string)
            const char* value_end = value_start;
            while (value_end < end && *value_end != '\n') {
                ++value_end;
            }

            // Parse the value using std::from_chars (fastest method)
            double value;
            auto [p, ec] = std::from_chars(value_start, value_end, value);

            if (ec == std::errc{}) {
                // Handle sea points if needed
                if (sea_at_zero && value <= -9999.0 && is_sea_type(type)) {
                    elevation_list.push_back(0.0);
                } else {
                    elevation_list.push_back(value);
                }
            } else {
                // Parse error - push default value
                elevation_list.push_back(-9999.0);
            }

            // Move to next line
            ptr = value_end;
            if (ptr < end && *ptr == '\n') {
                ++ptr;
            }
        }

        return elevation_list;
    }

   private:
    /**
     * @brief Check if type indicates a sea point
     */
    static bool is_sea_type(std::string_view type) {
        // Common sea types in FGD DEM
        return type == "海水面" || type == "海水底面";
    }

    /**
     * @brief Count newlines for better pre-allocation
     */
    static size_t count_newlines(std::string_view text) {
        return std::count(text.begin(), text.end(), '\n');
    }
};

}  // namespace fgd_converter::xml