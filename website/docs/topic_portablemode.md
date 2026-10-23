# Portable Mode

By default, MicroDict keeps its configuration and indexes in the user's XDG
directories (see [Configuration folders](topic_config_folders.md)).
To keep everything in one portable location, create a `portable` folder next to the
program file; once that folder exists the program launches in portable mode.

Portable Mode is especially convenient when you want to keep all data (including the large full-text search indexes) on an external drive or in a custom folder. If you prefer to keep the existing directory structure and only relocate the growing index folder, see [Move the index folder](topic_move_index_folder.md) instead.

In portable mode, the configurations and indexes are stored in the `portable` folder.
All dictionaries must be placed in or inside the subfolders of `content` folder, which located besides the main program file.

Folder structure:

```
<program directory>/
├── microdict       the program
├── content/              all dictionary files (.mdx, .dsl, .ifo, ...)
│   └── morphology/       morphology dictionaries
└── portable/             everything the program writes lives here
    ├── config            program configuration
    └── index/            full-text search indexes
```

All dictionary files you want to load can be placed in or inside the subfolders of the `content` folder described above.

## Portable mode in an AppImage

The `portable` folder cannot be used there: everything inside an AppImage is read-only
and is gone again when the program exits. The AppImage runtime has its own version of
the same idea — a writable directory next to the AppImage file, named after it with
`.config` appended:

```
/media/usb/MicroDict-0.0.0-x86_64.AppImage
/media/usb/MicroDict-0.0.0-x86_64.AppImage.config/
└── microdict/
    ├── config            program configuration
    └── index/            full-text search indexes
```

If that directory exists the runtime exports `XDG_CONFIG_HOME` to it before starting
the program, so the configuration — and under it the `microdict` folder the
program uses — is written beside the file rather than in the user's home. A directory
with `.home` appended does the same for `$HOME`, which moves the data and cache
folders along with it. Nothing else changes: dictionaries are still added from wherever
they live, since there is no `content/` folder inside an image to put them in.
