# The documentation site

CeeUI's guide, built with [VitePress](https://vitepress.dev/).

| Path | What |
|---|---|
| `index.md` | the home page |
| `guide/` | the guide's pages |
| `.vitepress/config.mts` | navigation, the sidebar, search, and CeeSafe highlighting |
| `.vitepress/ceesafe.tmLanguage.json` | the grammar of CeeSafe's VS Code extension, a copy (CeeSafe's `editors/vscode/syntaxes`) |
| `public/` | images |

## Working on it

```sh
cd site
pnpm install
pnpm run dev          # a local server with live reload
pnpm run build        # what CI runs; fails on a dead link
```

## The examples are compiled

Every `ceesafe` example of the guide is compiled against CeeUI by `tools/guide_check.py`, which CI runs:

```sh
python3 tools/guide_check.py                    # every page
python3 tools/guide_check.py site/guide/layout.md
```

What follows `ceesafe` on an example's fence says how it is checked:

| Fence | The example is |
|---|---|
| ` ```ceesafe ` | a whole source file, which imports `ceeui` itself |
| ` ```ceesafe frame ` | a frame's body: statements with `ui: borrow mut Ui` and `metrics` at hand |
| ` ```ceesafe skip ` | not compiled: a fragment of a host, or code that needs other packages |

A page says only what the source says: when the library changes, a page's example stops compiling, and the page is
changed with it.
