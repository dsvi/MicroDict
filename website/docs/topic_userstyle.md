# Appearance

MicroDict has exactly one look, the desktop's. Colours come from the desktop's colour
scheme and fonts from the host, and there is no style sheet, no theme picker and no
font override — not for dictionary articles and not for its own interface.

## Interface

The interface is drawn with the desktop's colour scheme and the desktop's font, both
chosen by the platform theme. Nothing is read from disk and nothing is picked in
Preferences:

* there is no `qt-style.css` in the configuration folder, and none is looked for;
* there is no add-on style folder and no "Add-on style" control;
* there is no "Interface Style" control — the widget style the platform theme selects
  is used as it is;
* there is no "Dark Mode" or "Dark Reader Mode" control: the desktop's colour scheme
  decides, and a running program follows it as it changes.

## Article colours

An article's colours are the desktop's colours: the base, the text, the links, the
selection and the accent are read from the desktop's colour scheme and written into the
page. To change how articles look, change the desktop's colour scheme — the article
view follows it immediately, without a restart.

Nothing else sets article colours:

* Every colour declaration in every article style sheet is removed before the sheet
  reaches the rendering engine, so the palette block is the only colour source. The
  built-in `src/stylesheets/article-style.css` keeps only structure — which elements
  are blocks, which are bold, how far they are indented.
* There is no list of article styles to pick from any more, and no `article-style.css`
  in the configuration folder is read.
* A dictionary's own explicit colours are content, not styling, and are left alone: an
  XDXF `<c c="red">` still renders red.

See [Article rendering](article_rendering.md) for what the renderer understands.

## Fonts

The program ships no font files and names none of its own. The interface uses the
desktop's font. Articles ask for the generic CSS families — `serif`, `sans-serif`,
`monospace` — and the host's font configuration resolves them. There is no font
preference to set.

## Accessing the Configuration Folder

To access MicroDict's configuration folder, follow these steps based on your operating
system:

### macOS

`~/.microdict`

### Linux

`~/.config/microdict`, or `$XDG_CONFIG_HOME/microdict` if that is set.

The fork never uses a configuration directory named after the program it was forked
from. See [Configuration folders](topic_config_folders.md).

## Configuration File Structure

The configuration folder holds the program's state and nothing style-related:

```
.
├── config      preferences
├── favorites   saved favorites, written when they change
└── history     search history
```

There is no style sheet to create here. A `qt-style.css` or a `styles/` folder left
over from an older version is simply never opened; both can be deleted.

## No scripts

There is no `article-script.js`: the article view has no JavaScript engine, so there is
nothing to inject into. Restyling, expanding collapsed parts and highlighting are all
done by the C++ side ([Article rendering](article_rendering.md)).
