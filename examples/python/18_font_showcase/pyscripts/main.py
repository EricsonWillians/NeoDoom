"""Typography wall: every builtin font, sizes, colors, a custom font, and
the canvas API v2 text features (align, shadow, outline, inline color
escapes, measure_text, layers, and duration)."""

import colorsys

import biaseddoom as bd


RAINBOW_TICS = 3  # tics between rainbow-title recolors (id-replace animation)
TITLE_TEXT = "F O N T S"
TITLE_SCALE = 1.4
EXPIRE_SECONDS = 12.0  # the duration= demo line self-clears after this long

BACKDROP_ID = 90
TITLE_ID = 100
TITLE_UNDERLINE_ID = 101
ROW_ID_BASE = 110  # one id per static wall row (auto-incremented)
SEPARATOR_ID_BASE = 60  # one id per thin section separator line
ALIGN_GUIDE_ID = 80  # vertical guide line for the alignment demo

# Canvas v2 uses layers for z-order: higher layers draw on top, and items
# within one layer draw in registration order. ALL text/lines here are
# layer=1; the backdrop rect is layer=0 and is registered LAST (at the end
# of draw_wall) to prove that layer, not registration order, decides what
# is behind. This now matters: the rainbow title is re-registered every 3
# tics (bumping its registration sequence), and only its layer=1 keeps it
# permanently above the backdrop.
TEXT_LAYER = 1
BACKDROP_LAYER = 0

# Builtin fonts at scales that give all six rows a similar visual height:
# the big fonts are scaled down, the tiny console/index fonts scaled up.
BUILTIN_ROWS = [
    ("smallfont", 1.0),
    ("smallfont2", 1.0),
    ("bigfont", 0.55),
    ("bigupper", 0.55),
    ("confont", 1.3),
    ("indexfont", 1.6),
]
SIZE_LADDER = [0.5, 1.0, 2.0, 3.0]
NAMED_COLORS = ["gold", "red", "green", "blue", "cyan", "orange", "yellow",
                "white"]
RGB_ROWS = [((255, 80, 200), 0.03), ((90, 220, 120), 0.21),
            ((255, 160, 40), 0.38)]
CUSTOM_RGB = (255, 120, 180)  # second pyhud line: custom font + custom color

ROW_PITCH = 0.036  # vertical step per content row
HEADER_PITCH = 0.030  # vertical step per section header (separator + label)

LEFT_X = 0.03  # left column text x
RIGHT_X = 0.60  # right column (canvas v2 sections) text x
ALIGN_GUIDE_X = 0.30  # vertical guide line for the alignment demo

HEADER_COLOR = "gold"
SEPARATOR_COLOR = (90, 90, 110)

hue = [0.0]  # list = mutable closure cell for the rainbow animation


def safe_draw(func, *args, **kwargs):
    # Draw calls raise RuntimeError while the world mutates (e.g. map
    # unload) and ValueError for unknown fonts on non-Doom II IWADs.
    try:
        func(*args, **kwargs)
    except (RuntimeError, ValueError):
        pass


def screen_pixels():
    """Best-effort screen size in pixels for measure_text unit conversion.

    measure_text returns PIXELS, while screen-space draw calls take
    normalized 0..1 fractions, so the underline below the title must
    convert. There is no script-visible drawer size, so use the configured
    default resolution as the reference canvas (exact in the default
    fullscreen setup; only the underline length is approximate otherwise).
    """
    try:
        return float(bd.get_cvar("vid_defwidth")), \
            float(bd.get_cvar("vid_defheight"))
    except Exception:
        return 640.0, 480.0


