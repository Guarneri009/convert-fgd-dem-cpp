# FGD DEM Converter (C++版)

基盤地図情報（FGD）のDEMデータ（XML形式）をGeoTIFFに変換するC++ツールです。

## 特徴

- **モダンC++20**: GCC 11以降対応、コンセプト、span、stringstream等を活用
- **高性能**: マルチスレッド対応、最適化されたメモリ使用量

## 必要な依存関係

- **コンパイラ**: GCC 11以降（C++20サポート必須）
- **CMake**: 3.16以降
- **libtiff**: TIFFファイル読み書き用
- **libgeotiff**: GeoTIFF対応用
- **PROJ**: 座標変換用
- **TBB (Intel Threading Building Blocks)**: 並列処理用
- **pkg-config**: 依存関係管理用

### Ubuntu/Debianでのインストール

```bash
sudo apt update
sudo apt install build-essential cmake gcc-11 g++-11
sudo apt install libtiff-dev libgeotiff-dev libproj-dev libtbb-dev pkg-config
```

### 依存関係（自動取得）

以下のライブラリは CMake により自動的にダウンロードされます：
- **cxxopts**: コマンドライン引数解析

## ビルド方法

### 1. 簡単ビルド（推奨）

```bash
cd convert_fgd_dem-cpp
chmod +x build.sh
./build.sh
```

### 2. 手動ビルド

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 3. デバッグビルド

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

### 4. リリースビルド成果物の利用

GitHub Releasesで配布されるビルド済み実行ファイルは、すべての依存関係が静的リンクされています。ダウンロードしてすぐに使用できます。

**対応プラットフォーム:**
- Linux (x64)
- Windows (x64)
- macOS (ARM64 / Apple Silicon)

**配布内容:**
- `convert_fgd_dem_cpp` - 実行ファイル
- `proj-data/` - PROJ座標変換データ（座標変換に必要）

**使用方法:**
1. GitHubのReleasesページから対応プラットフォームのアーカイブをダウンロード
2. アーカイブを展開
3. そのまま実行可能

```bash
# Linux/macOS
tar -xzf convert-fgd-dem-cpp-linux-x64.tar.gz
./convert_fgd_dem_cpp -i ./data -o ./output

# Windows
# ZIPファイルを展開後
convert_fgd_dem_cpp.exe -i ./data -o ./output
```

**注意事項:**
- `proj-data`フォルダは実行ファイルと同じディレクトリに配置してください
- 別の場所に配置する場合は環境変数`PROJ_DATA`でパスを指定できます

## 使用方法

### 基本的な使用例

```bash
# 基本的な変換
./convert_fgd_dem_cpp -i /path/to/dem/files -o /path/to/output

# EPSG:4326（WGS84）で出力
./convert_fgd_dem_cpp -i ./data -o ./output -e EPSG:4326

# RGB可視化用に変換
./convert_fgd_dem_cpp -i ./data -o ./output --rgbify true

# ZIPファイルの展開のみ
./convert_fgd_dem_cpp -i ./data --extract-only true
```

### コマンドラインオプション

| オプション | 短縮形 | デフォルト | 説明 |
|-----------|-------|----------|------|
| `--input` | `-i` | **(必須)** | DEM ZIPファイルが含まれる入力フォルダ |
| `--output` | `-o` | `./output` | GeoTIFFファイルの出力フォルダ |
| `--epsg` | `-e` | `EPSG:3857` | 出力EPSG座標系コード |
| `--rgbify` | `-r` | `false` | 可視化用RGB変換を有効にする |
| `--sea-at-zero` | `-z` | `false` | 海面レベルを0に設定する |
| `--extract-only` | `-x` | `false` | ZIPファイルの展開のみ実行する |
| `--merge` | `-m` | `""` | DEM種別を指定してTIFファイルをマージ (例: 5A, 5B, 10A) |
| `--merge-only` | `-M` | `false` | マージのみ実行（変換なし、-m と併用） |
| `--merge-dir` | `-d` | `./output` | マージ対象のTIFファイルがあるディレクトリ |
| `--resolution` | `-t` | `10.0` | マージ時の出力解像度（メートル） |
| `--help` | `-h` | - | ヘルプを表示する |

