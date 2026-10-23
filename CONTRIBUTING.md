# Contributing to MicroDict

There is no code of conduct for this repository. Behaviour expected of
contributors is the ordinary kind: report bugs with the dictionary or the exact
steps that reproduce them, keep the discussion about the problem, and do not
push someone else's work under your own name.

Where to read before changing anything:

* `packaging/docker/README.md` — the build recipe, the AppImage and the checks
  the image runs against itself.
* `DEPENDENCIES.md` — the dependency ledger; it explains why a library is
  linked, and a change that adds one back has to be argued there.
* `website/` — the user manual. A change that removes or renames a feature has
  to update it in the same commit; the nav in `website/mkdocs.yml` is part of it.

Branches and remotes: `main` is the working branch and `origin` is this fork.