def draw_wall():
    """Register the whole static wall once (screen-space items persist)."""
    row = 0  # running id source for wall rows
    sep = 0  # running id source for separator lines

    def put(text, y, x=LEFT_X, **kwargs):
        nonlocal row
        safe_draw(bd.draw_text, text, id=ROW_ID_BASE + row, x=x, y=y,
                  layer=TEXT_LAYER, **kwargs)
        row += 1

    def header(title, y, x=LEFT_X, sep_x2=0.53):
        # Thin separator line plus a small gold label above each section.
        nonlocal sep
        safe_draw(bd.draw_line, id=SEPARATOR_ID_BASE + sep,
                  x1=x, y1=y, x2=sep_x2, y2=y,
                  color=SEPARATOR_COLOR, alpha=0.8, layer=TEXT_LAYER)
        sep += 1
        put(title, y + 0.004, x=x, font="smallfont", color=HEADER_COLOR,
            scale=1.0)

    y = 0.085

    header("BUILTIN FONTS", y)
    y += HEADER_PITCH
    for font, scale in BUILTIN_ROWS:
        put("{}  ABCdef 123".format(font), y, font=font,
            color=(230, 230, 230), scale=scale)
        y += ROW_PITCH

    header("SCALE LADDER", y)
    y += HEADER_PITCH
    for scale in SIZE_LADDER:
        put("SIZE {}x".format(scale), y, font="smallfont",
            color="white", scale=scale)
        y += ROW_PITCH

    header("NON-UNIFORM SCALE", y)
    y += HEADER_PITCH
    # Non-uniform (sx, sy) scale stretches glyphs on one axis only.
    put("WIDE (2.5, 1.0)", y, font="smallfont", color="gold",
        scale=(2.5, 1.0))
    y += ROW_PITCH
    put("TALL (1.0, 2.5)", y, font="smallfont", color="gold",
        scale=(1.0, 2.5))
    y += ROW_PITCH

    header("NAMED COLORS", y)
    y += HEADER_PITCH
    # Named font colors: each word is its own draw_text in its own color.
    x = LEFT_X
    for name in NAMED_COLORS:
        put(name, y, x=x, font="smallfont", color=name, scale=1.0)
        x += 0.065
    y += ROW_PITCH

    header("RGB TUPLE COLORS", y)
    y += HEADER_PITCH
    # RGB tuple colors: the text prints the very values it is drawn in.
    for color, x in RGB_ROWS:
        put("RGB {},{},{}".format(*color), y, x=x, font="smallfont",
            color=color, scale=1.0)
    y += ROW_PITCH

    header("CUSTOM FONT (FONTDEFS)", y)
    y += HEADER_PITCH
    # Custom FONTDEFS font (Doom II STCFN glyphs, uppercase only). It does
    # not exist without a Doom IWAD, so fall back to bigfont.
    try:
        bd.draw_text("CUSTOM FONT: PYHUD", id=ROW_ID_BASE + row, x=LEFT_X,
                     y=y, font="pyhud", color="gold", scale=2, shadow=True,
                     layer=TEXT_LAYER)
        row += 1
        bd.draw_text("PYHUD + RGB COLOR", id=ROW_ID_BASE + row, x=LEFT_X,
                     y=y + 0.055, font="pyhud", color=CUSTOM_RGB, scale=1.0,
                     layer=TEXT_LAYER)
        row += 1
    except ValueError:
        put("CUSTOM FONT: PYHUD (bigfont fallback)", y, font="bigfont",
            color="gold", scale=0.55, shadow=True)
    except RuntimeError:
        pass  # world still mutating; nothing drawn this time

    # -- canvas v2 sections (right column) ---------------------------------

    header("V2: PLAIN / SHADOW / OUTLINE", 0.085, x=RIGHT_X, sep_x2=0.985)
    # Same white text three ways. shadow=True is a real dark offset copy at
    # (+2, +2) px; outline=True draws a 1px black halo on all four sides.
    put("THE LIVING END  (plain)", 0.115, x=RIGHT_X, font="smallfont",
        color="white", scale=1.5)
    put("THE LIVING END  (shadow)", 0.151, x=RIGHT_X, font="smallfont",
        color="white", scale=1.5, shadow=True)
    put("THE LIVING END  (outline)", 0.187, x=RIGHT_X, font="smallfont",
        color="white", scale=1.5, outline=True)

    header("V2: INLINE COLOR ESCAPES", 0.245, x=RIGHT_X, sep_x2=0.985)
    # \x1c[Gold] / \x1c[Fire] switch font color mid-string; \x1c- resets to
    # the base color. CAVEAT: pass a NAMED color (or none) as the base —
    # with an (r, g, b) tuple base the escapes only modulate brightness, so
    # never mix tuple colors and escapes on one line.
    put("\x1c[Gold]GOLD\x1c- normal \x1c[Fire]FIRE\x1c- reset", 0.275,
        x=RIGHT_X, font="smallfont", color="white", scale=1.5)

    header("V2: DURATION AUTO-EXPIRE", 0.335, x=RIGHT_X, sep_x2=0.985)
    # duration= self-clears the item after N seconds of GAME time (it
    # pauses with the game): no bd.schedule + bd.draw_clear cleanup needed.
    put("I VANISH AFTER {}s (duration=)".format(int(EXPIRE_SECONDS)),
        0.365, x=RIGHT_X, font="smallfont", color="orange", scale=1.0,
        duration=EXPIRE_SECONDS)

    # -- v2 alignment demo (full-width bottom band) -------------------------

    # Three copies of the same text at the SAME x, anchored left/center/
    # right on the vertical guide line (align= is screen-space text only).
    safe_draw(bd.draw_line, id=SEPARATOR_ID_BASE + sep,
              x1=0.015, y1=0.900, x2=0.985, y2=0.900,
              color=SEPARATOR_COLOR, alpha=0.8, layer=TEXT_LAYER)
    sep += 1
    put("V2 ALIGN: SAME X=0.30, THREE ANCHORS ON THE GUIDE LINE", 0.906,
        font="smallfont", color=HEADER_COLOR, scale=1.0)
    safe_draw(bd.draw_line, id=ALIGN_GUIDE_ID,
              x1=ALIGN_GUIDE_X, y1=0.926, x2=ALIGN_GUIDE_X, y2=0.998,
              color=(120, 120, 140), alpha=0.9, layer=TEXT_LAYER)
    put("ANCHOR (left)", 0.932, x=ALIGN_GUIDE_X, font="smallfont",
        color="white", scale=1.0, align="left")
    put("ANCHOR (center)", 0.956, x=ALIGN_GUIDE_X, font="smallfont",
        color="white", scale=1.0, align="center")
    put("ANCHOR (right)", 0.980, x=ALIGN_GUIDE_X, font="smallfont",
        color="white", scale=1.0, align="right")

    # -- backdrop ------------------------------------------------------------

    # Dark translucent backdrop behind the entire wall, registered LAST on
    # purpose: layer=0 guarantees it stays behind every layer=1 text/line
    # regardless of registration order. color2 gives a subtle vertical
    # gradient, color (top) -> color2 (bottom).
    safe_draw(bd.draw_rect, id=BACKDROP_ID, x=0.015, y=0.01, w=0.97, h=0.98,
              color=(8, 8, 16), color2=(26, 26, 52), alpha=0.6,
              layer=BACKDROP_LAYER)


