#include "geotiff.hpp"

#include <geotiff.h>
#include <geotiffio.h>
#include <geo_normalize.h>
#include <geo_tiffp.h>
#include <proj.h>
#include <tiffio.h>
#include <xtiffio.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

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

// GDAL互換 NODATA タグ (42113) を libtiff に登録
#define TIFFTAG_GDAL_NODATA 42113

static const TIFFFieldInfo gdal_field_info[] = {
    {TIFFTAG_GDAL_NODATA, -1, -1, TIFF_ASCII, FIELD_CUSTOM, TRUE, FALSE,
     const_cast<char*>("GDALNoDataValue")}};

static TIFFExtendProc parent_extender = nullptr;

static void gdal_tiff_extender(TIFF* tif) {
    TIFFMergeFieldInfo(tif, gdal_field_info,
                       sizeof(gdal_field_info) / sizeof(gdal_field_info[0]));
    if (parent_extender) {
        (*parent_extender)(tif);
    }
}

static void register_gdal_nodata_tag() {
    static bool registered = false;
    if (!registered) {
        parent_extender = TIFFSetTagExtender(gdal_tiff_extender);
        registered = true;
    }
}

class GeoTiff::Impl {
   public:
    explicit Impl(const Config& config)
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
};

GeoTiff::GeoTiff(Config config) : pImpl(std::make_unique<Impl>(config)) {
    register_gdal_nodata_tag();
}

GeoTiff::~GeoTiff() = default;

GeoTiff::GeoTiff(GeoTiff&&) noexcept = default;
GeoTiff& GeoTiff::operator=(GeoTiff&&) noexcept = default;

void GeoTiff::write_raster_bands(bool rgbify) {
    // この関数はcreate()内で直接処理するため、空実装
}

