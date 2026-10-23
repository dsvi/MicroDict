# Article rendering

MicroDict renders dictionary articles with **Qt's rich-text engine**
(`QTextBrowser` / `QTextDocument`), not with a browser. Everything else on this page
follows from that one choice.

## Why this engine

A browser engine would pull in QtWebEngine, QtWebChannel, a sandbox helper, a separate
render process and a network stack — none of which can be linked statically on musl.
Without them there is [one static binary](howto/build_static_linux.md). What
`QTextDocument` does not have is JavaScript and modern CSS layout.

## The adapter

`ArticleMaker` still produces the same web pages it always did. Before
`QTextDocument` sees them, `src/lite/lite_html.cc` rewrites them so that what is
left is renderable *and* means the same thing:

* **Unknown tags are mapped, not dropped.** `<article>`, `<section>`, `<header>`,
  `<footer>`, `<nav>`, `<aside>`, `<figure>`, `<main>` become `<div>`; `<mark>`,
  `<abbr>`, `<small>`, `<time>`, `<label>` become `<span>`; `<kbd>`/`<samp>` become
  `<code>`. The paragraph structure the article stylesheet depends on survives.
* **Subtrees that cannot render are removed with their content**: `script`,
  `noscript`, `form`, `button`, `select`, `textarea`, `object`, `video`, `audio`,
  `canvas`, inline `svg`, `math`, `template`, `dialog`, `iframe`. Deleting them is
  deliberate: leaving them in would show their source text.
* **`display: none` is honoured by deleting the element.** The rich-text engine
  ignores that property, so every element MicroDict's own stylesheet hides —
  `gdfromprefix`, `gddicticon`, `collapse_expand_area`, `bglpos`, MediaWiki's
  `mw-editsection` and friends — is removed instead.
* **Collapsed articles and DSL optional parts are expanded.** Without JavaScript
  there is no way to expand them later, so they are shown from the start.
  Preferences → Advanced → "Expand optional parts" still controls `.dsl_opt`.
* **`<style>` contents are sanitized.** Qt's CSS parser has no at-rules, so
  `@media` / `@supports` / `@layer` / `@container` / `@scope` blocks are unwrapped
  (their rules are kept unconditionally) and `@import` / `@font-face` are dropped.
* **Scripts cannot be reached.** Every `on*="…"` attribute is removed and
  `javascript:` URLs and bare `#` hrefs are neutralised.
* `<title>` is taken out of the body and used as the tab title.

## What CSS is honoured

Qt implements a documented subset of HTML and CSS — see Qt's *Rich Text HTML
Subset* and *Rich Text CSS Subset* pages for the authoritative list. In practice,
inside dictionary articles:

* **Works**: element, class and id selectors; `color`, `background-color`,
  `font-family`, `font-size` in absolute units, `font-weight`, `font-style`,
  `text-align`, `text-indent`, `text-decoration`, `vertical-align`, `margin`,
  `white-space: pre`, `list-style-type`, tables with `border`, `colspan`, `rowspan`,
  `scope`.
* **Does not work**: flexbox, grid, `position: fixed/absolute/sticky`,
  `display: none` (handled by the adapter instead), `::before`/`::after` generated
  content, CSS custom properties and `var()`, transforms, transitions, animations, `float`
  around images in the way a browser does it, `overflow`, `padding`, `border` on
  anything but a `<table>`, `border-radius`, `outline`, `box-shadow`, `opacity`,
  `font-size` in `em` or `%`, `background-image`, media queries (unwrapped,
  so their rules always apply).

A dictionary whose layout depends on the second list degrades to plain document
flow: the text is still there and still readable, the columns and boxes are not.

## Colours and dark mode

The desktop's palette is applied to the whole interface and then written into the
article as a `<style>` block. It is the **only** source of colour in the page: before
any article style sheet is handed to the engine, `lite_html.cc` strips every colour
declaration out of it. That is not caution for its own sake — the engine lets a sheet
containing a construct it does not understand win over a later sheet regardless of
order, so "put the palette last" does not reliably work. One `a:hover` rule in one
sheet was enough to make that sheet's `background: white` beat the palette block.

What is left is structure: the built-in `src/stylesheets/article-style.css` says which
elements are blocks, which are bold and how far they are indented, and nothing about
colour. A dictionary's own explicit colours are content and are left alone — an XDXF
`<c c="red">` still renders red.

There is no Dark Reader — that was a JavaScript script — so dark mode is exactly "the
desktop's colours, applied to the page". Change the desktop's colour scheme and the
article changes with it, no restart needed. There is nothing to override on top of
that — see [Appearance](topic_userstyle.md).

## Fonts

The program ships no font files and names none of its own: articles ask for the
generic CSS families (`serif`, `sans-serif`, `monospace`) and the host's fontconfig
answers with the machine's faces. There is no font preference — the four generic
families are mapped onto the engine's own defaults and that is the end of it.

## Images

Article resources (`bres://`) are resolved in-process — there is no network request
and no plugin directory. SVG files work because the `qsvg` image-format plugin is
compiled into the binary. An inline `<svg>` element in article markup is dropped
(the engine cannot draw it); `<img src="…svg">` is fine. Images that arrive after
the document was built are filled into it as they load.

## Navigation, search, audio

* `gdlookup://`, `bword://`, `gdfrom://`, `bres://` and friends are recognised by
  the view itself; there is no URL-scheme handler and no interceptor.
* Jumping between articles and to a dictionary's head is done with the document's
  own anchors (`gdfrom-…`), not with scripts.
* In-page search (`Ctrl+F`) and the full-text search panel's "highlight all" use the
  engine's selection highlighting; there is no mark.js.
* Audio playback is unaffected: it never went through the article view
  ([audio engine](ui_audioengine.md)).

## What stops working

* Anything a dictionary's JavaScript does: popups, tabs inside an article,
  collapsible widgets, search boxes in the article.
* MathJax/TeX in slob dictionaries — MathJax is JavaScript, so formulas stay as
  their source text.
* Websites and iframes: there is nothing to load them with
  ([dictionary formats](dictformats.md)).
* Articles laid out with flexbox/grid/absolute positioning.

If a specific dictionary renders badly, compare it with upstream and
say so in the [issue tracker](feedbacks.md) — the adapter is deliberately small, and
a missing tag mapping or a class that should be deleted is an easy fix.
