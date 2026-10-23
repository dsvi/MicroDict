# Local build setup

With venv
```
cd ./website
python3 -m venv ./venv/
source ./venv/bin/activate
# source ./venv/bin/activate.fish
pip3 install mkdocs-material==9.7.6
```

Then

```
mkdocs serve
```

`9.7.6` is the version `.github/workflows/deploy_website.yml` installs, so the local
build and the published one are the same. That workflow is what publishes the site:
`mkdocs gh-deploy --force` from this folder pushes the built site to the `gh-pages`
branch.

# More details can be found

* <https://squidfunk.github.io/mkdocs-material/>

* <https://www.mkdocs.org/>
