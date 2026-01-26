#!/bin/bash

echo $1 $2

# 基盤地図情報DEMは、10B以外は全国の陸域をカバーしているわけではありません。また、標高値の精度は次の順で高くなります。
# 低: 10B < 10A < 5C < 5B < 5A: 高

# マージ対象のフォルダー
INPUT_FOLDER="./output"

# GNU grepを使用（macOSではggrep、Linuxではgrep）
if command -v ggrep &> /dev/null; then
    GREP_CMD="ggrep"
else
    GREP_CMD="grep"
fi

# 入力ファイルを変数に格納（DEM種別でフィルタ）
# ファイル名パターン: FG-GML-*-DEM5A.tif または *DEM5A-日付.tif
INPUT_FILES=$(find "$INPUT_FOLDER" -type f \( -name "*-DEM$1.tif" -o -name "*DEM$1-*.tif" \))

# ファイルが見つからない場合はエラー
if [ -z "$INPUT_FILES" ]; then
    echo "Error: No files found matching pattern *-DEM$1.tif or *DEM$1-*.tif in $INPUT_FOLDER"
    exit 1
fi

# 最新の日付を取得（入力ファイルから抽出、日付付きファイルがある場合のみ）
LATEST_DATE=$(echo "$INPUT_FILES" | $GREP_CMD -oP 'DEM'"$1"'-\K[0-9]{8}' | sort -r | head -1)

# 出力ファイル名
if [ -n "$LATEST_DATE" ]; then
    OUTPUT_FILE="FG-GML-merged-DEM$1-$LATEST_DATE.tif"
else
    # 日付が取得できない場合は従来の形式
    OUTPUT_FILE="merged_output_$2m_$1.tif"
fi
printf "%s\n" $INPUT_FILES
gdalwarp -multi -wo NUM_THREADS=ALL_CPUS -tr $2 $2 -r bilinear -overwrite -srcnodata -9999 -dstnodata -9999 -co BIGTIFF=YES $INPUT_FILES "$OUTPUT_FILE"
# 完了メッセージ
echo "Merging completed. Output saved to $OUTPUT_FILE"

