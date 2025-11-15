#include "geotiff.hpp"

#include <cpl_conv.h>
#include <gdal_priv.h>
#include <gdal_utils.h>
#include <gdalwarper.h>
#include <ogr_spatialref.h>

#include <algorithm>
#include <execution>
#include <iostream>

// SIMD intrinsics for x86_64
#if defined(__x86_64__) || defined(_M_X64)
#    include <immintrin.h>
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
        // Terrain RGB conversion (Mapbox format)
        // Formula: height = (R * 65536 + G * 256 + B) / 10 - 10000
        // Inverse: offset_height = (height + 10000) * 10 = height * 10 + 100000
        constexpr double NO_DATA_VALUE = -9999.0;
        constexpr int R_MIN_HEIGHT = 65536;
        constexpr int G_MIN_HEIGHT = 256;

        auto convert_height_to_R = [](double height) -> uint8_t {
            if (height <= NO_DATA_VALUE) {
                return 1;  // NoData value
            }
            int offset_height = static_cast<int>(height * 10) + 100000;
            return static_cast<uint8_t>(offset_height / R_MIN_HEIGHT);
        };

        auto convert_height_to_G = [](double height, uint8_t r_value) -> uint8_t {
            if (height <= NO_DATA_VALUE) {
                return 134;  // NoData value
            }
            int offset_height = static_cast<int>(height * 10) + 100000;
            return static_cast<uint8_t>((offset_height - r_value * R_MIN_HEIGHT) / G_MIN_HEIGHT);
        };

        auto convert_height_to_B = [](double height, uint8_t r_value, uint8_t g_value) -> uint8_t {
            if (height <= NO_DATA_VALUE) {
                return 160;  // NoData value
            }
            int offset_height = static_cast<int>(height * 10) + 100000;
            return static_cast<uint8_t>(offset_height - r_value * R_MIN_HEIGHT -
                                        g_value * G_MIN_HEIGHT);
        };

        // Create RGB bands using Terrain RGB encoding
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

        pImpl->dataset->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, pImpl->x_length, pImpl->y_length,
                                                   red_band.data(), pImpl->x_length,
                                                   pImpl->y_length, GDT_Byte, 0, 0);
        pImpl->dataset->GetRasterBand(2)->RasterIO(GF_Write, 0, 0, pImpl->x_length, pImpl->y_length,
                                                   green_band.data(), pImpl->x_length,
                                                   pImpl->y_length, GDT_Byte, 0, 0);
        pImpl->dataset->GetRasterBand(3)->RasterIO(GF_Write, 0, 0, pImpl->x_length, pImpl->y_length,
                                                   blue_band.data(), pImpl->x_length,
                                                   pImpl->y_length, GDT_Byte, 0, 0);
    } else {
        // Single band float32 - optimized conversion
        std::vector<float> flat_array(pImpl->x_length * pImpl->y_length);

        // Convert double to float using SIMD or parallel processing
        size_t idx = 0;
        for (const auto &row : pImpl->np_array) {
            const size_t row_size = row.size();

#if defined(__AVX2__) && defined(__x86_64__)
            // AVX2 SIMD conversion (4 doubles at a time)
            size_t i = 0;
            for (; i + 4 <= row_size; i += 4) {
                __m256d src = _mm256_loadu_pd(&row[i]);
                __m128 dst = _mm256_cvtpd_ps(src);
                _mm_storeu_ps(&flat_array[idx + i], dst);
            }
            // Handle remaining elements
            for (; i < row_size; ++i) {
                flat_array[idx + i] = static_cast<float>(row[i]);
            }
#else
            // Standard conversion
            for (size_t i = 0; i < row_size; ++i) {
                flat_array[idx + i] = static_cast<float>(row[i]);
            }
#endif
            idx += row_size;
        }

        pImpl->dataset->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, pImpl->x_length, pImpl->y_length,
                                                   flat_array.data(), pImpl->x_length,
                                                   pImpl->y_length, GDT_Float32, 0, 0);

        // Set NoData value
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

    // Set geotransform
    pImpl->dataset->SetGeoTransform(pImpl->geo_transform.data());

    // Set projection - ALWAYS use EPSG:4326 initially (geo_transform is in
    // lat/lon)
    OGRSpatialReference srs;
    srs.importFromEPSG(4326);

    char *wkt = nullptr;
    srs.exportToWkt(&wkt);
    pImpl->dataset->SetProjection(wkt);
    CPLFree(wkt);

    // Write raster bands
    write_raster_bands(rgbify);

    GDALClose(pImpl->dataset);
    pImpl->dataset = nullptr;

    return true;
}

bool GeoTiff::resampling(std::string_view output_epsg, std::error_code &ec) {
    // Create temporary output path
    std::filesystem::path temp_path = pImpl->output_path;
    temp_path.replace_extension(".tmp.tif");

    // Setup warp options using GDALWarpAppOptions
    GDALWarpAppOptions *warp_opts = GDALWarpAppOptionsNew(nullptr, nullptr);

    // Set source and destination SRS
    std::string src_srs = "EPSG:4326";
    std::string dst_srs = std::string(output_epsg);

    // Setup warp options
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

    // Open source dataset
    GDALDataset *src_datasets[1];
    src_datasets[0] =
        static_cast<GDALDataset *>(GDALOpen(pImpl->output_path.string().c_str(), GA_ReadOnly));

    if (!src_datasets[0]) {
        GDALWarpAppOptionsFree(warp_opts);
        ec = std::make_error_code(std::errc::io_error);
        return false;
    }

    // Perform the warp
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

    // Replace original with resampled
    std::filesystem::remove(pImpl->output_path);
    std::filesystem::rename(temp_path, pImpl->output_path);

    return true;
}

}  // namespace fgd_converter