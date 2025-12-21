#include "geotiff.hpp"

#include <cpl_conv.h>
#include <gdal_priv.h>
#include <gdal_utils.h>
#include <gdalwarper.h>
#include <ogr_spatialref.h>

#include <algorithm>
#include <execution>
#include <iostream>

// SIMDイントリンシクスのプラットフォーム検出
#if defined(__x86_64__) || defined(_M_X64)
#    if defined(__AVX2__)
#        define HAS_AVX2 1
#        include <immintrin.h>
#    endif
#elif defined(__aarch64__) || defined(_M_ARM64)
#    define HAS_NEON 1
#    include <arm_neon.h>
#endif

namespace fgd_converter {

class GeoTiff::Impl {
   public:
    explicit Impl(const Config &config)
        : geo_transform(config.geo_transform),
          np_array(config.np_array.begin(), config.np_array.end()),
          x_length(config.x_length),
          y_length(config.y_length),
          output_path(config.output_path) {}

    std::array<double, 6> geo_transform;
    std::vector<std::vector<double>> np_array;
    int x_length;
    int y_length;
    std::filesystem::path output_path;
    GDALDataset *dataset = nullptr;
};

GeoTiff::GeoTiff(Config config) : pImpl(std::make_unique<Impl>(config)) {
    GDALAllRegister();
}

GeoTiff::~GeoTiff() {
    if (pImpl && pImpl->dataset) {
        GDALClose(pImpl->dataset);
    }
}

GeoTiff::GeoTiff(GeoTiff &&) noexcept = default;
GeoTiff &GeoTiff::operator=(GeoTiff &&) noexcept = default;

void GeoTiff::write_raster_bands(bool rgbify) {
    if (!pImpl->dataset)
        return;

    if (rgbify) {
        // Terrain RGB変換 (Mapbox形式)
        // 計算式: height = (R * 65536 + G * 256 + B) / 10 - 10000
        // 逆変換: offset_height = (height + 10000) * 10 = height * 10 + 100000
        constexpr double NO_DATA_VALUE = -9999.0;
        constexpr int R_MIN_HEIGHT = 65536;
        constexpr int G_MIN_HEIGHT = 256;

        auto convert_height_to_R = [](double height) -> uint8_t {
            if (height <= NO_DATA_VALUE) {
                return 1;  // NoData値
            }
            int offset_height = static_cast<int>(height * 10) + 100000;
            return static_cast<uint8_t>(offset_height / R_MIN_HEIGHT);
        };

        auto convert_height_to_G = [](double height, uint8_t r_value) -> uint8_t {
            if (height <= NO_DATA_VALUE) {
                return 134;  // NoData値
            }
            int offset_height = static_cast<int>(height * 10) + 100000;
            return static_cast<uint8_t>((offset_height - r_value * R_MIN_HEIGHT) / G_MIN_HEIGHT);
        };

        auto convert_height_to_B = [](double height, uint8_t r_value, uint8_t g_value) -> uint8_t {
            if (height <= NO_DATA_VALUE) {
                return 160;  // NoData値
            }
            int offset_height = static_cast<int>(height * 10) + 100000;
            return static_cast<uint8_t>(offset_height - r_value * R_MIN_HEIGHT -
                                        g_value * G_MIN_HEIGHT);
        };

        // Terrain RGBエンコーディングを使用してRGBバンドを作成
        std::vector<uint8_t> red_band(pImpl->x_length * pImpl->y_length);
        std::vector<uint8_t> green_band(pImpl->x_length * pImpl->y_length);
        std::vector<uint8_t> blue_band(pImpl->x_length * pImpl->y_length);

        for (int y = 0; y < pImpl->y_length; ++y) {
            for (int x = 0; x < pImpl->x_length; ++x) {
                int idx = y * pImpl->x_length + x;
                double height = pImpl->np_array[y][x];

                uint8_t r = convert_height_to_R(height);
                uint8_t g = convert_height_to_G(height, r);
                uint8_t b = convert_height_to_B(height, r, g);

                red_band[idx] = r;
                green_band[idx] = g;
                blue_band[idx] = b;
            }
        }

        CPLErr err1 = pImpl->dataset->GetRasterBand(1)->RasterIO(
            GF_Write, 0, 0, pImpl->x_length, pImpl->y_length, red_band.data(), pImpl->x_length,
            pImpl->y_length, GDT_Byte, 0, 0);
        CPLErr err2 = pImpl->dataset->GetRasterBand(2)->RasterIO(
            GF_Write, 0, 0, pImpl->x_length, pImpl->y_length, green_band.data(), pImpl->x_length,
            pImpl->y_length, GDT_Byte, 0, 0);
        CPLErr err3 = pImpl->dataset->GetRasterBand(3)->RasterIO(
            GF_Write, 0, 0, pImpl->x_length, pImpl->y_length, blue_band.data(), pImpl->x_length,
            pImpl->y_length, GDT_Byte, 0, 0);
        (void)err1;
        (void)err2;
        (void)err3;  // チェックしない場合の未使用変数警告を抑制
    } else {
        // 単一バンドfloat32 - 最適化された変換
        std::vector<float> flat_array(pImpl->x_length * pImpl->y_length);

        // SIMDまたは並列処理を使用してdoubleからfloatに変換
        size_t idx = 0;
        for (const auto &row : pImpl->np_array) {
            const size_t row_size = row.size();

#if defined(HAS_AVX2)
            // AVX2 SIMD変換 (一度に4つのdouble)
            size_t i = 0;
            for (; i + 4 <= row_size; i += 4) {
                __m256d src = _mm256_loadu_pd(&row[i]);
                __m128 dst = _mm256_cvtpd_ps(src);
                _mm_storeu_ps(&flat_array[idx + i], dst);
            }
            // 残りの要素を処理
            for (; i < row_size; ++i) {
                flat_array[idx + i] = static_cast<float>(row[i]);
            }
#elif defined(HAS_NEON)
            // NEON SIMD変換 (一度に2つのdouble)
            size_t i = 0;
            for (; i + 2 <= row_size; i += 2) {
                float64x2_t src = vld1q_f64(&row[i]);
                float32x2_t dst = vcvt_f32_f64(src);
                vst1_f32(&flat_array[idx + i], dst);
            }
            // 残りの要素を処理
            for (; i < row_size; ++i) {
                flat_array[idx + i] = static_cast<float>(row[i]);
            }
#else
            // 標準的な変換
            for (size_t i = 0; i < row_size; ++i) {
                flat_array[idx + i] = static_cast<float>(row[i]);
            }
#endif
            idx += row_size;
        }

        CPLErr err = pImpl->dataset->GetRasterBand(1)->RasterIO(
            GF_Write, 0, 0, pImpl->x_length, pImpl->y_length, flat_array.data(), pImpl->x_length,
            pImpl->y_length, GDT_Float32, 0, 0);
        (void)err;  // チェックしない場合の未使用変数警告を抑制

        // NoData値を設定
        pImpl->dataset->GetRasterBand(1)->SetNoDataValue(-9999.0);
    }
}

bool GeoTiff::create(std::string_view output_epsg, bool rgbify, std::error_code &ec) {
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        ec = std::make_error_code(std::errc::not_supported);
        return false;
    }

