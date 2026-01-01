#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

namespace fgd_converter::zip {

class ZipHandler {
   public:
    explicit ZipHandler(std::filesystem::path zip_path);
    ~ZipHandler();

    // ムーブのみ可能な型
    ZipHandler(const ZipHandler&) = delete;
    ZipHandler& operator=(const ZipHandler&) = delete;
    ZipHandler(ZipHandler&&) noexcept;
    ZipHandler& operator=(ZipHandler&&) noexcept;

    [[nodiscard]] auto extract(const std::filesystem::path& output_dir, std::error_code& ec)
        -> std::optional<std::vector<std::filesystem::path>>;

    [[nodiscard]] auto extract_specific(
        const std::filesystem::path& output_dir, std::span<const std::string_view> file_patterns,
        std::error_code& ec) -> std::optional<std::vector<std::filesystem::path>>;

    [[nodiscard]] auto list_files(std::error_code& ec) const
        -> std::optional<std::vector<std::string>>;

    [[nodiscard]] auto read_file(std::string_view filename,
                                 std::error_code& ec) const -> std::optional<std::vector<uint8_t>>;

   private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

// ユーティリティ関数
[[nodiscard]] inline auto is_zip_file(const std::filesystem::path& path) noexcept -> bool {
    return path.extension() == ".zip" || path.extension() == ".ZIP";
}

[[nodiscard]] auto extract_all_zips(const std::filesystem::path& directory,
                                    const std::filesystem::path& output_dir, std::error_code& ec)
    -> std::optional<std::vector<std::filesystem::path>>;

}  // namespace fgd_converter::zip