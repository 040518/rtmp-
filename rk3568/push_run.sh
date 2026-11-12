#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <aarch64 executable>" >&2
    exit 2
fi

source_path="$1"

if [ ! -f "$source_path" ]; then
    echo "ERROR: executable not found: $source_path" >&2
    exit 2
fi

adb_path="/home/hhh/tools/platform-tools/adb"
remote_dir="/root/edge-vision-media"
remote_name="$(basename "$source_path")"
remote_path="$remote_dir/$remote_name"

echo "push: $source_path -> $remote_path"
"$adb_path" push "$source_path" "$remote_path"

echo "run:  $remote_path"
"$adb_path" shell "chmod 755 '$remote_path' && '$remote_path'"

