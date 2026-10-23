# Install

MicroDict is not in any distribution's package repositories and not on
Flathub, which carries upstream's program instead. The fork also installs nothing
itself — there are no `.deb`/`.rpm` packages, no Flatpak, no `cmake --install` rules
on Linux and no AppStream metadata. For Linux there is exactly one kind of artifact,
and it is a single file: an AppImage, published for `x86_64` and `aarch64`.

## Linux: the AppImage

The [releases](https://github.com/dsvi/MicroDict/releases) page carries one file per
architecture plus their checksums:

```
MicroDict-<version>-x86_64.AppImage
MicroDict-<version>-aarch64.AppImage
SHA256SUMS.txt
```

```
sha256sum -c SHA256SUMS.txt
chmod +x MicroDict-<version>-<arch>.AppImage
./MicroDict-<version>-<arch>.AppImage
```

That is the whole installation. An AppImage is a filesystem image with a small
runtime in front of it: it mounts itself (or unpacks to a temporary directory when
FUSE is unavailable) and starts the program inside. Nothing is written outside the
program's own config directories, and removing the program means deleting the file.

What is inside it is the fork's fully static binary — `0` `NEEDED` entries, no
`PT_INTERP` — together with the three things a bare binary cannot carry: the
translations, the icon and the desktop entry. So the file also behaves like a normal
desktop program: it has an icon, an integration tool can put it in a menu, and its
`.desktop` entry is what the "start on login" preference copies into the autostart
directory the desktop reads — `$XDG_CONFIG_HOME/autostart/`, which is
`~/.config/autostart/` unless the user moved it. The copy rewrites `Exec=` to the
AppImage itself, because the path inside a mounted image disappears once it exits. A
writable directory named `MicroDict-<version>-<arch>.AppImage.config` next to the file
makes the program keep its configuration there instead of in `~/.config/microdict`;
the login entry still goes to `~/.config/autostart/`, because no desktop ever looks
inside that directory — see [Portable mode](topic_portablemode.md).

The requirements are the ones the binary has anyway:

* **x86-64 or aarch64 Linux.** Both the payload and the runtime in front of it are
  built for one of the two; the file name's suffix is what `uname -m` prints.
* **Wayland.** The static build contains the `wayland` and `offscreen` platform
  plugins and nothing else — no XCB plugin, so there is no X11 session to fall back
  to. Under X11, run it through a Wayland compositor (`cage`,
  `weston --backend=x11`). See [Wayland](topic_wayland.md).
* **Fonts and a portal come from the host.** The program ships no fonts and names
  none of its own: articles ask for the generic CSS families and the host's
  fontconfig answers with the machine's faces. Global hotkeys and the desktop's
  light/dark choice are asked of `xdg-desktop-portal`; without a portal, hotkeys are
  simply not registered.

One thing you may notice on the console and can ignore:

```
qt.qpa.services: Failed to register with host portal: Could not register app ID:
App info not found for 'io.github.dsvi.microdict'
```

Qt's Wayland platform asks the portal to look the desktop entry up, and the portal — a
separate process — searches its own data directories, where an entry that lives inside
an AppImage is not. Nothing depends on that registration here; it is the sound of the
program not being installed anywhere, which is the point.

## The bare binary

The binary is not published on its own: the AppImage is the only release artifact.
To get the bare executable, build it:

```
git clone https://github.com/dsvi/MicroDict
cd MicroDict
./packaging/docker/build.sh          # needs Docker; ~30 min from scratch
```

It lands in `./build-out/microdict`, with its translations linked into it.
Everything the AppImage wraps around the binary is optional; the binary runs alone.
`./build-appimage.sh` is the shorter path: it stops after the packaging stage and
writes just `build-out/MicroDict-<version>-<arch>.AppImage`.

`./user-install.sh` builds it and installs the rest around it: the binary into
`$XDG_BIN_HOME` or `~/.local/bin`, the icon and a `.desktop` entry under
`~/.local/share`. That entry is what makes the installed copy behave like a normal
desktop program — right-clicking it offers the two delete actions, and it is what the
"start on login" preference copies into the autostart directory.

Docker is only the build farm. `docker run` of the built image works too, but then
the container — not the host — decides which fonts exist:

```
docker build -f packaging/docker/Dockerfile -t microdict-static .
docker run --rm -e XDG_CONFIG_HOME=/cfg -v $PWD/cfg:/cfg -v $PWD/dicts:/dicts \
    -v /usr/share/fonts:/usr/share/fonts:ro --device /dev/shm microdict-static <word>
```

## macOS

There are no pre-built macOS binaries: the workflow that published them was removed
when the release automation was reduced to the AppImage. Build it yourself —
[Build from source](howto/build_from_source.md), whose macOS section covers
`macdeployqt` and code signing. There the install tree *is* the deliverable, so
`cmake --install` produces `MicroDict.app`, and a `.dmg` too once
[create-dmg](https://github.com/create-dmg/create-dmg) is installed.

There is no Windows build: the tree has no Windows dependency file, no Windows
packaging and no CI for it.

## From source, on any system

See [Build from source](howto/build_from_source.md). The Linux section there is the
plain dynamic build; the static one is
[Build the static Linux binary](howto/build_static_linux.md). Neither installs
anything on Linux: the program finds its data relative to its own executable.

## Version policy

The fork keeps its own number, in the `VERSION` file at the root of the repository.
That file is the only place the number is written: CMake reads it into
`PROJECT_VERSION`, the About dialog and the `PROGRAM_VERSION` compile definition take
it from there, and the AppImage file name is built from it. It says
nothing about which upstream commit the tree last merged.
