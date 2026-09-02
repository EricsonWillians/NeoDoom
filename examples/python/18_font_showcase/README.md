# Font Showcase

A static "typography wall" drawn in screen space on `map_load` (plus one
animated title), demonstrating every text-styling axis of the canvas API —
now including the v2 features: layers, `duration=`, `align=`, real
`shadow`, `outline`, inline color escapes, `measure_text`, and gradient
rects. All items are registered once against a dark translucent
`bd.draw_rect` backdrop; there is no per-frame Python drawing loop.

## Layers and durations (canvas v2)

Every `draw_*` call accepts keyword-only `layer` (z-order; higher draws on
top, ties broken by registration order) and `duration` (seconds of GAME
time — the item auto-expires and pauses with the game). This wall demos
both:

- **The backdrop is registered LAST but drawn BEHIND everything** — all
  text/lines are `layer=1`, the backdrop rect is `layer=0`. Registration
  order no longer decides z-order; layer does. This matters: the rainbow
  title is re-registered every 3 tics (bumping its registration sequence),
  and only its `layer=1` keeps it permanently above the backdrop.
- **One line self-destructs** — the orange `I VANISH AFTER 12s` line uses
  `duration=12.0`, so it needs no `bd.schedule` + `bd.draw_clear` cleanup.
- **The backdrop is a gradient** — `color2=` on `bd.draw_rect` fades
  vertically from `color` (top) to `color2` (bottom).

## What each section demonstrates

Left column (the classic wall):

- **Animated rainbow title + measured underline** — `F O N T S` in
  `bigfont` with `shadow=True`, re-registered every 3 tics by a repeating
  `bd.schedule` task (reusing the same `id` replaces the item, so cycling
  the RGB color via std-lib `colorsys.hsv_to_rgb` animates it in place).
  Every tick also calls `bd.measure_text(TITLE_TEXT, font="bigfont",
  scale=1.4)`, which returns the exact size **in pixels**, and resizes a
  `bd.draw_rect` underline to match — dynamic layout with no hardcoded
  width. (Pixel→normalized conversion uses `vid_defwidth`/`vid_defheight`
  as the reference canvas: exact at the default resolution, approximate
  otherwise, since scripts cannot query the live drawer size.)
- **Builtin fonts** — one line each for `smallfont`, `smallfont2`,
  `bigfont`, `bigupper`, `confont`, and `indexfont`, labeled with its own
  name. The scale per line is tuned by hand (big fonts down, tiny fonts up)
  so all six rows have a similar visual height.
- **Scale ladder** — `smallfont` at `scale` 0.5 / 1.0 / 2.0 / 3.0.
- **Non-uniform scale** — `scale=(2.5, 1.0)` stretches `WIDE` horizontally;
  `scale=(1.0, 2.5)` stretches `TALL` vertically.
- **Named colors** — `gold`, `red`, `green`, `blue`, `cyan`, `orange`,
  `yellow`, `white`: each word is its own `bd.draw_text` call in its own
  font-color name.
- **RGB tuple colors** — custom `(r, g, b)` tuples, with each line printing
  the exact values it is drawn in (`RGB 255,80,200`, ...).
- **Custom font** — see below.

Right column (canvas v2):

- **Plain / shadow / outline** — the same white text three ways:
  `shadow=True` is now a real dark offset copy at (+2, +2) px, and
  `outline=True` draws a black halo on all four sides (white text so the
  outline reads).
- **Inline color escapes** — one line mixing `\x1c[Gold]GOLD\x1c- normal
  \x1c[Fire]FIRE`: `\x1c[Name]` switches font color mid-string, `\x1c-`
  resets to the base color. **Caveat:** use a named color (or none) as the
  line's base `color` — with an `(r, g, b)` tuple base the escapes only
  modulate brightness. Pick named color OR escapes, not tuple + escapes.
- **Duration auto-expire** — the self-clearing line described above.

Full-width bottom band:

- **Alignment** — a vertical `bd.draw_line` guide at `x=0.30` with three
  copies of the same text anchored on it via `align="left"` / `"center"` /
  `"right"` (screen-space text only). The anchored edge sits exactly on the
  guide line.

All sections are labeled with gold headers and separated by thin
`bd.draw_line` rules.

## Custom fonts

The PK3 ships a root `FONTDEFS` lump (packed automatically by
`build-python-examples.sh`) defining `PYHUD`, a template font sourced from
the Doom IWAD's big yellow `STCFN` status bar glyphs. STCFN only contains
uppercase letters, digits, and a few symbols, so all custom-font text is
uppercase. The wall draws `CUSTOM FONT: PYHUD` in `font="pyhud"` at
`scale=2` with a drop shadow, plus `PYHUD + RGB COLOR` in a custom RGB
tuple to prove custom fonts and custom colors compose. Because the font
only exists with a Doom-family IWAD, both calls are wrapped in
`try/except ValueError` with a `bigfont` fallback.

## Run it

```sh
tools/play-python-example.py   # pick 18_font_showcase
```

or manually:

```sh
./tools/build-python-examples.sh 18_font_showcase
./build/biaseddoom -iwad /path/to/DOOM2.WAD \
    -file ./build/python-examples/18_font_showcase.pk3 \
    -python +map map01
```

The whole wall appears on screen as soon as the map starts; only the
rainbow title (and its measured underline) moves, and the duration line
vanishes on its own after 12 seconds.
