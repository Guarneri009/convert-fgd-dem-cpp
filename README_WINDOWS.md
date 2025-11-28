# FGD-DEM to GeoTIFF Converter - Windows版

基盤地図情報DEMデータ（FG-GML形式のZIPファイル）をGeoTIFF形式に変換するツールです。

---

## ビルド方法（開発者向け）

### 必要なソフトウェア

- **Visual Studio 2022** (Desktop development with C++ ワークロード)
- **CMake 3.21以上** (Visual Studio に含まれています)
- **vcpkg** (パッケージマネージャー)

### 1. vcpkg のインストール

```cmd
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
cd C:\vcpkg
.\bootstrap-vcpkg.bat
```

環境変数を設定：
```cmd
setx VCPKG_ROOT "C:\vcpkg"
```

**重要**: 設定後、コマンドプロンプトを再起動してください。

### 2. 依存パッケージのインストール

```cmd
cd C:\vcpkg
.\vcpkg install gdal:x64-windows-static libzip:x64-windows-static tbb:x64-windows-static
```

**注意**: GDALのビルドには時間がかかります（30分〜1時間程度）。

### 3. プロジェクトのビルド

#### Visual Studio を使う場合

**方法A: CMake プリセットを使う（推奨）**

```cmd
cd プロジェクトのフォルダ
cmake --preset windows-x64-release
cmake --build build-vs --config Release
```

**方法B: Visual Studio から直接開く**

1. Visual Studio 2022 を起動
2. 「フォルダーを開く」でプロジェクトフォルダを選択
3. CMake が自動的に設定を検出
4. ビルド → すべてビルド

#### Ninja を使う場合（高速）

Developer Command Prompt for VS 2022 を開いて：

```cmd
cd プロジェクトのフォルダ
cmake --preset windows-ninja-release
cmake --build build-ninja
```

### 4. ビルド成果物

ビルド後、実行ファイルは以下に生成されます：
- Visual Studio: `build-vs\Release\convert_fgd_dem_cpp.exe`
- Ninja: `build-ninja\convert_fgd_dem_cpp.exe`

---

## 実行に必要なソフトウェア（ユーザー向け）

このツールを使用するには、**OSGeo4W**が必要です。

### OSGeo4Wのインストール

1. **OSGeo4Wをダウンロード**
   - https://trac.osgeo.org/osgeo4w/ にアクセス
   - `OSGeo4W Network Installer (64bit)` をダウンロード

