#ifdef _WIN32
#include <windows.h>
#endif

#include <tbb/parallel_for_each.h>

#include <algorithm>
#include <cxxopts.hpp>
#include <iostream>
#include <mutex>
#include <sstream>

#include "converter.hpp"
#include "geotiff.hpp"
#include "zip_handler.hpp"

namespace fs = std::filesystem;

void process_zip(const fs::path &zip_path, const fs::path &output_dir,
                 const std::string &output_epsg, bool rgbify, bool sea_at_zero) {
    std::cout << "処理中: " << zip_path.string() << "\n";

    fgd_converter::Converter::Config config{.import_path = zip_path,
                                            .output_path = output_dir,
                                            .output_epsg = output_epsg,
                                            .file_name = std::nullopt,
                                            .rgbify = rgbify,
                                            .sea_at_zero = sea_at_zero};

    fgd_converter::Converter converter(config);
    std::error_code ec;
    if (!converter.run(ec)) {
        std::stringstream ss;
        ss << "処理エラー " << zip_path.string() << ": " << ec.message();
        std::cerr << ss.str() << "\n";
    }
}

void extract_zip(const fs::path &zip_path, const fs::path &extract_to) {
    fgd_converter::zip::ZipHandler handler(zip_path);
    std::error_code ec;
    auto result = handler.extract(extract_to, ec);

    if (!result) {
        std::stringstream ss;
        ss << "展開失敗 " << zip_path.string() << ": " << ec.message();
        std::cerr << ss.str() << "\n";
    } else {
        std::stringstream ss;
        ss << zip_path.string() << " から " << result->size() << " ファイルを展開しました";
        std::cout << ss.str() << "\n";
    }
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
    // Windowsコンソール出力をUTF-8に設定
    SetConsoleOutputCP(CP_UTF8);
#endif

    cxxopts::Options options("convert_fgd_dem", "基盤地図情報DEMをGeoTIFFに変換");

    options.add_options()("i,input", "DEM ZIPファイルが含まれる入力フォルダ",
                          cxxopts::value<std::string>())(
        "o,output", "GeoTIFFファイルの出力フォルダ",
        cxxopts::value<std::string>()->default_value("./output"))(
        "e,epsg", "出力EPSG座標系コード", cxxopts::value<std::string>()->default_value("EPSG:3857"))(
        "r,rgbify", "可視化用RGB変換を有効にする",
        cxxopts::value<bool>()->default_value("false"))(
        "z,sea-at-zero", "海面レベルを0に設定する", cxxopts::value<bool>()->default_value("false"))(
        "x,extract-only", "ZIPファイルの展開のみ実行する", cxxopts::value<bool>()->default_value("false"))(
        "m,merge", "DEM種別を指定してTIFファイルをマージ (例: 5A, 5B, 10A)",
        cxxopts::value<std::string>()->default_value(""))(
        "M,merge-only", "マージのみ実行（変換なし、-m と併用）",
        cxxopts::value<bool>()->default_value("false"))(
        "d,merge-dir", "マージ対象のTIFディレクトリ",
        cxxopts::value<std::string>()->default_value("./output"))(
        "t,resolution", "マージ時の出力解像度（メートル）",
        cxxopts::value<double>()->default_value("10.0"))("h,help", "ヘルプを表示する");

    try {
        auto result = options.parse(argc, argv);

        bool merge_only = result["merge-only"].as<bool>();
        std::string merge_dem_type = result["merge"].as<std::string>();

        // ヘルプ表示: -h または (-i なしかつマージのみモードでもない場合)
        if (result.count("help") || (!result.count("input") && !merge_only)) {
            std::cout << options.help() << std::endl;
            return 0;
        }

        // パスを正規化（末尾スラッシュ等を統一）
        fs::path output_folder = fs::path(result["output"].as<std::string>()).lexically_normal();
        fs::path merge_dir = fs::path(result["merge-dir"].as<std::string>()).lexically_normal();
        fs::path extract_folder = fs::path("./extracted").lexically_normal();
        std::string output_epsg = result["epsg"].as<std::string>();
        bool rgbify = result["rgbify"].as<bool>();
        bool sea_at_zero = result["sea-at-zero"].as<bool>();
        bool extract_only = result["extract-only"].as<bool>();
        double merge_resolution = result["resolution"].as<double>();

        // マージのみモード: -M オプションが指定された場合
        if (merge_only) {
            if (merge_dem_type.empty()) {
                std::cerr << "エラー: -M (--merge-only) を使用する場合は -m でDEM種別を指定してください\n";
                return 1;
            }

            fgd_converter::MergeConfig merge_config{
                .input_folder = merge_dir,  // -d で指定されたフォルダ（デフォルト: ./output）
                .dem_type = merge_dem_type,
                .resolution = merge_resolution,
                .output_file = {}  // 自動生成
            };

            std::error_code ec;
            if (!fgd_converter::merge_tif_files(merge_config, ec)) {
                std::cerr << "マージ失敗: " << ec.message() << "\n";
                return 1;
            }
            return 0;
        }

        // パスを正規化（末尾スラッシュ等を統一）
        fs::path input_folder = fs::path(result["input"].as<std::string>()).lexically_normal();

        if (!fs::exists(input_folder)) {
            std::stringstream ss;
            ss << "入力フォルダが存在しません: " << input_folder.string();
            std::cerr << ss.str() << "\n";
            return 1;
        }

        // 出力ディレクトリを作成
        fs::create_directories(output_folder);
        fs::create_directories(extract_folder);

        // 第1パス: すべてのzipファイルを収集
        std::vector<fs::path> zip_files;
        for (const auto &entry : fs::recursive_directory_iterator(input_folder)) {
            if (entry.is_regular_file() && fgd_converter::zip::is_zip_file(entry.path())) {
                zip_files.push_back(entry.path());
            }
        }

        // すべてのzipファイルを並列展開
        std::cout << zip_files.size() << " 個のZIPファイルを並列展開中...\n";
        tbb::parallel_for_each(zip_files, [&](const fs::path &zip_path) {
            std::stringstream ss;
            ss << "展開中: " << zip_path.string() << " → " << extract_folder.string();
            std::cout << ss.str() << "\n";
            extract_zip(zip_path, extract_folder);
        });

        if (extract_only) {
            std::cout << "展開完了。\n";
            return 0;
        }

        // 第2パス: ネストされたzipを収集して並列処理
        std::vector<fs::path> nested_zips;
        for (const auto &entry : fs::recursive_directory_iterator(extract_folder)) {
            if (entry.is_regular_file() && fgd_converter::zip::is_zip_file(entry.path())) {
                nested_zips.push_back(entry.path());
            }
        }

        // TBBを使用してすべてのzipを並列処理 (クロスプラットフォーム)
        std::mutex cout_mutex;  // std::coutを競合状態から保護

        tbb::parallel_for_each(nested_zips, [&](const fs::path &zip_path) {
            fs::path output_tif = output_folder / zip_path.stem();
            output_tif.replace_extension(".tif");

            {
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::stringstream ss;
                ss << "変換中: " << zip_path.filename().string() << " → "
                   << output_tif.filename().string();
                std::cout << ss.str() << "\n";
            }

            process_zip(zip_path, output_folder, output_epsg, rgbify, sea_at_zero);
        });

        std::cout << "変換完了。\n";

    } catch (const cxxopts::exceptions::exception &e) {
        std::cerr << "オプション解析エラー: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception &e) {
        std::cerr << "エラー: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}