bool GeoTiff::create(std::string_view output_epsg, bool rgbify, std::error_code& ec) {
    // 出力ディレクトリが存在しない場合は作成
    if (pImpl->output_path.has_parent_path()) {
        std::filesystem::create_directories(pImpl->output_path.parent_path());
    }

    // TIFFファイルを作成
    TIFF* tif = XTIFFOpen(pImpl->output_path.string().c_str(), "w");
    if (!tif) {
        ec = std::make_error_code(std::errc::io_error);
        return false;
    }

    const int nx = pImpl->x_length;
    const int ny = pImpl->y_length;

    // 基本TIFFタグを設定
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(nx));
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(ny));

    if (rgbify) {
        TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3);
        TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
        TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    } else {
        TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
        TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 32);
        TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_IEEEFP);
        TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
    }

    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);

    // タイル形式で圧縮
    const uint32_t tile_width = 256;
    const uint32_t tile_height = 256;
    TIFFSetField(tif, TIFFTAG_TILEWIDTH, tile_width);
    TIFFSetField(tif, TIFFTAG_TILELENGTH, tile_height);
    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_ADOBE_DEFLATE);

    // GeoTIFFハンドルを取得
    GTIF* gtif = GTIFNew(tif);
    if (!gtif) {
        XTIFFClose(tif);
        ec = std::make_error_code(std::errc::io_error);
        return false;
    }

    // geo_transform: [origin_x, pixel_width, 0, origin_y, 0, -pixel_height]
    double origin_x = pImpl->geo_transform[0];
    double pixel_width = pImpl->geo_transform[1];
    double origin_y = pImpl->geo_transform[3];
    double pixel_height = -pImpl->geo_transform[5];  // 負の値を正に

    // ModelPixelScaleTag: [ScaleX, ScaleY, ScaleZ]
    double pixel_scale[3] = {pixel_width, pixel_height, 0.0};
    TIFFSetField(tif, GTIFF_PIXELSCALE, 3, pixel_scale);

    // ModelTiepointTag: [I, J, K, X, Y, Z]
    double tiepoint[6] = {0.0, 0.0, 0.0, origin_x, origin_y, 0.0};
    TIFFSetField(tif, GTIFF_TIEPOINTS, 6, tiepoint);

    // GeoTIFFキーを設定 (EPSG:4326 = WGS84地理座標系)
    GTIFKeySet(gtif, GTModelTypeGeoKey, TYPE_SHORT, 1, ModelTypeGeographic);
    GTIFKeySet(gtif, GTRasterTypeGeoKey, TYPE_SHORT, 1, RasterPixelIsArea);
    GTIFKeySet(gtif, GeographicTypeGeoKey, TYPE_SHORT, 1, 4326);

    // GeoTIFFキーを書き込み
    GTIFWriteKeys(gtif);
    GTIFFree(gtif);

    // NODATA値タグを設定
    constexpr float NODATA_VALUE = -9999.0f;
    if (!rgbify) {
        std::string nodata_str = std::to_string(NODATA_VALUE);
        TIFFSetField(tif, TIFFTAG_GDAL_NODATA, nodata_str.c_str());
    }

    // タイルデータを書き込み
    if (rgbify) {
        // Terrain RGB変換 (Mapbox形式)
        constexpr double NO_DATA_VALUE = -9999.0;
        constexpr int R_MIN_HEIGHT = 65536;
        constexpr int G_MIN_HEIGHT = 256;

        auto convert_height_to_R = [](double height) -> uint8_t {
            if (height <= NO_DATA_VALUE) {
                return 1;
            }
            int offset_height = static_cast<int>(height * 10) + 100000;
            return static_cast<uint8_t>(offset_height / R_MIN_HEIGHT);
        };

        auto convert_height_to_G = [](double height, uint8_t r_value) -> uint8_t {
            if (height <= NO_DATA_VALUE) {
                return 134;
            }
            int offset_height = static_cast<int>(height * 10) + 100000;
            return static_cast<uint8_t>((offset_height - r_value * R_MIN_HEIGHT) / G_MIN_HEIGHT);
        };

        auto convert_height_to_B = [](double height, uint8_t r_value, uint8_t g_value) -> uint8_t {
            if (height <= NO_DATA_VALUE) {
                return 160;
            }
            int offset_height = static_cast<int>(height * 10) + 100000;
            return static_cast<uint8_t>(offset_height - r_value * R_MIN_HEIGHT -
                                        g_value * G_MIN_HEIGHT);
        };

        std::vector<uint8_t> tile_buffer(tile_width * tile_height * 3);

        for (uint32_t ty = 0; ty < static_cast<uint32_t>(ny); ty += tile_height) {
            for (uint32_t tx = 0; tx < static_cast<uint32_t>(nx); tx += tile_width) {
                std::fill(tile_buffer.begin(), tile_buffer.end(), 0);

                uint32_t actual_tile_width = std::min(tile_width, static_cast<uint32_t>(nx) - tx);
                uint32_t actual_tile_height = std::min(tile_height, static_cast<uint32_t>(ny) - ty);

                for (uint32_t row = 0; row < actual_tile_height; ++row) {
                    for (uint32_t col = 0; col < actual_tile_width; ++col) {
                        double height = pImpl->np_array[ty + row][tx + col];
                        uint8_t r = convert_height_to_R(height);
                        uint8_t g = convert_height_to_G(height, r);
                        uint8_t b = convert_height_to_B(height, r, g);

                        size_t dst_idx = (static_cast<size_t>(row) * tile_width + col) * 3;
                        tile_buffer[dst_idx] = r;
                        tile_buffer[dst_idx + 1] = g;
                        tile_buffer[dst_idx + 2] = b;
                    }
                }

                if (TIFFWriteTile(tif, tile_buffer.data(), tx, ty, 0, 0) < 0) {
                    XTIFFClose(tif);
                    ec = std::make_error_code(std::errc::io_error);
                    return false;
                }
            }
        }
    } else {
        // Float32データ
        std::vector<float> tile_buffer(tile_width * tile_height);

        for (uint32_t ty = 0; ty < static_cast<uint32_t>(ny); ty += tile_height) {
            for (uint32_t tx = 0; tx < static_cast<uint32_t>(nx); tx += tile_width) {
                std::fill(tile_buffer.begin(), tile_buffer.end(), NODATA_VALUE);

                uint32_t actual_tile_width = std::min(tile_width, static_cast<uint32_t>(nx) - tx);
                uint32_t actual_tile_height = std::min(tile_height, static_cast<uint32_t>(ny) - ty);

                for (uint32_t row = 0; row < actual_tile_height; ++row) {
                    for (uint32_t col = 0; col < actual_tile_width; ++col) {
                        size_t dst_idx = static_cast<size_t>(row) * tile_width + col;
                        tile_buffer[dst_idx] = static_cast<float>(pImpl->np_array[ty + row][tx + col]);
                    }
                }

                if (TIFFWriteTile(tif, tile_buffer.data(), tx, ty, 0, 0) < 0) {
                    XTIFFClose(tif);
                    ec = std::make_error_code(std::errc::io_error);
                    return false;
                }
            }
        }
    }

    XTIFFClose(tif);

    pImpl->np_array.clear();
    pImpl->np_array.shrink_to_fit();

    return true;
}