### オプション詳細

#### `--input, -i` (必須)
DEM ZIPファイルが格納されている入力フォルダのパスを指定します。このフォルダ内のすべてのZIPファイルが再帰的に検索・処理されます。

```bash
# 相対パス
./convert_fgd_dem_cpp -i ./data

# 絶対パス
./convert_fgd_dem_cpp -i /home/user/fgd_data
```

#### `--output, -o` (オプション)
変換されたGeoTIFFファイルの出力先フォルダを指定します。デフォルトは `./output` です。フォルダが存在しない場合は自動的に作成されます。

```bash
./convert_fgd_dem_cpp -i ./data -o ./converted_tiffs
```

#### `--epsg, -e` (オプション)
出力GeoTIFFファイルの座標参照系（CRS）をEPSGコードで指定します。デフォルトは `EPSG:3857`（Web メルカトル）です。

**よく使用されるEPSGコード:**
- `EPSG:3857` - Web メルカトル（Google Maps、OpenStreetMap等で使用）
- `EPSG:4326` - WGS84（GPS座標系、緯度経度）
- `EPSG:6668` - JGD2011（日本測地系2011）
- `EPSG:2451` - JGD2000 / Japan Plane Rectangular CS IX

```bash
# WGS84で出力
./convert_fgd_dem_cpp -i ./data -o ./output -e EPSG:4326

# 日本測地系2011で出力
./convert_fgd_dem_cpp -i ./data -o ./output -e EPSG:6668
```

#### `--rgbify, -r` (オプション)
標高データをRGB画像として可視化します。`true` を指定すると、標高に応じた色付けが行われ、視覚的に見やすいGeoTIFFが生成されます。デフォルトは `false` です。

```bash
# RGB可視化を有効にする
./convert_fgd_dem_cpp -i ./data -o ./output --rgbify true
./convert_fgd_dem_cpp -i ./data -o ./output -r true
```

#### `--sea-at-zero, -z` (オプション)
海面（標高0m以下）を0に正規化します。`true` の場合、負の標高値（海面下）を0に変換します。デフォルトは `false` です。

```bash
# 海面レベルを0に設定
./convert_fgd_dem_cpp -i ./data -o ./output --sea-at-zero true
./convert_fgd_dem_cpp -i ./data -o ./output -z true

# 海面下のデータもそのまま保持（デフォルト）
./convert_fgd_dem_cpp -i ./data -o ./output
```

#### `--extract-only, -x` (オプション)
ZIPファイルの展開のみを実行し、GeoTIFFへの変換は行いません。デフォルトは `false` です。

```bash
# ZIPファイルの展開のみ実行（./extracted フォルダに展開される）
./convert_fgd_dem_cpp -i ./data --extract-only true
./convert_fgd_dem_cpp -i ./data -x true
```

#### `--merge, -m` (オプション)
変換済みのTIFファイルをDEM種別ごとにマージします。基盤地図情報DEMには複数の精度（1A, 5A, 5B, 5C, 10A, 10B）があり、このオプションで指定した種別のファイルのみを1つのGeoTIFFに統合できます。

**DEM種別と精度:**
- `1A` - 1mメッシュ（航空レーザ測量）
- `5A` - 5mメッシュ（航空レーザ測量）
- `5B` - 5mメッシュ（写真測量）
- `5C` - 5mメッシュ（写真測量）
- `10A` - 10mメッシュ（火山基本図の等高線）
- `10B` - 10mメッシュ（地形図の等高線）

```bash
# DEM5AのTIFファイルをマージ（変換後に実行）
./convert_fgd_dem_cpp -i ./data -m 5A

# DEM10Aのファイルをマージ
./convert_fgd_dem_cpp -i ./data -m 10A
```

#### `--merge-only, -M` (オプション)
変換を行わず、既存のTIFファイルのマージのみを実行します。`-m` オプションと併用して使用します。`-i` オプションは不要です。

