#include <tbb/parallel_for_each.h>

#include <algorithm>
#include <cxxopts.hpp>
#include <iostream>
#include <mutex>
#include <sstream>

#include "converter.hpp"
#include "zip_handler.hpp"

namespace fs = std::filesystem;

void process_zip(const fs::path &zip_path, const fs::path &output_dir,
                 const std::string &output_epsg, bool rgbify, bool sea_at_zero) {
    std::cout << "Processing: " << zip_path.string() << "\n";

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
        ss << "Error processing " << zip_path.string() << ": " << ec.message();
        std::cerr << ss.str() << "\n";
    }
}

void extract_zip(const fs::path &zip_path, const fs::path &extract_to) {
    fgd_converter::zip::ZipHandler handler(zip_path);
    std::error_code ec;
    auto result = handler.extract(extract_to, ec);

    if (!result) {
        std::stringstream ss;
        ss << "Failed to extract " << zip_path.string() << ": " << ec.message();
        std::cerr << ss.str() << "\n";
    } else {
        std::stringstream ss;
        ss << "Extracted " << result->size() << " files from " << zip_path.string();
        std::cout << ss.str() << "\n";
    }
}

int main(int argc, char *argv[]) {
    cxxopts::Options options("convert_fgd_dem", "Convert FGD DEM XML to GeoTIFF");

    options.add_options()("i,input", "Input folder containing DEM zip files",
                          cxxopts::value<std::string>())(
        "o,output", "Output folder for GeoTIFF files",
        cxxopts::value<std::string>()->default_value("./output"))(
        "e,epsg", "Output EPSG code", cxxopts::value<std::string>()->default_value("EPSG:3857"))(
        "r,rgbify", "Convert to RGB for visualization",
        cxxopts::value<bool>()->default_value("false"))(
        "z,sea-at-zero", "Set sea level to zero", cxxopts::value<bool>()->default_value("false"))(
        "x,extract-only", "Only extract zip files", cxxopts::value<bool>()->default_value("false"))(
        "h,help", "Print usage");

    try {
        auto result = options.parse(argc, argv);

        if (result.count("help") || !result.count("input")) {
            std::cout << options.help() << std::endl;
            return 0;
        }

        fs::path input_folder = result["input"].as<std::string>();
        fs::path output_folder = result["output"].as<std::string>();
        fs::path extract_folder = "./extracted";
        std::string output_epsg = result["epsg"].as<std::string>();
        bool rgbify = result["rgbify"].as<bool>();
        bool sea_at_zero = result["sea-at-zero"].as<bool>();
        bool extract_only = result["extract-only"].as<bool>();

        if (!fs::exists(input_folder)) {
            std::stringstream ss;
            ss << "Input folder does not exist: " << input_folder.string();
            std::cerr << ss.str() << "\n";
            return 1;
        }

        // Create output directories
        fs::create_directories(output_folder);
        fs::create_directories(extract_folder);

        // First pass: collect all zip files
        std::vector<fs::path> zip_files;
        for (const auto &entry : fs::recursive_directory_iterator(input_folder)) {
            if (entry.is_regular_file() && fgd_converter::zip::is_zip_file(entry.path())) {
                zip_files.push_back(entry.path());
            }
        }

        // Extract all zip files in parallel
        std::cout << "Extracting " << zip_files.size() << " ZIP files in parallel...\n";
        tbb::parallel_for_each(zip_files, [&](const fs::path &zip_path) {
            std::stringstream ss;
            ss << "Extracting " << zip_path.string() << " → " << extract_folder.string();
            std::cout << ss.str() << "\n";
            extract_zip(zip_path, extract_folder);
        });

        if (extract_only) {
            std::cout << "Extraction complete.\n";
            return 0;
        }

        // Second pass: collect nested zips and process in parallel
        std::vector<fs::path> nested_zips;
        for (const auto &entry : fs::recursive_directory_iterator(extract_folder)) {
            if (entry.is_regular_file() && fgd_converter::zip::is_zip_file(entry.path())) {
                nested_zips.push_back(entry.path());
            }
        }

        // Process all zips in parallel using TBB (cross-platform)
        std::mutex cout_mutex;  // Protect std::cout from race conditions

        tbb::parallel_for_each(nested_zips, [&](const fs::path &zip_path) {
            fs::path output_tif = output_folder / zip_path.stem();
            output_tif.replace_extension(".tif");

            {
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::stringstream ss;
                ss << "Converting " << zip_path.filename().string() << " -> "
                   << output_tif.filename().string();
                std::cout << ss.str() << "\n";
            }

            process_zip(zip_path, output_folder, output_epsg, rgbify, sea_at_zero);
        });

        std::cout << "Conversion complete.\n";

    } catch (const cxxopts::exceptions::exception &e) {
        std::cerr << "Error parsing options: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}