def rainbow_tick():
    """Repeating recolor: re-registering TITLE_ID animates it in place."""
    hue[0] = (hue[0] + 0.03) % 1.0
    r, g, b = colorsys.hsv_to_rgb(hue[0], 0.85, 1.0)
    rgb = (int(r * 255), int(g * 255), int(b * 255))
    safe_draw(bd.draw_text, TITLE_TEXT, id=TITLE_ID, x=0.03, y=0.025,
              font="bigfont", color=rgb, scale=TITLE_SCALE, shadow=True,
              layer=TEXT_LAYER)
    # measure_text returns PIXELS: recompute the title's exact width every
    # hue tick and resize the underline rect to match (dynamic layout).
    try:
        w_px, h_px = bd.measure_text(TITLE_TEXT, font="bigfont",
                                     scale=TITLE_SCALE)
    except (RuntimeError, ValueError):
        return True
    screen_w, screen_h = screen_pixels()
    safe_draw(bd.draw_rect, id=TITLE_UNDERLINE_ID,
              x=0.03, y=0.025 + (h_px + 2.0) / screen_h,
              w=w_px / screen_w, h=2.0 / screen_h,
              color=rgb, alpha=0.9, layer=TEXT_LAYER)
    return True  # keep repeating


@bd.on("map_load")
def map_loaded(event):
    hue[0] = 0.0

    def initial_draw():
        draw_wall()  # screen-space items persist; register them once
        rainbow_tick()
        return False

    # Delay so the wall registers in-level, not during the load itself.
    bd.schedule(initial_draw, delay=1)
    # Map-local tasks auto-cancel on unload and never duplicate.
    bd.schedule(rainbow_tick, delay=RAINBOW_TICS, repeat=RAINBOW_TICS,
                map_local=True)
