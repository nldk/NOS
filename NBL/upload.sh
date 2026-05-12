#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
IMAGE="$SCRIPT_DIR/hdd.img"
DEST="/"
FORMAT=0
SIZE_MB=64

usage() {
    echo "Usage: $0 [-i image] [-d dest_dir] [-f] <src> [<src> ...]"
    echo "  -i image     Path to ext2 image (default: $IMAGE)"
    echo "  -d dest_dir  Destination directory inside image (default: /)"
    echo "  -f           Format image as ext2 (will overwrite contents)"
}

while getopts ":i:d:fh" opt; do
    case "$opt" in
        i) IMAGE="$OPTARG" ;;
        d) DEST="$OPTARG" ;;
        f) FORMAT=1 ;;
        h) usage; exit 0 ;;
        *) usage; exit 1 ;;
    esac
done
shift $((OPTIND - 1))

if [ "$#" -lt 1 ]; then
    usage
    exit 1
fi

if ! command -v mke2fs >/dev/null 2>&1; then
    echo "mke2fs not found. Install e2fsprogs."
    exit 1
fi
if ! command -v e2cp >/dev/null 2>&1 || ! command -v e2mkdir >/dev/null 2>&1; then
    echo "e2cp/e2mkdir not found. Install e2tools."
    exit 1
fi

if [ ! -f "$IMAGE" ]; then
    FORMAT=1
fi

if [ "$FORMAT" -eq 1 ]; then
    echo "Creating ext2 image at $IMAGE (${SIZE_MB}MiB)"
    dd if=/dev/zero of="$IMAGE" bs=1M count="$SIZE_MB" status=none
    mke2fs -t ext2 -b 4096 -I 256 -F "$IMAGE" >/dev/null
fi

if [ -z "$DEST" ]; then
    DEST="/"
fi

case "$DEST" in
    /*) : ;;
    *) DEST="/$DEST" ;;
esac

ensure_dir() {
    local path="$1"
    local cur=""
    IFS='/' read -r -a parts <<< "$path"
    for part in "${parts[@]}"; do
        if [ -z "$part" ]; then
            continue
        fi
        cur="$cur/$part"
        e2mkdir "$IMAGE:$cur" >/dev/null 2>&1 || true
    done
}

ensure_dir "$DEST"

for src in "$@"; do
    if [ ! -f "$src" ]; then
        echo "Skipping non-file: $src"
        continue
    fi
    base="$(basename "$src")"
    target="$DEST"
    if [ "$target" != "/" ]; then
        target="$target/$base"
    else
        target="/$base"
    fi
    echo "Copying $src -> $target"
    e2cp "$src" "$IMAGE:$target"
done

echo "Done."
