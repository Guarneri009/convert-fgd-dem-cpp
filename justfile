# Clean build directory
clean:
	rm -rf build


# Clean data and output
data_delete:
	rm -rf ./extracted/*
	rm -rf ./output/*

# Build optimized version (Release mode)
build: clean
	@echo "========================================="
	@echo "Building optimized version (Release)"
	@echo "========================================="
	mkdir -p build
	cd build && cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
	cd build && ninja
	@echo ""
	@echo "✓ Build complete!"
	@echo "Binary: ./build/convert_fgd_dem_cpp"

# Build debug version
build-debug: clean
	@echo "Building debug version..."
	mkdir -p build
	cd build && cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug ..
	cd build && ninja
	@echo "✓ Debug build complete!"

# Rebuild without cleaning
rebuild:
	@echo "Rebuilding (incremental)..."
	mkdir -p build
	cd build && cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
	cd build && ninja

# Run the program
run: data_delete
	@echo "Running convert_fgd_dem_cpp..."
	time ./build/convert_fgd_dem_cpp -i ./.data_5m

#time ./build/convert_fgd_dem_cpp -i ./data_5m --rgbify true

marge:
	GDAL_NUM_THREADS=ALL_CPUS OMP_NUM_THREADS=6 sh merge_separate_tif.sh 5A 5
	GDAL_NUM_THREADS=ALL_CPUS OMP_NUM_THREADS=6 sh merge_separate_tif.sh 5B 5
	GDAL_NUM_THREADS=ALL_CPUS OMP_NUM_THREADS=6 sh merge_separate_tif.sh 5C 5

# Full workflow: build and run
go: build run
	@echo "Merging TIFFs..."

# Show build info
info:
	@echo "Build configuration:"
	@echo "  Build system: Ninja (parallel builds)"
	@echo "  Build type: Release"
	@echo "  Optimization: -O3 -march=native -mavx2 -mfma -flto (parallel LTO)"
	@echo "  Binary: ./build/convert_fgd_dem_cpp"
	@if [ -f ./build/convert_fgd_dem_cpp ]; then \
		echo "  Size: $$(du -h ./build/convert_fgd_dem_cpp | cut -f1)"; \
	else \
		echo "  Status: Not built yet"; \
	fi

# Default target
default: build


# Format code (if clang-format is available)
format:
    @if command -v clang-format >/dev/null 2>&1; then \
        find src include -name '*.cpp' -o -name '*.hpp' -o -name '*.cu' -o -name '*.cuh' | xargs clang-format -i; \
        echo "Code formatted successfully"; \
    else \
        echo "clang-format not found. Skipping formatting."; \
    fi
