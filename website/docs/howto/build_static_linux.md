# Build the static Linux binary

The Linux release of MicroDict is one file with no shared-library
dependencies: `0` `NEEDED` entries, no `PT_INTERP`, no bundled fonts. It runs on any
x86-64 or aarch64 Linux — glibc or musl, any distribution, any version — that has a
Wayland compositor. Shipped as an AppImage, which is that binary plus its
translations, icon and desktop entry: one file per architecture.

The recipe is `packaging/docker/Dockerfile`. Docker is only the build farm: the
results are copied out of the image, and the image itself is not needed to run the
program.

## Build it

```
cd MicroDict
./build-appimage.sh              # the AppImage only, into build-out/
./packaging/docker/build.sh      # the whole image, plus the bare binary
```

`build.sh` builds the image, runs the checks inside it, and extracts:

```
build-out/microdict                    # the binary, translations linked into it
build-out/MicroDict-<version>-<arch>.AppImage
build-out/portalmock                   # the portal test double, see below
```

From scratch this compiles Qt and every dependency, so expect roughly half an hour
on 32 cores. Later runs reuse Docker's layer cache; changing only the program's own
sources re-runs just the last steps.

The image is architecture-neutral: `TARGETARCH` picks the matching AppImage runtime
and names the output `MicroDict-<version>-x86_64.AppImage` or
`MicroDict-<version>-aarch64.AppImage`. Build each one natively —
`docker buildx build --platform linux/arm64` on an arm64 host — because the same
build under QEMU emulation is 5–20× slower, which is what the release workflow does
not have to pay for.

To build without extracting:

```
docker build -f packaging/docker/Dockerfile -t microdict-static .
```

to stop after the dependencies (Qt + everything else) are built:

```
docker build -f packaging/docker/Dockerfile --target deps -t microdict-deps .
```

and to rebuild only the AppImage stage:

```
docker build -f packaging/docker/Dockerfile --target appimage -t microdict-appimage .
```

## Why it has to be Alpine

