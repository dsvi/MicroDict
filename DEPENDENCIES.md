# External dependencies of MicroDict

Scope: what the **fork** actually needs to build and run — a **fully static
Alpine/musl** build of a program that renders articles with `QTextBrowser` and has no
network support at all.

There is **one** build: a fully static Alpine/musl binary. Everything below was
verified by a clean configure + full build in that image, then `objdump -p`,
`readelf -l`, `nm`, `strings`, `pkg-config` and `grep` over `src/`. Nothing is copied
from upstream docs.

```
docker build -f packaging/docker/Dockerfile -t microdict-static .
                                # configure + build + qtprobe + portalmock + migtest
                                # + smoke test + AppImage
./packaging/docker/build.sh     # extracts build-out/microdict and
                                # build-out/MicroDict-<version>-x86_64.AppImage
```

| Build | Image | Toolchain | Result |
|---|---|---|---|
| **static (the only one)** | `microdict-static:latest` (`packaging/docker/Dockerfile`) | Alpine 3.24, musl 1.2.6, Qt 6.12 built from source static, GCC 15.2, CMake 4.2.3 | `build/microdict`, 55 784 536 B stripped, static-pie, **0 `NEEDED`, 0 `PT_INTERP`, no `RUNPATH`**; wrapped as `MicroDict-0.0.0-x86_64.AppImage`, 24 463 864 B — see §10 |

`cmake -S . -B build` against a distro's shared Qt still configures and builds, but
that is not what is built or tested here, so no numbers from it are quoted below.

Both the image and a plain host build see the same 237 `src/*.cc|.hh` files (plus 3
`.mm` and 14 `.ui`) and 344 resource entries across the three `.qrc` files.

---

## 1. The real link line

### 1.1  The fully static build (`MICRODICT_STATIC_BUILD=ON`, Alpine/musl)

The Alpine image builds Qt 6.12 from source with `BUILD_SHARED_LIBS=OFF` and links
the application with `-static`. The result is a static-pie image: no interpreter, no
`DT_NEEDED`, nothing to load:

```
$ readelf -l build/microdict | grep -c INTERP
0
$ objdump -p build/microdict | awk '/NEEDED/{c++} END{print c+0}'
0
$ readelf -d build/microdict | grep -cE "NEEDED|RPATH|RUNPATH"
0
$ file build/microdict
ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), static-pie linked, ...
```

