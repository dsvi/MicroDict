#!/usr/bin/env python3
"""MicroDict: build a tiny StarDict dictionary for manual/GUI testing.

The StarDict reader wants one .idx record per entry as
    word NUL  offset:uint32be  size:uint32be
so with sametypesequence=h the .dict is just the definitions back to back.
"""
import os
import struct
import sys

out = sys.argv[1] if len(sys.argv) > 1 else "."
os.makedirs(out, exist_ok=True)

entries = [
    ("hello",
     '<h3>hello</h3>'
     '<p><b>interjection</b> /h&#601;&#712;l&#601;&#650;/</p>'
     '<p>1. A greeting used when meeting someone.</p>'
     '<p>Try the <a href="bword://theme">theme</a> entry, or the '
     '<span style="color:#c06000">coloured span</span>.</p>'
     '<div class="dsl_opt"><span class="dsl_opt_marker">[optional part]</span></div>'),
    ("theme",
     '<h3>theme</h3>'
     '<p><b>noun</b> 1. A subject of discourse; a unifying idea.</p>'
     '<p>2. A set of colours and fonts applied to an interface.</p>'
     '<table><tr><td>light</td><td>dark</td></tr>'
     '<tr><td>#ffffff</td><td>#272828</td></tr></table>'
     '<p>See also <a href="bword://hello">hello</a>.</p>'),
    ("dark",
     '<h3>dark</h3><p><b>adjective</b> (darker, darkest)</p>'
     '<p>1. With little or no light.</p>'
     '<p>2. (of colours) deep, not light.</p>'
     '<pre>print("dark mode")</pre>'),
    ("test",
     '<h3>test</h3><p><b>noun</b> A procedure intended to establish quality.</p>'
     '<p>This article comes from a hand-made StarDict dictionary used to check '
     'that the rich-text article view still renders, navigates and re-colours.</p>'),
]

entries.sort(key=lambda e: e[0].encode())

dict_blob = b""
idx = b""
for word, defn in entries:
    data = defn.encode("utf-8")
    idx += word.encode("utf-8") + b"\0" + struct.pack(">II", len(dict_blob), len(data))
    dict_blob += data

base = os.path.join(out, "litetest")
with open(base + ".dict", "wb") as f:
    f.write(dict_blob)
with open(base + ".idx", "wb") as f:
    f.write(idx)
with open(base + ".ifo", "w") as f:
    f.write("StarDict's dict ifo file\n"
            "version=2.4.2\n"
            "wordcount=%d\n"
            "idxfilesize=%d\n"
            "bookname=Lite Test\n"
            "sametypesequence=h\n" % (len(entries), len(idx)))

print("wrote", base + ".{ifo,idx,dict}", "words:", len(entries))