    char **options = nullptr;
    options = CSLSetNameValue(options, "COMPRESS", "DEFLATE");
    options = CSLSetNameValue(options, "TILED", "YES");
    options = CSLSetNameValue(options, "BLOCKXSIZE", "256");
    options = CSLSetNameValue(options, "BLOCKYSIZE", "256");

    int band_count = rgbify ? 3 : 1;
    GDALDataType data_type = rgbify ? GDT_Byte : GDT_Float32;

    pImpl->dataset = driver->Create(pImpl->output_path.string().c_str(), pImpl->x_length,
                                    pImpl->y_length, band_count, data_type, options);

    CSLDestroy(options);

    if (!pImpl->dataset) {
        ec = std::make_error_code(std::errc::io_error);
        return false;
    }

    // ジオ変換を設定
    pImpl->dataset->SetGeoTransform(pImpl->geo_transform.data());

    // 投影法を設定 - 常に最初はEPSG:4326を使用 (geo_transformは
    // 緯度/経度形式)
    OGRSpatialReference srs;
    srs.importFromEPSG(4326);

    char *wkt = nullptr;
    srs.exportToWkt(&wkt);
    pImpl->dataset->SetProjection(wkt);
    CPLFree(wkt);

    // ラスターバンドを書き込み
    write_raster_bands(rgbify);

