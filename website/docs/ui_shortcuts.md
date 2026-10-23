# Shortcuts

| Shortcut              | Action                                                                                                                           |
|-----------------------|----------------------------------------------------------------------------------------------------------------------------------|
| Alt+Left, Backspace   | (Main window) history navigation: show previous founded results                                                    |
| Alt+Right             | (Main window) history navigation: show next founded results                                                        |
| Alt+Down              | Jump to article from next dictionary                                                                                             |
| Alt+Up                | Jump to article from previous dictionary                                                                                         |
| Alt+Enter, Ctrl+Enter | (Search line) Show translation in new tab                                                                                        |
| Alt+D, Ctrl+L         | (Main window) go to search line                                                                                    |
| Alt+G                 | (Main window) choose dictionaries group from group list                                                            |
| Alt+S                 | (Main window) playback pronunciation for current word                                                              |
| Alt+PgDown            | (Main window) switch to next dictionaries group                                                                    |
| Alt+PgUp              | (Main window) switch to previous dictionaries group                                                                |
| Ctrl++                | Increase articles font size                                                                                                      |
| Ctrl+–                | Decrease articles font size                                                                                                      |
| Ctrl+0                | Set default articles font size                                                                                                   |
| Ctrl+MouseWheel       | Increase/decrease articles font size                                                                                             |
| Ctrl+A                | Select all articles in current page                                                                                              |
| Ctrl+C                | Copy selected                                                                                                                    |
| Ctrl+D                | Switch to dictionary headwords dialog (if it is open)                                                                            |
| Ctrl+E                | Add current tab to Favorites                                                                                                     |
| Ctrl+F4               | (Main window) minimize to system tray                                                                                            |
| Ctrl+F5, F5           | Rescan dictionaries                                                                                                              |
| Ctrl+F                | (Main window) search in page / (In dictionaries dialog) go to filter line                                          |
| Ctrl+H                | Show/hide history pane                                                                                                           |
| Ctrl+I                | Show/hide Favorites pane                                                                                                         |
| Ctrl+M                | Show/hide main menu                                                                                                              |
| Ctrl+N                | (In dictionary headwords dialog and full-text search dialog) switch to main window                                               |
| Ctrl+O                | Above all windows                                                                                                                |
| Ctrl+Q                | (In main window) close MicroDict                                                                                                |
| Ctrl+R                | Show/hide results pane                                                                                                           |
| Ctrl+S                | Show/hide search pane                                                                                                            |
| Ctrl+T                | (In main window) create new tab                                                                                                  |
| Ctrl+W                | (In main window) close current tab                                                                                               |
| Ctrl+PgDown           | (In main window) switch to next tab                                                                                              |
| Ctrl+PgUp             | (In main window) switch to previous tab                                                                                          |
| Ctrl+Tab              | (In main window) switch to next tab (can be changed in preferences)                                                              |
| Ctrl+Shift+A          | Select current article only                                                                                                      |
| Ctrl+Shift+C          | Copy selected as text                                                                                                            |
| Ctrl+Shift+F          | Open/switch to full-text search dialog                                                                                           |
| Ctrl+Shift+W          | Close all tabs                                                                                                                   |
| Ctrl+Shift+S          | Stop current playing sound                                                                                                       |
| Del                   | (History) delete select line                                                                                                     |
| Enter                 | (In main window) go to search line                                                                                               |
| Esc                   | (Main window) go to search line (the action for the Esc key can be changed in preferences)                                        |
| F1                    | MicroDict reference                                                                                                             |
| F2                    | (In main window) save current page to html                                                                                       |
| F3                    | Dictionaries dialog                                                                                                              |
| F4                    | MicroDict preferences                                                                                                           |
| F11                   | Open configuration folder                                                                                                        |

There is no print path and no JavaScript to inspect.

## Global (system-wide) hotkey

One hotkey works while the window is not focused. On Linux and BSD it is requested
from the desktop through `org.freedesktop.portal.GlobalShortcuts`, so the desktop
decides who finally gets the key combination and can refuse it:

| Hotkey (default) | Action |
|---|---|
| F12 | Show the main window, then look up the clipboard contents |

It is configured in Preferences → Hotkeys, and it needs a running
`xdg-desktop-portal`; without one it is simply not registered. Pressing it shows the
main window, reads the clipboard and searches for what it finds — one action, one
window. There is no separate show/hide hotkey and no separate lookup hotkey.

A hotkey written as two combinations separated by a comma is a two-step sequence: the
desktop can only grab a single combination, so MicroDict asks for the first one and
matches the second press itself — the action fires only when the combination is
pressed twice within half a second. A hotkey written as a single combination fires on
the first press.

The desktop grabs the first combination exclusively: no other application can use it
while MicroDict runs. `Ctrl+C, Ctrl+C` is therefore not a usable default — it is copy
in every program on the desktop.

### The desktop hands out the binding

The requested combination is only a request: the desktop stores the binding in its own
settings and asks you to press the key to confirm it. On KDE those stored bindings live
in `~/.config/kglobalshortcutsrc`, under a group named after the application; System
Settings → Shortcuts writes there too. A stored sequence is not overwritten from the
outside, so an application that merely asks again for a shortcut id it already
registered is ignored.

MicroDict therefore releases the registration it no longer needs — on KDE that is
`org.kde.kglobalaccel.Component`'s `cleanUp`, which drops the application's group from
that file — when it quits, and before it asks for a different combination. That is what
makes a changed key in Preferences take effect, and it also clears a grab left behind by
a process that was killed.

### The key has to mean the same thing in every keyboard layout

A compositor matches a global shortcut against the layout that is active at the moment
of the press. A letter's keysym differs between layouts, so a letter-based default
only fires in the layout it was written for: on KDE a `Win+C` grab never fires while a
Russian layout is active. This is a known KDE defect, reported as
[KDE bug 457776](https://bugs.kde.org/show_bug.cgi?id=457776) and, for Wayland,
[KDE bug 453661](https://bugs.kde.org/show_bug.cgi?id=453661).

Function keys, arrows and the like carry the same keysym in every layout, which is why
the default sits on `F12`. If you choose a letter, expect it to work only in the
layout you typed it in.

### Wayland: the clipboard goes to the focused window

On Wayland a client receives clipboard data only while it holds focus, so a background
application cannot read the clipboard at all. The hotkey therefore shows the main
window first and reads the clipboard a moment later — the window has to be focused
before the data offer arrives.
