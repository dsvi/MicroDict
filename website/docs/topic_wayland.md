# Wayland

MicroDict runs as a **native Wayland client**. The static Linux binary contains
the `wayland` and `offscreen` Qt platform plugins and nothing else — there is no xcb
plugin, so there is no X11 session to fall back to and no XWayland trick that would
make one appear.

## Choosing the platform

Priority at startup:

| Setting | Effect |
|---|---|
| `QT_QPA_PLATFORM` | honoured first; `offscreen` is what the repository's headless checks use |
| `MICRODICT_FORCE_XCB=1` | asks for the X11 platform. Only does anything in a build that actually contains the xcb plugin (a dynamic build on a distribution that ships it); the static binary has no xcb plugin, so the app then simply fails to find a platform |
| `MICRODICT_FORCE_WAYLAND=1` | force the native Wayland platform |
| nothing set | Qt decides, which on a Wayland session means native Wayland |

```bash
env MICRODICT_FORCE_WAYLAND=1 microdict
```

## How the hotkey and clipboard features reach the desktop

| Feature | How it works |
|---|---|
| Global hotkey (`F12`, clipboard lookup) | requested from `org.freedesktop.portal.GlobalShortcuts`; the desktop decides who owns the combination and can refuse it |
| Giving the binding back on exit or when it changes | `org.kde.kglobalaccel`'s `cleanUp()` is called on every component that owns our shortcut id |
| What the lookup reads | `QClipboard`: the clipboard on Wayland, the X11 selection under X11 |
| Position of the main window | wherever the compositor puts it; Wayland clients do not position themselves |

If `xdg-desktop-portal` is not running, hotkeys are simply not registered — the
program logs that and carries on. Everything else works.

## Wayland-specific notes

* The window's `app_id` is `io.github.dsvi.microdict`, taken from the desktop
  file the program installs; that is what the taskbar matches the icon against.
* Fractional scaling and HiDPI are handled by Qt and the compositor; nothing in the
  program overrides them.
* Input methods work through Qt 6's Wayland input-method protocol, provided the
  compositor offers one.
* Screenshots of the scan popup are the compositor's business; the program never
  grabs the screen.

## Running under X11 only

There is no supported path in the static binary. Either run a Wayland compositor
(weston, cage, or a Wayland session), or build the program yourself: a dynamic build
against a distribution Qt that ships the xcb plugin gets the X11 platform back, and
`MICRODICT_FORCE_XCB=1` then selects it.
