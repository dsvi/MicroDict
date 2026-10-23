MicroDict is an advanced dictionary lookup program: it is the dictionary program it
descends from, with the web engine taken out — articles are rendered by Qt's own
rich-text engine, so the program needs no Chromium, no network stack and no
bundled fonts.

![MicroDict on Linux](img/linux_time.png)

## Features

* Supports many dictionary formats, such as MDX, DSL, StarDict, XDXF, Slob, DictD,
  BGL, GLS, Aard, SDictionary — see [dictionary formats](dictformats.md)
* Supports special types of "dictionaries", such as external programs, morphology,
  transliteration and audio files — see [sources management](manage_sources.md)
* Built-in full-text search
* Supports user-submitted pronunciation recordings
* One look only: no style sheets, no themes, no font overrides — see
  [appearance](topic_userstyle.md)
* Follows the desktop's colours and its dark/light choice, including the accent
  colour, and switches together with the desktop without a restart
* On Linux: one fully static binary for x86-64 and aarch64 — no shared libraries,
  no interpreter, no bundled fonts ([how it is built](howto/build_static_linux.md))


The full dependency ledger is in
[`DEPENDENCIES.md`](https://github.com/dsvi/MicroDict/blob/main/DEPENDENCIES.md);
what the article view can and cannot render is in
[Article rendering](article_rendering.md).

## Quick start

Start the program and type a word into the search box. To have dictionaries found,
add their folders under `Edit` → `Dictionaries` → `Files`; see
[managing sources](manage_sources.md).

[Install](install.md) covers the Linux AppImage — `x86_64` and `aarch64` — and
building from source. macOS is source-only, and there is no Windows build.