    GDALClose(pImpl->dataset);
    pImpl->dataset = nullptr;

    return true;
}

bool GeoTiff::resampling(std::string_view output_epsg, std::error_code &ec) {
    // 一時出力パスを作成
    std::filesystem::path temp_path = pImpl->output_path;
    temp_path.replace_extension(".tmp.tif");

    // GDALWarpAppOptionsを使用してワープオプションを設定
    GDALWarpAppOptions *warp_opts = GDALWarpAppOptionsNew(nullptr, nullptr);

    // ソースと出力先のSRSを設定
    std::string src_srs = "EPSG:4326";
    std::string dst_srs = std::string(output_epsg);

    // ワープオプションを設定
    char *src_srs_str = const_cast<char *>(src_srs.c_str());
    char *dst_srs_str = const_cast<char *>(dst_srs.c_str());

    char **argv = nullptr;
    argv = CSLAddString(argv, "-s_srs");
    argv = CSLAddString(argv, src_srs_str);
    argv = CSLAddString(argv, "-t_srs");
    argv = CSLAddString(argv, dst_srs_str);
    argv = CSLAddString(argv, "-dstnodata");
    argv = CSLAddString(argv, "-9999");
    argv = CSLAddString(argv, "-r");
    argv = CSLAddString(argv, "bilinear");
    argv = CSLAddString(argv, "-of");
    argv = CSLAddString(argv, "GTiff");

    GDALWarpAppOptionsFree(warp_opts);
    warp_opts = GDALWarpAppOptionsNew(argv, nullptr);
    CSLDestroy(argv);

    // ソースデータセットを開く
    GDALDataset *src_datasets[1];
    src_datasets[0] =
        static_cast<GDALDataset *>(GDALOpen(pImpl->output_path.string().c_str(), GA_ReadOnly));

    if (!src_datasets[0]) {
        GDALWarpAppOptionsFree(warp_opts);
        ec = std::make_error_code(std::errc::io_error);
        return false;
    }

    // ワープを実行
    int error = 0;
    GDALDataset *warped_dataset = static_cast<GDALDataset *>(
        GDALWarp(temp_path.string().c_str(), nullptr, 1,
                 reinterpret_cast<GDALDatasetH *>(src_datasets), warp_opts, &error));

    GDALWarpAppOptionsFree(warp_opts);
    GDALClose(src_datasets[0]);

    if (!warped_dataset || error != 0) {
        if (warped_dataset)
            GDALClose(warped_dataset);
        ec = std::make_error_code(std::errc::operation_canceled);
        return false;
    }

    GDALClose(warped_dataset);

    // 元のファイルをリサンプリングしたファイルで置換
    std::filesystem::remove(pImpl->output_path);
    std::filesystem::rename(temp_path, pImpl->output_path);

    return true;
}

