# Configuration folders

MicroDict keeps its own files everywhere. It can be installed next to the program
it was forked from, and neither one touches the other's files.

## Where this fork keeps things

| What | Linux / BSD | macOS |
|---|---|---|
| Configuration, history, favourites | `$XDG_CONFIG_HOME/microdict` (default `~/.config/microdict`) | `~/.microdict` |
| Data (dictionaries' extra files) | `$XDG_DATA_HOME/microdict` (default `~/.local/share/microdict`) | — |
| Search indexes | `$XDG_CACHE_HOME/microdict/index` (default `~/.cache/microdict/index`) | — |
| Read-only program data (OpenCC data) | beside the executable: `<binary dir>/../share/microdict` | inside the `.app` |
| Translations | inside the executable — they are linked in as a Qt resource | inside the executable |

Nothing is installed to `$PREFIX` on Linux, so that path is worked out from where the
program was started from rather than compiled in: the AppImage keeps its payload at
`usr/bin` and `usr/share/microdict` inside the mounted image, and a bare binary with
no such directory beside it simply has no OpenCC data. The translations take no part
in that search — they are linked into the executable.

`XDG_CONFIG_HOME` &co. are honoured, so a container or a test run can point the
whole program at a scratch directory:

```
XDG_CONFIG_HOME=/tmp/cfg XDG_DATA_HOME=/tmp/data XDG_CACHE_HOME=/tmp/cache \
    microdict
```

## Nothing is read from the old program's folders

The fork reads and writes only directories named `microdict`. Upstream fell back to
a directory named after itself whenever the XDG variables were unset, and an older
revision of this fork copied upstream's `config` into its own directory on the first
run. Neither happens now: starting this program beside upstream changes nothing over
there, and the fork's own `config` is the only configuration file it will ever open.

To bring settings across from the old program, copy its `config` file into
`$XDG_CONFIG_HOME/microdict/config` yourself.

Absolute dictionary paths carry over as they are; relative ones were relative to the
*old* config directory, so adjust them under `Edit` → `Dictionaries` → `Files`. Keys
belonging to features this fork does not have — Anki, printing, text to speech, ZIM,
web sites — are ignored on the way in and are gone the next time the file is written.
Search indexes are not in that file at all; they are rebuilt in the fork's own cache
directory.

## If the configuration is broken

A `config` file that cannot be parsed does not get silently overwritten. The program
asks what to do; choosing to continue with default settings renames the file to

```
config.<yyyyMMdd_HHmmss>.bad
```

and starts over. Choosing not to continue leaves the file exactly where it was.

## Portable mode

A `portable` folder next to the executable moves everything into
`<exe directory>/portable`: no XDG, no registry. See
[Portable mode](topic_portablemode.md).

## Single instance

The program refuses to start twice; the second invocation hands its command line to
the running one. The IPC name it registers is `MicroDict`. `QtLocalPeer` keeps only
the first six letters of a peer name, so this one registers `MicroD`; upstream's
peer keeps its own name and the two programs can run at the same time.

## Verifying it

The repository ships a script that checks exactly the behaviour above — the
`microdict` directories created and used, nothing at all created anywhere else,
the fork's own `config` the only one there is, and a truncated one left whole
rather than rewritten:

```
QT_QPA_PLATFORM=offscreen sh packaging/docker/migtest.sh
```

It prints `=== migtest.sh: config-directory checks passed ===` and exits 0 when
everything holds.
