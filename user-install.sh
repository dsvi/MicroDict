#!/bin/sh
#
# user-install.sh -- build MicroDict statically in Docker, then install it for
# the current user into the XDG paths under $HOME:
#
#   ~/.local/bin/microdict              the static binary
#   ~/.local/bin/microdict-uninstall    its remover
#   ~/.local/share/applications/io.github.dsvi.microdict.desktop
#   ~/.local/share/icons/hicolor/256x256/apps/microdict.png
#
# No root, no package manager, nothing under /usr. The uninstall story is the
# desktop entry itself: its two Desktop Actions are the submenu a launcher
# shows when MicroDict is right-clicked --
#
#   Delete MicroDict          the binary, the icon, the entry and the remover
#   Delete with user data     those, plus microdict/ in the XDG paths
#
# Both actions run microdict-uninstall. A Desktop Action's Exec is read by
# several independent parsers and nested shell quoting does not survive them
# (GLib will not even load an entry whose Exec holds a backslash-quote), so the
# Exec stays a plain path plus a flag and the asking and the deleting live in
# the script it runs.
#
# Dictionary files are never touched: they sit wherever their owner put them
# and the config only points at them.

set -eu

here=$(cd -- "$(dirname -- "$0")" && pwd)

APP_ID=io.github.dsvi.microdict
BIN_NAME=microdict
UNINSTALL_NAME=microdict-uninstall
ICON_NAME=microdict
ICON_SIZE=256x256    # redist/icons/microdict.png, as in packaging/docker/appimage.sh

[ -n "${HOME:-}" ] || { echo 'user-install: HOME is not set' >&2; exit 1; }

bindir=${XDG_BIN_HOME:-$HOME/.local/bin}
datadir=${XDG_DATA_HOME:-$HOME/.local/share}
cfgdir=${XDG_CONFIG_HOME:-$HOME/.config}
cachedir=${XDG_CACHE_HOME:-$HOME/.cache}
statedir=${XDG_STATE_HOME:-$HOME/.local/state}
appdir=$datadir/applications
icondir=$datadir/icons/hicolor/$ICON_SIZE/apps

binfile=$bindir/$BIN_NAME
uninstallfile=$bindir/$UNINSTALL_NAME
appfile=$appdir/$APP_ID.desktop
iconfile=$icondir/$ICON_NAME.png

printf '== docker static build ==\n'
"$here/packaging/docker/build.sh"

bin=$here/build-out/$BIN_NAME
[ -x "$bin" ] || { echo "user-install: no binary at $bin" >&2; exit 1; }

printf '\n== installing for %s ==\n' "${USER:-$(id -un)}"
printf '  %-16s %s\n' binary "$binfile"
printf '  %-16s %s\n' uninstaller "$uninstallfile"
printf '  %-16s %s\n' 'desktop entry' "$appfile"
printf '  %-16s %s\n' icon "$iconfile"

install -d "$bindir" "$appdir" "$icondir"
install -m 755 "$bin" "$binfile"
install -m 644 "$here/redist/icons/$ICON_NAME.png" "$iconfile"

# ------------------------------------------------------------- the uninstaller

tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

cat > "$tmp" <<'UNINSTALLER'
#!/bin/sh
# microdict-uninstall -- removes the MicroDict that user-install.sh installed.
#
#   no argument   the install: the binary, the icon, the desktop entry, itself.
#                 Settings, caches and dictionaries stay where they are.
#   --data        the same, plus MicroDict's own folders in the XDG config,
#                 data, cache and state paths.
#
# This is what the two Desktop Actions of io.github.dsvi.microdict.desktop run.

data=0
case ${1:-} in
  --data) data=1 ;;
  '')     ;;
  *)  printf 'usage: %s [--data]\n' "$0" >&2
      exit 2 ;;
esac

# The application writes its config and its history back when it exits, so a
# data wipe done while it runs leaves those two behind. Better to say so.
running=0
pgrep -x '@BINNAME@' >/dev/null 2>&1 && running=1

# A launcher gives no terminal, so the confirmation is a dialog -- when the
# desktop has one of these two tools. With neither, the click is taken at face
# value and the delete simply happens.
ask() {
  if command -v kdialog >/dev/null 2>&1; then
    kdialog --title MicroDict --warningyesno "$1" || exit 1
  elif command -v zenity >/dev/null 2>&1; then
    zenity --question --title MicroDict --text "$1" || exit 1
  fi
}

if [ "$data" = 1 ]; then
  question='Delete MicroDict and all its settings, caches and indexes? Dictionary files themselves are not touched.'
  if [ "$running" = 1 ]; then
    question='MicroDict is still running -- quit it first, or it writes its settings back when it closes. Delete it and all its settings, caches and indexes?'
  fi
else
  question='Delete MicroDict? Your settings and dictionaries are kept.'
fi
ask "$question"