musl is the only libc that is sane to link statically; glibc's static libc pulls in
NSS and `dlopen` stubs and refuses to do DNS or locale lookups without a matching
runtime. Everything therefore comes from Alpine 3.24, and everything Alpine does
not ship as a `.a` archive is built from source in the image: Qt 6.12 (qtbase,
qtsvg, qtwayland, qtmultimedia, qtshadertools, qttools), GNU libiconv, wayland,
libffi, hunspell, xapian-core 1.4, OpenCC, LZO, toml++, FFmpeg, PulseAudio
and D-Bus. Each download is pinned by sha256. The version choices and the reasons
are in
[`DEPENDENCIES.md`](https://github.com/dsvi/MicroDict/blob/main/DEPENDENCIES.md).

Two consequences worth knowing:

* **Qt plugins are compiled in.** A static binary cannot `dlopen()` a plugin, so the
  platform plugin (wayland, offscreen), the image formats, the multimedia backend and
  the `xdgdesktopportal` platform theme are imported at link time by
  `qt_import_plugins()` in `CMakeLists.txt`.
* **The host tools Qt builds must be static too.** With dynamic host tools, musl's
  static-TLS surplus makes the TLS offsets baked into `libQt6Core.a` wrong and every
  tool dies inside `QThread::currentThread()` — `rcc --help` segfaults. That is why
  `CMAKE_EXE_LINKER_FLAGS=-static` is passed to the Qt builds.

## The AppImage

`packaging/docker/appimage.sh` wraps the binary in the last stage of the image. It is
the only Linux packaging the fork does — there are no `install()` rules, no CPack
generators, no Flatpak and no AppStream metadata anywhere in the tree.

An AppImage is a squashfs of a program tree with a small runtime prepended to it, and
the format is three commands:

```
size=$(stat -c %s runtime-x86_64)
mksquashfs AppDir out.AppImage -offset "$size" -comp zstd -b 1048576 \
           -root-owned -noappend -mkfs-time 0
dd if=runtime-x86_64 of=out.AppImage conv=notrunc bs=65536
chmod 755 out.AppImage
```

`-offset` is what makes it work: `mksquashfs` leaves `size` bytes of padding at the
front and computes every internal offset as if the image started there, and the
runtime then overwrites exactly that padding. The runtime finds its payload as "the
squashfs that begins where this ELF ends", so there is no table of contents to patch
and nothing to sign — the type-2 runtime reads only the `.upd_info` and `.sha256_sig`
ELF sections, and neither is required.

Notes that cost some experimenting:

* **`appimagetool` is not used.** It is a glibc program and this image is musl. Its
  recipe is the three commands above, and the runtime it would embed is pinned by
  sha256 instead — `20251108` from
  [AppImage/type2-runtime](https://github.com/AppImage/type2-runtime), a static musl
  binary, one per architecture (`runtime-x86_64`, `runtime-aarch64`), which is also
  why the AppImage can be *run* as a check inside the build.
* **Compression is zstd, not xz.** That runtime's bundled `squashfuse` was built with
  zlib and zstd only; asking it for xz fails with "Squashfs image uses xz compression,
  this version supports only zlib, zstd".
* **The AppDir mirrors an FHS tree** (`usr/bin` next to `usr/share/microdict/`)
  because that is what the program expects: `Config::getProgramDataDir()` is resolved
  relative to the executable, not compiled in from `$PREFIX`.
* **OpenCC's data travels.** `libopencc.a` has `/usr/share/opencc` compiled into it,
  and inside a mounted AppImage that path is the host's, not the bundle's. So the
  `.json`/`.ocd2` files go into the program's own data dir and
  `Config::getOpenCCDir()` names that directory when it exists.

## The checks that run inside the image

The build fails unless all five pass — `qtprobe.sh`, `portalmock.sh`, `smoke.sh`,
`migtest.sh` and `appimage.sh`. What each of those files is and what it proves:
[`packaging/docker/README.md`](https://github.com/dsvi/MicroDict/blob/main/packaging/docker/README.md).

Then the image asserts the link is really static — `! ldd` is not a valid test on this
stack, because musl's `ldd` exits `0` for a static-pie image:

```
file build/microdict            # says "static-pie linked"
[ "$(readelf -l build/microdict | grep -c INTERP)" = 0 ]
! readelf -d build/microdict | grep -qE 'NEEDED|RPATH|RUNPATH'
```

`portalmock` is also copied out to `build-out/portalmock`, so the same desktop
double can be driven against the extracted binary on the host:

```
build-out/portalmock --serve-theme 40     # dark -> light -> dark, printing each step
```

### What those checks do not cover

None of the five scripts ever opens a Babylon `.bgl`: `mkdict.py` generates a
StarDict dictionary and `smoke.sh` loads an xdxf folder, so `Babylon::readBlock()`
and the `readEntry()` loop that builds a `.bgl` index have no automated test.
Checking them needs a real `.bgl`, and those cannot be shipped here — they are
Babylon's own files. One that is freely downloadable and does the job:

```
https://archive.org/download/BabylonEnglishSpanish/Babylon_English_Spanish.BGL
sha256 38791c9d8f63cdc3622052f2df31ddced9d0b37c8814bf09bf856b8727c02d32   7 225 214 B
```

Put it in a folder, point a `<paths><path>` entry at it, and start the program.
The log should show `Bgl: Building the index for dictionary: …`, then
`Default charset: WINDOWS-1252`, `Writing index…`, `Building a tree of 493
elements`, `Load done`; a lookup of `house` then comes back from
`Babylon English-Spanish`. That file carries 161 879 headwords, 231 839 alternate
spellings and 2 resource records, so it drives the block loop hard — which is
where the buffer ownership lives.

## Running the result

```
./build-out/MicroDict-<version>-<arch>.AppImage <word>   # or chmod +x and double-click
./build-out/microdict <word>                             # the bare binary
```

Either way it uses the host's fonts (`/etc/fonts`), the host's desktop portal for
hotkeys and colours, and its own configuration folders — see
[Configuration folders](../topic_config_folders.md).

If you run the image instead of the extracted files, the container decides which
fonts exist; mount the host's if you want the same text as everywhere else:

```
docker run --rm -e XDG_CONFIG_HOME=/cfg -v $PWD/cfg:/cfg -v $PWD/dicts:/dicts \
    -v /usr/share/fonts:/usr/share/fonts:ro --device /dev/shm microdict-static <word>
```
