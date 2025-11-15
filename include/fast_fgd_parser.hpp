#pragma once

#include <algorithm>
#include <charconv>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "simd_utils.hpp"

namespace fgd_converter::xml {

struct TuplePoint;
struct GridEnvelope;
struct StartPoint;

class FastFGDParser {
   public:
    struct ParsedData {
        double lower_corner_x = 0.0;
        double lower_corner_y = 0.0;
        double upper_corner_x = 0.0;
        double upper_corner_y = 0.0;
        int grid_low_x = 0;
        int grid_low_y = 0;
        int grid_high_x = 0;
        int grid_high_y = 0;
        double start_x = 0.0;
        double start_y = 0.0;
        std::string mesh_code;
        std::string dem_type;
        std::vector<double> elevation_list;

        // Flags to track what we've found
        bool has_lower_corner = false;
        bool has_upper_corner = false;
        bool has_grid_envelope = false;
        bool has_start_point = false;
        bool has_mesh_code = false;
        bool has_dem_type = false;
        bool has_tuple_list = false;
    };

    /**
     * @brief Parse FGD DEM XML in a single pass
     *
     * @param xml The complete XML content
     * @param sea_at_zero Convert -9999 sea values to 0
     * @return Parsed data or std::nullopt on critical error
     */
    static auto parse_all(std::string_view xml,
                          bool sea_at_zero = true) -> std::optional<ParsedData> {
        ParsedData data;

        const char* ptr = xml.data();
        const char* end = xml.data() + xml.size();

        // Pre-estimate elevation list size for better performance
        if (auto tuple_start = xml.find("<gml:tupleList>"); tuple_start != std::string_view::npos) {
            size_t estimated_lines = count_newlines_in_range(
                xml.data() + tuple_start, std::min(xml.data() + tuple_start + 100000, end));
            data.elevation_list.reserve(estimated_lines);
        }

        // Single pass through the XML using SIMD-optimized search
        while (ptr < end) {
            // Find next '<' character using SIMD
            ptr = simd::find_char_avx2(ptr, end, '<');
            if (!ptr)
                break;

            ++ptr;  // Skip '<'

            // Check what tag we found
            if (starts_with(ptr, end, "gml:lowerCorner>")) {
                ptr = parse_double_pair(ptr + 16, end, data.lower_corner_x, data.lower_corner_y);
                data.has_lower_corner = true;
            } else if (starts_with(ptr, end, "gml:upperCorner>")) {
                ptr = parse_double_pair(ptr + 16, end, data.upper_corner_x, data.upper_corner_y);
                data.has_upper_corner = true;
            } else if (starts_with(ptr, end, "gml:low>")) {
                ptr = parse_int_pair(ptr + 8, end, data.grid_low_x, data.grid_low_y);
            } else if (starts_with(ptr, end, "gml:high>")) {
                ptr = parse_int_pair(ptr + 9, end, data.grid_high_x, data.grid_high_y);
                data.has_grid_envelope = true;  // Set after we have both low and high
            } else if (starts_with(ptr, end, "gml:startPoint>")) {
                ptr = parse_double_pair(ptr + 15, end, data.start_x, data.start_y);
                data.has_start_point = true;
            } else if (starts_with(ptr, end, "mesh>")) {
                ptr = parse_simple_text(ptr + 5, end, data.mesh_code);
                data.has_mesh_code = true;
            } else if (starts_with(ptr, end, "type>")) {
                ptr = parse_simple_text(ptr + 5, end, data.dem_type);
                data.has_dem_type = true;
            } else if (starts_with(ptr, end, "gml:tupleList>")) {
                ptr = parse_tuple_list(ptr + 14, end, data.elevation_list, sea_at_zero);
                data.has_tuple_list = true;
                // tupleList is usually last, can break early if we have everything
                if (data.has_lower_corner && data.has_upper_corner && data.has_grid_envelope &&
                    data.has_start_point) {
                    break;
                }
            }
        }

        return data;
    }

   private:
    /**
     * @brief Check if ptr starts with pattern
     */
    static bool starts_with(const char* ptr, const char* end, const char* pattern) {
        size_t len = std::strlen(pattern);
        return static_cast<size_t>(end - ptr) >= len && std::memcmp(ptr, pattern, len) == 0;
    }

