#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace fgd_converter {

template <typename T>
class MemoryPool {
   public:
    static constexpr size_t BLOCK_SIZE = 1024 * 1024 / sizeof(T);  // 1MB per block

    MemoryPool() { allocate_new_block(); }

    ~MemoryPool() = default;

    // Disable copy
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    // Enable move
    MemoryPool(MemoryPool&&) noexcept = default;
    MemoryPool& operator=(MemoryPool&&) noexcept = default;

    /**
     * @brief Allocate n elements from the pool
     */
    T* allocate(size_t n) {
        // If request is too large for a single block, use standard allocation
        if (n > BLOCK_SIZE) {
            large_allocations_.push_back(std::unique_ptr<T[]>(new T[n]));
            return large_allocations_.back().get();
        }

        // Check if current block has enough space
        if (!current_block_ || current_offset_ + n > BLOCK_SIZE) {
            allocate_new_block();
        }

        T* result = current_block_ + current_offset_;
        current_offset_ += n;
        return result;
    }

    /**
     * @brief Deallocate (no-op for pool allocator)
     */
    void deallocate(T*, size_t) noexcept {
        // Pool allocator doesn't free individual allocations
        // Memory is freed when the pool is destroyed
    }

    /**
     * @brief Clear all allocations and reset the pool
     */
    void clear() {
        blocks_.clear();
        large_allocations_.clear();
        current_block_ = nullptr;
        current_offset_ = 0;
        allocate_new_block();
    }

    /**
     * @brief Get total memory allocated by the pool
     */
    size_t total_allocated_bytes() const {
        size_t total = blocks_.size() * BLOCK_SIZE * sizeof(T);
        for (const auto& alloc : large_allocations_) {
            total += sizeof(T);  // Approximation
        }
        return total;
    }

   private:
    void allocate_new_block() {
        blocks_.push_back(std::unique_ptr<T[]>(new T[BLOCK_SIZE]));
        current_block_ = blocks_.back().get();
        current_offset_ = 0;
    }

    std::vector<std::unique_ptr<T[]>> blocks_;
    std::vector<std::unique_ptr<T[]>> large_allocations_;
    T* current_block_ = nullptr;
    size_t current_offset_ = 0;
};

/**
 * @brief STL-compatible allocator wrapper for MemoryPool
 */
template <typename T>
class PoolAllocator {
   public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    template <typename U>
    struct rebind {
        using other = PoolAllocator<U>;
    };

    PoolAllocator() = default;

    template <typename U>
    PoolAllocator(const PoolAllocator<U>&) noexcept {}

    pointer allocate(size_type n) { return static_cast<pointer>(::operator new(n * sizeof(T))); }

    void deallocate(pointer p, size_type) noexcept { ::operator delete(p); }

    template <typename U>
    bool operator==(const PoolAllocator<U>&) const noexcept {
        return true;
    }

    template <typename U>
    bool operator!=(const PoolAllocator<U>&) const noexcept {
        return false;
    }
};

}  // namespace fgd_converter
