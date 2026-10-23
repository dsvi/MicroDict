#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/../.."

IMAGE=${IMAGE:-microdict-static:latest}

docker build -f packaging/docker/Dockerfile -t "$IMAGE" "$@" .

id=$(docker create "$IMAGE")
mkdir -p build-out
docker cp "$id:/usr/local/bin/microdict" build-out/microdict
docker cp "$id:/out/." build-out/
docker rm "$id" >/dev/null

docker build -q --target deps -t "${IMAGE%:*}-deps:latest" -f packaging/docker/Dockerfile "$@" . >/dev/null
pid=$(docker create "${IMAGE%:*}-deps:latest")
docker cp "$pid:/opt/portalmock" build-out/portalmock
docker rm "$pid" >/dev/null

HI='\033[0;32m'
NC='\033[0m'

echo
echo " =^..^=   =^..^=   =^..^=    =^..^=    =^..^=    =^..^=    =^..^="
echo
APPIMAGE=$(ls build-out/*.AppImage 2>/dev/null | head -1)
echo -e "Output is at ${HI}./build-out/microdict${NC} ($(stat -c %s build-out/microdict) bytes)"
echo -e "... and at ${HI}./${APPIMAGE}${NC} ($(stat -c %s "$APPIMAGE") bytes): the same"
echo -e "binary with its desktop entry and icon around it -- the Linux deliverable,"
echo -e "runnable from a file manager or a shell, needing nothing else. The catalogues"
echo -e "are linked into the binary, so there is no locale/ folder to keep next to it."
echo -e "Fully static (musl): no shared libraries, no libc, no interpreter -"
echo -e "it runs on any x86-64 Linux, glibc or musl."
echo -e "Wayland only (no xcb plugin) and software rendered - no OpenGL/EGL/mesa."
echo -e "Audio goes through libpulse, so PulseAudio or PipeWire is needed for it."
echo -e "Qt's qsvg image-format plugin is linked in, so SVG in articles renders."
echo -e "./build-out/portalmock is the stand-in desktop portal: --serve drives the"
echo -e "global hotkeys and --serve-theme flips the desktop's colours under a live"
echo -e "instance, which is how the theme following gets watched on screen."