2. **OSGeo4Wをインストール**
   - ダウンロードした `osgeo4w-setup.exe` を実行
   - **Express Desktop Install** を選択（推奨）
   - または **Advanced Install** で以下を選択：
     - GDAL
     - PROJ
     - SQLite
   - デフォルトのインストール先： `C:\OSGeo4W64\`

3. **環境変数を設定（重要）**

   コマンドプロンプトから使用するには、システムPATHに追加します：

   **方法1: 自動設定スクリプト（簡単）**

   管理者としてコマンドプロンプトを開き：
   ```cmd
   setx /M PATH "%PATH%;C:\OSGeo4W64\bin"
   ```

   **方法2: 手動設定**
   1. 「スタート」→「システムのプロパティ」→「環境変数」
   2. システム環境変数の「Path」を選択し「編集」
   3. 「新規」をクリックして `C:\OSGeo4W64\bin` を追加
   4. OKをクリックして閉じる

   設定後、**コマンドプロンプトを再起動**してください。

## ツールのインストール

1. **ZIPファイルをダウンロード**
   - GitHubのReleasesページから `convert-fgd-dem-cpp-windows-x64-osgeo4w.zip` をダウンロード

2. **展開する**
   - 好きな場所に展開してください（例：`C:\tools\fgd-dem-converter\`）
   - 展開するだけで完了です！

## 使い方

### 方法1: バッチファイルを使う（簡単）

コマンドプロンプトまたはPowerShellを開いて：

```cmd
cd C:\tools\fgd-dem-converter
convert.bat 入力ファイル.zip 出力ファイル.tif
```

**例：**
```cmd
convert.bat FG-GML-5339-45-DEM10B-20250101.zip output.tif
```

### 方法2: 直接EXEを実行

```cmd
convert_fgd_dem_cpp.exe 入力ファイル.zip 出力ファイル.tif
```

### 方法3: エクスプローラーからドラッグ&ドロップ

1. `convert.bat` を作成：
```batch
@echo off
convert_fgd_dem_cpp.exe %1 output.tif
pause
```

2. ZIPファイルを `convert.bat` にドラッグ&ドロップ

## オプション

### ヘルプを表示

```cmd
convert_fgd_dem_cpp.exe --help
```

### 複数ファイルを一度に変換

```cmd
for %f in (*.zip) do convert_fgd_dem_cpp.exe "%f" "%~nf.tif"
```

## トラブルシューティング

### 「gdal*.dllが見つかりません」エラーが出る

→ OSGeo4Wが正しくインストールされていないか、PATHが設定されていません。
   1. `C:\OSGeo4W64\bin` フォルダが存在するか確認
   2. システムのPATH環境変数に `C:\OSGeo4W64\bin` が含まれているか確認
   3. コマンドプロンプトを再起動

   確認方法：
   ```cmd
   where gdal_translate
   ```
   → `C:\OSGeo4W64\bin\gdal_translate.exe` と表示されればOK

### 「プログラムを開始できません」エラーが出る

→ Windows Defenderや他のセキュリティソフトがブロックしている可能性があります。
   - ファイルを右クリック → プロパティ → 「許可する」にチェック

### OSGeo4W Shellを使う方法（PATH設定不要）

環境変数を設定したくない場合は、OSGeo4W Shellを使用できます：

1. スタートメニューから「OSGeo4W Shell」を起動
2. ツールのフォルダに移動：
   ```bash
   cd /c/tools/fgd-dem-converter
   ```
3. ツールを実行：
   ```bash
   ./convert_fgd_dem_cpp.exe input.zip output.tif
   ```

### パスにスペースが含まれる場合

ファイルパスを引用符で囲んでください：

```cmd
convert_fgd_dem_cpp.exe "C:\My Documents\data.zip" "C:\My Documents\output.tif"
```

## システム要件

- Windows 10/11（64bit）
- OSGeo4W 64bit版

## フォルダ構成

```
convert-fgd-dem-cpp-windows-x64-osgeo4w/
├── convert_fgd_dem_cpp.exe  # メインプログラム
├── convert.bat               # 簡単に使うためのバッチファイル
├── README_WINDOWS.md        # このファイル
└── LICENSE                  # ライセンス情報
```

**注意**: DLLファイルは含まれていません。OSGeo4Wのインストールが必要です。

## よくある質問

**Q: なぜOSGeo4Wが必要なのですか？**
A: このツールはGDAL（地理空間データライブラリ）を使用しています。OSGeo4WはGDALとその依存関係を提供します。

**Q: OSGeo4W以外の方法でGDALをインストールできますか？**
A: はい、conda、vcpkg、または独自ビルドも可能ですが、OSGeo4Wが最も簡単です。

**Q: 他のPCで使えますか？**
A: はい、ただし各PCにOSGeo4Wをインストールする必要があります。

**Q: アンインストール方法は？**
A:
   1. ツールのフォルダを削除
   2. OSGeo4Wもアンインストールする場合は、コントロールパネルからアンインストール

**Q: 32bit版はありますか？**
A: 現在は64bit版のみです。

**Q: DLL同梱版はありますか？**
A: OSGeo4W版のみを提供しています。これにより、ファイルサイズが小さく、GDALのアップデートにも対応できます。

## ライセンス

このソフトウェアはオープンソースです。詳細は `LICENSE` ファイルを参照してください。

## お問い合わせ

問題が発生した場合は、GitHubのIssuesページで報告してください：
https://github.com/your-repo/convert-fgd-dem-cpp/issues
