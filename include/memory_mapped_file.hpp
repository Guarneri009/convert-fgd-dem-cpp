#pragma once

#include <filesystem>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <fcntl.h>
#    include <sys/mman.h>
#    include <sys/stat.h>
#    include <unistd.h>
#endif

namespace fgd_converter {

class MemoryMappedFile {
   public:
    /**
     * @brief メモリマッピングでファイルを開く
     * @param path マッピングするファイルパス
     */
    explicit MemoryMappedFile(const std::filesystem::path& path) {
#ifdef _WIN32
        // Windows実装
        file_handle_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (file_handle_ == INVALID_HANDLE_VALUE) {
            return;
        }

        LARGE_INTEGER file_size;
        if (!GetFileSizeEx(file_handle_, &file_size)) {
            CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
            return;
        }

        size_ = static_cast<size_t>(file_size.QuadPart);

        map_handle_ = CreateFileMappingW(file_handle_, nullptr, PAGE_READONLY, 0, 0, nullptr);

        if (!map_handle_) {
            CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
            return;
        }

        data_ = MapViewOfFile(map_handle_, FILE_MAP_READ, 0, 0, 0);

        if (!data_) {
            CloseHandle(map_handle_);
            CloseHandle(file_handle_);
            map_handle_ = nullptr;
            file_handle_ = INVALID_HANDLE_VALUE;
        }
#else
        // POSIX実装 (Linux, macOS)
        fd_ = open(path.c_str(), O_RDONLY);
        if (fd_ == -1) {
            return;
        }

        struct stat sb;
        if (fstat(fd_, &sb) == -1) {
            close(fd_);
            fd_ = -1;
            return;
        }

        size_ = static_cast<size_t>(sb.st_size);

        data_ = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
        if (data_ == MAP_FAILED) {
            close(fd_);
            fd_ = -1;
            data_ = nullptr;
        }

        // カーネルにアクセスパターンを通知
        if (data_) {
            // MADV_SEQUENTIAL: 順次読み取りを行う
            // MADV_WILLNEED: プリフェッチを開始
            madvise(data_, size_, MADV_SEQUENTIAL | MADV_WILLNEED);
        }
#endif
    }

    /**
     * @brief デストラクタ - ファイルのマッピングを解除
     */
    ~MemoryMappedFile() {
#ifdef _WIN32
        if (data_)
            UnmapViewOfFile(data_);
        if (map_handle_)
            CloseHandle(map_handle_);
        if (file_handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(file_handle_);
#else
        if (data_)
            munmap(data_, size_);
        if (fd_ != -1)
            close(fd_);
#endif
    }

    // コピー禁止
    MemoryMappedFile(const MemoryMappedFile&) = delete;
    MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;

    // ムーブ可能
    MemoryMappedFile(MemoryMappedFile&& other) noexcept
        : data_(other.data_),
          size_(other.size_)
#ifdef _WIN32
          ,
          file_handle_(other.file_handle_),
          map_handle_(other.map_handle_)
#else
          ,
          fd_(other.fd_)
#endif
    {
        other.data_ = nullptr;
        other.size_ = 0;
#ifdef _WIN32
        other.file_handle_ = INVALID_HANDLE_VALUE;
        other.map_handle_ = nullptr;
#else
        other.fd_ = -1;
#endif
    }

    MemoryMappedFile& operator=(MemoryMappedFile&& other) noexcept {
        if (this != &other) {
            // 現在のリソースをクリーンアップ
            this->~MemoryMappedFile();

            // otherからムーブ
            data_ = other.data_;
            size_ = other.size_;
#ifdef _WIN32
            file_handle_ = other.file_handle_;
            map_handle_ = other.map_handle_;
#else
            fd_ = other.fd_;
#endif

            // otherをリセット
            other.data_ = nullptr;
            other.size_ = 0;
#ifdef _WIN32
            other.file_handle_ = INVALID_HANDLE_VALUE;
            other.map_handle_ = nullptr;
#else
            other.fd_ = -1;
#endif
        }
        return *this;
    }

    /**
     * @brief マッピングが成功したか確認
     */
    bool is_open() const { return data_ != nullptr; }

    /**
     * @brief マッピングデータのstring_viewを取得
     */
    std::string_view view() const {
        if (!data_)
            return {};
        return {static_cast<const char*>(data_), size_};
    }

    /**
     * @brief マッピングデータへの生ポインタを取得
     */
    const void* data() const { return data_; }

    /**
     * @brief マッピングデータのサイズを取得
     */
    size_t size() const { return size_; }

   private:
    void* data_ = nullptr;
    size_t size_ = 0;

#ifdef _WIN32
    HANDLE file_handle_ = INVALID_HANDLE_VALUE;
    HANDLE map_handle_ = nullptr;
#else
    int fd_ = -1;
#endif
};

}  // namespace fgd_converter
