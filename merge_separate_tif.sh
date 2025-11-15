#!/bin/bash

echo $1 $2

# 基盤地図情報DEMは、10B以外は全国の陸域をカバーしているわけではありません。また、標高値の精度は次の順で高くなります。
# 低: 10B < 10A < 5C < 5B < 5A: 高

# マージ対象のフォルダー
INPUT_FOLDER="./output"

# 最新の日付を取得（入力ファイルから抽出）
LATEST_DATE=$(find "$INPUT_FOLDER" -type f -name "*DEM$1-*.tif" | grep -oP 'DEM'"$1"'-\K[0-9]{8}' | sort -r | head -1)

# 出力ファイル名（FG-GML形式に対応）
if [ -n "$LATEST_DATE" ]; then
    OUTPUT_FILE="FG-GML-merged-DEM$1-$LATEST_DATE.tif"
else
    # 日付が取得できない場合は従来の形式
    OUTPUT_FILE="merged_output_$2m_$1.tif"
fi

# 入力ファイルを変数に格納（DEM種別でフィルタ、ハイフン後の日付も含む）
INPUT_FILES=$(find "$INPUT_FOLDER" -type f -name "*DEM$1-*.tif")
printf "%s\n" $INPUT_FILES
gdalwarp -multi -wo NUM_THREADS=ALL_CPUS -tr $2 $2 -r bilinear -overwrite -srcnodata -9999 -dstnodata -9999 -co BIGTIFF=YES $INPUT_FILES "$OUTPUT_FILE"
# 完了メッセージ
echo "Merging completed. Output saved to $OUTPUT_FILE"