// GeoTIFFデータ構造体
struct GeoTiffData {
    std::vector<float> data;
    int width;
    int height;
    double geo_transform[6];  // [x_origin, pixel_width, 0, y_origin, 0, -pixel_height]
    int epsg;
    float nodata_value;
    bool has_nodata;
};

// GeoTIFFを読み込むヘルパー関数
static bool read_geotiff(const std::filesystem::path& path, GeoTiffData& result) {
    TIFF* tif = XTIFFOpen(path.string().c_str(), "r");
    if (!tif) {
        return false;
    }

    uint32_t width, height;
    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);

    result.width = static_cast<int>(width);
    result.height = static_cast<int>(height);

    // GeoTIFF情報を読み込み
    GTIF* gtif = GTIFNew(tif);
    if (gtif) {
        short model_type = 0;
        GTIFKeyGet(gtif, GTModelTypeGeoKey, &model_type, 0, 1);

        short projected_cs = 0;
        short geographic_cs = 0;
        if (GTIFKeyGet(gtif, ProjectedCSTypeGeoKey, &projected_cs, 0, 1)) {
            result.epsg = projected_cs;
        } else if (GTIFKeyGet(gtif, GeographicTypeGeoKey, &geographic_cs, 0, 1)) {
            result.epsg = geographic_cs;
        } else {
            result.epsg = 0;
        }
        GTIFFree(gtif);
    }

    // PixelScaleとTiepointを読み込み
    double* pixel_scale = nullptr;
    double* tiepoints = nullptr;
    uint16_t count = 0;

    result.geo_transform[0] = 0.0;   // x_origin
    result.geo_transform[1] = 1.0;   // pixel_width
    result.geo_transform[2] = 0.0;   // rotation
    result.geo_transform[3] = 0.0;   // y_origin
    result.geo_transform[4] = 0.0;   // rotation
    result.geo_transform[5] = -1.0;  // -pixel_height

    if (TIFFGetField(tif, GTIFF_PIXELSCALE, &count, &pixel_scale) && count >= 2) {
        result.geo_transform[1] = pixel_scale[0];
        result.geo_transform[5] = -pixel_scale[1];
    }

    if (TIFFGetField(tif, GTIFF_TIEPOINTS, &count, &tiepoints) && count >= 6) {
        result.geo_transform[0] = tiepoints[3];
        result.geo_transform[3] = tiepoints[4];
    }

    // NODATA値を読み込み
    result.has_nodata = false;
    result.nodata_value = std::numeric_limits<float>::quiet_NaN();
    char* nodata_str = nullptr;
    if (TIFFGetField(tif, TIFFTAG_GDAL_NODATA, &nodata_str) && nodata_str) {
        result.nodata_value = static_cast<float>(std::atof(nodata_str));
        result.has_nodata = true;
    }

    // ラスターデータを読み込み
    result.data.resize(static_cast<size_t>(width) * height);

    // タイル形式かストリップ形式かを判定
    if (TIFFIsTiled(tif)) {
        uint32_t tile_width, tile_height;
        TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tile_width);
        TIFFGetField(tif, TIFFTAG_TILELENGTH, &tile_height);

        std::vector<float> tile_buffer(tile_width * tile_height);

        for (uint32_t ty = 0; ty < height; ty += tile_height) {
            for (uint32_t tx = 0; tx < width; tx += tile_width) {
                if (TIFFReadTile(tif, tile_buffer.data(), tx, ty, 0, 0) < 0) {
                    XTIFFClose(tif);
                    return false;
                }

                uint32_t actual_tile_width = std::min(tile_width, width - tx);
                uint32_t actual_tile_height = std::min(tile_height, height - ty);

                for (uint32_t row = 0; row < actual_tile_height; ++row) {
                    for (uint32_t col = 0; col < actual_tile_width; ++col) {
                        size_t src_idx = static_cast<size_t>(row) * tile_width + col;
                        size_t dst_idx = static_cast<size_t>(ty + row) * width + (tx + col);
                        result.data[dst_idx] = tile_buffer[src_idx];
                    }
                }
            }
        }
    } else {
        // ストリップ形式
        for (uint32_t row = 0; row < height; ++row) {
            if (TIFFReadScanline(tif, result.data.data() + row * width, row, 0) < 0) {
                XTIFFClose(tif);
                return false;
            }
        }
    }

    XTIFFClose(tif);
    return true;
}

