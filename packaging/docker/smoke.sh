#!/bin/sh
set -u

BIN=${BIN:-/src/build/microdict}
fail=0
say() { printf '%-42s %s\n' "$1" "$2"; }

echo "================ 1. staticness ================"
file "$BIN"
say "PT_INTERP entries" "$(readelf -l "$BIN" 2>/dev/null | grep -c INTERP) (want 0)"
say "DT_NEEDED entries" "$(objdump -p "$BIN" 2>/dev/null | awk '/NEEDED/{c++} END{print c+0}') (want 0)"
if file "$BIN" | grep -qE 'statically linked|static-pie linked'; then
  say "file: static" "yes"
else
  say "file: static" "NO - $(file -b "$BIN" | cut -c1-60)"; fail=1
fi
if readelf -d "$BIN" 2>/dev/null | grep -qE "NEEDED|RPATH|RUNPATH"; then
  say "dynamic section names" "$(readelf -d "$BIN" | grep -E 'NEEDED|RPATH|RUNPATH' | tr -s ' ' | cut -d' ' -f2- | tr '\n' ' ')"; fail=1
else
  say "dynamic section names" "nothing (no NEEDED/RPATH)"
fi
for pat in libQt6 libhunspell libxapian libopencc liblzo2 libvorbis libfmt libwayland- libpulse libEGL libGL libX11; do
  if strings "$BIN" | grep -q "$pat\.so"; then say "dynamic lib string $pat" "FOUND (bad)"; fail=1;
  else say "dynamic lib string $pat" "none"; fi
done

echo
echo "===== 2. plugins that cannot be dlopen'ed ====="
for cls in QSvgHandler QJpegHandler QGifHandler QFFmpegMediaIntegration QPulseAudioSink QOffscreenIntegration; do
  if strings "$BIN" | grep -q "$cls"; then say "class $cls" "linked in"; else say "class $cls" "MISSING"; fail=1; fi
done
out=$(QT_QPA_PLATFORM=wayland timeout 10 "$BIN" 2>&1 | head -5)
if echo "$out" | grep -qi 'could not find the Qt platform plugin "wayland"'; then
  say "platform plugin wayland" "MISSING"; fail=1
else
  say "platform plugin wayland" "linked in (no compositor here)"
fi

echo
echo "============ 3. iconv: GNU, not musl =========="
say "libiconv_open in binary"     "$(nm "$BIN" 2>/dev/null | grep -c ' [Tt] libiconv_open') (want >=1)"
say "undefined iconv_open (musl)" "$(nm "$BIN" 2>/dev/null | grep -c ' U iconv_open') (want 0)"
for enc in GB18030 Big5 EUC-JP Shift_JIS EUC-KR CP866 IBM437 Windows-1251; do
  if strings "$BIN" | grep -qi "$enc"; then say "encoding table $enc" "present"; else say "encoding table $enc" "MISSING"; fail=1; fi
done
cat > /tmp/probe.c <<'CPP'
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <iconv.h>
int main( void )
{
  static const char in[] = { (char)0xD6, (char)0xD0, (char)0xCE, (char)0xC4 };
  char out[64] = { 0 };
  char *inp = (char *)in;
  char *outp = out;
  size_t left = sizeof in, outleft = sizeof out - 1;
  iconv_t c = iconv_open( "UTF-8", "GB18030" );
  if ( c == (iconv_t)-1 ) { printf( "  iconv_open(GB18030): unsupported\n" ); return 0; }
  if ( iconv( c, &inp, &left, &outp, &outleft ) == (size_t)-1 )
    printf( "  iconv(GB18030): failed: %s\n", strerror( errno ) );
  else
    printf( "  GB18030 -> UTF-8: %s\n", out );
  iconv_close( c );
  return 0;
}
CPP
cc /tmp/probe.c -o /tmp/probe-musl 2>/dev/null && { echo "musl libc iconv:"; /tmp/probe-musl; }
cc -I/opt/gnu-iconv/include /tmp/probe.c /opt/gnu-iconv/lib/libiconv.a -o /tmp/probe-gnu 2>/dev/null \
  && { echo "GNU libiconv (what the app links):"; /tmp/probe-gnu; }