bool merge_tif_files(const MergeConfig &config, std::error_code &ec) {
    namespace fs = std::filesystem;

    if (!fs::exists(config.input_folder)) {
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return false;
    }

    // DEM種別に一致するTIFファイルを収集
    // パターン: *-DEM5A.tif または *DEM5A-*.tif
    std::vector<std::string> input_files;
    std::string latest_date;

    std::string pattern1 = "-DEM" + config.dem_type + ".tif";
    std::string pattern2 = "DEM" + config.dem_type + "-";

    for (const auto &entry : fs::recursive_directory_iterator(config.input_folder)) {
        if (!entry.is_regular_file())
            continue;

        std::string filename = entry.path().filename().string();
        if (filename.find(pattern1) != std::string::npos ||
            filename.find(pattern2) != std::string::npos) {
            input_files.push_back(entry.path().string());

            // 日付を抽出 (DEM5A-YYYYMMDD形式から)
            size_t pos = filename.find(pattern2);
            if (pos != std::string::npos) {
                size_t date_start = pos + pattern2.length();
                if (date_start + 8 <= filename.length()) {
                    std::string date = filename.substr(date_start, 8);
                    // 数字のみかチェック
                    bool is_date = true;
                    for (char c : date) {
                        if (!std::isdigit(c)) {
                            is_date = false;
                            break;
                        }
                    }
                    if (is_date && date > latest_date) {
                        latest_date = date;
                    }
                }
            }
        }
    }

    if (input_files.empty()) {
        std::cerr << "エラー: パターン *-DEM" << config.dem_type << ".tif または *DEM"
                  << config.dem_type << "-*.tif に一致するファイルが "
                  << config.input_folder.string() << " に見つかりません\n";
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return false;
    }

    std::cout << "マージ対象: " << input_files.size() << " ファイルが見つかりました\n";

    // 出力ファイル名を決定
    fs::path output_file = config.output_file;
    if (output_file.empty()) {
        if (!latest_date.empty()) {
            output_file = "FG-GML-merged-DEM" + config.dem_type + "-" + latest_date + ".tif";
        } else {
            output_file = "merged_output_" + std::to_string(static_cast<int>(config.resolution)) +
                          "m_" + config.dem_type + ".tif";
        }
    }

    // GDALWarpを使用してマージ
    GDALAllRegister();

    // 入力データセットを開く
    std::vector<GDALDatasetH> src_datasets;
    for (const auto &file : input_files) {
        GDALDatasetH ds = GDALOpen(file.c_str(), GA_ReadOnly);
        if (ds) {
            src_datasets.push_back(ds);
        } else {
            std::cerr << "警告: ファイルを開けませんでした: " << file << "\n";
        }
    }

    if (src_datasets.empty()) {
        ec = std::make_error_code(std::errc::io_error);
        return false;
    }

    // GDALWarpオプションを設定
    std::string res_str = std::to_string(config.resolution);

    std::vector<const char *> argv_vec;
    argv_vec.push_back("-multi");
    argv_vec.push_back("-wo");
    argv_vec.push_back("NUM_THREADS=ALL_CPUS");
    argv_vec.push_back("-tr");
    argv_vec.push_back(res_str.c_str());
    argv_vec.push_back(res_str.c_str());
    argv_vec.push_back("-r");
    argv_vec.push_back("bilinear");
    argv_vec.push_back("-overwrite");
    argv_vec.push_back("-srcnodata");
    argv_vec.push_back("-9999");
    argv_vec.push_back("-dstnodata");
    argv_vec.push_back("-9999");
    argv_vec.push_back("-co");
    argv_vec.push_back("BIGTIFF=YES");
    argv_vec.push_back("-co");
    argv_vec.push_back("COMPRESS=DEFLATE");
    argv_vec.push_back("-co");
    argv_vec.push_back("TILED=YES");
    argv_vec.push_back(nullptr);

    char **argv = const_cast<char **>(argv_vec.data());
    GDALWarpAppOptions *warp_opts = GDALWarpAppOptionsNew(argv, nullptr);

    if (!warp_opts) {
        for (auto ds : src_datasets)
            GDALClose(ds);
        ec = std::make_error_code(std::errc::invalid_argument);
        return false;
    }

    // マージを実行
    int error = 0;
    GDALDatasetH warped_dataset =
        GDALWarp(output_file.string().c_str(), nullptr, static_cast<int>(src_datasets.size()),
                 src_datasets.data(), warp_opts, &error);

    GDALWarpAppOptionsFree(warp_opts);
    for (auto ds : src_datasets)
        GDALClose(ds);

    if (!warped_dataset || error != 0) {
        if (warped_dataset)
            GDALClose(warped_dataset);
        std::cerr << "エラー: GDALWarpが失敗しました\n";
        ec = std::make_error_code(std::errc::io_error);
        return false;
    }

    GDALClose(warped_dataset);

    std::cout << "マージ完了。出力先: " << output_file.string() << "\n";
    return true;
}

}  // namespace fgd_converter