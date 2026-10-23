#!/bin/sh
set -eu

SRC=${SRC:-/src}
BIN=${BIN:-$SRC/build/microdict}
DESKTOP=${DESKTOP:-$SRC/redist/io.github.dsvi.microdict.desktop}
ICON=${ICON:-$SRC/redist/icons/microdict.png}
OPENCC_DATA=${OPENCC_DATA:-/usr/share/opencc}
OUT=${OUT:-$SRC/build}

APP_ID=io.github.dsvi.microdict
APP_NAME=MicroDict
DATA_NAME=microdict
ICON_NAME=microdict
ARCH=${ARCH:-x86_64}
RUNTIME_RELEASE=20251108
VERSION=$(tr -d '[:space:]' < "$SRC/VERSION")
APPIMAGE="$OUT/$APP_NAME-$VERSION-$ARCH.AppImage"
APPDIR=/tmp/AppDir

case "$ARCH" in
  x86_64) RUNTIME_SHA256=${RUNTIME_SHA256:-2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d} ;;
  aarch64) RUNTIME_SHA256=${RUNTIME_SHA256:-00cbdfcf917cc6c0ff6d3347d59e0ca1f7f45a6df1a428a0d6d8a78664d87444} ;;
  *) echo "unsupported ARCH: $ARCH"; exit 1 ;;