echo
echo "===== 4. weak references resolved, not zero ====="
if [ "$(nm "$BIN" 2>/dev/null | wc -l)" -lt 10 ]; then
  say "symbol table" "absent (stripped) - section 4 skipped"
else
  for sym in pthread_mutexattr_destroy thrd_exit __cxa_pure_virtual; do
    if nm "$BIN" 2>/dev/null | grep -qE " [TWtw] $sym\$"; then say "symbol $sym" "defined"
    else say "symbol $sym" "MISSING (a call would jump to 0)"; fail=1; fi
  done
  say "undefined symbols" "$(nm "$BIN" 2>/dev/null | grep -c ' U ') (want 0)"
fi

echo
echo "======== 5. fonts: the system's, never ours ========"
repoFonts=$(find /src -iname '*.ttf' -o -iname '*.otf' -o -iname '*.ttc' -o -iname '*.woff*' 2>/dev/null | wc -l)
say "font files in the tree" "$repoFonts (want 0)"
[ "$repoFonts" -eq 0 ] || fail=1
embedded=$(strings "$BIN" | grep -ciE '\.(ttf|otf|ttc)$')
say "font file names in binary" "$embedded (want 0)"
[ "$embedded" -eq 0 ] || fail=1

echo
echo "================ 6. global hotkeys: asked for, not grabbed =========="
portal=$(strings "$BIN" | grep -c 'org.freedesktop.portal.GlobalShortcuts')
say "portal interface in binary" "$portal (want >= 1)"
[ "$portal" -ge 1 ] || fail=1

for name in XGrabKey XNextEvent XRecordEnableContext; do
  n=$(strings "$BIN" | grep -c "^$name$")
  say "X11 hotkey symbol $name" "$n (want 0)"
  [ "$n" -eq 0 ] || fail=1
done

if nm "$BIN" 2>/dev/null | grep -q .; then
  dbusfns=$(nm "$BIN" | grep -c ' T dbus_')
  say "libdbus linked in" "$dbusfns dbus_ functions (want >= 100)"
  [ "$dbusfns" -ge 100 ] || fail=1
fi

echo
echo "================ 7. the app runs =============="
rm -rf /tmp/mdtest /tmp/cfg /tmp/data /tmp/cache
mkdir -p /tmp/mdtest/xdxf /tmp/cfg/microdict /tmp/data /tmp/cache
cat > /tmp/mdtest/xdxf/fruits.xdxf <<'XML'
<?xml version="1.0" encoding="UTF-8"?>
<xdxf lang_from="ENG" lang_to="ENG" format="5.0">
 <abbr/>
 <full_index/>
 <ar><k>apple</k><d><b>apple</b> (noun) A round fruit. See <a href="bword://banana">banana</a>.</d></ar>
 <ar><k>banana</k><d><b>banana</b> (noun) A long curved tropical fruit.</d></ar>
</xdxf>
XML
cat > /tmp/mdtest/xdxf/fruits.dcl <<'DCL'
language-orig=ENG
language-target=ENG
DCL
cat > /tmp/cfg/microdict/config <<'XML'
<config>
 <paths>
  <path recursive="1">/tmp/mdtest/xdxf</path>
 </paths>
 <preferences>
  <enableClipboardHotkey>1</enableClipboardHotkey>
  <clipboardHotkey>Ctrl+F11, Ctrl+F11</clipboardHotkey>
  <autoStart>1</autoStart>
 </preferences>
</config>
XML

export XDG_CONFIG_HOME=/tmp/cfg XDG_DATA_HOME=/tmp/data XDG_CACHE_HOME=/tmp/cache
export QT_QPA_PLATFORM=offscreen
export LANG=de_DE.UTF-8 LC_ALL=de_DE.UTF-8

# A menu entry under a name this program did not choose for itself, the way an
# AppImage integrated by a launcher gets one. The login entry has to borrow it.
mkdir -p /tmp/data/applications
cat > /tmp/data/applications/appimagekit_cafebabe-MicroDict.desktop <<EOF
[Desktop Entry]
Type=Application
Name=MicroDict (smoke)
Icon=appimagekit_cafebabe_microdict
Exec=$BIN %U
TryExec=$BIN
Actions=remove;

[Desktop Action remove]
Name=Delete this AppImage
Exec=/bin/true
EOF