// GeoTIFFを書き込むヘルパー関数
static bool write_geotiff(const std::filesystem::path& path, const GeoTiffData& data) {
    TIFF* tif = XTIFFOpen(path.string().c_str(), "w");
    if (!tif) {
        return false;
    }

    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(data.width));
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(data.height));
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 32);
    TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_IEEEFP);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);

    const uint32_t tile_width = 256;
    const uint32_t tile_height = 256;
    TIFFSetField(tif, TIFFTAG_TILEWIDTH, tile_width);
    TIFFSetField(tif, TIFFTAG_TILELENGTH, tile_height);
    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_LZW);

    GTIF* gtif = GTIFNew(tif);
    if (!gtif) {
        XTIFFClose(tif);
        return false;
    }

    double pixel_scale[3] = {data.geo_transform[1], -data.geo_transform[5], 0.0};
    TIFFSetField(tif, GTIFF_PIXELSCALE, 3, pixel_scale);

    double tiepoint[6] = {0.0, 0.0, 0.0, data.geo_transform[0], data.geo_transform[3], 0.0};
    TIFFSetField(tif, GTIFF_TIEPOINTS, 6, tiepoint);

    GTIFKeySet(gtif, GTModelTypeGeoKey, TYPE_SHORT, 1, ModelTypeProjected);
    GTIFKeySet(gtif, GTRasterTypeGeoKey, TYPE_SHORT, 1, RasterPixelIsArea);
    if (data.epsg > 0) {
        GTIFKeySet(gtif, ProjectedCSTypeGeoKey, TYPE_SHORT, 1, data.epsg);
    }

    GTIFWriteKeys(gtif);
    GTIFFree(gtif);

    if (data.has_nodata) {
        std::string nodata_str = std::to_string(data.nodata_value);
        TIFFSetField(tif, TIFFTAG_GDAL_NODATA, nodata_str.c_str());
    }

    std::vector<float> tile_buffer(tile_width * tile_height);
    float fill_value = data.has_nodata ? data.nodata_value : 0.0f;

    for (uint32_t ty = 0; ty < static_cast<uint32_t>(data.height); ty += tile_height) {
        for (uint32_t tx = 0; tx < static_cast<uint32_t>(data.width); tx += tile_width) {
            std::fill(tile_buffer.begin(), tile_buffer.end(), fill_value);

            uint32_t actual_tile_width =
                std::min(tile_width, static_cast<uint32_t>(data.width) - tx);
            uint32_t actual_tile_height =
                std::min(tile_height, static_cast<uint32_t>(data.height) - ty);

            for (uint32_t row = 0; row < actual_tile_height; ++row) {
                for (uint32_t col = 0; col < actual_tile_width; ++col) {
                    size_t src_idx = static_cast<size_t>(ty + row) * data.width + (tx + col);
                    size_t dst_idx = static_cast<size_t>(row) * tile_width + col;
                    tile_buffer[dst_idx] = data.data[src_idx];
                }
            }

            if (TIFFWriteTile(tif, tile_buffer.data(), tx, ty, 0, 0) < 0) {
                XTIFFClose(tif);
                return false;
            }
        }
    }

    XTIFFClose(tif);
    return true;
}