rm -f '@BIN@' '@DESKTOP@' '@ICON@'

# The login entry the application writes when "Start with system" is on names
# the binary deleted above, so it goes either way -- otherwise the desktop
# tries to start MicroDict on every login. Its name is the application id the
# portal reads back out of systemd's unit name, so the application borrows it
# from whatever menu entry the desktop already has and it is not known here:
# everything carrying this program's own mark goes, plus the names older
# versions used and the plain default config dir.
autostart="${XDG_CONFIG_HOME:-@CFG@}/autostart"
for d in "$autostart" "$HOME/.config/autostart"; do
  for f in "$d"/*.desktop; do
    [ -f "$f" ] || continue
    if [ "$(basename "$f")" = microdict-owned-by-preferences.desktop ] \
       || grep -q '^X-MicroDict-LoginEntry' "$f"; then
      rm -f "$f"
    fi
  done
done

if [ "$data" = 1 ]; then
  # resolved now, not at install time: wherever this user's XDG variables point
  # is exactly where the application wrote its folders.
  rm -rf "${XDG_CONFIG_HOME:-@CFG@}/microdict" \
         "${XDG_DATA_HOME:-@DATA@}/microdict" \
         "${XDG_CACHE_HOME:-@CACHE@}/microdict" \
         "${XDG_STATE_HOME:-@STATE@}/microdict"
fi

rmdir '@ICONDIR@' "$autostart" 2>/dev/null || true

# so the menu, the icon theme and the URL handlers stop offering MicroDict now
command -v update-desktop-database >/dev/null 2>&1 \
  && update-desktop-database '@APPS@' 2>/dev/null || true
command -v gtk-update-icon-cache >/dev/null 2>&1 \
  && gtk-update-icon-cache -q '@ICONS@/hicolor' 2>/dev/null || true

# last, and safe: an unlinked script keeps the inode its shell is reading
rm -f '@SELF@'

if [ "$data" = 1 ]; then
  printf 'MicroDict is gone, with its settings, caches and indexes.\n'
  if [ "$running" = 1 ]; then
    printf 'It was running, so it may write its config back as it closes.\n'
  fi
else
  printf 'MicroDict is gone; its settings and dictionaries were kept.\n'
fi
UNINSTALLER

sed -e "s|@BIN@|$binfile|g" \
    -e "s|@DESKTOP@|$appfile|g" \
    -e "s|@ICON@|$iconfile|g" \
    -e "s|@ICONDIR@|$icondir|g" \
    -e "s|@SELF@|$uninstallfile|g" \
    -e "s|@APPS@|$appdir|g" \
    -e "s|@ICONS@|$datadir/icons|g" \
    -e "s|@BINNAME@|$BIN_NAME|g" \
    -e "s|@CFG@|$cfgdir|g" \
    -e "s|@DATA@|$datadir|g" \
    -e "s|@CACHE@|$cachedir|g" \
    -e "s|@STATE@|$statedir|g" \
    "$tmp" > "$uninstallfile"
chmod 755 "$uninstallfile"

# ------------------------------------------------- the desktop entry + actions
#
# redist carries one group, so the two keys go straight after its last line and
# the actions follow. Exec becomes absolute: ~/.local/bin is not in every
# PATH, and TryExec gives launchers something to grey out the entry with.

sed -e "s|^Exec=.*|Exec=\"$binfile\" %u|" "$here/redist/$APP_ID.desktop" > "$tmp"
printf 'TryExec=%s\nActions=uninstall;purge;\n' "$binfile" >> "$tmp"

cat >> "$tmp" <<EOF

# Two Desktop Actions: the submenu a launcher shows when MicroDict is
# right-clicked. They are the whole uninstall story. The Exec stays a plain
# absolute path plus a flag -- no shell, no nested quotes -- because a desktop
# entry is parsed by several independent parsers and nested quoting does not
# survive all of them. The asking and the deleting live in the script.
[Desktop Action uninstall]
Name=Delete MicroDict
Name[ru]=Удалить MicroDict
Name[zh_CN]=删除 MicroDict
Icon=user-trash
Exec="$uninstallfile"

[Desktop Action purge]
Name=Delete with user data
Name[ru]=Удалить вместе с данными
Name[zh_CN]=删除并清除用户数据
Icon=user-trash
Exec="$uninstallfile" --data
EOF

install -m 644 "$tmp" "$appfile"

command -v update-desktop-database >/dev/null 2>&1 \
  && update-desktop-database "$appdir" 2>/dev/null || true
command -v gtk-update-icon-cache >/dev/null 2>&1 \
  && gtk-update-icon-cache -q "$datadir/icons" 2>/dev/null || true

case ":$PATH:" in
  *":$bindir:"*) ;;
  *) printf '\n  %s is not in PATH: add it to start microdict from a terminal\n' "$bindir" ;;
esac

printf '\n  Right-click MicroDict in the menu for the two delete actions.\n'
