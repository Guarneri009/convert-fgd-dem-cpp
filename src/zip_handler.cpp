#include "zip_handler.hpp"

#include <zip.h>

#include <algorithm>
#include <fstream>

namespace fgd_converter::zip {

class ZipHandler::Impl {
   public:
    explicit Impl(const std::filesystem::path &zip_path) : zip_path_(zip_path) {}

    std::filesystem::path zip_path_;
};

ZipHandler::ZipHandler(std::filesystem::path zip_path) : pImpl(std::make_unique<Impl>(zip_path)) {}

ZipHandler::~ZipHandler() = default;
ZipHandler::ZipHandler(ZipHandler &&) noexcept = default;
ZipHandler &ZipHandler::operator=(ZipHandler &&) noexcept = default;

auto ZipHandler::extract(const std::filesystem::path &output_dir,
                         std::error_code &ec) -> std::optional<std::vector<std::filesystem::path>> {
    // Convert to absolute path with native separators for cross-platform compatibility
    auto abs_zip_path = std::filesystem::absolute(pImpl->zip_path_).make_preferred();
    auto abs_output_dir = std::filesystem::absolute(output_dir).make_preferred();

    int err = 0;
    zip_t *z = zip_open(abs_zip_path.string().c_str(), 0, &err);
    if (!z) {
        ec = std::make_error_code(std::errc::io_error);
        return std::nullopt;
    }

    std::vector<std::filesystem::path> extracted_files;
    zip_int64_t num_entries = zip_get_num_entries(z, 0);

    std::filesystem::create_directories(abs_output_dir);

    for (zip_int64_t i = 0; i < num_entries; ++i) {
        const char *name = zip_get_name(z, i, 0);
        if (!name)
            continue;

        std::filesystem::path output_path = abs_output_dir / name;
        output_path = output_path.make_preferred();

        // Create parent directories if needed
        if (output_path.has_parent_path()) {
            std::filesystem::create_directories(output_path.parent_path());
        }

        zip_file_t *zf = zip_fopen_index(z, i, 0);
        if (!zf)
            continue;

        std::ofstream out(output_path, std::ios::binary);
        if (out) {
            constexpr size_t buffer_size = 128 * 1024;  // 128KB buffer for better I/O performance
            std::vector<char> buffer(buffer_size);

            // Set output stream buffer for better I/O performance
            out.rdbuf()->pubsetbuf(buffer.data(), buffer_size);

            std::vector<char> read_buffer(buffer_size);
            zip_int64_t sum = 0;
            zip_int64_t len = 0;

            while ((len = zip_fread(zf, read_buffer.data(), read_buffer.size())) > 0) {
                out.write(read_buffer.data(), len);
                sum += len;
            }
            extracted_files.push_back(output_path);
        }
        zip_fclose(zf);
    }

    zip_close(z);
    return extracted_files;
}

auto ZipHandler::extract_specific(
    const std::filesystem::path &output_dir, std::span<const std::string_view> file_patterns,
    std::error_code &ec) -> std::optional<std::vector<std::filesystem::path>> {
    // Convert to absolute path with native separators for cross-platform compatibility
    auto abs_zip_path = std::filesystem::absolute(pImpl->zip_path_).make_preferred();
    auto abs_output_dir = std::filesystem::absolute(output_dir).make_preferred();

    int err = 0;
    zip_t *z = zip_open(abs_zip_path.string().c_str(), 0, &err);
    if (!z) {
        ec = std::make_error_code(std::errc::io_error);
        return std::nullopt;
    }

    std::vector<std::filesystem::path> extracted_files;
    zip_int64_t num_entries = zip_get_num_entries(z, 0);

    std::filesystem::create_directories(abs_output_dir);

    for (zip_int64_t i = 0; i < num_entries; ++i) {
        const char *name = zip_get_name(z, i, 0);
        if (!name)
            continue;

        std::string_view name_view(name);
        bool should_extract = false;
        for (auto pattern : file_patterns) {
            if (name_view.find(pattern) != std::string_view::npos) {
                should_extract = true;
                break;
            }
        }

        if (!should_extract)
            continue;

        std::filesystem::path output_path = abs_output_dir / name;
        output_path = output_path.make_preferred();

        if (output_path.has_parent_path()) {
            std::filesystem::create_directories(output_path.parent_path());
        }

        zip_file_t *zf = zip_fopen_index(z, i, 0);
        if (!zf)
            continue;

        std::ofstream out(output_path, std::ios::binary);
        if (out) {
            constexpr size_t buffer_size = 128 * 1024;  // 128KB buffer for better I/O performance
            std::vector<char> buffer(buffer_size);

            // Set output stream buffer for better I/O performance
            out.rdbuf()->pubsetbuf(buffer.data(), buffer_size);

            std::vector<char> read_buffer(buffer_size);
            zip_int64_t len = 0;

            while ((len = zip_fread(zf, read_buffer.data(), read_buffer.size())) > 0) {
                out.write(read_buffer.data(), len);
            }
            extracted_files.push_back(output_path);
        }
        zip_fclose(zf);
    }

    zip_close(z);
    return extracted_files;
}

auto ZipHandler::list_files(std::error_code &ec) const -> std::optional<std::vector<std::string>> {
    auto abs_zip_path = std::filesystem::absolute(pImpl->zip_path_).make_preferred();

    int err = 0;
    zip_t *z = zip_open(abs_zip_path.string().c_str(), 0, &err);
    if (!z) {
        ec = std::make_error_code(std::errc::io_error);
        return std::nullopt;
    }

    std::vector<std::string> files;
    zip_int64_t num_entries = zip_get_num_entries(z, 0);

    for (zip_int64_t i = 0; i < num_entries; ++i) {
        const char *name = zip_get_name(z, i, 0);
        if (name) {
            files.emplace_back(name);
        }
    }

    zip_close(z);
    return files;
}

auto ZipHandler::read_file(std::string_view filename,
                           std::error_code &ec) const -> std::optional<std::vector<uint8_t>> {
    auto abs_zip_path = std::filesystem::absolute(pImpl->zip_path_).make_preferred();

    int err = 0;
    zip_t *z = zip_open(abs_zip_path.string().c_str(), 0, &err);
    if (!z) {
        ec = std::make_error_code(std::errc::io_error);
        return std::nullopt;
    }

    zip_file_t *zf = zip_fopen(z, filename.data(), 0);
    if (!zf) {
        zip_close(z);
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return std::nullopt;
    }

    zip_stat_t stat;
    zip_stat_init(&stat);
    if (zip_stat(z, filename.data(), 0, &stat) != 0) {
        zip_fclose(zf);
        zip_close(z);
        ec = std::make_error_code(std::errc::io_error);
        return std::nullopt;
    }

    std::vector<uint8_t> buffer(stat.size);
    if (zip_fread(zf, buffer.data(), stat.size) != static_cast<zip_int64_t>(stat.size)) {
        zip_fclose(zf);
        zip_close(z);
        ec = std::make_error_code(std::errc::io_error);
        return std::nullopt;
    }

    zip_fclose(zf);
    zip_close(z);
    return buffer;
}

auto extract_all_zips(const std::filesystem::path &directory,
                      const std::filesystem::path &output_dir,
                      std::error_code &ec) -> std::optional<std::vector<std::filesystem::path>> {
    std::vector<std::filesystem::path> all_extracted;

    for (const auto &entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file() && is_zip_file(entry.path())) {
            ZipHandler handler(entry.path());
            auto result = handler.extract(output_dir, ec);
            if (result) {
                all_extracted.insert(all_extracted.end(), result->begin(), result->end());
            } else {
                return std::nullopt;
            }
        }
    }

    return all_extracted;
}

}  // namespace fgd_converter::zip