bool GeoTiff::resampling(std::string_view output_epsg, std::error_code& ec) {
    register_gdal_nodata_tag();

    // 入力GeoTIFFを読み込み
    GeoTiffData src_data;
    if (!read_geotiff(pImpl->output_path, src_data)) {
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return false;
    }

    // PROJコンテキストを作成
    PJ_CONTEXT* ctx = proj_context_create();
    if (!ctx) {
        ec = std::make_error_code(std::errc::not_enough_memory);
        return false;
    }

    // ソースCRSを構築 (EPSG:4326)
    std::string src_crs = "EPSG:4326";
    std::string dst_crs = std::string(output_epsg);

    // 座標変換オブジェクトを作成
    PJ* src_pj = proj_create(ctx, src_crs.c_str());
    PJ* dst_pj = proj_create(ctx, dst_crs.c_str());

    if (!src_pj || !dst_pj) {
        if (src_pj) proj_destroy(src_pj);
        if (dst_pj) proj_destroy(dst_pj);
        proj_context_destroy(ctx);
        ec = std::make_error_code(std::errc::invalid_argument);
        return false;
    }

    // CRSが同じかチェック
    if (proj_is_equivalent_to(src_pj, dst_pj, PJ_COMP_EQUIVALENT)) {
        proj_destroy(src_pj);
        proj_destroy(dst_pj);
        proj_context_destroy(ctx);
        return true;  // 変換不要
    }

    PJ* transform = proj_create_crs_to_crs(ctx, src_crs.c_str(), dst_crs.c_str(), nullptr);
    if (!transform) {
        proj_destroy(src_pj);
        proj_destroy(dst_pj);
        proj_context_destroy(ctx);
        ec = std::make_error_code(std::errc::invalid_argument);
        return false;
    }

    // 正規化された変換を取得
    PJ* norm_transform = proj_normalize_for_visualization(ctx, transform);
    if (norm_transform) {
        proj_destroy(transform);
        transform = norm_transform;
    }

    // ソース画像の四隅を変換してバウンディングボックスを計算
    double src_corners[4][2] = {
        {src_data.geo_transform[0], src_data.geo_transform[3]},
        {src_data.geo_transform[0] + src_data.width * src_data.geo_transform[1],
         src_data.geo_transform[3]},
        {src_data.geo_transform[0],
         src_data.geo_transform[3] + src_data.height * src_data.geo_transform[5]},
        {src_data.geo_transform[0] + src_data.width * src_data.geo_transform[1],
         src_data.geo_transform[3] + src_data.height * src_data.geo_transform[5]}};

    double dst_min_x = std::numeric_limits<double>::max();
    double dst_max_x = std::numeric_limits<double>::lowest();
    double dst_min_y = std::numeric_limits<double>::max();
    double dst_max_y = std::numeric_limits<double>::lowest();

    for (int i = 0; i < 4; ++i) {
        PJ_COORD src_coord = proj_coord(src_corners[i][0], src_corners[i][1], 0, 0);
        PJ_COORD dst_coord = proj_trans(transform, PJ_FWD, src_coord);

        dst_min_x = std::min(dst_min_x, dst_coord.xy.x);
        dst_max_x = std::max(dst_max_x, dst_coord.xy.x);
        dst_min_y = std::min(dst_min_y, dst_coord.xy.y);
        dst_max_y = std::max(dst_max_y, dst_coord.xy.y);
    }

    // 出力解像度を計算
    double src_pixel_width = src_data.geo_transform[1];
    double src_pixel_height = -src_data.geo_transform[5];

    // メートル単位の解像度を推定（緯度1度 ≈ 111km）
    double dst_pixel_width = src_pixel_width * 111000.0;
    double dst_pixel_height = src_pixel_height * 111000.0;

    int dst_width = static_cast<int>(std::ceil((dst_max_x - dst_min_x) / dst_pixel_width));
    int dst_height = static_cast<int>(std::ceil((dst_max_y - dst_min_y) / dst_pixel_height));

    // サイズが0以下にならないようにする
    dst_width = std::max(dst_width, 1);
    dst_height = std::max(dst_height, 1);

    // 逆変換を作成
    PJ* inv_transform = proj_create_crs_to_crs(ctx, dst_crs.c_str(), src_crs.c_str(), nullptr);
    if (!inv_transform) {
        proj_destroy(transform);
        proj_destroy(src_pj);
        proj_destroy(dst_pj);
        proj_context_destroy(ctx);
        ec = std::make_error_code(std::errc::invalid_argument);
        return false;
    }

    PJ* norm_inv = proj_normalize_for_visualization(ctx, inv_transform);
    if (norm_inv) {
        proj_destroy(inv_transform);
        inv_transform = norm_inv;
    }

    // 出力データを初期化
    GeoTiffData dst_data;
    dst_data.width = dst_width;
    dst_data.height = dst_height;
    dst_data.data.resize(static_cast<size_t>(dst_width) * dst_height, src_data.nodata_value);
    dst_data.geo_transform[0] = dst_min_x;
    dst_data.geo_transform[1] = dst_pixel_width;
    dst_data.geo_transform[2] = 0.0;
    dst_data.geo_transform[3] = dst_max_y;
    dst_data.geo_transform[4] = 0.0;
    dst_data.geo_transform[5] = -dst_pixel_height;
    dst_data.nodata_value = src_data.nodata_value;
    dst_data.has_nodata = src_data.has_nodata;

    // 出力CRSからEPSGコードを抽出
    dst_data.epsg = 0;
    if (dst_crs.find("EPSG:") == 0) {
        dst_data.epsg = std::stoi(dst_crs.substr(5));
    }

    // バイリニア補間で再投影
    for (int dst_row = 0; dst_row < dst_height; ++dst_row) {
        for (int dst_col = 0; dst_col < dst_width; ++dst_col) {
            double dst_x = dst_min_x + (dst_col + 0.5) * dst_pixel_width;
            double dst_y = dst_max_y - (dst_row + 0.5) * dst_pixel_height;

            PJ_COORD dst_coord = proj_coord(dst_x, dst_y, 0, 0);
            PJ_COORD src_coord = proj_trans(inv_transform, PJ_FWD, dst_coord);

            double src_col_f =
                (src_coord.xy.x - src_data.geo_transform[0]) / src_data.geo_transform[1] - 0.5;
            double src_row_f =
                (src_data.geo_transform[3] - src_coord.xy.y) / (-src_data.geo_transform[5]) - 0.5;

            // バイリニア補間
            int src_col0 = static_cast<int>(std::floor(src_col_f));
            int src_row0 = static_cast<int>(std::floor(src_row_f));
            int src_col1 = src_col0 + 1;
            int src_row1 = src_row0 + 1;

            if (src_col0 >= 0 && src_col1 < src_data.width && src_row0 >= 0 &&
                src_row1 < src_data.height) {
                double dx = src_col_f - src_col0;
                double dy = src_row_f - src_row0;

                float v00 = src_data.data[static_cast<size_t>(src_row0) * src_data.width + src_col0];
                float v01 = src_data.data[static_cast<size_t>(src_row0) * src_data.width + src_col1];
                float v10 = src_data.data[static_cast<size_t>(src_row1) * src_data.width + src_col0];
                float v11 = src_data.data[static_cast<size_t>(src_row1) * src_data.width + src_col1];

                // NODATAチェック
                if (src_data.has_nodata) {
                    if (v00 == src_data.nodata_value || v01 == src_data.nodata_value ||
                        v10 == src_data.nodata_value || v11 == src_data.nodata_value) {
                        continue;  // NODATAのままにする
                    }
                }

                float value = static_cast<float>((1 - dx) * (1 - dy) * v00 + dx * (1 - dy) * v01 +
                                                  (1 - dx) * dy * v10 + dx * dy * v11);
                dst_data.data[static_cast<size_t>(dst_row) * dst_width + dst_col] = value;
            }
        }
    }

    // 変換オブジェクトを解放
    proj_destroy(transform);
    proj_destroy(inv_transform);
    proj_destroy(src_pj);
    proj_destroy(dst_pj);
    proj_context_destroy(ctx);

    // 一時ファイルに書き込み
    std::filesystem::path temp_path = pImpl->output_path;
    temp_path.replace_extension(".tmp.tif");

    if (!write_geotiff(temp_path, dst_data)) {
        ec = std::make_error_code(std::errc::io_error);
        return false;
    }

    // 元のファイルを置換
    std::filesystem::remove(pImpl->output_path);
    std::filesystem::rename(temp_path, pImpl->output_path);

    return true;
}

