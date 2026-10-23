# Command Line Interface

MicroDict's command line is the one it was forked with, minus the options that
belonged to removed features taken out. The executable is `microdict`.

```
microdict [options] [word]
```

## Options

| Option | Description |
|---|---|
| `-h`, `--help` | Show the built-in help |
| `-v`, `--version` | Print the version and diagnostic information |
| `-l`, `--log-to-file` | Save debug messages to `microdict_log.txt` in the configuration folder |
| `-g`, `--group-name <name>` | Switch the main window to the named dictionary group |
| `-r`, `--reset-window-state` | Forget the saved window geometry and state |
| `word` | Word or phrase to look up at startup |

`microdict --help` is always the current truth; this page is not.

## Examples

```bash
microdict hello                       # look a word up in the main window
microdict -r                          # start with a clean window state
microdict -l                          # write microdict_log.txt into the config folder
microdict -v                          # version + build diagnostics
```

## URL scheme

The desktop entry registers the program as the handler for `microdict:` and
`dict:` URLs:

```
microdict://<word>
```

## Environment variables

| Variable | Effect |
|---|---|
| `MICRODICT_FORCE_WAYLAND` | Force the native Wayland platform |
| `MICRODICT_FORCE_XCB` | Force the X11 platform. Only meaningful in a build that contains the xcb plugin — the static Linux binary ships the Wayland and offscreen plugins and nothing else |
| `QT_QPA_PLATFORM` | Qt's own override; `offscreen` is what the repository's headless checks use and it wins over both variables above |

## Notes

* MicroDict is single-instance: if it is already running, the arguments are
  handed to the running copy through its local peer (`MicroDict`) and the second
  process exits. That peer name is this program's own: `QtLocalPeer` keeps six letters
  of it as the instance id, so another program running at the same time keeps its own.
