# Build from source

The workflow files under `.github` — `Release-appimage.yml` for the release build,
`PR-check-cmake.yml` for the dynamic ones — carry the actual build commands and are
the reference.

The Linux release build is the fully static one; if that is all you want,
`./build-appimage.sh` at the repository root runs it in Docker and leaves
`build-out/MicroDict-<version>-<arch>.AppImage` behind. See
[Build the static Linux binary](build_static_linux.md) — this page is for a normal
dynamic build, which is what you want while developing.

## Dependencies

* A C++23 compiler (GCC 13+ or Clang 17+; the static image uses GCC 15.2)
* Qt 6 (6.8 or newer recommended; the fork's static build uses 6.12)
  — qtbase, qtsvg, qt5compat, qtmultimedia, qttools (for `lrelease`), and on Linux
  the Wayland platform plugin
* The libraries MicroDict links (see
  [`DEPENDENCIES.md`](https://github.com/dsvi/MicroDict/blob/main/DEPENDENCIES.md)
  for why each one is there):
  * hunspell — spell checking
  * xapian — full-text index. **Must be the 1.4 series**: Alpine 3.24 and recent
    Debian/Ubuntu ship Xapian 2.0, whose C++ API this program does not target
  * OpenCC — Chinese conversion
  * toml++
  * FFmpeg and/or Qt Multimedia — audio playback
  * libvorbis, libogg — Vorbis in DSL sound archives
  * zlib, lzo2, xz (lzma) — compressed dictionary bodies. Not bzip2: it is not
    linked, and the image builds freetype without it so nothing else pulls it in
    either.
  * PulseAudio (libpulse) and D-Bus — audio output and the desktop portal
  * wayland client — global hotkeys go through the desktop portal, which is a
    Wayland/D-Bus service


### Alpine (the distribution this fork's build is defined against)

```shell
apk add build-base cmake ninja pkgconf python3 git \
    qt6-qtbase-dev qt6-qtsvg-dev qt6-qt5compat-dev qt6-qtmultimedia-dev \
    qt6-qttools-dev qt6-qtwayland-dev \
    hunspell-dev opencc-dev tomlplusplus-dev lzo-dev \
    xapian-core-dev ffmpeg-dev libpulse-dev dbus-dev wayland-dev \
    libvorbis-dev libogg-dev zlib-dev lzo-dev xz-dev \
    libxkbcommon-dev freetype-dev fontconfig-dev libpng-dev expat-dev \
    pcre2-dev libsndfile-dev gettext-dev fontconfig font-dejavu
```

Xapian is the exception: Alpine's `xapian-core-dev` is 2.0, so build
`xapian-core-1.4.x` from source and point CMake at it, or use the Dockerfile, which
does exactly that.

### Other distributions

The same set under different names. `PR-check-cmake.yml` builds against Debian/Ubuntu,
Arch and macOS, so its three install lists are the exact package names:

* Ubuntu 24.04: `build-essential ninja-build libhunspell-dev libxapian-dev
  libopencc-dev liblzo2-dev libvorbis-dev zlib1g-dev libavutil-dev
  libavformat-dev libzstd-dev libxkbcommon-dev qt6-base-dev qt6-multimedia-dev
  qt6-tools-dev qt6-tools-dev-tools libtomlplusplus-dev`
* Arch: `cmake ninja ffmpeg hunspell libvorbis lzo opencc qt6-base qt6-multimedia
  qt6-tools tomlplusplus xapian-core`
* macOS: `brew install create-dmg ccache opencc ffmpeg@6 libao libiconv lzip
  libvorbis hunspell xapian tomlplusplus qt`

None of them installs `qt6-webengine-dev`, `qt6-webchannel-dev`, `qt6-speech-dev`,
`libeb16-dev`, `libzim-dev`, `libxtst-dev` or `x11proto-record-dev` — those were
upstream's, for the features this fork does not have.

## Build

```shell
cd MicroDict && mkdir build_dir
# config step
cmake -S . -B build_dir \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Release

# actual build
cmake --build build_dir --parallel 7
```

The executable is `microdict`, and on Linux that is the end of it: there is no
install step, because the program finds its read-only data relative to its own
executable (`Config::getProgramDataDir()`), and its translations are linked into
the executable itself. macOS does have a `cmake --install` step,
because there the install tree is the deliverable.

### Feature flags

Append `-D<flag>=ON/OFF` to the config step:

| Flag | Default | Meaning |
|---|---|---|
| `MICRODICT_STATIC_BUILD` | `OFF` | Link everything from `.a` archives: no `NEEDED`, no `PT_INTERP`. Needs a toolchain with static libc/libstdc++ (Alpine) and a Qt built with `BUILD_SHARED_LIBS=OFF`. See `cmake/Deps_Linux.cmake` for the archive list and the `--undefined=` tricks static linking forces |
| `WITH_QT_MULTIMEDIA` | `ON` | Qt Multimedia — the only internal audio player. It also pulls in the FFmpeg libraries, because Qt's own media plugin decodes through them and a static build links that plugin in. |

### macOS

Built from an IDE, the produced `MicroDict.app` runs from the IDE, which sets
up the paths for you.

`cmake --install build_dir/` runs `macdeployqt`, ad-hoc code signing and the rest;
the app bundle ends up in `build_dir/redist/MicroDict.app`. To also produce a
`.dmg`, install
[create-dmg](https://github.com/create-dmg/create-dmg) first.