```bash
# ./output ディレクトリ内のDEM5Aファイルをマージのみ実行
./convert_fgd_dem_cpp -M -m 5A

# カスタムディレクトリのファイルをマージ
./convert_fgd_dem_cpp -M -m 5A -d ./my_tifs

# 解像度を指定してマージ
./convert_fgd_dem_cpp -M -m 10B -t 30
```

#### `--merge-dir, -d` (オプション)
マージ対象のTIFファイルがあるディレクトリを指定します。デフォルトは `./output` です。`-M` オプションと組み合わせて使用します。

```bash
# カスタムディレクトリからマージ
./convert_fgd_dem_cpp -M -m 5A -d /path/to/tif/files

# デフォルト（./output）からマージ
./convert_fgd_dem_cpp -M -m 5A
```

#### `--resolution, -t` (オプション)
マージ時の出力解像度をメートル単位で指定します。デフォルトは `10.0` メートルです。`--merge` オプションと組み合わせて使用します。

```bash
# 5m解像度でマージ
./convert_fgd_dem_cpp -i ./data -m 5A -t 5

# 1m解像度でマージ（高解像度、ファイルサイズ大）
./convert_fgd_dem_cpp -i ./data -m 5A -t 1

# 30m解像度でマージ（低解像度、処理高速）
./convert_fgd_dem_cpp -i ./data -m 10A -t 30
```

#### `--help, -h` (オプション)
使用方法とすべてのオプションの説明を表示します。

```bash
./convert_fgd_dem_cpp --help
./convert_fgd_dem_cpp -h
```

### 実行例

```bash
# 基本的な変換（Web メルカトル座標系で出力）
./convert_fgd_dem_cpp -i ./fgd_data -o ./output

# WGS84座標系でRGB可視化
./convert_fgd_dem_cpp -i ./fgd_data -o ./output -e EPSG:4326 -r true

# 海面を0mに設定してWGS84で出力
./convert_fgd_dem_cpp -i ./fgd_data -o ./output -e EPSG:4326 -z true

# ZIPファイルの展開のみ
./convert_fgd_dem_cpp -i ./fgd_data -x true

# すべてのオプションを指定（海面を0に設定）
./convert_fgd_dem_cpp \
  --input ./fgd_data \
  --output ./converted \
  --epsg EPSG:4326 \
  --rgbify true \
  --sea-at-zero true

# 変換後、DEM5Aのファイルを10m解像度でマージ
./convert_fgd_dem_cpp -i ./fgd_data -m 5A -t 10

# 変換後、DEM10Bのファイルを30m解像度でマージ
./convert_fgd_dem_cpp -i ./fgd_data -m 10B -t 30
```

### 典型的なワークフロー

基盤地図情報DEMを使用する典型的なワークフローは以下の通りです：

```bash
# ステップ1: DEM ZIPファイルをGeoTIFFに変換
./convert_fgd_dem_cpp -i ./downloaded_dem -o ./output -e EPSG:4326

# ステップ2: DEM種別ごとにマージ（例：5Aを10m解像度で）
./convert_fgd_dem_cpp -i ./downloaded_dem -m 5A -t 10

# または、シェルスクリプトを使用してマージ（下記参照）
./merge_separate_tif.sh 5A 10
```

## シェルスクリプトによるマージ

`merge_separate_tif.sh` スクリプトを使用して、`gdalwarp` コマンドで直接TIFファイルをマージすることもできます。

### 使用方法

```bash
./merge_separate_tif.sh <DEM種別> <解像度(メートル)>
```

### 引数

| 引数 | 説明 | 例 |
|------|------|-----|
| DEM種別 | マージするDEMの種類 | `1A`, `5A`, `5B`, `5C`, `10A`, `10B` |
| 解像度 | 出力解像度（メートル） | `5`, `10`, `30` |

### 実行例

```bash
# DEM5Aを10m解像度でマージ
./merge_separate_tif.sh 5A 10

# DEM10Bを30m解像度でマージ
./merge_separate_tif.sh 10B 30

# DEM5Bを5m解像度でマージ
./merge_separate_tif.sh 5B 5
```

