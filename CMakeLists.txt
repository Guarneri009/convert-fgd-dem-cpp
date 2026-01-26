cmake_minimum_required(VERSION 3.16)
project(convert_fgd_dem_cpp VERSION 1.0.0 LANGUAGES CXX)

# 高速ビルドのためNinjaを推奨
if(NOT CMAKE_GENERATOR)
    find_program(NINJA_EXECUTABLE ninja)
    if(NINJA_EXECUTABLE)
        message(STATUS "Ninjaが見つかりました。高速ビルドには次を使用: cmake -G Ninja -B build")
    endif()
endif()

# C++20標準 (GCC 11向けに機能を制限)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC: 静的ランタイムを全ターゲットに適用 (FetchContentも含む)
# CMP0091はcmake_minimum_required後、project()前に設定するのが理想だが、
# ここで設定しても動作する
if(MSVC)
    cmake_policy(SET CMP0091 NEW)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # FetchContentで取得されるターゲットにも適用されるようにキャッシュ変数として設定
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
endif()

# IDE対応のためコンパイルコマンドをエクスポート
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 必須パッケージの検索
find_package(Threads REQUIRED)

# 依存関係管理の戦略:
# - VCPKG_TARGET_TRIPLETが設定されている場合はvcpkgを使用 (全プラットフォーム)
# - それ以外: pkg-configを使用 (macOSのHomebrewで推奨)
if(DEFINED VCPKG_TARGET_TRIPLET)
    # vcpkgモード - find_packageを使用 (Windows, macOS, Linuxで動作)
    message(STATUS "vcpkgで依存関係を管理 (triplet: ${VCPKG_TARGET_TRIPLET})")
    find_package(TIFF REQUIRED)
    find_package(GeoTIFF CONFIG REQUIRED)
    find_package(PROJ CONFIG REQUIRED)
    set(USE_VCPKG TRUE)
else()
    # vcpkgなし: pkg-configを使用 (macOSのHomebrewで推奨)
    message(STATUS "pkg-configで依存関係を管理")
    find_package(PkgConfig REQUIRED)

    # pkg-configでlibtiff, libgeotiff, projを検索
    pkg_check_modules(TIFF REQUIRED libtiff-4)
    pkg_check_modules(GEOTIFF REQUIRED libgeotiff)
    pkg_check_modules(PROJ REQUIRED proj)

    set(USE_VCPKG FALSE)
endif()

# 依存関係の検索またはフェッチ
include(FetchContent)

# コマンドライン解析用にcxxoptsをフェッチ
FetchContent_Declare(
    cxxopts
    GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
    GIT_TAG v3.1.1
    GIT_SHALLOW TRUE
)

