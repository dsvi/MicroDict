## Index file

Each index file has 4 sections.

1. `IdxHeader`:
2. `ExtraInfo` (Being used but unnamed in source code):
3. `Chunks`:
4. `BtreeIndex`:

The `IdxHeader` are 32bits blocks of various meta info of the index. The most important info are `chunksOffset` and `indexRootOffset` pointing to the starting offset of `BtreeIndex` and `Chunks`.

Some dicts only have one `ExtraInfo`: the `dictionaryName` which is an uint32 size of a string followed and the string.

Each chunk contains uint32 size of uncompressed data, uint32 size of zlib compressed data, and the zlib compressed data.

The `Chunks` maybe used by both `IdxHeader` and `BtreeIndex`.

By adding new a new chunk to `Chunks` and store an offset to `IdxHeader`, `ExtraInfo` can store arbitrary long information.

`BtreeIndex` is a zlib compressed typical btree implementation in which each Node will include `word` info and a `offset` that pointing to corresponding `chunk`'s position.

Note that a `chunk` only includes necessary data to find an article, and it does not contain the `word`.

The exact data in `chunk` is decided and interpreted by dictionary implementations. For example, the starting and ending position of an article in a dictionary file.

## What's under the hood after a word is queried?

After typing a word into the search box and pressing enter, the article view is
asked to show `gdlookup://localhost?word=<wanted word>`. There is no browser and no
URL-scheme handler: `ArticleView` recognises that address itself and asks
`ArticleMaker` for the page, which starts the `DataRequest`s on the dictionary
formats. Resource files are requested through `bres://` and resolved the same way,
in-process - a static binary cannot `dlopen()` anything, and there is no network
request left to intercept.

The HTML that comes back is then handed to `QTextBrowser`, which is a much smaller
capability set than a web engine's: see
[Article rendering](article_rendering.md) for what that means in practice.

