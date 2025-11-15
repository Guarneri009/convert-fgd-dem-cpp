#!/bin/bash

echo $1 $2

# 基盤地図情報DEMは、10B以外は全国の陸域をカバーしているわけではありません。また、標高値の精度は次の順で高くなります。
# 低: 10B < 10A < 5C < 5B < 5A: 高

# マージ対象のフォルダー
INPUT_FOLDER="./output"
# 出力ファイル名
OUTPUT_FILE="merged_output_$2m_$1.tif"
# 入力ファイルを変数に格納
INPUT_FILES=$(find "$INPUT_FOLDER" -type f -name "*$1.tif")
printf "%s\n" $INPUT_FILES
gdalwarp -multi -wo NUM_THREADS=ALL_CPUS -tr $2 $2 -r bilinear -overwrite -srcnodata -9999 -dstnodata -9999 -co BIGTIFF=YES $INPUT_FILES "$OUTPUT_FILE"
# 完了メッセージ
echo "Merging completed. Output saved to $OUTPUT_FILE"