### 出力ファイル名

- 日付付きファイルがある場合: `FG-GML-merged-DEM<種別>-<最新日付>.tif`
  - 例: `FG-GML-merged-DEM5A-20241201.tif`
- 日付がない場合: `merged_output_<解像度>m_<種別>.tif`
  - 例: `merged_output_10m_5A.tif`

### 処理内容

このスクリプトは以下の処理を行います：

1. `./output` フォルダ内から指定されたDEM種別に一致するTIFファイルを検索
2. ファイル名から最新の日付を抽出（日付付きファイルがある場合）
3. `gdalwarp` を使用して以下のオプションでマージ：
   - マルチスレッド処理（`-multi`, `NUM_THREADS=ALL_CPUS`）
   - bilinear補間（`-r bilinear`）
   - NoData値: `-9999`
   - BIGTIFF対応（大容量ファイル対応）

### 必要な依存関係

- GDAL (`gdalwarp` コマンド)
- GNU grep（macOSの場合は `ggrep`）

```bash
# Ubuntu/Debian
sudo apt install gdal-bin

# macOS (Homebrew)
brew install gdal grep
```

## プロジェクト構造

```
convert_fgd_dem-cpp/
├── CMakeLists.txt          # CMakeビルド設定
├── README.md               # このファイル
├── build.sh                # ビルドスクリプト
├── merge_separate_tif.sh   # TIFマージ用シェルスクリプト
├── include/                # ヘッダーファイル
│   ├── converter.hpp      # メイン変換クラス
│   ├── dem.hpp           # DEM データ処理
│   ├── geotiff.hpp       # GeoTIFF書き込み
│   ├── xml_parser.hpp    # XML解析
│   ├── zip_handler.hpp   # ZIP展開
│   ├── fast_fgd_parser.hpp   # 高速FGD XMLパーサー
│   ├── flat_array_2d.hpp     # 2次元配列最適化
│   ├── memory_mapped_file.hpp # メモリマップドファイル
│   ├── memory_pool.hpp       # メモリプール管理
│   ├── simd_utils.hpp        # SIMD最適化ユーティリティ
│   └── tbb_pipeline.hpp      # TBBパイプライン処理
└── src/                  # ソースファイル
    ├── main.cpp          # メインプログラム
    ├── converter.cpp     # 変換処理実装
    ├── dem.cpp           # DEM処理実装
    ├── geotiff.cpp       # GeoTIFF実装
    ├── xml_parser.cpp    # XML解析実装
    └── zip_handler.cpp   # ZIP処理実装
```

## アーキテクチャ

### 主要クラス

- **`Converter`**: メイン変換処理を統括
- **`Dem`**: DEM データの読み込みと処理
- **`GeoTiff`**: GeoTIFFファイルの作成と書き込み
- **`XmlParser`**: XML解析とデータ抽出
- **`ZipHandler`**: ZIPファイルの展開

### C++20機能の活用

- **Concepts**: テンプレート制約
- **std::span**: 軽量配列ビュー
- **三方比較演算子**: 自動比較演算子生成
- **std::filesystem**: ファイルシステム操作
- **Range-based for loops**: モダンなイテレーション

## パフォーマンス最適化

C++版は以下の最適化技術により高速な処理を実現しています：

### メモリ最適化
- **メモリマップドファイル**: 大容量ファイルの効率的な読み込み
- **メモリプール**: 動的メモリ割り当てのオーバーヘッド削減
- **Flat 2D配列**: キャッシュ効率の良いメモリレイアウト
- **std::span & move semantics**: コピーレスなデータ転送

### 並列処理
- **TBB (Threading Building Blocks)**:
  - ZIP展開の並列化（`tbb::parallel_for_each`）
  - GeoTIFF変換の並列化（`std::execution::par`）
  - パイプライン処理による効率的なデータフロー

### SIMD最適化
- **AVX2/FMA命令**: ベクトル化による高速計算
- **自動ベクトル化**: コンパイラによる最適化（`-ftree-vectorize`）
- **カスタムSIMDユーティリティ**: 数値計算の高速化

