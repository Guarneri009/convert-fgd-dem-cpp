#include "zip_handler.hpp"

#include <mz.h>
#include <mz_strm.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>

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
    auto abs_zip_path = std::filesystem::absolute(pImpl->zip_path_).make_preferred();
    auto abs_output_dir = std::filesystem::absolute(output_dir).make_preferred();

    void *reader = mz_zip_reader_create();
    if (!reader) {
        ec = std::make_error_code(std::errc::not_enough_memory);
        return std::nullopt;
    }

    std::vector<std::filesystem::path> extracted_files;

    int32_t err = mz_zip_reader_open_file(reader, abs_zip_path.string().c_str());
    if (err != MZ_OK) {
        std::cout << "ZIPファイルを開けませんでした: " << abs_zip_path.string()
                  << " (エラー: " << err << ")" << std::endl;
        mz_zip_reader_delete(&reader);
        ec = std::make_error_code(std::errc::io_error);
        return std::nullopt;
    }

    std::filesystem::create_directories(abs_output_dir);

    err = mz_zip_reader_goto_first_entry(reader);
    while (err == MZ_OK) {
        mz_zip_file *file_info = nullptr;
        err = mz_zip_reader_entry_get_info(reader, &file_info);
        if (err != MZ_OK) {
            break;
        }

        // ディレクトリをスキップ
        if (mz_zip_reader_entry_is_dir(reader) == MZ_OK) {
            err = mz_zip_reader_goto_next_entry(reader);
            continue;
        }

        std::string filename(file_info->filename);
        std::filesystem::path output_path = abs_output_dir / filename;
        output_path = output_path.make_preferred();

        // 必要に応じて親ディレクトリを作成
        if (output_path.has_parent_path()) {
            std::filesystem::create_directories(output_path.parent_path());
        }

        // ファイルを展開
        err = mz_zip_reader_entry_save_file(reader, output_path.string().c_str());
        if (err == MZ_OK) {
            extracted_files.push_back(output_path);
        } else {
            std::cout << "展開に失敗しました: " << filename << " (エラー: " << err << ")"
                      << std::endl;
        }

        err = mz_zip_reader_goto_next_entry(reader);
    }

    // 反復完了時にMZ_END_OF_LISTが期待される
    if (err != MZ_END_OF_LIST && err != MZ_OK) {
        std::cout << "ZIP展開中にエラーが発生しました (エラー: " << err << ")" << std::endl;
    }

    mz_zip_reader_close(reader);
    mz_zip_reader_delete(&reader);

    return extracted_files;
}

auto ZipHandler::extract_specific(
    const std::filesystem::path &output_dir, std::span<const std::string_view> file_patterns,
    std::error_code &ec) -> std::optional<std::vector<std::filesystem::path>> {
    auto abs_zip_path = std::filesystem::absolute(pImpl->zip_path_).make_preferred();
    auto abs_output_dir = std::filesystem::absolute(output_dir).make_preferred();

    void *reader = mz_zip_reader_create();
    if (!reader) {
        ec = std::make_error_code(std::errc::not_enough_memory);
        return std::nullopt;
    }

    std::vector<std::filesystem::path> extracted_files;

    int32_t err = mz_zip_reader_open_file(reader, abs_zip_path.string().c_str());
    if (err != MZ_OK) {
        std::cout << "ZIPファイルを開けませんでした: " << abs_zip_path.string()
                  << " (エラー: " << err << ")" << std::endl;
        mz_zip_reader_delete(&reader);
        ec = std::make_error_code(std::errc::io_error);
        return std::nullopt;
    }

    std::filesystem::create_directories(abs_output_dir);

    err = mz_zip_reader_goto_first_entry(reader);
    while (err == MZ_OK) {
        mz_zip_file *file_info = nullptr;
        err = mz_zip_reader_entry_get_info(reader, &file_info);
        if (err != MZ_OK) {
            break;
        }

        // ディレクトリをスキップ
        if (mz_zip_reader_entry_is_dir(reader) == MZ_OK) {
            err = mz_zip_reader_goto_next_entry(reader);
            continue;
        }

        std::string filename(file_info->filename);
        std::string_view name_view(filename);

        // ファイル名がパターンに一致するか確認
        bool should_extract = false;
        for (auto pattern : file_patterns) {
            if (name_view.find(pattern) != std::string_view::npos) {
                should_extract = true;
                break;
            }
        }

        if (!should_extract) {
            err = mz_zip_reader_goto_next_entry(reader);
            continue;
        }

        std::filesystem::path output_path = abs_output_dir / filename;
        output_path = output_path.make_preferred();

        if (output_path.has_parent_path()) {
            std::filesystem::create_directories(output_path.parent_path());
        }

        err = mz_zip_reader_entry_save_file(reader, output_path.string().c_str());
        if (err == MZ_OK) {
            extracted_files.push_back(output_path);
        } else {
            std::cout << "展開に失敗しました: " << filename << " (エラー: " << err << ")"
                      << std::endl;
        }

        err = mz_zip_reader_goto_next_entry(reader);
    }

    if (err != MZ_END_OF_LIST && err != MZ_OK) {
        std::cout << "ZIP展開中にエラーが発生しました (エラー: " << err << ")" << std::endl;
    }

    mz_zip_reader_close(reader);
    mz_zip_reader_delete(&reader);

    return extracted_files;
}

