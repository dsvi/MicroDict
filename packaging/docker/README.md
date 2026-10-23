# `packaging/docker/`

The whole Linux build story: one recipe that produces a fully static binary, the
package that wraps it, and the checks that have to pass inside the image before the
build is allowed to succeed.

Nothing here is compiled into MicroDict and nothing here is needed to run it. Docker
is only the farm the build happens in; the deliverables are copied out of the image,
and afterwards the image is irrelevant.

Everything except `build.sh` runs *inside* the build image, and the image build runs
it itself — a green `docker build` already means every check below passed.

| File | What it is |
|---|---|
| `Dockerfile` | the recipe: Alpine 3.24, everything static, four stages |
| `build.sh` | the entry point: builds the image, copies the results out |
| `appimage.sh` | wraps the binary in the last stage — the only Linux package |
| `qtprobe.sh` + `qtprobe/` | proves the static Qt itself works |
| `portalmock.sh` + `portalmock/` | a fake desktop portal, to test the portal clients |
| `smoke.sh` | the application itself, eight checks |
| `migtest.sh` | the configuration directories |
| `mkdict.py` | a four-word StarDict dictionary for manual checks |

## `Dockerfile`

The recipe. Alpine 3.24, because musl is the only libc that is sane to link
statically. Four stages:

* **`deps-base`** — the toolchain plus the `-static` archives Alpine ships.
* **`deps`** — everything Alpine does not ship as a plain `.a` is built from source:
  Qt 6.12 (qtbase, qtsvg, qtwayland, qtmultimedia, qtshadertools, qttools), GNU
  libiconv, wayland, libffi, hunspell, xapian-core, OpenCC, LZO, toml++, FFmpeg,
  PulseAudio, D-Bus. Each download is pinned by sha256. MicroDict is then configured
  with `-DMICRODICT_STATIC_BUILD=ON`, built, and the four check scripts below are run.
* **`appimage`** — the AppImage, assembled from the binary that stage produced.
* the unnamed last stage — Alpine plus exactly three things: the binary, `locale/`
  and the AppImage.

The result is one binary with no `PT_INTERP` and no `DT_NEEDED` at all. Why musl, why
Qt has to be built from source too, and the reason for every version:
`DEPENDENCIES.md`.

## `build.sh`

The entry point, run on the host:

```sh
./packaging/docker/build.sh              # extra docker build args pass through
```

It builds the image, then `docker create` + `docker cp` to extract into `build-out/`:
the binary, `locale/*.qm`, the AppImage, and `portalmock`. The mock is copied out
because it lives in the build stage rather than the run image, and driving the real
application through it (`--serve`, `--serve-theme`, `--serve-chooser`) needs it on the
host.

## `appimage.sh`

MicroDict's only Linux package: no deb/rpm, no Flatpak, no `install()` rules, no
AppStream metadata anywhere in the tree. The static binary already needs nothing from
the system, but on its own it is one file out of context — no icon, no desktop entry,
no way to start it from a menu, and its translations have to be carried beside it by
hand. The AppImage is that same binary plus exactly those things, and the binary
inside is byte-identical to the one `build.sh` extracts.

It assembles the file with the three commands `appimagetool` would run, without
`appimagetool` — that is a glibc program and this image is musl: `mksquashfs` with
`-offset`, the runtime ELF copied over the padding mksquashfs leaves at the front, and
`chmod`. The runtime it embeds is pinned by sha256 instead. Compression is zstd, not
xz, because that runtime's bundled `squashfuse` understands only zlib and zstd.
because that runtime's bundled `squashfuse` understands only zlib and zstd.

It then checks the result rather than trusting it: the squashfs starts where the
runtime will look for it, the runtime has no `NEEDED`, the payload extracted back
through the runtime's own reader is byte-identical to what went in, and `--version`
is answered all the way through runtime → squashfuse → AppRun → program.

## `qtprobe.sh` + `qtprobe/`

Builds and runs the probe: a ~100-line application linked against the image's static
Qt. Its `CMakeLists.txt` is deliberately a miniature copy of the project's own static
setup (`CMakeLists.txt` + `cmake/Deps_Linux.cmake`, `MICRODICT_STATIC_BUILD`) — archives only,
`-static`, a linker group — so a failure here points at the same thing a failure of
the real build would.

A static binary cannot `dlopen()` a plugin, so the platform, image-format,
icon-engine and multimedia plugins have to be linked in. The probe checks the parts
the application's own smoke test cannot see: raster painting over a static
fontconfig/FreeType/HarfBuzz, that the generic families `serif` / `sans-serif` /
`monospace` really do reach three different faces, the image-format plugins and
`qsvg`, Qt Multimedia's FFmpeg backend, QtDBus — which the global hotkeys need to
reach the portal at all — and that this Qt is software-rendered only: no OpenGL, no
EGL, no Vulkan.

## `portalmock.sh` + `portalmock/`

