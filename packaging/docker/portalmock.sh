#!/bin/sh
set -u

SRC=${SRC:-/src}

cmake -B /tmp/portalmock-build -S "$SRC/packaging/docker/portalmock" -GNinja \
      -DCMAKE_BUILD_TYPE=Release -DMICRODICT_STATIC_QT=ON \
      -DCMAKE_PREFIX_PATH="${QT_PREFIX:-/opt/qt}" >/tmp/portalmock-cmake.log 2>&1 \
  || { echo "configure failed:"; tail -20 /tmp/portalmock-cmake.log; exit 1; }

cmake --build /tmp/portalmock-build >/tmp/portalmock-build.log 2>&1 \
  || { echo "build failed:"; grep -E "error|undefined reference" /tmp/portalmock-build.log | head -20; exit 1; }

echo "=== mock portal binary ==="
file /tmp/portalmock-build/portalmock
ldd /tmp/portalmock-build/portalmock 2>&1 | head -2

if ! command -v dbus-run-session >/dev/null 2>&1; then
    echo
    echo "portalmock.sh: no dbus-run-session in this image, cannot run the test"
    exit 1
fi

echo
echo "=== portal client against a portal ==="
dbus-run-session -- /tmp/portalmock-build/portalmock
status=$?

echo
echo "=== file dialogs through the portal ==="
dbus-run-session -- /tmp/portalmock-build/portalmock --files
status3=$?

echo
echo "=== portal client against nothing ==="
dbus-run-session -- /tmp/portalmock-build/portalmock --no-portal
status2=$?

echo
[ $status -eq 0 ] && [ $status2 -eq 0 ] && [ $status3 -eq 0 ] \
                && echo "portalmock.sh: portal client checks passed (hotkeys, colours, file chooser)" \
                || echo "portalmock.sh: portal client checks FAILED"
exit $(( status + status2 + status3 ))