bool merge_tif_files(const MergeConfig& config, std::error_code& ec) {
    register_gdal_nodata_tag();
    namespace fs = std::filesystem;

    if (!fs::exists(config.input_folder)) {
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return false;
    }

    // DEM種別に一致するTIFファイルを収集
    std::vector<std::string> input_files;
    std::string latest_date;

    std::string pattern1 = "-DEM" + config.dem_type + ".tif";
    std::string pattern2 = "DEM" + config.dem_type + "-";

    for (const auto& entry : fs::recursive_directory_iterator(config.input_folder)) {
        if (!entry.is_regular_file())
            continue;

        std::string filename = entry.path().filename().string();
        if (filename.find(pattern1) != std::string::npos ||
            filename.find(pattern2) != std::string::npos) {
            input_files.push_back(entry.path().string());

            size_t pos = filename.find(pattern2);
            if (pos != std::string::npos) {
                size_t date_start = pos + pattern2.length();
                if (date_start + 8 <= filename.length()) {
                    std::string date = filename.substr(date_start, 8);
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

    // 全てのGeoTIFFを読み込み、バウンディングボックスを計算
    std::vector<GeoTiffData> datasets;
    datasets.reserve(input_files.size());

    double min_x = std::numeric_limits<double>::max();
    double max_x = std::numeric_limits<double>::lowest();
    double min_y = std::numeric_limits<double>::max();
    double max_y = std::numeric_limits<double>::lowest();
    double pixel_width = 0.0;
    double pixel_height = 0.0;
    int epsg = 0;
    float nodata_value = -9999.0f;

    for (const auto& tiff : input_files) {
        GeoTiffData data;
        if (!read_geotiff(tiff, data)) {
            std::cerr << "ファイルを開けませんでした: " << tiff << std::endl;
            ec = std::make_error_code(std::errc::io_error);
            return false;
        }

        std::cout << "読み込み: " << tiff << std::endl;

        double x0 = data.geo_transform[0];
        double y0 = data.geo_transform[3];
        double x1 = x0 + data.width * data.geo_transform[1];
        double y1 = y0 + data.height * data.geo_transform[5];

        min_x = std::min(min_x, std::min(x0, x1));
        max_x = std::max(max_x, std::max(x0, x1));
        min_y = std::min(min_y, std::min(y0, y1));
        max_y = std::max(max_y, std::max(y0, y1));

        if (pixel_width == 0.0) {
            pixel_width = data.geo_transform[1];
            pixel_height = -data.geo_transform[5];
            epsg = data.epsg;
            if (data.has_nodata) {
                nodata_value = data.nodata_value;
            }
        }

        datasets.push_back(std::move(data));
    }

    // 解像度の調整（メートル単位の場合）
    if (config.resolution > 0) {
        // 既存のピクセルサイズがメートル単位か度単位か判断
        // EPSG:3857等の投影座標系はメートル単位
        if (epsg == 3857 || epsg == 2451 || (epsg >= 32601 && epsg <= 32660) ||
            (epsg >= 32701 && epsg <= 32760)) {
            pixel_width = config.resolution;
            pixel_height = config.resolution;
        }
    }

    // 出力サイズを計算
    int out_width = static_cast<int>(std::ceil((max_x - min_x) / pixel_width));
    int out_height = static_cast<int>(std::ceil((max_y - min_y) / pixel_height));

    out_width = std::max(out_width, 1);
    out_height = std::max(out_height, 1);

    // 出力データを初期化
    GeoTiffData output;
    output.width = out_width;
    output.height = out_height;
    output.data.resize(static_cast<size_t>(out_width) * out_height, nodata_value);
    output.geo_transform[0] = min_x;
    output.geo_transform[1] = pixel_width;
    output.geo_transform[2] = 0.0;
    output.geo_transform[3] = max_y;
    output.geo_transform[4] = 0.0;
    output.geo_transform[5] = -pixel_height;
    output.epsg = epsg;
    output.nodata_value = nodata_value;
    output.has_nodata = true;

    // 各データセットをマージ
    for (const auto& src : datasets) {
        double src_x0 = src.geo_transform[0];
        double src_y0 = src.geo_transform[3];

        int dst_col_start = static_cast<int>(std::round((src_x0 - min_x) / pixel_width));
        int dst_row_start = static_cast<int>(std::round((max_y - src_y0) / pixel_height));

        for (int row = 0; row < src.height; ++row) {
            for (int col = 0; col < src.width; ++col) {
                int dst_row = dst_row_start + row;
                int dst_col = dst_col_start + col;

                if (dst_row >= 0 && dst_row < out_height && dst_col >= 0 && dst_col < out_width) {
                    float value = src.data[static_cast<size_t>(row) * src.width + col];

                    if (!src.has_nodata || value != src.nodata_value) {
                        output.data[static_cast<size_t>(dst_row) * out_width + dst_col] = value;
                    }
                }
            }
        }
    }

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

    // 出力ファイルを書き込み
    if (!write_geotiff(output_file, output)) {
        ec = std::make_error_code(std::errc::io_error);
        return false;
    }

    std::cout << "マージ完了。出力先: " << output_file.string() << "\n";
    return true;
}

}  // namespace fgd_converter