Builds and runs a stand-in desktop portal. MicroDict asks the desktop for three things
and none of them can be tested without something owning
`org.freedesktop.portal.Desktop`: the global hotkeys
(`src/hotkey/portalshortcuts.cc`), the desktop's colours
(`src/lite/lite_theme.cc`) and whether a file chooser exists at all
(`src/lite/portal_filechooser.cc`).

The mock exports `GlobalShortcuts`, `Settings` and `FileChooser` on a private bus and
answers `CreateSession` / `BindShortcuts` / `Session.Close` and the per-key
`Settings.Read` the way the portal's own XML describes them. The hotkey and the
colour clients are then driven through it and every step checked — the
`a(sa{sv})` marshalling, the request
object paths the client has to predict, that a session belonging to another
application is ignored, that a `SettingChanged` signal arrives instead of a poll, and
that a desktop with no portal is noticed rather than stalled on. It also stands in for
KDE's `org.kde.kglobalaccel`, so the release path is checked too: the client walks every
component, calls `cleanUp` only on the one holding its own shortcut id, and leaves the
other alone. It publishes KDE's
`org.kde.kdeglobals.*` namespaces filled with colours that deliberately disagree with
the `kdeglobals` file it writes, so a client reading the wrong channel fails.

Modes:

| Mode | What it does |
|---|---|
| *(none)* | export the portal, drive the clients against it, check everything |
| `--no-portal` | export nothing: the client must notice and stay off the bus |
| `--probe` | export nothing and only report — run it on a real desktop to see what that desktop offers |
| `--serve [id]` | wait for the real application to bind its shortcuts, then fire `Activated` twice |
| `--serve-theme [s]` | a desktop that changes its mind: dark, light after `s` s, dark again — the colours watched on screen |
| `--files` | drive a real `QFileDialog` through the chooser in the test process: the theme Qt builds, the call it makes, the filters, the path back |
| `--serve-chooser [s]` | the same, with the real application on a real display |

## `smoke.sh`

The application itself, under `QT_QPA_PLATFORM=offscreen`. Eight checks: the binary
really is static; the plugins it cannot `dlopen()` are inside it; the encoding layer
is GNU libiconv and not musl's, which is what keeps GB18030/Big5/EUC-JP dictionaries
readable; no weak reference was left bound to address 0, a failure mode a static link
has and a dynamic one does not; no font is shipped or embedded; the hotkeys are asked
of the portal rather than grabbed off an X server; the app still works — dictionary
indexing, a lookup, a config round-trip; and the colours are the desktop's, with the
program saying out loud which it followed.

It runs with `LANG=de_DE.UTF-8` on purpose twice over: a locale is set, which is what
makes libpulse call `gettext()` — the first such call was exactly the kind of weak
reference that binds to 0 in a static link — and a German locale is the only way left to
reach the `:/locale/` catalogues, since there is no interface-language setting any more.
See `DEPENDENCIES.md` §1.3.

## `migtest.sh`

The configuration story, in three parts: with the XDG variables redirected, the fork
must create and use `<dir>/microdict` and nothing else anywhere; with a plain `$HOME`
and no XDG variables, the same, and no directory that already exists may gain a byte;
and a broken config of its own is left whole rather than silently rewritten.

The one check that needs no container: against the extracted binary it carries nothing,
so `./packaging/docker/migtest.sh` runs on the host directly.

## `mkdict.py`

Writes a four-word StarDict dictionary (`litetest.ifo` / `.idx` / `.dict`) into a
folder: `hello`, `theme`, `dark`, `test`. Not used by the image build — it is the
fixture you point a `<paths><path>` entry at for manual and GUI checks, and its
definitions deliberately carry the things the article view has to cope with: a
`bword://` link, a coloured span, a `dsl_opt` block, IPA.

## Running one check by hand

```sh
docker build --target deps -t microdict-deps -f packaging/docker/Dockerfile .
docker run --rm -v "$PWD":/src microdict-deps sh /src/packaging/docker/qtprobe.sh
docker run --rm -v "$PWD":/src microdict-deps sh /src/packaging/docker/smoke.sh
docker run --rm -v "$PWD":/src microdict-deps sh /src/packaging/docker/portalmock.sh
docker run --rm -e QT_QPA_PLATFORM=offscreen microdict-deps sh /src/packaging/docker/migtest.sh
./packaging/docker/migtest.sh          # or against ./build-out/microdict, no container
```

`smoke.sh` mounts only the script: the image's own `/src/build` holds the static
binary, and a whole-repo mount would replace it with whatever the host last built.
`BIN=...` points any of them at a different binary; `QT_PREFIX=...` builds the probes
against a shared Qt instead of the image's static one.

## What none of this covers

No script here ever opens a Babylon `.bgl`: `mkdict.py` writes a StarDict dictionary
and `smoke.sh` loads an xdxf folder, so `Babylon::readBlock()` and the `readEntry()`
loop that builds a `.bgl` index have no automated test. Checking them needs a real
`.bgl`, and those cannot be shipped here — they are Babylon's own files. How to do it
by hand, with a file that is freely downloadable:
[`website/docs/howto/build_static_linux.md`](https://github.com/dsvi/MicroDict/blob/main/website/docs/howto/build_static_linux.md).