# EXCLUDE_FROM_ALL を指定して不要なターゲットを除外
FetchContent_GetProperties(cxxopts)
if(NOT cxxopts_POPULATED)
    FetchContent_Populate(cxxopts)
    add_subdirectory(${cxxopts_SOURCE_DIR} ${cxxopts_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# ZIP展開用にminizip-ngをフェッチ (解凍のみ)
# MSVC: minizip-ngが静的ランタイムでビルドされるようにCフラグを設定
if(MSVC)
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd")
endif()
set(MZ_ZLIB ON CACHE BOOL "ZLIB圧縮を有効化" FORCE)
set(MZ_BZIP2 OFF CACHE BOOL "BZIP2圧縮を無効化" FORCE)
set(MZ_LZMA OFF CACHE BOOL "LZMA圧縮を無効化" FORCE)
set(MZ_ZSTD OFF CACHE BOOL "ZSTD圧縮を無効化" FORCE)
set(MZ_LIBCOMP OFF CACHE BOOL "libcompressionを無効化" FORCE)
set(MZ_OPENSSL OFF CACHE BOOL "OpenSSL暗号化を無効化" FORCE)
set(MZ_LIBBSD OFF CACHE BOOL "libbsdを無効化" FORCE)
set(MZ_PKCRYPT OFF CACHE BOOL "PKWARE暗号化を無効化" FORCE)
set(MZ_WZAES OFF CACHE BOOL "WinZIP AES暗号化を無効化" FORCE)
set(MZ_SIGNING OFF CACHE BOOL "署名を無効化" FORCE)
set(MZ_COMPRESS OFF CACHE BOOL "圧縮を無効化 (展開のみ)" FORCE)
set(MZ_DECOMPRESS ON CACHE BOOL "解凍を有効化" FORCE)
set(MZ_BUILD_TESTS OFF CACHE BOOL "minizip-ngテストを無効化" FORCE)
set(MZ_BUILD_UNIT_TESTS OFF CACHE BOOL "minizip-ngユニットテストを無効化" FORCE)
set(MZ_BUILD_FUZZ_TESTS OFF CACHE BOOL "minizip-ngファズテストを無効化" FORCE)
set(MZ_COMPAT OFF CACHE BOOL "互換レイヤーを無効化" FORCE)
set(MZ_FETCH_LIBS ON CACHE BOOL "必要なライブラリをフェッチ" FORCE)
set(MZ_FORCE_FETCH_LIBS OFF CACHE BOOL "システムにライブラリがあれば強制フェッチしない" FORCE)

FetchContent_Declare(
    minizip-ng
    GIT_REPOSITORY https://github.com/zlib-ng/minizip-ng.git
    GIT_TAG 4.0.7
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(minizip-ng)

# MSVC: minizip-ngターゲットに静的ランタイムを明示的に適用
# 注: MINIZIP::minizipはエイリアスターゲットなのでset_propertyは使用不可
if(MSVC)
    if(TARGET minizip)
        set_property(TARGET minizip PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

# ソースファイル
set(SOURCES
    src/main.cpp
    src/converter.cpp
    src/dem.cpp
    src/geotiff.cpp
    src/xml_parser.cpp
    src/zip_handler.cpp
)

# 静的リンクで実行ファイルを作成
add_executable(${PROJECT_NAME} ${SOURCES})

# C++ランタイムのみ静的リンクを有効化 (Windows MinGW)
if(WIN32 AND NOT MSVC)
    # MSYS2/MinGW: DLL依存を減らすためC++ランタイムを静的リンク
    target_link_options(${PROJECT_NAME} PRIVATE
        -static-libgcc
        -static-libstdc++
    )
elseif(MSVC)
    # MSVC静的ランタイム
    set_property(TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# インクルードディレクトリ
if(USE_VCPKG)
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${GEOTIFF_INCLUDE_DIR}
    )
else()
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${TIFF_INCLUDE_DIRS}
        ${GEOTIFF_INCLUDE_DIRS}
        ${PROJ_INCLUDE_DIRS}
    )
endif()

# 静的ライブラリを優先してリンク
if(USE_VCPKG)
    # vcpkg: 静的ライブラリを強制
    if(WIN32)
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a .lib)
    else()
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
    endif()

    target_link_libraries(${PROJECT_NAME} PRIVATE
        TIFF::TIFF
        ${GeoTIFF_LIBRARIES}
        PROJ::proj
        cxxopts::cxxopts
        Threads::Threads
        MINIZIP::minizip
    )

    # macOS: vcpkg使用時に必要なシステムフレームワークをリンク
    if(APPLE)
        target_link_libraries(${PROJECT_NAME} PRIVATE
            "-framework CoreFoundation"
            "-framework Security"
            "-framework SystemConfiguration"
        )
    endif()

    # Windows MSVC: 完全な静的リンク
    if(MSVC)
        # 静的ランタイムとインクリメンタルリンクの無効化
        set_target_properties(${PROJECT_NAME} PROPERTIES
            LINK_FLAGS "/INCREMENTAL:NO"
        )
    endif()
else()
    # pkg-config (macOS/Linux) または MSYS2 pacman
    if(WIN32 AND NOT MSVC)
        # MSYS2/MinGW: 静的ライブラリを強制
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
    endif()

    target_link_directories(${PROJECT_NAME} PRIVATE
        ${TIFF_LIBRARY_DIRS}
        ${GEOTIFF_LIBRARY_DIRS}
        ${PROJ_LIBRARY_DIRS}
    )

    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${TIFF_LIBRARIES}
        ${GEOTIFF_LIBRARIES}
        ${PROJ_LIBRARIES}
        cxxopts::cxxopts
        Threads::Threads
        MINIZIP::minizip
    )
endif()

# コンパイラフラグ
if(MSVC)
    # MSVCコンパイラフラグ
    target_compile_options(${PROJECT_NAME} PRIVATE
        /std:c++20
        /W4
        /permissive-
        /Zc:__cplusplus
        $<$<CONFIG:Debug>:/Od /Zi>
        $<$<CONFIG:Release>:/O2 /Ob2 /DNDEBUG>
    )
    # std::min/std::maxとの競合を避けるためWindowsのmin/maxマクロを無効化
    target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX)
