# BiasedDoom Website

The static site for BiasedDoom's Python scripting: full API reference,
guides, the twenty-five-example showcase, and the integration/roadmap
analysis — styled as a zero-JS 90s fansite (black/ember/blood, bevel
chrome, tiled sidebar, screenshot strips) with modern hygiene
(responsive collapse, print stylesheet, focus outlines).

## Build

```bash
python3 website/build.py
```

Output lands in `website/out/` (git-ignored). Python 3.9+ is the only
requirement — the generator is standard-library only. The build fails
loudly on broken internal links or missing asset references.

## How it fits together

- `template.html` + `style.css` — the 90s page frame (masthead banner,
  sectioned sidebar, footer joke).
- `assets/` — committed media: the banner logo (`logo.png`, scaled from
  `wadsrc/static/widgets/banner.png`), the sidebar tile (`grid.png`),
  `favicon.png`, and in-engine screenshots under `assets/shots/`.
- `content/*.html` — hand-written page fragments (no `<html>` wrapper).
- `out/api.html` — **generated** from `docs/scripting/biaseddoom.pyi`
  (parsed with `ast`): module constants, function signatures and
  docstrings, the `Actor`/`Line`/`Sector`/`Player`/`RngStream` handles,
  the `bd.actors` registry, and the full actor-constant table.
- `out/roadmap.html` — rendered from
  `docs/development/python-api-roadmap.md`.
- Navigation is data-driven: `NAV_SECTIONS` in `build.py` (adding a page
  = fragment + one `PAGES` entry + one sidebar link).

The `.pyi` is the single source of truth for both VSCode completions and
the website API reference. Its actor-constant block is regenerated from
the live engine with the `dumppystub` console command. **Rule: prose
lives in docstrings, never in generated files.**

## Publish

Deployment is via GitHub Actions (`.github/workflows/pages.yml`) on every
push to `master` that touches the site or its sources. The site publishes
to `https://ericsonwillians.github.io/BiasedDoom/`.
