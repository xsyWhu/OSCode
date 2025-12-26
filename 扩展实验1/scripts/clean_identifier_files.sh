#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<EOF
Usage: $0 [--dry-run|-n] [--yes|-y] [path]

Options:
  -n, --dry-run   Show files that would be deleted, do not delete
  -y, --yes       Do not prompt for confirmation
  path            Directory to scan (default: current directory)
EOF
  exit 1
}

DRY=0
YES=0
TARGET="."

while [ "$#" -gt 0 ]; do
  case "$1" in
    -n|--dry-run) DRY=1; shift ;;
    -y|--yes) YES=1; shift ;;
    -h|--help) usage ;;
    *) TARGET="$1"; shift ;;
  esac
done

# collect files (null-separated)
mapfile -d '' files < <(find "$TARGET" -type f -name "*.Identifier" -print0)
count=${#files[@]}

if [ "$count" -eq 0 ]; then
  echo "No .Identifier files found under: $TARGET"
  exit 0
fi

echo "Found $count .Identifier file(s):"
for f in "${files[@]}"; do
  printf '%s\n' "$f"
done

if [ "$DRY" -eq 1 ]; then
  echo "Dry-run: no files were deleted."
  exit 0
fi

if [ "$YES" -ne 1 ]; then
  printf "Delete these %d files? [y/N] " "$count"
  read -r ans
  case "$ans" in
    [yY]|[yY][eE][sS]) ;;
    *) echo "Aborted."; exit 0 ;;
  esac
fi

# perform deletion
printf "Deleting...\n"
# use null-separated stream to safely handle spaces/newlines
printf '%s\0' "${files[@]}" | xargs -0 rm -v --

echo "Done."