    /**
     * @brief Parse two space-separated doubles (e.g., "35.0 139.0")
     */
    static const char* parse_double_pair(const char* ptr, const char* end, double& val1,
                                         double& val2) {
        // Skip whitespace using SIMD
        ptr = simd::skip_whitespace_avx2(ptr, end);

        // Find the end of content (before '<')
        const char* content_end = ptr;
        while (content_end < end && *content_end != '<') {
            ++content_end;
        }

        // Parse first double
        auto [p1, ec1] = std::from_chars(ptr, content_end, val1);
        if (ec1 != std::errc{})
            return content_end;

        // Skip whitespace between values
        ptr = p1;
        while (ptr < content_end && (*ptr == ' ' || *ptr == '\n' || *ptr == '\r' || *ptr == '\t')) {
            ++ptr;
        }

        // Parse second double
        auto [p2, ec2] = std::from_chars(ptr, content_end, val2);

        return content_end;
    }

    /**
     * @brief Parse two space-separated integers (e.g., "0 0")
     */
    static const char* parse_int_pair(const char* ptr, const char* end, int& val1, int& val2) {
        // Skip whitespace
        while (ptr < end && (*ptr == ' ' || *ptr == '\n' || *ptr == '\r' || *ptr == '\t')) {
            ++ptr;
        }

        // Find the end of content
        const char* content_end = ptr;
        while (content_end < end && *content_end != '<') {
            ++content_end;
        }

        // Parse first int
        auto [p1, ec1] = std::from_chars(ptr, content_end, val1);
        if (ec1 != std::errc{})
            return content_end;

        // Skip whitespace
        ptr = p1;
        while (ptr < content_end && (*ptr == ' ' || *ptr == '\n' || *ptr == '\r' || *ptr == '\t')) {
            ++ptr;
        }

        // Parse second int
        auto [p2, ec2] = std::from_chars(ptr, content_end, val2);

        return content_end;
    }

    /**
     * @brief Parse simple text content (e.g., mesh code)
     */
    static const char* parse_simple_text(const char* ptr, const char* end, std::string& result) {
        // Skip whitespace
        while (ptr < end && (*ptr == ' ' || *ptr == '\n' || *ptr == '\r' || *ptr == '\t')) {
            ++ptr;
        }

        // Find the end of content
        const char* content_start = ptr;
        const char* content_end = ptr;
        while (content_end < end && *content_end != '<') {
            ++content_end;
        }

        // Trim trailing whitespace
        while (content_end > content_start &&
               (*(content_end - 1) == ' ' || *(content_end - 1) == '\n' ||
                *(content_end - 1) == '\r' || *(content_end - 1) == '\t')) {
            --content_end;
        }

        result.assign(content_start, content_end - content_start);
        return content_end;
    }

    /**
     * @brief Parse tuple list (the elevation data)
     * Format: "地表面,586.18\n地表面,587.37\n..."
     */
    static const char* parse_tuple_list(const char* ptr, const char* end,
                                        std::vector<double>& elevation_list, bool sea_at_zero) {
        // Skip whitespace/newlines at start
        while (ptr < end && (*ptr == ' ' || *ptr == '\n' || *ptr == '\r' || *ptr == '\t')) {
            ++ptr;
        }

        while (ptr < end) {
            // Check for closing tag
            if (*ptr == '<') {
                break;
            }

            // Find comma (separator between type and value) using SIMD
            const char* comma = simd::find_char_avx2(ptr, end, ',');
            if (!comma || comma >= end)
                break;

            // Extract type for sea detection
            std::string_view type(ptr, comma - ptr);

            // Move past comma
            const char* value_start = comma + 1;

            // Find end of value (newline or '<')
            const char* value_end = value_start;
            while (value_end < end && *value_end != '\n' && *value_end != '<') {
                ++value_end;
            }

            // Parse the value using std::from_chars
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

        return ptr;
    }

    /**
     * @brief Check if type indicates a sea point
     */
    static bool is_sea_type(std::string_view type) {
        return type == "海水面" || type == "海水底面";
    }

    /**
     * @brief Count newlines for better pre-allocation
     */
    static size_t count_newlines_in_range(const char* start, const char* end) {
        size_t count = 0;
        const char* ptr = start;
        while ((ptr = static_cast<const char*>(std::memchr(ptr, '\n', end - ptr))) != nullptr) {
            ++count;
            ++ptr;
        }
        return count;
    }
};

}  // namespace fgd_converter::xml