# And a menu entry of some other program whose name only happens to contain the
# letters -- the login entry must not borrow that one.
cat > /tmp/data/applications/notmicrodict.desktop <<EOF
[Desktop Entry]
Type=Application
Name=Not MicroDict
Exec=/bin/true
EOF

timeout 60 "$BIN" > /tmp/md.log 2>&1 &
APP=$!
sleep 20
"$BIN" apple >/dev/null 2>&1
sleep 5
kill -TERM $APP 2>/dev/null
sleep 4

grep -i "article view loaded\|receive dicts\|current word" /tmp/md.log | tail -4
grep -qi "article view loaded" /tmp/md.log || { echo "!! no article was rendered"; fail=1; }
grep -q ':/locale/de.qm' /tmp/md.log \
  || { echo "!! LANG=de_DE.UTF-8 did not pull the catalogue out of the resource bundle"; fail=1; }
echo "--- app warnings/errors ---"
grep -iE "^(ERROR|WARNING)|qWarning|Global hotkey" /tmp/md.log | sort -u | head -10
echo
echo "--- autostart: the entry the 'Start with system' preference writes ---"
# it has to land in the config dir the desktop reads, name this very binary,
# and keep the menu's delete actions out of a login entry. Its name is not a
# label either: systemd's autostart generator names the login unit after the
# file, xdg-desktop-portal reads the application id out of that unit name, and
# the GlobalShortcuts portal only serves a process whose id resolves to a menu
# entry in an applications dir. So the login entry takes the name of the menu
# entry that is already there -- here the launcher-style one planted above.
autostart=/tmp/cfg/autostart/appimagekit_cafebabe-MicroDict.desktop
say "login entry named after the menu entry" "$([ -f "$autostart" ] && echo yes || echo MISSING)"
[ -f "$autostart" ] || fail=1
# a name of its own would generate a second login unit whose id the portal
# cannot resolve, and the old name would generate a third
for stale in io.github.dsvi.microdict.desktop microdict-owned-by-preferences.desktop notmicrodict.desktop; do
  [ -f "/tmp/cfg/autostart/$stale" ] \
    && { echo "!! a second login entry is being written: $stale"; fail=1; }
done
grep -qF "Exec=\"$BIN\" %u" "$autostart" \
  || { echo "!! the login entry does not start $BIN"; fail=1; }
grep -q '^X-MicroDict-LoginEntry=' "$autostart" \
  || { echo "!! the login entry carries no mark, so a later run cannot find it"; fail=1; }
grep -q '^\[Desktop Action' "$autostart" \
  && { echo "!! the menu's delete actions leaked into the login entry"; fail=1; }
head -3 "$autostart"
echo
echo "===== 8. colours: the desktop's, not the fork's ====="
for pat in kdeglobals org.freedesktop.portal.Settings; do
  if strings "$BIN" | grep -q "$pat"; then
    say "theme source $pat" "linked in"
  else
    say "theme source $pat" "MISSING"
    fail=1
  fi
done
if strings "$BIN" | grep -q QXdgDesktopPortalTheme; then
  say "Qt portal theme plugin" "linked in"
else
  say "Qt portal theme plugin" "MISSING"
  fail=1
fi

if strings "$BIN" | grep -q org.freedesktop.portal.FileChooser; then
  say "portal file chooser" "linked in"
else
  say "portal file chooser" "MISSING"
  fail=1
fi
grep -i "Interface appearance:" /tmp/md.log | tail -2
grep -qi "Interface appearance:" /tmp/md.log || { echo "!! the app never reported its appearance"; fail=1; }
grep -i "colour scheme" /tmp/md.log | tail -2

grep -i "File chooser:" /tmp/md.log | tail -1
grep -qi 'File chooser: Qt.s own' /tmp/md.log \
  || { echo "!! the app never reported which file chooser it uses"; fail=1; }

echo
echo "=== config after shutdown ==="
python3 - <<'PY'
import xml.dom.minidom
d = xml.dom.minidom.parse("/tmp/cfg/microdict/config")
for k in ("enableClipboardHotkey", "clipboardHotkey"):
    els = d.getElementsByTagName(k)
    print(f"  {k} =", els[0].firstChild.data if els and els[0].firstChild else "MISSING")
PY

echo
[ $fail -eq 0 ] && echo "SMOKE OK" || echo "SMOKE FAILED"
exit $fail