### コンパイラ最適化（Releaseビルド）
```bash
-O3                    # 最高レベルの最適化
-march=native          # CPUネイティブ命令の使用
-mavx2 -mfma          # AVX2とFMA命令の有効化
-flto                  # リンク時最適化
-ffast-math            # 高速浮動小数点演算
-funroll-loops         # ループ展開
-ftree-vectorize       # 自動ベクトル化
-fomit-frame-pointer   # フレームポインタの削減
```

### 処理フロー
1. **ZIP展開**: TBB並列処理で複数ZIPを同時展開
2. **XML解析**: 高速FGDパーサーによる効率的なデータ抽出
3. **DEM処理**: SIMD命令による高速計算
4. **GeoTIFF変換**: 並列処理による複数ファイル同時変換

## トラブルシューティング

### コンパイルエラー

#### 1. GCC バージョンが古い
```bash
# バージョン確認（11.0以降が必要）
gcc --version

# Ubuntu 20.04の場合、GCC 11をインストール
sudo apt install gcc-11 g++-11
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-11 100
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-11 100
```

#### 2. 依存関係が見つからない
```bash
# 依存関係の確認
pkg-config --exists gdal libzip && echo "OK" || echo "NG"

# GDALのバージョン確認
gdal-config --version

# 必要なパッケージの再インストール
sudo apt install --reinstall libgdal-dev libzip-dev libtbb-dev
```

#### 3. TBBが見つからない
```bash
# TBBのインストール確認
dpkg -l | grep libtbb

# TBBのインストール
sudo apt install libtbb-dev

# TBBのバージョン確認
pkg-config --modversion tbb
```

#### 4. CMake バージョンが古い
```bash
# バージョン確認（3.16以降が必要）
cmake --version

# 新しいCMakeのインストール（Ubuntu 18.04など）
sudo apt remove cmake
sudo snap install cmake --classic
```

### 実行時エラー

#### 1. ライブラリが見つからない
```bash
# 共有ライブラリの確認
ldd ./build/convert_fgd_dem_cpp

# ライブラリパスの設定
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

# 永続的に設定する場合
echo 'export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH' >> ~/.bashrc
source ~/.bashrc
```

#### 2. 出力フォルダへの書き込み権限がない
```bash
# 権限の確認
ls -ld ./output

# 権限の変更
chmod 755 ./output

# または別の出力先を指定
./convert_fgd_dem_cpp -i ./data -o ~/my_output
```

#### 3. 入力ZIPファイルの形式エラー
```bash
# ZIPファイルの整合性チェック
unzip -t your_file.zip

# ファイルが本当にZIPか確認
file your_file.zip

# 正しいFGD DEMフォーマットか確認
# （ZIPファイル内にXMLファイルが含まれているか）
unzip -l your_file.zip | grep -E "\.xml$"
```

#### 4. メモリ不足エラー
大量のファイルを処理する際にメモリ不足が発生する場合：
```bash
# システムのメモリ使用状況を確認
free -h

# スワップを一時的に増やす（要root権限）
sudo fallocate -l 4G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile

# または処理を分割して実行
./convert_fgd_dem_cpp -i ./data/batch1 -o ./output
./convert_fgd_dem_cpp -i ./data/batch2 -o ./output
```

#### 5. 並列処理でのクラッシュ
TBBの並列処理でクラッシュする場合：
```bash
# TBBのスレッド数を制限
export TBB_NUM_THREADS=4
./convert_fgd_dem_cpp -i ./data -o ./output

# または環境変数で制御
TBB_NUM_THREADS=2 ./convert_fgd_dem_cpp -i ./data -o ./output
```

## 開発者向け情報

### テスト実行

```bash
cd build
ctest --verbose
```

### デバッグビルド

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug
```

### コード分析

```bash
# Static analysis
cppcheck --enable=all src/

# Memory leak check
valgrind --leak-check=full ./convert_fgd_dem_cpp
```
