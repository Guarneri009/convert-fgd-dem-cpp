#pragma once

#include <algorithm>
#include <charconv>
#include <concepts>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fgd_converter::xml {

// XMLパース用コンセプト
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

    // ムーブのみ可能な型
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
     * @brief XMLテキストからタプルリストをパース
     *
     * @param text tupleListのXMLコンテンツ
     * @param sea_at_zero trueの場合、海域ポイントの-9999を0に置換
     * @return 標高値のベクター
     *
     * 入力フォーマット例:
     *   "その他,13.90\nその他,13.50\n海水面,-9999.\n"
     *
     * 出力:
     *   {13.90, 13.50, 0.0}  (sea_at_zero = true の場合)
     */
    static std::vector<double> parse(std::string_view text, bool sea_at_zero = true) {
        std::vector<double> elevation_list;

        // 推定行数に基づいて事前割り当て
        size_t estimated_lines = count_newlines(text);
        elevation_list.reserve(estimated_lines);

        const char* ptr = text.data();
        const char* end = text.data() + text.size();

        // 先頭の改行があればスキップ
        if (ptr < end && *ptr == '\n') {
            ++ptr;
        }

        while (ptr < end) {
            // 種別と値の間のカンマ区切りを検索
            const char* comma = ptr;
            while (comma < end && *comma != ',') {
                ++comma;
            }

            if (comma >= end)
                break;

            // 種別を抽出 (海域判定用)
            std::string_view type(ptr, comma - ptr);

            // カンマの次へ移動
            const char* value_start = comma + 1;

            // 値の終端を検索 (改行または文字列終端)
            const char* value_end = value_start;
            while (value_end < end && *value_end != '\n') {
                ++value_end;
            }

            // 値をパース
            double value;
#if defined(__APPLE__) || !defined(__cpp_lib_to_chars) || __cpp_lib_to_chars < 201611L
            // macOS/AppleClangは浮動小数点のstd::from_charsをサポートしない
            // フォールバックとしてstrtodを使用
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

        return elevation_list;
    }

   private:
    /**
     * @brief 種別が海域ポイントを示すか確認
     */
    static bool is_sea_type(std::string_view type) {
        // FGD DEMの一般的な海域種別
        return type == "海水面" || type == "海水底面";
    }

    /**
     * @brief より良い事前割り当てのために改行数をカウント
     */
    static size_t count_newlines(std::string_view text) {
        return std::count(text.begin(), text.end(), '\n');
    }
};

}  // namespace fgd_converter::xml