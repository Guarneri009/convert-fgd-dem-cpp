#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "memory_mapped_file.hpp"

// Check if TBB is available
#ifdef __has_include
#    if __has_include(<tbb/parallel_pipeline.h>)
#        define HAS_TBB_PIPELINE 1
#        include <tbb/global_control.h>
#        include <tbb/parallel_pipeline.h>
#    endif
#endif

namespace fgd_converter {

#ifdef HAS_TBB_PIPELINE

template <typename ResultType>
class TBBPipeline {
   public:
    using ProcessFunc = std::function<ResultType(std::string_view)>;

    /**
     * @brief Create TBB pipeline with custom processing function
     *
     * @param process_func Function to process loaded file content
     * @param max_tokens Maximum items in flight (default: auto-detect)
     */
    explicit TBBPipeline(ProcessFunc process_func, size_t max_tokens = 0)
        : process_func_(std::move(process_func)),
          max_tokens_(max_tokens == 0 ? std::thread::hardware_concurrency() * 3 : max_tokens) {}

    /**
     * @brief Process files through TBB pipeline
     *
     * @param file_paths Paths to files to process
     * @return Vector of processing results in same order as input
     */
    std::vector<ResultType> process_files(const std::vector<std::filesystem::path>& file_paths) {
        if (file_paths.empty()) {
            return {};
        }

        std::vector<ResultType> results;
        results.reserve(file_paths.size());

        // Atomic counter for file reading stage
        std::atomic<size_t> file_index{0};
        const size_t total_files = file_paths.size();

        // Stage 1: File reading (serial - I/O bound)
        auto read_stage = tbb::make_filter<void, std::optional<std::pair<size_t, std::string>>>(
            tbb::filter_mode::serial_in_order,
            [&](tbb::flow_control& fc) -> std::optional<std::pair<size_t, std::string>> {
                size_t idx = file_index.fetch_add(1, std::memory_order_relaxed);

                if (idx >= total_files) {
                    fc.stop();
                    return std::nullopt;
                }

                // Memory-mapped file reading
                MemoryMappedFile mmap(file_paths[idx]);
                if (!mmap.is_open()) {
                    return std::make_pair(idx, std::string{});
                }

                return std::make_pair(idx, std::string(mmap.view()));
            });

        // Stage 2: XML parsing (parallel - CPU bound)
        auto parse_stage = tbb::make_filter<std::optional<std::pair<size_t, std::string>>,
                                            std::optional<std::pair<size_t, ResultType>>>(
            tbb::filter_mode::parallel,
            [&](std::optional<std::pair<size_t, std::string>> input)
                -> std::optional<std::pair<size_t, ResultType>> {
                if (!input)
                    return std::nullopt;

                auto [idx, content] = std::move(*input);

                if (content.empty()) {
                    return std::make_pair(idx, ResultType{});
                }

                // Process with user-provided function
                ResultType result = process_func_(content);
                return std::make_pair(idx, std::move(result));
            });

        // Stage 3: Result collection (serial - maintain order)
        auto collect_stage = tbb::make_filter<std::optional<std::pair<size_t, ResultType>>, void>(
            tbb::filter_mode::serial_in_order,
            [&](std::optional<std::pair<size_t, ResultType>> input) {
                if (!input)
                    return;

                auto [idx, result] = std::move(*input);

                // Ensure results vector is large enough
                if (results.size() <= idx) {
                    results.resize(idx + 1);
                }

                results[idx] = std::move(result);
            });

        // Execute pipeline with limited number of tokens (items in flight)
        tbb::parallel_pipeline(max_tokens_, read_stage & parse_stage & collect_stage);

        return results;
    }

    /**
     * @brief Set maximum number of threads for TBB
     */
    static void set_max_threads(size_t num_threads) {
        static tbb::global_control global_limit(tbb::global_control::max_allowed_parallelism,
                                                num_threads);
    }

   private:
    ProcessFunc process_func_;
    size_t max_tokens_;
};

#else

// Fallback when TBB pipeline is not available - use simple TBB parallel processing
#include <tbb/parallel_for.h>

template <typename ResultType>
class TBBPipeline {
   public:
    using ProcessFunc = std::function<ResultType(std::string_view)>;

    explicit TBBPipeline(ProcessFunc process_func, size_t = 0)
        : process_func_(std::move(process_func)) {}

    std::vector<ResultType> process_files(const std::vector<std::filesystem::path>& file_paths) {
        std::vector<ResultType> results(file_paths.size());

        // Use TBB parallel_for for simple parallel processing
        tbb::parallel_for(tbb::blocked_range<size_t>(0, file_paths.size()),
            [&](const tbb::blocked_range<size_t>& range) {
                for (size_t i = range.begin(); i != range.end(); ++i) {
                    MemoryMappedFile mmap(file_paths[i]);
                    if (mmap.is_open()) {
                        results[i] = process_func_(mmap.view());
                    }
                }
            });

        return results;
    }

    static void set_max_threads(size_t num_threads) {
        static tbb::global_control global_limit(tbb::global_control::max_allowed_parallelism,
                                                num_threads);
    }

   private:
    ProcessFunc process_func_;
};

#endif  // HAS_TBB_PIPELINE

}  // namespace fgd_converter