esac
RUNTIME_URL=${RUNTIME_URL:-https://github.com/AppImage/type2-runtime/releases/download/$RUNTIME_RELEASE/runtime-$ARCH}

fail=0
say() { printf '%-40s %s\n' "$1" "$2"; }

for f in "$BIN" "$DESKTOP" "$ICON" "$SRC/VERSION"; do
  [ -e "$f" ] || { echo "missing input: $f"; exit 1; }
done

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps"

cp "$BIN" "$APPDIR/usr/bin/$DATA_NAME"
cp "$DESKTOP" "$APPDIR/usr/share/applications/$APP_ID.desktop"
cp "$DESKTOP" "$APPDIR/$APP_ID.desktop"
cp "$ICON" "$APPDIR/$ICON_NAME.png"
cp "$ICON" "$APPDIR/usr/share/icons/hicolor/256x256/apps/$ICON_NAME.png"
ln -s "$ICON_NAME.png" "$APPDIR/.DirIcon"

if [ -d "$OPENCC_DATA" ]; then
  mkdir -p "$APPDIR/usr/share/$DATA_NAME/opencc"
  cp "$OPENCC_DATA"/*.json "$OPENCC_DATA"/*.ocd2 "$APPDIR/usr/share/$DATA_NAME/opencc/"
fi

cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/sh
APPDIR=${APPDIR:-$(dirname "$(readlink -f "$0")")}
export APPDIR
PATH="$APPDIR/usr/bin:$PATH"
XDG_DATA_DIRS="$APPDIR/usr/share${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"
export PATH XDG_DATA_DIRS
exec "$APPDIR/usr/bin/microdict" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"

say "AppDir binary" "$(stat -c %s "$APPDIR/usr/bin/$DATA_NAME") bytes"
say "opencc data files" "$(ls "$APPDIR/usr/share/$DATA_NAME/opencc" 2>/dev/null | wc -l)"

if command -v desktop-file-validate >/dev/null 2>&1; then
  if desktop-file-validate "$APPDIR/$APP_ID.desktop"; then
    say "desktop entry" "valid"
  else
    say "desktop entry" "INVALID"; fail=1
  fi
fi

runtime=/tmp/runtime-$ARCH
curl -fsSL --retry 3 -o "$runtime" "$RUNTIME_URL"
echo "$RUNTIME_SHA256  $runtime" | sha256sum -c -
chmod +x "$runtime"
size=$(stat -c %s "$runtime")

rm -f "$APPIMAGE"
mksquashfs "$APPDIR" "$APPIMAGE" -offset "$size" -comp zstd -b 1048576 \
           -root-owned -noappend -mkfs-time 0 >/dev/null
dd if="$runtime" of="$APPIMAGE" conv=notrunc bs=65536 status=none
chmod 755 "$APPIMAGE"

echo
echo "================ the AppImage ================"
file "$APPIMAGE"
say "size" "$(stat -c %s "$APPIMAGE") bytes"
say "runtime offset" "$size bytes"

if [ "$(dd if="$APPIMAGE" bs=1 skip="$size" count=4 2>/dev/null | od -An -c | tr -d ' \n')" = "hsqs" ]; then
  say "squashfs magic at offset" "hsqs"
else
  say "squashfs magic at offset" "MISSING -- payload is not where the runtime looks"; fail=1
fi

if readelf -d "$APPIMAGE" 2>/dev/null | grep -qE "NEEDED|RPATH|RUNPATH"; then
  say "runtime dynamic section" "$(readelf -d "$APPIMAGE" | grep -E 'NEEDED|RPATH|RUNPATH' | tr -s ' ' | cut -d' ' -f2- | tr '\n' ' ')"; fail=1
else
  say "runtime dynamic section" "names nothing to load"
fi

rm -rf /tmp/appimage-verify && mkdir -p /tmp/appimage-verify
(cd /tmp/appimage-verify && "$APPIMAGE" --appimage-extract >/dev/null 2>&1)
root=/tmp/appimage-verify/squashfs-root
if [ -x "$root/AppRun" ] && [ -f "$root/usr/bin/$DATA_NAME" ] && \
   [ -f "$root/$APP_ID.desktop" ] && [ -f "$root/$ICON_NAME.png" ]; then
  say "extracted tree" "AppRun, binary, desktop entry, icon"
else
  say "extracted tree" "INCOMPLETE: $(ls "$root" 2>/dev/null | tr '\n' ' ')"; fail=1
fi
if [ -f "$root/usr/share/icons/hicolor/256x256/apps/$ICON_NAME.png" ]; then
  say "extracted hicolor icon" "there: Icon= and the tray resolve it by name"
else
  say "extracted hicolor icon" "MISSING -- Icon= and the tray fall back to the built-in one"; fail=1
fi
if grep -q XDG_DATA_DIRS "$root/AppRun"; then
  say "AppRun publishes the icon dir" "yes"
else
  say "AppRun publishes the icon dir" "NO -- nothing looks inside the image"; fail=1
fi
say "extracted opencc files" "$(ls "$root/usr/share/$DATA_NAME/opencc" 2>/dev/null | wc -l)"
if cmp -s "$BIN" "$root/usr/bin/$DATA_NAME"; then
  say "binary inside == binary built" "identical"
else
  say "binary inside == binary built" "DIFFERENT"; fail=1
fi

out=$(APPIMAGE_EXTRACT_AND_RUN=1 QT_QPA_PLATFORM=offscreen \
      XDG_CONFIG_HOME=/tmp/ai-cfg XDG_CACHE_HOME=/tmp/ai-cache \
      timeout 60 "$APPIMAGE" --version 2>&1)
echo
echo "--- \$APPIMAGE --version ---"
echo "$out" | head -6
echo "$out" | grep -q "Version:" || { echo "!! the AppImage did not report a version"; fail=1; }
echo "$out" | grep -q "Qt 6" || { echo "!! the AppImage did not report Qt"; fail=1; }
rm -rf /tmp/ai-loc && mkdir -p /tmp/ai-loc
loc=$(APPIMAGE_EXTRACT_AND_RUN=1 QT_QPA_PLATFORM=offscreen LANG=de_DE.UTF-8 LC_ALL=de_DE.UTF-8 \
      XDG_CONFIG_HOME=/tmp/ai-loc XDG_CACHE_HOME=/tmp/ai-cache \
      timeout 30 "$APPIMAGE" 2>&1 || true)
if echo "$loc" | grep -q ':/locale/de.qm'; then
  say "built-in translations" "de.qm loaded from the resource"
else
  say "built-in translations" "NOT LOADED -- no :/locale/de.qm with LANG=de_DE.UTF-8"; fail=1
fi

echo
[ "$fail" -eq 0 ] && echo "APPIMAGE OK: $APPIMAGE" || echo "APPIMAGE FAILED"
[ "$fail" -eq 0 ]