auto ZipHandler::list_files(std::error_code &ec) const -> std::optional<std::vector<std::string>> {
    auto abs_zip_path = std::filesystem::absolute(pImpl->zip_path_).make_preferred();

    void *reader = mz_zip_reader_create();
    if (!reader) {
        ec = std::make_error_code(std::errc::not_enough_memory);
        return std::nullopt;
    }

    int32_t err = mz_zip_reader_open_file(reader, abs_zip_path.string().c_str());
    if (err != MZ_OK) {
        std::cout << "ZIPファイルを開けませんでした: " << abs_zip_path.string()
                  << " (エラー: " << err << ")" << std::endl;
        mz_zip_reader_delete(&reader);
        ec = std::make_error_code(std::errc::io_error);
        return std::nullopt;
    }

    std::vector<std::string> filenames;

    err = mz_zip_reader_goto_first_entry(reader);
    while (err == MZ_OK) {
        mz_zip_file *file_info = nullptr;
        err = mz_zip_reader_entry_get_info(reader, &file_info);
        if (err != MZ_OK) {
            break;
        }

        // ディレクトリをスキップ
        if (mz_zip_reader_entry_is_dir(reader) != MZ_OK) {
            filenames.emplace_back(file_info->filename);
        }

        err = mz_zip_reader_goto_next_entry(reader);
    }

    mz_zip_reader_close(reader);
    mz_zip_reader_delete(&reader);

    return filenames;
}

auto ZipHandler::read_file(std::string_view filename,
                           std::error_code &ec) const -> std::optional<std::vector<uint8_t>> {
    auto abs_zip_path = std::filesystem::absolute(pImpl->zip_path_).make_preferred();

    void *reader = mz_zip_reader_create();
    if (!reader) {
        ec = std::make_error_code(std::errc::not_enough_memory);
        return std::nullopt;
    }

    int32_t err = mz_zip_reader_open_file(reader, abs_zip_path.string().c_str());
    if (err != MZ_OK) {
        std::cout << "ZIPファイルを開けませんでした: " << abs_zip_path.string()
                  << " (エラー: " << err << ")" << std::endl;
        mz_zip_reader_delete(&reader);
        ec = std::make_error_code(std::errc::io_error);
        return std::nullopt;
    }

    // ファイルを検索
    err = mz_zip_reader_locate_entry(reader, std::string(filename).c_str(), 0);
    if (err != MZ_OK) {
        mz_zip_reader_close(reader);
        mz_zip_reader_delete(&reader);
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return std::nullopt;
    }

    // サイズ取得のためファイル情報を取得
    mz_zip_file *file_info = nullptr;
    err = mz_zip_reader_entry_get_info(reader, &file_info);
    if (err != MZ_OK) {
        mz_zip_reader_close(reader);
        mz_zip_reader_delete(&reader);
        ec = std::make_error_code(std::errc::io_error);
        return std::nullopt;
    }

    // 読み取り用にエントリを開く
    err = mz_zip_reader_entry_open(reader);
    if (err != MZ_OK) {
        mz_zip_reader_close(reader);
        mz_zip_reader_delete(&reader);
        ec = std::make_error_code(std::errc::io_error);
        return std::nullopt;
    }

    // ファイル内容を読み取り
    std::vector<uint8_t> buffer(static_cast<size_t>(file_info->uncompressed_size));
    int32_t bytes_read =
        mz_zip_reader_entry_read(reader, buffer.data(), static_cast<int32_t>(buffer.size()));

    mz_zip_reader_entry_close(reader);
    mz_zip_reader_close(reader);
    mz_zip_reader_delete(&reader);

    if (bytes_read < 0) {
        ec = std::make_error_code(std::errc::io_error);
        return std::nullopt;
    }

    buffer.resize(static_cast<size_t>(bytes_read));
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
