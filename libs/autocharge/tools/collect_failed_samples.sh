#!/usr/bin/env bash
set -euo pipefail

SRC_DIR=/tmp/autocharge_debug
DST_DIR=/home/pi/dolydev/libs/autocharge/tests/data/failed
mkdir -p "$DST_DIR"

count=0
for f in "$SRC_DIR"/binary_*.png "$SRC_DIR"/roi_*.png "$SRC_DIR"/frame_*_raw.jpg "$SRC_DIR"/frame_*_debug.jpg; do
  if [ -e "$f" ]; then
    base=$(basename "$f")
    ts=$(date +%s)
    cp -a "$f" "$DST_DIR/${ts}_$base"
    echo "copied $f -> $DST_DIR/${ts}_$base"
    count=$((count+1))
  fi
done

# write an index file
index="$DST_DIR/index.txt"
echo "Collected failed autocharge marker detection samples" > "$index"
echo "Source: /tmp/autocharge_debug" >> "$index"
echo "Count: $count" >> "$index"

echo "Done. $count files copied to $DST_DIR" 
