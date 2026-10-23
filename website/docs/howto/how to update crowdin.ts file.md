This project uses Crowdin to organize its translations. When new translatable strings
appear in the sources, `locale/crowdin.ts` has to be refreshed: that file is what
Crowdin shows the translators, and the per-language `.ts` files come back from it.

# How to update the crowdin.ts file

```
lupdate -no-obsolete -no-ui-lines -locations none src -ts locale/crowdin.ts
```

`lupdate` ships with Qt Tools — `qt6-tools-dev` on Debian/Ubuntu, `qt6-qttools-dev`
on Alpine, `qt6-tools` on Arch.

the option `-no-obsolete` will remove obsolete items from crowdin.ts file.

the option `-no-ui-lines` will not generate line numbers in the crowdin.ts file. The line numbers change too often, which will make too many code changes between commits.

The Crowdin side of it is configured by `crowdin.yml` in the repository root.