else()
    # GCC/Clangコンパイラフラグ
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
        $<$<CONFIG:Debug>:-g -O0>
    )
endif()

# プラットフォーム固有のRelease最適化 (GCC/Clangのみ)
if(NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Release")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        # x86_64固有の最適化 (AVX2使用)
        target_compile_options(${PROJECT_NAME} PRIVATE
            -O3 -march=native -mavx2 -mfma -DNDEBUG -flto -ffast-math -funroll-loops -ftree-vectorize -fomit-frame-pointer
        )
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        # ARM64 (Apple Silicon / M1/M2/M3) 固有の最適化 (NEON使用)
        target_compile_options(${PROJECT_NAME} PRIVATE
            -O3 -mcpu=apple-m1 -DNDEBUG -flto -ffast-math -funroll-loops -ftree-vectorize -fomit-frame-pointer
        )
    else()
        # その他のアーキテクチャ向け汎用最適化
        target_compile_options(${PROJECT_NAME} PRIVATE
            -O3 -march=native -DNDEBUG -flto -ffast-math -funroll-loops -ftree-vectorize -fomit-frame-pointer
        )
    endif()
endif()

# リンク時最適化 (LTO) を有効化
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

# 並列LTOコンパイル (シリアルコンパイル警告を抑制) - GCCのみ
if(NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Release")
    include(ProcessorCount)
    ProcessorCount(N)
    if(NOT N EQUAL 0)
        # AppleClangは-flto=N構文をサポートしない、GCCのみ
        if(NOT CMAKE_CXX_COMPILER_ID MATCHES "AppleClang")
            target_link_options(${PROJECT_NAME} PRIVATE
                $<$<CONFIG:Release>:-flto=${N}>
            )
        endif()
    endif()
endif()

# 並列実行サポートのためTBBをリンク
find_package(TBB CONFIG QUIET)
if(TBB_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE TBB::tbb)
    message(STATUS "TBBが見つかりました: 並列実行が有効")

    # MSVC静的TBB向け
    if(MSVC)
        target_compile_definitions(${PROJECT_NAME} PRIVATE __TBB_NO_IMPLICIT_LINKAGE)
    endif()
else()
    find_package(TBB QUIET)
    if(TBB_FOUND)
        target_link_libraries(${PROJECT_NAME} PRIVATE TBB::tbb)
        message(STATUS "TBBが見つかりました (pkg-config): 並列実行が有効")
    else()
        message(WARNING "TBBが見つかりません: 逐次実行にフォールバック")
        # フォールバックとして-ltbbでリンクを試行
        if(NOT WIN32 OR NOT MSVC)
            target_link_libraries(${PROJECT_NAME} PRIVATE tbb)
        endif()
    endif()
endif()

# GCC 11固有の機能チェックと利用不可時の無効化
include(CheckCXXCompilerFlag)

# C++20サポートのチェック
if(MSVC)
    check_cxx_compiler_flag("/std:c++20" HAS_CXX20)
else()
    check_cxx_compiler_flag("-std=c++20" HAS_CXX20)
endif()

if(NOT HAS_CXX20)
    message(FATAL_ERROR "C++20サポートが必要です")
endif()

# 古いコンパイラ向けに追加フラグが必要な場合
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "11.0")
        message(FATAL_ERROR "GCC 11以降が必要です")
    endif()

    # GCC 11固有のフラグ
    target_compile_options(${PROJECT_NAME} PRIVATE
        -fconcepts
    )
endif()

# インストールルール
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# テストを有効化
enable_testing()

# オプション: testディレクトリが存在すればテストをビルド
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    add_subdirectory(tests)
endif()

# 設定サマリーを表示
message(STATUS "")
message(STATUS "=== 設定サマリー ===")
message(STATUS "C++標準: ${CMAKE_CXX_STANDARD}")
message(STATUS "ビルドタイプ: ${CMAKE_BUILD_TYPE}")
message(STATUS "コンパイラ: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "インストール先: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "=============================")
message(STATUS "")