is Qt 6.12's larger multimedia/gui archives plus FFmpeg 8). It runs on any x86-64
**55 784 536 bytes stripped** (Qt 6.8.2 + FFmpeg 6.1.2 was 38 715 768; the 17 MB growth
Linux with a Wayland compositor, glibc or musl, with nothing installed.

What that costs, in archives:

| Group | Linked in from |
|---|---|
| C / C++ runtime | `libc.a` + `libm.a` + `libpthread.a` (musl 1.2.6), `libstdc++.a` (GCC 15.2) |
| Qt | `libQt6{Core,Gui,Widgets,Xml,Svg,Concurrent,Network,Multimedia,WaylandClient,WlShellIntegration}.a` plus the private `Qt6CoreTools`/`GuiTools`/`WidgetsTools`/`GuiPrivate`/`CorePrivate` archives a static Qt exports, and its vendored `libQt6BundledHarfbuzz.a` / `libQt6BundledLibjpeg.a` (double-conversion and md4c end up *inside* `libQt6Core.a` / `libQt6Gui.a`) |
| Qt plugins (no `dlopen()` is possible) | `CMakeLists.txt` lists them explicitly and calls `qt_import_plugins( ... INCLUDE ... )` for each: platform `wayland` plus `offscreen`, the Wayland shell integrations `xdg` and `wl_shell`, the client decorations `bradient` and `adwaita`, imageformats `qjpeg`/`qsvg`/`qgif`/`qico`, iconengines `qsvgicon`, platformtheme `xdgdesktopportal` (Qt's own portal theme: its read of the desktop's portal Settings, §8.2, and its file chooser, §8.12), and the FFmpeg media backend — see §5 |
| third-party | `libhunspell-1.7.a`, `libxapian.a`, `libopencc.a` + `libmarisa.a`, `liblzo2.a`, `liblzma.a`, `libz.a`, `libvorbisfile.a` + `libvorbis.a` + `libogg.a`, `libtomlplusplus.a`, `libav{format,codec,util}.a` + `libswresample.a` + `libswscale.a` |
| Qt's own private deps | `libfontconfig.a`, `libfreetype.a`, `libpng16.a` + `libpng.a`, `libbrotli{dec,common}.a`, `libexpat.a`, `libpcre2-16.a`, `libdbus-1.a`, `libxkbcommon.a`, `libwayland-{client,cursor}.a`, `libffi.a`, `libuuid.a`, `libpulse.a` + `libpulsecommon-17.0.a`, `libintl.a` (GNU gettext), `libatomic.a`, and `-ldl -lrt -lm -lpthread` (musl ships those as stubs) |

Two archives in that table look like regressions against the "no network support" claim
and are not:

* **`libQt6Concurrent.a`** — `QtConcurrent::run` was always header-only for us (26 call
  sites). A static Qt ships QtConcurrent as a real archive, so it shows up here;
  nothing changed in the source.
* **`libQt6Network.a`** — not ours either: Qt Multimedia's FFmpeg media plugin requires
  it. The application still opens no socket — `ArticleResourceManager` refuses
  `http`/`https`/`ftp`, and no `QNetwork*` class appears anywhere in `src/`. It is dead
  weight inside the binary, not network support. Turning `WITH_QT_MULTIMEDIA` off would
  drop it, at the cost of the internal audio player.

Two other things in the table are not obvious and cost a build cycle each:

* **`libuuid`** — `libxapian.a`'s `uuids.o` has an undefined `uuid_generate()`; on
  Alpine that lives in `libuuid.a`, shipped by **util-linux**, not by any "uuid"
  package. Alpine also has no `-static` subpackage for hunspell, xapian, opencc, lzo,
  package. Alpine also has no `-static` subpackage for hunspell, xapian, opencc,
  lzo, toml++, FFmpeg or PulseAudio, so those are rebuilt from source as archives.
* **`libswscale`** — nothing in this project calls `sws_*`; Qt Multimedia's FFmpeg
  media plugin calls `sws_getContext`/`sws_scale`/`sws_freeContext`.
  With the plugin linked in, those become undefined references at the end of the link,
  so `-lswscale` is added in the `MICRODICT_STATIC_BUILD` block of `cmake/Deps_Linux.cmake`
  and *not* in the pkg-config list, which would otherwise force `libswscale-dev` on
  anyone building against a distro's Qt.

Because ld makes a single left-to-right pass over archives and Qt's interface lists
interleave providers (`libbrotlidec` after `libbrotlicommon`, `libopencc` before
`libmarisa`, `libvorbisfile` before `libvorbis`), both `CMakeLists.txt` and
`cmake/Deps_Linux.cmake` wrap the provider set in `-Wl,--start-group … -Wl,--end-group`
instead of trying to order it by hand.

The static build is **Wayland-only**: Alpine ships no static archives for the xcb-util
family (`xcb-cursor`, `xcb-icccm`, `xcb-render-util`, `libxkbcommon-x11`), and the fork
has no X11 code left.

What the port and the dependency diet removed is visible in the binary as well. Checked
with `nm` and `strings` over the unstripped executable: no `Qt6WebEngine` or
`Qt6WebChannel`, no `Qt6PrintSupport`, no `Qt6TextToSpeech`, no X11 hotkey symbols
(`XGrabKey`, `XNextEvent`, `XRecordEnableContext` — `smoke.sh` asserts all three are
absent), no `libeb`, no `libzim`, and no `libbz2` either. No file in `src/` has called
`BZ2_*` since slob's bzip2 support went away; what still pulled `libbz2.a` into the link
was Alpine's own `libfreetype.a`, whose PCF font driver calls `FT_Stream_OpenBzip2()`.
The image builds freetype with bzip2 off, so that edge does not exist — see the BZip2
row in §1.1 and the static-only table in §2.

### 1.2  The failure mode a static link has and a dynamic one does not

The first static binary ran fine in the image and **segfaulted on a real desktop**. The
backtrace ended at `rip = 0`, inside GNU libintl:

```
#0  0x0000000000000000 in ?? ()
#1  _libintl_recursive_lock_init_multithreaded () at glthread/lock.c:580
#4  _libintl_find_domain (… "LC_MESSAGES/pulseaudio.mo" …)
#6  pa_bytes_snprint ()            <- libpulse translating its own strings
#9  QPulseAudioEngine::prepare()   <- Qt Multimedia enumerating audio devices
```

The disassembly at the fault site is `call 0`, and the reason is a linker rule:

> ld pulls a member out of an archive only to satisfy a **strong** undefined symbol. A
> *weak* undefined reference is simply bound to address 0 — no error, no warning.

`libintl.a` refers to `pthread_mutexattr_destroy` weakly (musl declares the pthread
functions weak), and nothing else in the link needed it strongly, so musl's member was
never extracted. The call only happens on libintl's error path, and libintl is only
entered when a locale is set — the image's smoke test ran with `LANG` unset, so it
never got there. Three such holes existed:

| Symbol | Referenced weakly by | Reached from |
|---|---|---|
| `pthread_mutexattr_destroy` | `libintl.a` (`glthread/lock.c`) | the first `gettext()` call — libpulse i18n, i.e. any non-C locale |
| `thrd_exit` | OpenCC / marisa's C11 thread wrapper | a worker thread finishing |
| `__cxa_pure_virtual` | `libxapian.a`, `libopencc.a` | calling a pure virtual (UB anyway) |

`cmake/Deps_Linux.cmake` now names them on the link line
(`-Wl,--undefined=…`), which makes ld extract the members, and `smoke.sh` runs the
application with `LANG=de_DE.UTF-8` so the path is actually exercised, and checks the
three symbols are defined. Worth remembering for any future static link: **check for
weak references that resolved to 0**, because the link will not tell you.

---

## 2. Required third-party libraries

Versions are what the Alpine image resolves: `pkg-config` for the archives the image
builds from source, `apk` for the ones it installs.

| Library | Version | What it is for | Where it is used |
|---|---|---|---|
| **hunspell** | 1.7.5 (rebuilt static) | Spell checking for the morphology sources. | `src/dict/hunspell.cc` |
| **liblzma** (xz) | 5.8.4 | XZ / `dictzip` payloads: `.dz` files and LZMA2-compressed slob records. | `src/dict/utils/decompress.{cc,hh}`, `src/dict/slob.cc` |
| **lzo2** | 2.10 (rebuilt static) | LZO-compressed blocks inside MDict (`.mdx`) files. | `src/dict/mdictparser.cc` |
| **OpenCC** | 1.4.2 (rebuilt static, bundles marisa under `deps/`) | Chinese traditional/simplified conversion. | `src/dict/morphology.cc`, `src/transliteration/` |
| **toml++** | 3.4.0 (archive from upstream `src/toml.cpp`) | Parsing the `metadata.toml` / `.dcl`-style dictionary metadata sidecars. | `src/metadata.{cc,hh}` (the only direct user) |
| **libvorbis / vorbisfile** | 1.3.7 / 1.3.6 (apk `-static`) | Vorbis audio streams embedded in Lingvo BGL dictionaries. | `src/dict/bgl.cc` |
| **xapian-core** | 1.4.32 (rebuilt static — Alpine 3.24 ships 2.0, a different ABI) | The full-text index. | `src/dict/*.cc` |
| **zlib** | 1.3.2 | Raw deflate everywhere: BGL, BtreeIdx, chunked storage, DSL, GLS, MDict, StarDict, slob, XDXF, dictzip, `ufile`. | `src/dict/{bgl,btreeidx,chunkedstorage,dsl_details,gls,mdictparser,stardict,xdxf}.cc`, `src/dict/utils/{decompress,dictzip,indexedzip,ufile}.{cc,hh}` |
| **BZip2** | — | **not linked.** Nothing in `src/` calls `BZ2_*`. Alpine's own `libfreetype.a` would otherwise drag `libbz2.a` in: it is built with `FT_CONFIG_OPTION_USE_BZIP2` and its PCF font driver calls `FT_Stream_OpenBzip2()`, so a static link needs `-lbz2` after `-lfreetype`. The image builds freetype with bzip2 off, so that edge is not there; `nm` over the binary finds no `BZ2_` symbol at all. | — |
| **Iconv** | GNU libiconv 1.19, static | musl's built-in iconv lacks GB18030/GBK/BIG5/EUC-KR/IBM437/MacRoman/windows-31j; see §4. | `src/dsl/dsl_utils.cc`, `src/dict/dsl.cc`, `src/dict/xdxf.cc` |
| **D-Bus** (`libdbus-1`) | 1.16.2 (built in the image) | The session bus, and with it the desktop's portal: the global hotkeys are *asked for* rather than grabbed, and the desktop's light/dark choice is read from it when no platform theme knows it. Only ever reached through `Qt6::DBus`, which is a thin wrapper over this library — the application includes no `dbus.h` and opens no socket of its own. | `src/hotkey/portalshortcuts.{cc,hh}`, `src/hotkey/unixhotkeywrapper.cc`, `src/lite/lite_theme.{cc,hh}` |

`cmake/Deps_Linux.cmake` now reads:

```cmake
pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET
        hunspell liblzma lzo2 opencc tomlplusplus
        vorbis vorbisfile xapian-core zlib ${Optional_Pkgs})
find_package(Iconv REQUIRED)      # GNU libiconv on musl, not the C library's — see §4

target_link_libraries(${MICRODICT} PRIVATE Qt6::DBus)   # the hotkey portal
```

### Where each version comes from

Alpine 3.24 packages some of these; the image rebuilds the rest as archives, because
Alpine's `-dev` packages ship no `.a` (see the next table). The rebuilt ones are pinned
to an upstream release rather than to whatever apk happens to carry — and every
from-source download is pinned by sha256, so a version that moved upstream is a build
failure rather than a surprise (see §9 for why that matters):

| Piece | Version in the image | From |
|---|---|---|
| base image | **Alpine 3.24** | apk |
| libc | musl 1.2.6-r2 | apk |
| compiler / binutils | GCC 15.2.0-r5 / 2.45.1 | apk |
| CMake / meson | 4.2.3 / 1.11.1 | apk |
| Qt | **6.12.0** (current LTS line) | built from source, `BUILD_SHARED_LIBS=OFF` — see §3 |
| hunspell | 1.7.5 | rebuilt static (apk has 1.7.3, shared only) |
| xapian-core | **1.4.32** | rebuilt static — apk's is 2.0, a different ABI |
| OpenCC | 1.4.2 | rebuilt static, bundles marisa under `deps/` |
| lzo2 | 2.10 | rebuilt static (apk has the same version, shared only) |
| toml++ | 3.4.0 | archive compiled from upstream `src/toml.cpp` (apk has it, shared only) |
| zlib | 1.3.2 | apk `-static` |
| liblzma (xz) | 5.8.4 | apk `-static` |
| libvorbis / libogg | 1.3.7 / 1.3.6 | apk `-static` |
| FFmpeg | **8.1.3, audio-only** (`--disable-everything` plus an explicit codec/demuxer list) | rebuilt static |
| dbus | 1.16.2 | rebuilt static — see the note below |
| Iconv | **GNU libiconv 1.19** | rebuilt static — see §4 |
| AppImage runtime | type2-runtime **20251108** | downloaded from its GitHub release, sha256-pinned, prepended to the squashfs — see §10 |
### What only the static build pulls in

None of these are dependencies of the *application source*; they are dependencies of
a link that has no shared objects to lean on. They appear only in the
`MICRODICT_STATIC_BUILD` block of `cmake/Deps_Linux.cmake`:

| Archive | Version | Why it is needed |
|---|---|---|
| `libpulse.a` + `libpulsecommon-17.0.a` | PulseAudio 17.0 | Qt Multimedia's only Linux audio output; the FFmpeg media plugin is not even built without it. Speaks the Pulse protocol over a Unix socket, so PipeWire's pulse-server works too. Alpine has no `-static` package and hardcodes `shared_library()` for libpulse, so the meson calls are rewritten to `static_library()`. |
| `libwayland-client.a`, `libwayland-cursor.a` | 1.26.0 | the Qt Wayland platform plugin. Alpine 3.24 does ship `wayland-static` (1.25.0), but its archives are LTO bytecode (linkable only by a matching GCC) and it is a release behind, so wayland is rebuilt from source without LTO. |
| `libffi.a` | 3.8.0 | wayland's closure marshalling; no `-static` package. |
| `libfontconfig.a`, `libfreetype.a`, `libpng16.a`, `libbrotli{dec,common}.a`, `libexpat.a` | 2.17.1 / 2.14.3 / 1.6.59 / 1.2.0 / 2.9.0 | Qt's text stack: fontconfig finds the faces, freetype rasterises them, and the rest are freetype's own dependencies. freetype is the same 2.14.3 Alpine ships but is rebuilt in the image, with bzip2 switched off — see the BZip2 row in §1.1 for why that is not just a size question. |
| `libxkbcommon.a` | 1.13.1 | Keymaps: the Wayland plugin hands the compositor's keymap through it before any key reaches the program. |
| `libuuid.a` | util-linux 2.42.3 | Pulled in by `xapian-core`'s `uuids.o`. |
| `libintl.a` | gettext 1.0 | Pulled in by `libpulsecommon`'s gettext-wrapped strings. |
| `libdbus-1.a` | 1.16.2, built in the image | `Qt6::DBus` is a wrapper around it and the hotkey portal is a D-Bus service. Alpine's `dbus-dev` ships headers, a `.pc` and a shared object only, and dbus 1.16's own CMakeLists hardcodes `add_library(dbus-1 SHARED)` with no option for anything else, so the image `sed`s that one line to `STATIC`; `ENABLE_SYSTEMD=OFF` (libdbus would call into `sd_bus_*`), `DBUS_WITH_GLIB=OFF`, `DBUS_BUILD_X11=OFF`. The daemon and `dbus-run-session` are installed too, because the image build runs the portal test. |
| `libswscale.a` | FFmpeg 8.1.3 | Qt Multimedia's FFmpeg plugin, see §1.2. |
| harfbuzz, double-conversion, libjpeg, libmd4c | Qt's bundled copies | Alpine's `libharfbuzz.a` is LTO bytecode and `double-conversion` has no archive at all, so Qt uses its vendored copies; libjpeg/md4c are bundled by default. |

### Optional (audio only)

| Option | Default | Pulls in | Code |
|---|---|---|---|
| `WITH_QT_MULTIMEDIA` | `ON` | `Qt6::Multimedia` (and transitively `Qt6::Network`, `libpulse`) | `src/audio/multimediaaudioplayer.*`, `src/audio/internalplayerbackend.cc` |

It is the only internal player there is: `InternalPlayerBackend::getActualPlayer()`
takes the first name in `availableBackends()`, and there is no `audioBackend` field in
the config and no Preferences control for it. The external `audio/` helper programs
(`src/audio/externalaudioplayer.*`) are selectable as well.

**FFmpeg the library is still a dependency.** Qt Multimedia decodes and resamples
through its `ffmpegmedia` plugin, and a static binary cannot `dlopen()` that plugin,
so it is linked in (`Qt6::QFFmpegMediaPlugin`, asserted in the `MICRODICT_STATIC_BUILD`
plugin list) and needs `libavformat`/`libavcodec`/`libavutil`/`libswresample` on the
link line — plus `libswscale`, which Qt's plugin interface does not carry. The
Dockerfile's FFmpeg 8 build, its sha256 pin and §9.1's FFmpeg ABI row all stay for
that reason. (`src/dict/utils/ripemd.*` matches an `av*` grep by coincidence — it is
not an FFmpeg user.)

---

## 3. Qt modules

`CMakeLists.txt`:

```cmake
set(MD_QT_COMPONENTS Concurrent LinguistTools Multimedia Widgets Xml)

if (LINUX OR BSD)
    list(APPEND MD_QT_COMPONENTS DBus)
endif ()

find_package(Qt6 6.2 REQUIRED COMPONENTS ${MD_QT_COMPONENTS})
target_link_libraries(microdict PRIVATE
        Qt6::Xml Qt6::Concurrent Qt6::Multimedia Qt6::Widgets)
```

| Module | Status | Why |
|---|---|---|
| `Core`, `Gui`, `Widgets` | linked (pulled in by `Widgets`) | The whole UI; `QTextBrowser`/`QTextDocument` is the article renderer. |
| `Xml` | linked | `QDomDocument` config I/O and dictionary parsing: `src/config.{cc,hh}`, `src/dict/{mdictparser,stardict,xdxf2html}.cc`, `src/ui/favoritespanewidget.{cc,hh}`. |
| `Multimedia` | linked | The Qt Multimedia audio player backend. |
| `DBus` | **linked on Linux/BSD only** | The only way to the desktop's `org.freedesktop.portal.GlobalShortcuts`, which is where the global hotkeys are requested. It is a wrapper around `libdbus-1`; nothing else in the program touches a bus. Windows/macOS never ask, so the component is not even looked for there. |
| `Concurrent` | **needed at build time, absent from the link line** | `QtConcurrent::run()` is a header-only template over `QThreadPool`; 26 call sites in `src/dict/*` and `src/audio/ffmpeg_audiooutput.cc`. The component is required for its headers; `--as-needed` drops the `.so` because no symbol from it is referenced. |
| `LinguistTools` | build tool only | `lrelease` compiles `locale/*.ts` → `.qm`. No runtime library. |
| `PrintSupport` | not used | There is no printing and no PDF export. |
| `Svg` | not a component; the plugin is linked | Nothing in `src/` uses `QSvgRenderer`. SVG inside articles is painted by Qt's `qsvg` image-format plugin, which the static build links in — see §5 for that caveat. |
| `Core5Compat` | not used | Zero `QTextCodec` references in `src/` and `thirdparty/`. The only Qt codec API in use is `QStringConverter` from **Qt Core** (`src/dict/dsl_details.cc`). |
| `TextToSpeech` | not used | There is no text-to-speech here. |
| `WebEngine*`, `WebChannel` | not used | Articles render through `QTextBrowser`. |

---

## 4. Iconv — what it is actually for

**Short version:** iconv is the character-set converter for *dictionary data*. It
has nothing to do with the network or the UI, and on Linux it costs nothing, because
glibc *is* iconv.

### The wrapper

`src/common/iconv.{cc,hh}` defines `Text::Iconv`, a thin RAII wrapper over the POSIX
`iconv_open()` / `iconv()` / `iconv_close()` trio from `<iconv.h>`. It converts a
byte buffer in `fromCode` into `toCode`, throwing `Text::Iconv::Ex` on an unknown
name or an unconvertible byte. Convenience entry points: `toUtf8()`, `toQString()`
(UTF-16), `toWstring()` (UTF-32) and `findValidEncoding()`, which probes a candidate
list and returns the first name the platform understands.

### Who calls it, and on what

10 files, 29 call sites:

| Caller | What is converted |
|---|---|
| `src/dict/dsl.cc`, `src/dict/dsl_details.{cc,hh}` | DSL/DSL dictionaries declare a code page in their index header. `Text::getEncodingNameFor()` (`src/common/text.cc`) maps that enum to an iconv name: `UTF-8`, `UTF-16LE/BE`, `UTF-32LE/BE`, `Windows-1250`, `Windows-1251`, `Windows-1252`. Every `.dsl` source line and every article body goes through it. |
| `src/dict/gls.cc` | GLS uses the same encoding table as DSL, for both indexing and article bodies. |
| `src/dict/mdictparser.cc` | The `encoding` key of an MDict record; `GB2312`/`GBK` are normalised to `GB18030` before being handed to iconv. |
| `src/dict/slob.cc` | The `charset=` part of each slob record's content type. |
| `src/dict/bgl_babylon.cc` | Babylon BGL transcription blocks, hard-coded `Windows-1252`. |
| `src/dict/lsa.cc` | `UTF-16LE` file names inside `.lsa` sound archives. |
| `src/dict/hunspell.cc` | The encoding declared by a spell dictionary's `.aff` file: query words are converted from UTF-32 into the dictionary's code page and results back out. |
| `src/dict/programs.cc` | Output of external helper programs that emit `UTF-16LE`/`UTF-16BE` (Windows tooling). |
| `src/dict/utils/indexedzip.cc` | ZIP entry names that are not UTF-8 — it probes `LOCAL, IBM437, CP866, CP1251, UTF-8` and decodes with whichever works. Used by dictzip'd and zipped MDX dictionaries. |

### Why it cannot simply be dropped

Qt's own conversion API is not enough here:

* `QTextCodec` lives in **Qt5Compat**, which this fork deliberately does not link.
* Qt Core's `QStringConverter` (the only codec API still in the tree, used at
  `src/dict/dsl_details.cc`) covers a fixed small set — UTF-8/16/32 in the usual
  flavours — and offers no `GB18030`, `IBM437`, `CP866`, `Windows-125x`, `EUC-JP`,
  `Shift_JIS`, and no way to ask "is this name valid at all?".

