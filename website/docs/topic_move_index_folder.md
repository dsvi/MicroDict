# Move the index folder

The **index** folder stores the full-text search indexes produced by MicroDict. For large dictionaries (e.g. Wikipedia, Wiktionary), the size of these indexes can grow very large, potentially filling up your system drive.

There are generally two approaches to deal with the growing index size:

- Enable [Portable Mode](topic_portablemode.md) so that indexes are kept in a location of your choosing.
- Move the index folder **in place** using a symbolic (soft) link, keeping the original path intact.

This page explains the latter approach.

!!! note
    By default MicroDict keeps its configurations and indexes in the user's home directory. To find out the exact location used on your system, see the storage layout in [Portable Mode](topic_portablemode.md).

!!! note
    This fork always uses the XDG cache directory for indexes:
    `$XDG_CACHE_HOME/microdict/index`, default
    `~/.cache/microdict/index`. Point `XDG_CACHE_HOME` somewhere else instead
    of linking if that is simpler — see
    [Configuration folders](topic_config_folders.md).

---

## Linux, BSD and macOS

1. Close MicroDict.
2. Copy the index folder to another place, take `/mnt/big/index_new` for example.
3. Delete the original index folder.
4. Create the link:

   ```sh
   ln -s /mnt/big/index_new ~/.cache/microdict/index
   ```

5. Run MicroDict.

!!! warning
    Do **not** put the target folder (e.g. `/mnt/big/index_new`) *inside* the folder
    being linked, otherwise you may create an infinite recursion. Keep them on
    separate paths.

!!! warning
    Removing the link must not follow it. `rm ~/.cache/microdict/index` removes
    the link itself; `rm -r` on a path with a trailing slash removes the files it
    points to.
