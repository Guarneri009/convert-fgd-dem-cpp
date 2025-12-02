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

# Benchmark (compare with Python)
benchmark: build
	@echo "Running benchmark..."
	./benchmark.sh data_5m/ output/

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

# ============================================
# Windows (vcpkg + MSVC) targets
# ============================================

# vcpkg directory (default: C:\vcpkg)
vcpkg_root := env_var_or_default("VCPKG_ROOT", "C:/vcpkg")

# Setup vcpkg on Windows (run once)
[windows]
win-setup-vcpkg:
    @echo "Setting up vcpkg..."
    @if not exist "{{vcpkg_root}}" ( \
        git clone https://github.com/Microsoft/vcpkg.git "{{vcpkg_root}}" && \
        "{{vcpkg_root}}\bootstrap-vcpkg.bat" \
    ) else ( \
        echo "vcpkg already exists at {{vcpkg_root}}" \
    )

# Install dependencies via vcpkg on Windows
[windows]
win-deps:
    @echo "Installing dependencies via vcpkg..."
    @echo "This may take 30-120 minutes for GDAL..."
    "{{vcpkg_root}}\vcpkg.exe" install gdal:x64-windows libzip:x64-windows tbb:x64-windows

# Clean build directory (Windows)
[windows]
win-clean:
    @if exist build rmdir /s /q build

# Build on Windows (Release)
[windows]
win-build: win-clean
    @echo "========================================="
    @echo "Building on Windows (Release)"
    @echo "========================================="
    cmake -B build -G Ninja ^
        -DCMAKE_BUILD_TYPE=Release ^
        -DCMAKE_TOOLCHAIN_FILE="{{vcpkg_root}}/scripts/buildsystems/vcpkg.cmake" ^
        -DVCPKG_TARGET_TRIPLET="x64-windows"
    cmake --build build --config Release
    @echo ""
    @echo "Build complete!"
    @echo "Binary: .\build\convert_fgd_dem_cpp.exe"

# Build on Windows (Debug)
[windows]
win-build-debug: win-clean
    @echo "Building on Windows (Debug)..."
    cmake -B build -G Ninja ^
        -DCMAKE_BUILD_TYPE=Debug ^
        -DCMAKE_TOOLCHAIN_FILE="{{vcpkg_root}}/scripts/buildsystems/vcpkg.cmake" ^
        -DVCPKG_TARGET_TRIPLET="x64-windows"
    cmake --build build --config Debug

# Rebuild on Windows (incremental)
[windows]
win-rebuild:
    @echo "Rebuilding (incremental)..."
    cmake -B build -G Ninja ^
        -DCMAKE_BUILD_TYPE=Release ^
        -DCMAKE_TOOLCHAIN_FILE="{{vcpkg_root}}/scripts/buildsystems/vcpkg.cmake" ^
        -DVCPKG_TARGET_TRIPLET="x64-windows"
    cmake --build build --config Release

# Run on Windows
[windows]
win-run:
    @echo "Running convert_fgd_dem_cpp..."
    .\build\convert_fgd_dem_cpp.exe -i .\.data_5m

# Full setup on Windows (first time)
[windows]
win-init: win-setup-vcpkg win-deps
    @echo ""
    @echo "========================================="
    @echo "Windows setup complete!"
    @echo "Now run: just win-build"
    @echo "========================================="

# Show Windows build info
[windows]
win-info:
    @echo "Windows Build Configuration:"
    @echo "  vcpkg root: {{vcpkg_root}}"
    @echo "  Triplet: x64-windows"
    @echo "  Generator: Ninja"
    @echo "  Compiler: MSVC"
    @if exist .\build\convert_fgd_dem_cpp.exe ( \
        echo "  Binary: .\build\convert_fgd_dem_cpp.exe" && \
        for %%A in (.\build\convert_fgd_dem_cpp.exe) do echo "  Size: %%~zA bytes" \
    ) else ( \
        echo "  Status: Not built yet" \
    )

# Copy DLLs for distribution (Windows)
[windows]
win-package:
    @echo "Packaging for distribution..."
    @if not exist dist mkdir dist
    copy .\build\convert_fgd_dem_cpp.exe dist\
    copy "{{vcpkg_root}}\installed\x64-windows\bin\*.dll" dist\
    @echo "Package created in .\dist\"
