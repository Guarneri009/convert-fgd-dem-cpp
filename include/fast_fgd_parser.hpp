#pragma once

#include <algorithm>
#include <charconv>
#include <cstdlib>
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

        // 見つかったものを追跡するフラグ
        bool has_lower_corner = false;
        bool has_upper_corner = false;
        bool has_grid_envelope = false;
        bool has_start_point = false;
        bool has_mesh_code = false;
        bool has_dem_type = false;
        bool has_tuple_list = false;
    };

    /**
     * @brief FGD DEM XMLを1パスでパース
     *
     * @param xml 完全なXMLコンテンツ
     * @param sea_at_zero -9999の海域値を0に変換
     * @return パースされたデータ、または重大なエラー時にstd::nullopt
     */
    static auto parse_all(std::string_view xml,
                          bool sea_at_zero = true) -> std::optional<ParsedData> {
        ParsedData data;

        const char* ptr = xml.data();
        const char* end = xml.data() + xml.size();

        // パフォーマンス向上のため標高リストサイズを事前推定
        if (auto tuple_start = xml.find("<gml:tupleList>"); tuple_start != std::string_view::npos) {
            size_t estimated_lines = count_newlines_in_range(
                xml.data() + tuple_start, std::min(xml.data() + tuple_start + 100000, end));
            data.elevation_list.reserve(estimated_lines);
        }

        // SIMD最適化検索を使用してXMLを1パスで処理
        while (ptr < end) {
            // SIMDを使用して次の'<'文字を検索
            ptr = simd::find_char_avx2(ptr, end, '<');
            if (!ptr)
                break;

            ++ptr;  // '<'をスキップ

            // 見つかったタグを確認
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
                data.has_grid_envelope = true;  // lowとhighの両方が揃った後に設定
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
                // tupleListは通常最後なので、すべて揃っていれば早期終了可能
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
     * @brief ptrがpatternで始まるか確認
     */
    static bool starts_with(const char* ptr, const char* end, const char* pattern) {
        size_t len = std::strlen(pattern);
        return static_cast<size_t>(end - ptr) >= len && std::memcmp(ptr, pattern, len) == 0;
    }

    /**
     * @brief スペース区切りの2つのdoubleをパース (例: "35.0 139.0")
     */
    static const char* parse_double_pair(const char* ptr, const char* end, double& val1,
                                         double& val2) {
        // SIMDを使用して空白をスキップ
        ptr = simd::skip_whitespace_avx2(ptr, end);

        // コンテンツの終端を検索 ('<'の前)
        const char* content_end = ptr;
        while (content_end < end && *content_end != '<') {
            ++content_end;
        }

        // 最初のdoubleをパース
#if defined(__APPLE__) || !defined(__cpp_lib_to_chars) || __cpp_lib_to_chars < 201611L
        // macOS/AppleClangは浮動小数点のstd::from_charsをサポートしない
        std::string temp_str1(ptr, content_end - ptr);
        char* end_ptr1;
        val1 = std::strtod(temp_str1.c_str(), &end_ptr1);
        if (end_ptr1 == temp_str1.c_str())
            return content_end;
        ptr += (end_ptr1 - temp_str1.c_str());
#else
        // サポートするプラットフォーム (Linux/GCC) ではstd::from_charsを使用
        auto [p1, ec1] = std::from_chars(ptr, content_end, val1);
        if (ec1 != std::errc{})
            return content_end;
        ptr = p1;
#endif

        // 値の間の空白をスキップ
        while (ptr < content_end && (*ptr == ' ' || *ptr == '\n' || *ptr == '\r' || *ptr == '\t')) {
            ++ptr;
        }

        // 2番目のdoubleをパース
#if defined(__APPLE__) || !defined(__cpp_lib_to_chars) || __cpp_lib_to_chars < 201611L
        std::string temp_str2(ptr, content_end - ptr);
        char* end_ptr2;
        val2 = std::strtod(temp_str2.c_str(), &end_ptr2);
        if (end_ptr2 == temp_str2.c_str())
            return content_end;
#else
        auto [p2, ec2] = std::from_chars(ptr, content_end, val2);
        if (ec2 != std::errc{})
            return content_end;
#endif

        return content_end;
    }

    /**
     * @brief スペース区切りの2つのintをパース (例: "0 0")
     */
    static const char* parse_int_pair(const char* ptr, const char* end, int& val1, int& val2) {
        // 空白をスキップ
        while (ptr < end && (*ptr == ' ' || *ptr == '\n' || *ptr == '\r' || *ptr == '\t')) {
            ++ptr;
        }

        // コンテンツの終端を検索
        const char* content_end = ptr;
        while (content_end < end && *content_end != '<') {
            ++content_end;
        }

        // 最初のintをパース
        auto [p1, ec1] = std::from_chars(ptr, content_end, val1);
        if (ec1 != std::errc{})
            return content_end;

        // 空白をスキップ
        ptr = p1;
        while (ptr < content_end && (*ptr == ' ' || *ptr == '\n' || *ptr == '\r' || *ptr == '\t')) {
            ++ptr;
        }

        // 2番目のintをパース
        auto [p2, ec2] = std::from_chars(ptr, content_end, val2);
        if (ec2 != std::errc{})
            return content_end;

        return content_end;
    }

    /**
     * @brief シンプルなテキストコンテンツをパース (例: メッシュコード)
     */
    static const char* parse_simple_text(const char* ptr, const char* end, std::string& result) {
        // 空白をスキップ
        while (ptr < end && (*ptr == ' ' || *ptr == '\n' || *ptr == '\r' || *ptr == '\t')) {
            ++ptr;
        }

        // コンテンツの終端を検索
        const char* content_start = ptr;
        const char* content_end = ptr;
        while (content_end < end && *content_end != '<') {
            ++content_end;
        }

        // 末尾の空白をトリム
        while (content_end > content_start &&
               (*(content_end - 1) == ' ' || *(content_end - 1) == '\n' ||
                *(content_end - 1) == '\r' || *(content_end - 1) == '\t')) {
            --content_end;
        }

        result.assign(content_start, content_end - content_start);
        return content_end;
    }

    /**
     * @brief タプルリストをパース (標高データ)
     * フォーマット: "地表面,586.18\n地表面,587.37\n..."
     */
    static const char* parse_tuple_list(const char* ptr, const char* end,
                                        std::vector<double>& elevation_list, bool sea_at_zero) {
        // 先頭の空白/改行をスキップ
        while (ptr < end && (*ptr == ' ' || *ptr == '\n' || *ptr == '\r' || *ptr == '\t')) {
            ++ptr;
        }

        while (ptr < end) {
            // 閉じタグを確認
            if (*ptr == '<') {
                break;
            }

            // SIMDを使用してカンマ (種別と値の区切り) を検索
            const char* comma = simd::find_char_avx2(ptr, end, ',');
            if (!comma || comma >= end)
                break;

            // 海域判定用に種別を抽出
            std::string_view type(ptr, comma - ptr);

            // カンマの次へ移動
            const char* value_start = comma + 1;

            // 値の終端を検索 (改行または'<')
            const char* value_end = value_start;
            while (value_end < end && *value_end != '\n' && *value_end != '<') {
                ++value_end;
            }

            // 値をパース
            double value;
#if defined(__APPLE__) || !defined(__cpp_lib_to_chars) || __cpp_lib_to_chars < 201611L
            // macOS/AppleClangは浮動小数点のstd::from_charsをサポートしない
            std::string temp_str(value_start, value_end - value_start);
            char* end_ptr;
            value = std::strtod(temp_str.c_str(), &end_ptr);
            bool parse_ok = (end_ptr != temp_str.c_str());
#else
            // サポートするプラットフォーム (Linux/GCC) ではstd::from_charsを使用
            auto [p, ec] = std::from_chars(value_start, value_end, value);
            bool parse_ok = (ec == std::errc{});
#endif

            if (parse_ok) {
                // 必要に応じて海域ポイントを処理
                if (sea_at_zero && value <= -9999.0 && is_sea_type(type)) {
                    elevation_list.push_back(0.0);
                } else {
                    elevation_list.push_back(value);
                }
            } else {
                // パースエラー - デフォルト値をプッシュ
                elevation_list.push_back(-9999.0);
            }

            // 次の行へ移動
            ptr = value_end;
            if (ptr < end && *ptr == '\n') {
                ++ptr;
            }
        }

        return ptr;
    }

    /**
     * @brief 種別が海域ポイントを示すか確認
     */
    static bool is_sea_type(std::string_view type) {
        return type == "海水面" || type == "海水底面";
    }

    /**
     * @brief より良い事前割り当てのために改行数をカウント
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