iconv gives access to the ~150 code pages glibc ships, which is exactly what the
legacy Russian / Chinese / Japanese dictionary formats in the wild need.

### What it costs

On the fork's build (Alpine/musl) it costs a static GNU libiconv, because `find_package`
is told not to trust the C library:

```
-DIconv_IS_BUILT_IN=OFF -DIconv_LIBRARY=/opt/gnu-iconv/lib/libiconv.a
-DIconv_INCLUDE_DIR=/opt/gnu-iconv/include
```

GNU's header renames the entry points, so the symbol names in the binary say which
library got linked — `smoke.sh` asserts both halves of that:

```
$ nm build/microdict | grep -c ' [Tt] libiconv_open'
1
$ nm build/microdict | grep -c ' U iconv_open'      # musl's own, must not be used
0
```

On a glibc system the same probe costs nothing: glibc's libc *is* iconv, so no library
is added and no package is needed. The probe is still worth keeping, because it is not
a no-op elsewhere:

* **macOS** — `cmake/Deps_macOS.cmake` runs `find_package(Iconv REQUIRED)` and links
  `Iconv::Iconv`; the CI installs `brew install libiconv`. There it is a real
  shared library.
* **FreeBSD** — deliberately *not* linked: there are two iconv implementations
  (libc's and GNU libiconv) and the build pins libc's by defining `LIBICONV_PLUG`,
  the same trick SDL uses.

### musl: libc's iconv is a subset, so GNU libiconv gets linked

On Alpine `find_package(Iconv)` would also answer "built in to C library" — musl does
ship `iconv()` — but there the answer is wrong for this program. Measured inside the
image, `iconv_open()` in both directions, musl 1.2.6's libc versus the GNU 1.19
archive:

| Name | musl: dict → UTF-8 | musl: UTF-8 → dict | GNU libiconv |
|---|---|---|---|
| `UTF-8/16/32`, `UTF-16LE`, `UTF-32BE`, `UCS-2*`, `ASCII` | ok | ok | ok |
| `WINDOWS-1250/1251/1252`, `CP125x`, `CP866`, `CP850`, `KOI8-R/U`, `ISO-8859-x`, `TIS-620`, `EUC-JP`, `SHIFT_JIS`, `CP932` | ok | ok | ok |
| `GB18030`, `GB2312`, `GBK`, `BIG5`, `Big5-HKSCS`, `EUC-KR`, `CP949` | ok | **fails** | ok |
| `EUC-CN`, `IBM866`, `MacRoman`, `HZ`, `UTF-7`, `ARMSCII-8`, `GEORGIAN-PS`, `PT154`, `RK1048`, `TCVN`, `VISCII` | **fails** | **fails** | ok |
| `UTF-8//IGNORE` | **fails** | **fails** | ok |
| `CP437`, `utf8`, `windows1252` (no dash) | ok | ok | **fails** |
| `IBM437`, `LOCAL`, `MS932`, `windows-31j`, `GB18030:2000`, `CP1006`, `VIQR` | fails | fails | fails |

So it is not a strict superset either way, but GNU libiconv wins where it matters:
musl's CJK pages are **decode-only**, and its alias table is missing a long tail of
names. Dictionary files carry either fixed legacy names (`DSL`, `BGL`, `MDICT`,
hunspell affix files) or completely arbitrary `charset=` strings (`slob`), so the
wider table is the one that avoids silent mojibake. Two honest caveats:

* The encode direction is what musl lacks for CJK, and `Iconv::fromUnicode()` — the
  only call in the wrapper that converts *to* a legacy encoding — currently has **no
  callers** in the tree. The real-world argument today is the alias/codepage table, not
  the direction.
* `IBM437` and `LOCAL`, both probed by `src/dict/utils/indexedzip.cc`, fail with *both*
  libraries; that probe simply falls through to `CP866`, which both provide. It behaved
  the same way on glibc.

The image therefore builds GNU libiconv 1.19 static into a private prefix
(`/opt/gnu-iconv`, so musl's `<iconv.h>` stays the one every other package sees) and
configures with

```
-DIconv_IS_BUILT_IN=OFF -DIconv_LIBRARY=/opt/gnu-iconv/lib/libiconv.a \
-DIconv_INCLUDE_DIR=/opt/gnu-iconv/include
```

FindIconv then hands over the GNU header — which `#define`s `iconv_open` to
`libiconv_open` — instead of concluding that libc provides everything. The proof in
the finished binary:

```
$ nm build/microdict | grep -c ' [Tt] libiconv_open'   # before strip
2
$ nm build/microdict | grep -c ' U iconv_open'
0
```

`libcharset.a` is merged into `libiconv.a` in the Dockerfile, because `iconv_open()`
asks `locale_charset()` whenever the name `LOCAL` is requested.
  the same trick SDL uses.

---

## 5. Transitive and runtime-only pieces

Things that show up in `ldd` but are **not** our dependencies:

| Appears because | Pulled in by |
|---|---|
| `libicui18n`, `libicuuc`, `libzstd`, `libb2`, `libdouble-conversion`, `libpcre2-16` | `libQt6Core.so.6` |
| `libmd4c`, `libpng16`, `libharfbuzz`, `libfreetype`, `libEGL`, `libGLX`, `libxkbcommon`, **`libX11`** | `libQt6Gui.so.6` |
| `libQt6Network`, `libpulse` | `libQt6Multimedia.so.6` |
| `libgssapi_krb5`, `libbrotlidec`, `libproxy` | `libQt6Network.so.6` |

`libmd4c` is *not* from `src/third_party/` — that directory does not exist and no
CMake block references md4c or xxhash any more.

### Fonts: the fork ships none, and asks for none of its own

There is no font file anywhere in the tree: `git ls-files` matches no
`.ttf/.otf/.ttc/.woff`, `resources.qrc` has 89 `<file>` entries and not one is a
font, and the static binary carries none — `strings build-out/microdict |
grep -ciE '\.(ttf|otf|ttc)$'` is 0. `smoke.sh` section 5 asserts both facts on
every image build.

What the application asks for is the **generic CSS family**; the platform's font
configuration picks the face. Measured with the image's static Qt 6.12
(`packaging/docker/qtprobe` builds and runs that check) and with the same binary
run on the host:

| asked for | inside the Alpine image | on the CachyOS test host |
|---|---|---|
| `font-family: 'serif'` | DejaVu Serif — laid out 507.2 px | **Noto Serif** — 484.8 px |
| `font-family: 'sans-serif'` | DejaVu Sans — 500.6 px | Noto Sans — 471.6 px |
| `font-family: 'monospace'` | DejaVu Sans Mono — 527.8 px, fixedPitch | Noto Sans Mono — 526.1 px |
| `font-family: 'NoSuchFontAtAll'` | DejaVu Sans — 500.6 px | Noto Sans — 471.6 px |
| `systemFont( GeneralFont )` | DejaVu Sans | Noto Sans |

The generic names lay out at exactly the width of the concrete family fontconfig
reports for them, so nothing has to be assumed about which fonts are installed.
That does make fontconfig's generic aliases a real runtime dependency rather than
a nicety — §8.9 tracks the version skew between the fontconfig linked into the
binary and the host's configuration files.
Two shortcuts that look like they would work do not, which is why `LiteFonts`
asks by name and by that name alone:

* An empty-family `QFont` with `setStyleHint( QFont::Serif )` resolves to the
  interface font on both machines — an unset family means the interface font, and
  the hint does not override a family that is already set.
* A `font-family` list is not "first one that works wins". Qt hands the whole
  list to fontconfig, which scores an exact family name above an alias: on a host
  whose serif default is Noto Serif, `'serif'` alone lays out as Noto Serif
  (484.8 px) while `'serif','DejaVu Sans Mono'` lays out as DejaVu Sans Mono
  (527.8 px). So no list of well-known families is appended as a safety net — it
  would override the very choice it is supposed to fall back from. `QFontInfo`
  cannot rescue that either, because a nonsense name comes back with the default
  face: "resolved" and "fell back" look identical.

The runtime image does `apk add font-dejavu` (10.6 MB, 75 files). That is the
distro's own package inside the container — a bare container has no glyphs at
all — not a font the fork ships. The extracted binary uses the host's fonts and
the host's `/etc/fonts`; `docker run` can see them with
`-v /usr/share/fonts:/usr/share/fonts:ro`.

Dictionaries that carry their own fonts are a separate matter: MDX `@font-face`
rules and Stardict `.pib` font records still become `bres://` resources and
`font-family:` spans, but Qt's rich-text engine has no `@font-face` and nothing
calls `QFontDatabase::addApplicationFont()`, so such a font is never loaded and
the text falls back to the system face.

### SVG: an image-format plugin, not a link dependency

`Qt6::Svg` is not a component of the build: nothing in `src/` calls `QSvgRenderer`. SVG
inside an article does not go through `Qt6::Svg` at all: it goes through Qt's
**`qsvg` image-format plugin**, which `QTextDocument` uses for
goes through Qt's **`qsvg` image-format plugin**, which `QTextDocument` uses for
`<img src="…svg">`. A plugin is normally a runtime file, and a static binary has no
plugin directory to load one from, so `CMakeLists.txt` adds `Svg` (and
`WaylandClient`) to `MD_QT_COMPONENTS` under `MICRODICT_STATIC_BUILD` and `qt_import_plugins()`
links `Qt6::QSvgPlugin` into the executable. Measured by `packaging/docker/qtprobe.sh`
against the static Qt:

```
image format svg         ok
image format svgz        ok
all formats              bmp cur gif ico jfif jpeg jpg pbm pgm png ppm svg svgz xbm xpm
QSvgRenderer             ok 4096
svg imageformats plugin  ok 64x64
```

So `Qt6::Svg` is a link-time dependency of the static build, and the sound-play and
fold/unfold icons `src/dict/dsl.cc` embeds from `qrc:` — along with any SVG a dictionary
ships — render. Anyone building against a distro's shared Qt needs that distro's `qsvg`
image-format plugin present at runtime; that is a packaging question, not something the
CMake can arrange.

### Qt plugins we do rely on at runtime

* **Static build** (the only one): every plugin the program can reach is compiled in --
  see §1.1. A static binary has nothing to dlopen, so `qt_import_plugins()` is what
  decides what exists at run time: without the `offscreen` platform plugin the program
  cannot even start headless, without `qsvg` no SVG renders, and without
  `xdgdesktopportal` the desktop's palette is not there when the first window is
  created. That list is deliberately closed -- `platformthemes/libqgtk3.a` and the xcb
  platform plugin are not in it and are not wanted. It is also guarded, because Qt
  renames these targets: 6.12 exports the ICO format as `Qt6::QICOPlugin` where 6.8 used
  `Qt6::QIcoPlugin`, and a `if(TARGET)`-guarded list just quietly stops linking it. The
  list now carries both spellings, CMake warns about every target it cannot find and
  fails if a plugin the program cannot run without is missing, and the Dockerfile
  asserts the plugin archives are on the link line.
* **Translations** are not plugins either: `lrelease`'s output is linked into the
  executable as a Qt resource at `:/locale`, which is what `Config::getLocDir()`
  returns; Qt's own catalogues are still looked for under the prefix Qt was built
  with (§8.4).

---

## 6. Vendored / bundled code

| Path | What | Licence |
|---|---|---|
| `thirdparty/qtsingleapplication/` | Single-instance enforcement and "send the word to the running instance". Its `qtlocalpeer.cpp` uses `QLocalSocket`/`QLocalServer` — a **filesystem socket**, never a network socket, so it survives the network removal. | LGPL-2.1 |
| `src/lite/` (`lite_html`, `lite_fonts`, `lite_resources`) | The fork's own adapter: HTML pre-processing for `QTextDocument`, font/stylesheet generation, and the internal-scheme resource resolver. | GPLv3 (ours) |
| `src/stylesheets/article-style.css` | Structure only: it says which elements are blocks, which are bold and how far they are indented. Every colour declaration in it is stripped before it reaches the engine (§8.11). It is the only style sheet the program ships. | GPLv3 |
| `locale/*.ts` | Compiled by `lrelease` at build time. Upstream's files: their `<source>` texts drift away from the code, which silently drops translations — §8.16. | — |

---

## 7. Build options

```
WITH_FFMPEG_PLAYER   ON    # FFmpeg audio backend
WITH_QT_MULTIMEDIA   ON    # Qt Multimedia audio backend
USE_ALTERNATIVE_NAME OFF   # packaging: rename the binary
MICRODICT_STATIC_BUILD      OFF   # fully static link - the Alpine image turns it on
```

Removed: `WITH_EPWING_SUPPORT`, `WITH_ZIM`, `WITH_TTS`, `WITH_X11`.

Preprocessor guards removed: `EPWING_SUPPORT`, `MAKE_ZIM_SUPPORT`, `TTS_SUPPORT` and
the `WITH_X11` guard in the sources. The feature guards that remain are
`MAKE_CHINESE_CONVERSION_SUPPORT`, `MAKE_FFMPEG_PLAYER` and
`MAKE_QTMULTIMEDIA_PLAYER`.

`MICRODICT_STATIC_BUILD=ON` is the new one, and it does exactly four things:

1. `CMAKE_FIND_LIBRARY_SUFFIXES = .a …` plus `-static` on the final link;
2. adds `Svg` and `WaylandClient` to `MD_QT_COMPONENTS` — neither can be loaded at run
   time, and the qsvg image-format plugin behind them is what makes SVG in articles
   render (§5);
3. `qt_import_plugins( INCLUDE … )` for the platform, shell-integration, decoration,
   image-format and multimedia plugins;
4. the private-dependency `-l` list, `libpulsecommon`, `libintl` and the
   `--start-group` link rule of §1.2.

With the option unset the CMake still configures and builds against a distro's shared
Qt — that is how the fork was developed before the static build existed — but nothing
is built or tested that way here, so no numbers from it are quoted in this file.

---

## 8. Known gaps / open items

1. **The static binary is Wayland-only and software-rendered.** No xcb plugin (Alpine
   ships no static archives for the xcb-util family), no EGL, no OpenGL, no RHI. An
   X11-only session needs a build against a distro's Qt, which this repository does
   not provide or test.

   That is asserted in three places, so a Qt bump cannot quietly undo it:

   * the image checks the Qt it has just installed -- `QT_NO_OPENGL`, `QT_NO_EGL` and
     `QT_FEATURE_vulkan -1` in `/opt/qt/include/QtGui/qtgui-config.h`, no GL feature
     set to 1, no mesa/libdrm/libgl/vulkan package installed, and no xcb, eglfs,
     linuxfb or vkkhrdisplay plugin in `/opt/qt/plugins/platforms`;
   * `packaging/docker/qtprobe` checks it from the inside, at run time:
     `QT_CONFIG(opengl)`, `QT_CONFIG(egl)` and `QT_CONFIG(vulkan)` are all off, and
     the paint engine a `QPainter` actually gets is `QPaintEngine::Raster`;
   * the finished binary is checked for `libGL.so`, `libEGL.so`, `libvulkan`,
     `libglapi`, `/dev/dri`, `eglGet*` and `vkCreateInstance`, and `build/build.ninja`
     for a `-lGL`, `-lEGL`, `-lGLESv2`, `-lvulkan` or `-lglapi` on the link line.

   The Vulkan half is what keeps Qt's RHI backing-store path shut: it opens only if
   the platform integration reports `RhiBasedRendering`, and neither the wayland nor
   the offscreen integration does -- both return false for that capability.

   The plugin's metadata still advertises the `wayland-egl` and `wayland-brcm`
   platform names, but with EGL off the plugin returns nullptr for both (main.cpp
   in qtbase's `src/plugins/platforms/wayland`), so `QT_QPA_PLATFORM=wayland-egl`
   ended the program before it had a window, with a message saying the plugin was
   found but could not be loaded. `main()` rewrites those two names to `wayland`
   and says so on stderr: same compositor, same windows, only the buffer path
   differs, and shared memory is the one this binary has. `xcb` is left alone --
   that asks for a different session type rather than a different buffer path, and
   nothing in this build can answer it.

   One thing the binary *does* still carry is FFmpeg's hardware-acceleration **name**
   strings: `vaapi`, `vdpau`, `d3d11va`, `h264_nvenc`. Those are entries in avcodec's
   static name tables, not code -- `--disable-everything` and `--disable-autodetect`
   leave no hardware path and no external library behind. That is why the
   binary-level check greps for driver names rather than for those words.
2. **The desktop's colours are read from the desktop.** Two things do that, and both are
   linked in rather than loaded. Qt's own `xdgdesktopportal` platform-theme plugin reads
   the portal's Settings interface and puts the desktop's palette on the application
   before any of this program's code has run; Qt's `QKdeTheme` (kdeglobals) and
   `QGnomeTheme` (GNOME settings) are compiled into `libQt6Gui`, so the static binary
   has them too, and `strings` finds `kdeglobals` and the `org.kde.kdeglobals.*` names
   -- `smoke.sh` asserts they are there. Neither is relied on to stay current: on KDE 6
   the platform theme's palette goes stale when the scheme changes even though the
   portal announces it, so `src/lite/lite_theme.cc` reads the desktop's look itself, on
   two channels, each carrying only what it is the better source for. The portal is asked
   for its own two keys, `org.freedesktop.appearance color-scheme` and `accent-color`, one
   at a time with `Settings.Read`; every other colour role has no standard key to ask for,
   so it comes from `~/.config/kdeglobals` -- watched through its *directory* rather than
   the file, because KDE replaces the file and a replaced file is un-watched. KDE does
   publish those roles over the portal as well, as `org.kde.kdeglobals.*` namespaces, but
   that is KDE's own file spelled over D-Bus: reading the file means one reader, one
   format, and the same code on a KDE that runs no portal at all. The accent has one
   source, the standard `accent-color`, and it becomes `Highlight`; `Colors:View
   ForegroundLink` still becomes `Link`, and a desktop that publishes no accent keeps the
   accent its palette already wore rather than a made-up one. The KDE selection group
   is not read at all: `xdg-desktop-portal-kde` publishes its palette's `Highlight` under
   `accent-color`, and has since Plasma 5.27, so the portal key already carries the KDE
   selection colour — and the text on the accent follows the accent's own brightness
   rather than a foreground belonging to a background that is no longer painted.
   A KDE user who wants the desktop's *widget style* will not get it: Breeze is
   a KDE Frameworks plugin, so the widgets stay Fusion with the desktop's colours on
   them.
3. **Audio needs a running PulseAudio/PipeWire server.** Qt Multimedia's only Linux
   output is libpulse; without one `QMediaDevices::audioOutputs()` is empty, which the
   probe reports. The external-player path (`audio/` helper programs) is unaffected.
4. **Qt's own catalogues are not inside the single binary.** MicroDict's are: the
   `.qm` files `lrelease` produces are linked in as a Qt resource at `:/locale`, so a
   copied-out static binary translates exactly as well as the AppImage does and needs
   nothing beside itself (§10). Qt's *own* catalogues (standard dialog buttons) are
   looked for under the prefix Qt was built with — `/opt/qt/translations` inside the
   image — so those strings stay English unless `qt_*.qm` is shipped there too.
5. **Global hotkeys are asked of the desktop**, so on Linux/BSD they need a
   session bus and xdg-desktop-portal 1.16 or newer; without them the key stays
   unregistered and the main window says so once. What a portal cannot do: a two-key
   hotkey (`Ctrl+F11, Ctrl+F11`, say) can only have its first combination grabbed by the
   desktop — MicroDict matches the second press itself, within half a second — and the
   desktop, not this application, has the last word on the binding, so what the user
   typed into Preferences is a request, not a guarantee. That request is honoured only
   while the shortcut id is new to the desktop: once a sequence is stored for an id,
   every later request for that id is ignored. So MicroDict releases the registration it
   no longer needs — on KDE through `org.kde.kglobalaccel.Component`'s `cleanUp`, which
   drops the application's group from `~/.config/kglobalshortcutsrc` — when it quits and
   whenever the configured combination changes, which is what makes a changed key in
   Preferences take effect. A compositor also matches the grab against the
   keyboard layout active at the moment of the press, so a letter-based default fires
   only in the layout it was written for — KDE bugs 457776 and 453661 — which is why
   the default sits on a function key. A portal can further accept the request and
   still never fire it: a compositor with no global-shortcut support behind the portal
   answers `0` and then has nothing to grab, and nothing in this application can tell
   that apart — upstream's X11 back end had the same blind spot.
6. **bzip2-compressed SDict / slob / Aard content is unreadable.** Refused with a clear
   log line rather than silently.
7. **The macOS path is untested.** `cmake/Deps_macOS.cmake` was pruned by inspection
   only and has never been compiled here. `Iconv` is a real library there, not a
   no-op.
8. `WITH_QT_MULTIMEDIA` is the only
    audio option left. Dropping even that would remove `Qt6::Multimedia`, its
    transitive `Qt6::Network` and `libpulse`, and the FFmpeg libraries — but it would
    also take away the only internal player, leaving just the external helper programs.
9. **The static binary carries fontconfig 2.15.0**, and reads the *host's*
    `/etc/fonts` configuration. On a rolling distro whose fontconfig is newer, config
    elements it does not know are rejected on stderr — running the binary on CachyOS
    produced ~90 lines like
    `Fontconfig warning: "/etc/fonts/conf.d/48-guessfamily.conf", line 84: unknown attribute "nil"`.
    The newer constants it cannot parse are `ui-serif`, `ui-monospace`, `ui-rounded`,
    `fangsong`, `emoji` and `math`, plus the `serif` / `sans-serif` / `monospace`
    constants of `49-sansserif.conf`. Harmless (the fonts still resolve — `serif`
    measured to Noto Serif on that same host), but noisy, and worth watching now that
    the application *depends* on the generic aliases: `FONTCONFIG_FILE` pointing at a
    matching config, or building a newer fontconfig into the image, would silence it.
10. **Own configuration directories, and nothing inherited.** The fork reads and
    writes `microdict` under `$XDG_CONFIG_HOME`, `$XDG_DATA_HOME` and
    `$XDG_CACHE_HOME`, and never opens a directory named after another program. The
    fork's own `config` is the only configuration file there is -- bringing settings
    across from somewhere else is a manual `cp`.
    `QStandardPaths` and `QFile` only -- no new dependency.
    `packaging/docker/migtest.sh` checks it, both with redirected XDG variables and
    with a plain `$HOME`, and the image build runs that script. The single-instance
    peer name is the fork's own (`MicroDict`): `QtLocalPeer` keeps only the first six
    letters of a peer name, so this one reads `MicroD`, and two programs with
    different names run side by side instead of waking each other up.
11. **The interface and the articles follow the desktop, colours and all.**
    `SystemAppearance` reads the desktop's scheme and its colours -- the portal's
    `org.freedesktop.appearance` keys and kdeglobals -- and puts them on the
    application's palette in its own `refresh()`, before it emits `changed()`: the
    article views are not the last listener called, and a palette set by the main window
    would mean the first tab repainted from the scheme that had just been replaced.
    Where the desktop publishes no colours of its own nothing is set, and Qt's platform
    theme -- which read that same desktop -- already has them. Each `ArticleView` takes
    Base/Text/Link/Highlight from that palette for its widget *and* writes the same
    colours into the document as a `<style>` block. That block is the page's only source
    of colour: `lite_html.cc` strips every colour declaration out of every article style
    sheet on the way in. Placing the block last is not enough on its own -- the engine
    lets a sheet containing a construct it does not understand win over a later sheet
    regardless of order, so one `a:hover` rule made that sheet's `background: white` beat
    the palette. Stripping makes the palette authoritative by construction rather than by
    cascade order. Dictionary-authored inline colours are content and survive. There is
    no *Dark Mode* or *Dark Reader Mode* switch to disagree with the desktop either:
    those controls, and the article style sheets they used to select, are gone from the
    program.
    And when the desktop publishes no colours, whether the article is dark is judged
    from the palette actually in use rather than from the announced scheme: a desktop
    that says dark and hands over a light palette would otherwise get a dark article
    inside a light interface.

12. **File dialogs are the desktop's own when it has a chooser, and Qt's own when it has
    not.** Every file dialog in the program is a `QFileDialog`, and by default a
    `QFileDialog` on Linux is a widget tree Qt paints itself -- its own sidebar, its own
    list of entries, its own idea of where the user keeps things. The desktop's chooser is
    `org.freedesktop.portal.FileChooser`, and Qt already speaks it: `QXdgDesktopPortalFileDialog`,
    part of the `xdgdesktopportal` platform theme. But Qt builds that theme only for a
    process it believes is sandboxed -- `qguiapplication.cpp`'s `checkNeedPortalSupport()`
    looks for the flatpak and snap markers -- or for a theme the user named, and an AppImage
    is neither, so every dialog in this program was Qt's own.

    `main()` now names that theme, but only after asking the session bus whether its portal
    offers `FileChooser` at version 3 or newer (`src/lite/portal_filechooser.hh`: one
    blocking `Properties.Get`, deliberately before the `QApplication` exists, because the
    theme is picked while the application object is built and cannot be changed afterwards).
    Version 3 is the one that added the `directory` option, and three of this program's
    dialogs pick directories -- the dictionary paths, the sound directories and the
    spell-checker path -- so an older portal keeps Qt's dialog rather than getting a chooser
    that cannot answer them. A `QT_QPA_PLATFORMTHEME` the user already set is left alone
    whatever it names. The application says which it chose: `File chooser: the desktop's
    portal one`, or `File chooser: Qt's own`.

    What the theme changes is the file dialog and nothing else: the palette, the fonts, the
    button labels, the icons, the key bindings and every other dialog are forwarded to the
    theme Qt would have used anyway, which is why §8.2 is untouched by this. `portalmock
    --files` drives a real `QFileDialog` through a mock portal and checks the whole chain --
    the theme that gets built, the portal call it makes, the title, the starting folder and
    the filter list that arrive, the `directory` option, the path that comes back and a
    chooser the user closes. `--serve-chooser [s]` does the same against the real
    application on a real display; on this desktop's Wayland session its "Choose a
    directory" is now served by `xdg-desktop-portal-kde`.

    Two things that test cannot reach, both because mock and client share one connection
    there so nothing goes over the wire: the filters arrive as Qt's own `Filter` struct, a
    type the mock cannot name, so the mock checks only that the option was sent; and the
    name filter the portal reports back as selected -- the mapping `articlesaver.cc` decides
    "complete article" versus "single article" on -- is sent by the mock but decodes only
    for a client that received it over a real bus. Both are worth checking by hand against
    a real portal.

    One gap worth knowing, because it is the portal's rather than this program's: when a
    portal advertises `FileChooser` but then fails to open a chooser, Qt's helper has nothing
    underneath it to fall back to -- the base theme has no file dialog of its own -- so the
    dialog comes back empty, exactly as it does for every sandboxed program. A portal that
    does not offer the interface at all is caught before the theme is named, and Qt's own
    dialog is used.

13. **The portal's Settings interface is read key by key, not with `ReadAll`.**
    `ReadAll`'s reply is `a{sa{sv}}`, a map of maps, and QtDBus has no decoder for the
    metatype of the inner values, so each namespace decodes to an empty map; on this
    desktop it answers with 34 namespaces and not one key inside any of them. The reader
    asks for two keys with `Settings.Read`, whose reply is a plain variant and decodes
    cleanly, and it first checks the Settings interface's version property, so a machine
    with no portal pays one timeout rather than one per key. The colour roles that have
    no standard key are read from kdeglobals instead -- the fallback of §8.2, which is
    also what GNOME-independent and wlroots-independent reading comes down to -- where a
    missing one is simply a missing one. `portalmock` answers `Read`/`ReadOne` from the
    same table; Qt's own portal theme still calls `ReadAll`, so it logs a warning against
    the mock and falls back to its own reading. That is noise on stderr, not a failure of
    this client.

14. **No session-end client.** Session management is not something this fork takes on,
    and the save such a client was for is already done by the paths that do run:
    `SIGINT`/`SIGTERM` reaches
    `quitApp()` through `src/unix/ksignalhandler.cc`, `aboutToQuit` reaches
    `performCleanup()`, and both end in `commitData()`, which is where
    config, window state, history and favorites are written. On Wayland there is no X11
    session manager to call `QSessionManager::commitDataRequest` anyway.

15. **`x = {}` on a `QColor` does not mean "unset it".** `QColor` has
    `operator=(Qt::GlobalColor)` next to its copy assignment, and an empty braced list
    prefers the enum overload — `{}` value-initialises an enum to 0, which is
    `Qt::color0` — over the user-defined conversion the copy assignment would need. What
    comes back is an **opaque white**: valid, and not a colour any desktop chose. The
    mock caught `portalAccent = {}` doing exactly that, which made every desktop with no
    portal accent white and, since `Link` falls back to the accent, made links white too.
    `= QColor()` is the spelling that means unset.
16. **`locale/*.ts` is upstream's, and a renamed string loses its translation silently.**
     Qt matches a translation to a string by its **source text**, so when the code asks for
     a string the `.ts` files do not carry, the interface shows English and the translation
     sits in the file unused. Nothing warns about it. Some of the sources in
     `locale/*.ts` are not in the code at all — they belong to features this program
     does not have. `lupdate -no-obsolete` drops those (the `auto format.yml`
     workflow already runs it); where the English itself was rewritten rather than renamed
     — the welcome page is the clearest case — a translator is needed, and the fork has no
     translation project of its own (§Translations in `README.md`).

---

## 9. ABI stability: which of these can be swapped under a running binary

"Stable ABI" answers three different questions, and this project touches all three:

1. **Shared-object policy** — may a *different version* of the same `.so` be dropped
   in place of the one it was built against? This is what a soname and symbol
   versioning are for, and it is what a distro packaging a *dynamic* build cares about.
2. **Interface language** — a C interface with an opaque handle is easy to keep
   compatible; a C++ class with vtables, inline functions and templates in the header
   means the caller compiled against *that* header, and there is no ABI promise to
   fall back on.
3. **Toolchain compatibility** — with everything linked statically, question 1
   disappears at run time (there is nothing to swap), but it comes back as: may two
   archives built by *different* compilers or with different flags be linked
   together? Alpine's LTO archives and libstdc++'s dual ABI say no.

The fork's answer to all of it is structural: the release binary is static, so no
library is ever resolved at run time, and every archive is built in one image by one
compiler. What follows is the ledger behind that decision — and the reason the
version pins in `packaging/docker/Dockerfile` are not laziness.

### 9.1 The libraries, one by one

Sonames below are what Alpine 3.24 actually ships (`ls /usr/lib`), i.e. the policy each
project publishes in practice.

| Library | Interface | Soname in Alpine 3.24 | Stable ABI? | What it means here |
|---|---|---|---|---|
| **zlib** | C | `libz.so.1` | **Yes.** Same soname since 1.0.4; additions only | Safe to take from the distro; the image does |
| **bzip2** | C | `libbz2.so.1` | **Yes.** Frozen since 1.0.x | Only Qt's FreeType chain needs it now |
| **liblzma (xz)** | C | `liblzma.so.5` | **Yes** within 5.x; libtool versioning, additive | Distro-provided |
| **lzo2** | C | `liblzo2.so.2` | **Yes.** Frozen since 2.10 (2016) | Rebuilt only because Alpine has no archive |
| **libogg / libvorbis / vorbisfile** | C | `.so.0` / `.so.0` / `.so.3` | **Yes.** Xiph's ABI policy is strict | Distro-provided |
| **expat** | C | `libexpat.so.1` | **Yes** within soname 1 | fontconfig's dependency |
| **libpng** | C | `libpng16.so.16` | **Yes** within 1.6; the *name* carries the ABI | freetype's dependency |
| **brotli** | C | `libbrotlidec.so.1` | **Yes** within 1.x | freetype's dependency |
| **freetype** | C | `libfreetype.so.6` | **Mostly.** Soname 6 for years, but the libtool version moves and headers change | Qt's text stack; version comes from apk |
| **fontconfig** | C | `libfontconfig.so.1` | **Yes** for the library; the *font cache format* is a separate compatibility axis and is version- and arch-specific | Never a problem statically; a reason not to mix a distro `libfontconfig.so` with a foreign cache |
| **libxkbcommon** | C | `libxkbcommon.so.0` | **Yes** within 0.x, additive with versioned symbols | wayland plugin's keymap handling |
| **D-Bus (`libdbus-1`)** | C | `libdbus-1.so.3` | **Yes** — soname 3 has stood for a decade | The *daemon* is a separate process: its protocol version, not the library's ABI, is what has to keep working |
| **PulseAudio (`libpulse`)** | C | `libpulse.so.0` | **Yes** by explicit project policy | But see `libpulsecommon-17.0.a` below — the same project's *internal* library is the opposite |
| **GNU libiconv** | C | `libiconv.so.2` | **Yes** within soname 2 | musl's built-in iconv is a subset, so this one is linked; its `libcharset.a` half has to be merged in (§4) |
| **libintl (gettext)** | C | `libintl.so.8` | **Yes** within soname 8 | Pulled in by libpulse's gettext-wrapped strings |
| **libuuid (util-linux)** | C | `libuuid.so.1` | **Yes** | Xapian's `uuids.o` needs it |
| **pcre2** | C | `libpcre2-16.so.0` | **Yes** within 10.x | Qt's own regex backend |
| **FFmpeg** | C | `libavcodec.so.62.28.102`, `libavformat.so.62.12.102`, `libavutil.so.60.26.102`, `libswresample.so.6.3.102`, `libswscale.so.9.5.102` | **No — deliberately not.** FFmpeg bumps the major soname of every libav* on each minor release, and its headers pass structs by value with sizes baked into the caller | The single strictest dependency here: 6.1 → 8.1 is not a swap, it is a rebuild of everything that touches it — which here is Qt Multimedia's plugin alone, this program having no FFmpeg code of its own left. The image pins one version by sha256 |
| **hunspell** | **C++** (`HyphenHashManager`, `Hunspell` classes, exported through a thin C-ish wrapper) | `libhunspell-1.7.so.0` | **No.** The soname carries the *API series* (1.7), and the sub-version is what tracks ABI; upstream does not promise C++ ABI stability between releases | Class layout, vtables and `std::string` in the interface mean a caller must be rebuilt against the same series. `src/dict/hunspell.cc` includes `<hunspell.hxx>` directly |
| **xapian-core** | **C++** (whole API is classes) | Alpine 3.24: `libxapian.so.45` (Xapian 2.0); 1.4.x is `libxapian.so.30` | **No across series.** Xapian does version its ABI and bumps the soname when it breaks — 1.4 → 2.0 is exactly such a break (30 → 45) | Two independent compatibilities: the C++ ABI *and* the on-disk index format. The image pins 1.4.32 and does not install Alpine's `xapian-core-dev` at all, because Alpine's is 2.0 |
| **OpenCC** | **C++** (`opencc::SimpleConverter`) | Alpine 3.24: `libopencc.so.1.3` (1.3.1); the fork builds 1.4.2 | **No.** The soname tracks the minor version, i.e. upstream treats minors as ABI breaks | Vendored `libmarisa.a` (`libmarisa.so.0`) is C++ too, and `opencc.pc` does not even list it — hence the hand-added `-lmarisa`. OpenCC 1.4 also gained an optional `dlopen()`ed segmentation-plugin mechanism (`libopencc-<name>.so`): a static binary has nothing to load and needs none of it, the built-in dictionaries are compiled in — but the strings of that mechanism are still in the binary, worth knowing when grepping for `libopencc` |
| **marisa** | **C++** | `libmarisa.so.0` | **No** | Comes from OpenCC's `deps/`, built by the same compiler |
| **toml++** | **C++**, header-only + one TU | n/a | **No** | Same reasoning; the archive is compiled from the upstream `src/toml.cpp` |
| **Qt 6** | **C++**, with a real policy | `libQt6Core.so.6` etc. | **Yes within 6.x** — Qt guarantees binary compatibility across minor releases and version-symbols its exports. **No** for `*Private` modules, and **no** for plugins | The static build links `Qt6CorePrivate`/`GuiPrivate`/`WidgetsPrivate` and the tool archives, which are explicitly *not* ABI-stable; and a Qt plugin must match the exact Qt build. Both are why Qt is built from source here rather than taken from apk |
| **musl (libc)** | C | `libmusl.so.1`, but musl declares ABI compatibility only within a release series | **No across minors** for dynamic linking | Irrelevant for the static binary — the libc is inside it. It *is* the reason the release binary runs on a glibc host and a musl host alike |
| **libstdc++** | C++ | `libstdc++.so.6` | **Yes** within GCC's own policy, plus a *dual ABI* (`_GLIBCXX_USE_CXX11_ABI`) that silently produces two incompatible symbol sets | Every C++ archive in the image is built by the same GCC 15.2, so the two symbol sets never meet. Mixing a distro-built C++ `.a` into this link is the classic way to get an undefined-symbol soup |

### 9.2 Four things that are not ABI problems but look like them

* **Alpine's LTO archives.** `wayland-static` and `harfbuzz-static` ship `.a` files
  full of compiler bytecode rather than machine code: they are only linkable by a GCC
  of the same major version, and the linker's error is about the *object*, not the
  symbols. That is why wayland is rebuilt from source without LTO and Qt uses its
  vendored HarfBuzz.
* **`libpulsecommon-17.0.a`.** PulseAudio promises `libpulse.so.0` stays ABI-stable,
  and at the same time names its internal support library after the exact release and
  refuses to install its headers. Static linking has to reach into that private
  library, so the fork is now tied to PulseAudio 17.0 in a way a dynamic build never
  was — a good illustration of how a static link converts a *packaging* detail into a
  dependency.
* **Weak symbols.** Not an ABI question but a link-model one: a dynamic link fails
  loudly when a symbol is missing, a static one binds an undefined *weak* reference to
  address 0 and crashes at run time. §1.2 is the crash this produced, and
  `-Wl,--undefined=…` in `cmake/Deps_Linux.cmake` is the fix.
* **Qt's plugin target names move between releases.** Qt 6.12 exports the ICO image
  format as `Qt6::QICOPlugin` where 6.8 exported `Qt6::QIcoPlugin`. A
  `qt_import_plugins()` list guarded by `if(TARGET …)` silently stops linking that
  plugin, and the only symptom is that one image format is missing from the article
  view — nothing fails to build. `CMakeLists.txt` now lists both spellings, warns about
  every target it cannot find, fails outright if a plugin the program cannot live
  without is missing, and the Dockerfile asserts that the plugin archives are actually
  on the link line.

### 9.3 What the fork therefore does

1. **One image, one compiler, one pass.** Qt and every C++ dependency are built inside
   the same image by the same GCC, so questions 2 and 3 never arise at run time.
2. **Every from-source download is pinned by sha256** (`/md-fetch.sh` in the
   Dockerfile). A version that changed under the build is a build failure, not a
   silently different binary.
3. **Version choices are deliberate, not latest:** Xapian stays on 1.4.32 because
   Alpine's 2.0 is an ABI break that would need porting `src/dict/*.cc`; FFmpeg moves
   only as a set, together with Qt Multimedia's expectations; hunspell's archive keeps
   the `1.7` soname the CMake looks up.
4. **Nothing shared is shipped.** The runtime image contains the binary and nothing
   else; there is no `.so` of any kind to be swapped, and `ldd` on the binary is an
   error rather than a list.
5. **A distro/dynamic build is still possible** (`cmake -S . -B build`), and there the
   first table is the contract: the C libraries can come from the distro, the C++ ones
   must match the compiler and the series, and Qt's plugins must come from the same Qt
   build. That build is not what this repository tests.

---

## 10. The AppImage — the only Linux package

Everything above is about what is *linked*. This is about what is *shipped*, because
the fork ships exactly one Linux artifact and it is not an installed tree.

There is no `install()` step behind this: `cmake --install` on Linux does nothing, and
the desktop entry is copied into the AppDir verbatim. The one line that cannot ship
verbatim is `Exec=`, and the program rewrites that itself when it writes its autostart
copy (`MainWindow::setAutostart`), to `$APPIMAGE` when it is running from an image and
to its own path otherwise. `Config::getProgramDataDir()` resolves
`<dir of the binary>/../share/microdict` — correct for an AppDir, for an FHS tree and
for a build directory alike — because nothing names a `$PREFIX` that was never used.

macOS keeps its packaging (`Package_macOS.cmake`): there the install tree *is* the
deliverable. There is no Windows packaging.

### 10.1 The one new third-party binary: the runtime

| | |
|---|---|
| What | `runtime-x86_64` from [AppImage/type2-runtime](https://github.com/AppImage/type2-runtime) |
| Version | tag **`20251108`** — the newest *tagged* release; `continuous` moves under a pinned hash, so it is not what to pin |
| Size / shape | 944 632 B, static-pie, musl, no `NEEDED` — it runs on the host whatever the host is |
| sha256 | `2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d` |
| Where it is pinned | `packaging/docker/appimage.sh`, same discipline as every from-source download in the Dockerfile |
| What it does | it is the first bytes of the AppImage: it finds the squashfs that begins where its own ELF ends, mounts it with the squashfuse compiled into it (or unpacks it to a temporary directory when FUSE is missing), exports `APPDIR`/`APPIMAGE`/`ARGV0` and execs `AppRun` |

It is linked to nothing and it is not the program — a 900 KB loader. It is in this
ledger because it is a third-party binary this repository downloads and ships, and
because how it was built constrains the packaging:

* **Its squashfuse knows only zlib and zstd.** Asking `mksquashfs` for xz — what
  `appimagetool` does by default — produces an image that fails at run time with
  *"Squashfs image uses xz compression, this version supports only zlib, zstd"*. The
  build uses `-comp zstd`.
* **It is static, which is why the AppImage can be tested inside the build.** The
  stage runs the finished file with `--appimage-extract-and-run --version`; a glibc
  runtime would not execute in a musl image at all.

### 10.2 The format, in three commands

```sh
size=$(stat -c %s runtime-x86_64)
mksquashfs AppDir out.AppImage -offset "$size" -comp zstd -b 1048576 \
           -root-owned -noappend -mkfs-time 0
dd if=runtime-x86_64 of=out.AppImage conv=notrunc bs=65536
chmod 755 out.AppImage
```

`-offset` is what makes the two halves fit: `mksquashfs` leaves `size` bytes of
padding at the front of the file and computes every internal offset as though the
image started there, and the runtime is then copied over exactly that padding. The
runtime locates the payload with `appimage_get_elf_size()` — "the squashfs that begins
where this ELF ends" — so there is no table of contents to patch afterwards, no
`.digest_md5` section to fill (the type-2 runtime reads only the `.upd_info` and
`.sha256_sig` ELF sections, and treats both as optional) and no signature that has to
verify. That is the whole reason this can be done with Alpine's `squashfs-tools`
instead of `appimagetool`, which is a glibc program.

The AppDir it wraps mirrors an FHS tree, because that is the shape the program expects
now that no prefix is compiled in:

```
AppDir/
├── AppRun                                   # PATH, XDG_DATA_DIRS, APPDIR; execs the binary
├── io.github.dsvi.microdict.desktop   # the spec wants it at the root
├── microdict.png   .DirIcon           # Icon= resolves inside the AppDir
└── usr/
    ├── bin/microdict                  # byte-identical to build/microdict
    └── share/
        ├── applications/io.github.dsvi.microdict.desktop   # autostart reads this
        ├── microdict/locale/*.qm                          # 44 catalogues
        ├── microdict/opencc/*.json *.ocd2                 # 1.3 MB, see below
        └── icons/hicolor/256x256/apps/microdict.png
```

Two of those are consequences of the static link rather than of the format:

* **OpenCC's data travels.** `libopencc.a` has `/usr/share/opencc` compiled into it,
  and inside a mounted image that path is the *host's*. So the `.json`/`.ocd2` files
  ride along in the program's own data dir and `Config::getOpenCCDir()` names that
  directory when it exists — OpenCC resolves a config name relative to the CWD and to
  the config file's own directory before falling back to its compiled-in path, which
  was checked against the built library rather than assumed.
* **The desktop entry exists twice** — at the AppDir root for the AppImage spec, and
  under `usr/share/applications` because that is where `MainWindow::setAutostart` looks
  for the entry it copies into `~/.config/autostart/`.

### 10.3 What the build asserts

`appimage.sh` does not stop at "the file was produced":

| Check | Why it is the right one |
|---|---|
| squashfs magic `hsqs` at exactly the runtime's size | the one way the two halves can come apart |
| `readelf -d` on the AppImage shows no `NEEDED`/`RPATH`/`RUNPATH` | an AppImage that needs a loader stops being one file |
| payload extracted with `--appimage-extract`, i.e. with the runtime's own squashfuse, and `cmp` against the binary that went in | proves the reader on a user's machine can read it, and that nothing was substituted |
| `desktop-file-validate` on the entry | a malformed entry integrates as nothing |
| `APPIMAGE_EXTRACT_AND_RUN=1 … --version` answered | runtime → squashfuse → `AppRun` → program → Qt, end to end |

Measured on the current build: binary 55 784 536 B, AppImage 24 463 864 B (zstd at
1 MiB blocks), runtime 944 632 B of it.
