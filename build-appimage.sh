#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

version=$(tr -d '[:space:]' < VERSION)
out=build-out
mkdir -p "$out"

docker buildx build --target dist --output "type=local,dest=$out" \
  -f packaging/docker/Dockerfile .

appimage="$out/MicroDict-$version-$(uname -m).AppImage"
echo
printf -- '--- %s ---\n' "$appimage"
ls -l "$appimage"
printf 'sha256  %s\n' "$(sha256sum "$appimage" | cut -d' ' -f1)"
