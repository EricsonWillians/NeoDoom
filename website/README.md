# BiasedDoom Website

The static site for BiasedDoom's Python scripting: full API reference,
guides, and the integration/roadmap analysis.

## Build

```bash
python3 website/build.py
```

Output lands in `website/out/` (git-ignored). Python 3.9+ is the only
requirement — the generator is standard-library only.

## How it fits together

- `template.html` + `style.css` — the restrained-retro page frame.
- `content/*.html` — hand-written page fragments (no `<html>` wrapper).
- `out/api.html` — **generated** from `docs/scripting/biaseddoom.pyi`
  (parsed with `ast`): module constants, function signatures and
  docstrings, the `Actor`/`Line`/`Sector`/`Player` handles, the
  `bd.actors` registry, and the full actor-constant table.
- `out/roadmap.html` — rendered from
  `docs/development/python-api-roadmap.md`.

The `.pyi` is the single source of truth for both VSCode completions and
the website API reference. Its actor-constant block is regenerated from
the live engine with the `dumppystub` console command. **Rule: prose
lives in docstrings, never in generated files.**

## Publish

Deployment is via GitHub Actions (`.github/workflows/pages.yml`) on every
push to `master` that touches the site or its sources. One-time setup:
repository **Settings → Pages → Source: GitHub Actions**. The site then
publishes to `https://ericsonwillians.github.io/BiasedDoom/`.
