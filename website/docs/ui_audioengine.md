# Audio playback

By default the program plays pronunciations through its own internal player, which is
Qt Multimedia — the only internal back-end here, so the drop-down next to
`Internal Audio Player` has a single entry. There is no FFmpeg player; see
[`DEPENDENCIES.md`](https://github.com/dsvi/MicroDict/blob/main/DEPENDENCIES.md).

To use a program of your own instead: `Edit` → `Preferences` → `Audio`, select
`External Audio Player` and type its command line.

**Note:** The player command must be either in your system PATH or specified with an absolute path.

**Linux/macOS examples:**

vlc:
```
vlc --intf dummy --play-and-exit --no-video
```

ffplay:
```
ffplay -autoexit -nodisp
```

mpv:
```
mpv --no-video --no-audio-display --no-config
```

or other audio player.

