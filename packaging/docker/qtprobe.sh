#!/bin/sh
set -u

SRC=${SRC:-/src}
QT_PREFIX=${QT_PREFIX:-/opt/qt}

cmake -B /tmp/qtprobe-build -S "$SRC/packaging/docker/qtprobe" -GNinja \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_PREFIX" >/tmp/qtprobe-cmake.log 2>&1 \
  || { echo "configure failed:"; tail -20 /tmp/qtprobe-cmake.log; exit 1; }

cmake --build /tmp/qtprobe-build >/tmp/qtprobe-build.log 2>&1 \
  || { echo "build failed:"; grep -E "error|undefined reference" /tmp/qtprobe-build.log | head -20; exit 1; }

echo "=== probe binary ==="
file /tmp/qtprobe-build/qtprobe
ldd /tmp/qtprobe-build/qtprobe 2>&1 | head -2

echo
echo "=== probe output ==="
QT_QPA_PLATFORM=offscreen /tmp/qtprobe-build/qtprobe
status=$?

echo
[ $status -eq 0 ] && echo "qtprobe.sh: static Qt checks passed" \
                || echo "qtprobe.sh: static Qt checks FAILED"
exit $status
