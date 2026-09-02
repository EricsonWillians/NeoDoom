"""Run HUD: slim always-on strip, toggleable stats panel, popups, toasts.

Presentation layer (config, runstate, mutators). The slim strip and the
panel rows re-render only when their content changes; a score increase
flashes the SCORE line gold. All screen text uses bd.draw_text's
resolution-independent height= (a normalized screen-height fraction), so
the HUD reads the same at any resolution.

Also hosts the non-combat scoring events (secret_found, item_picked):
they are pure "feedback" events — mutate run state, then update the HUD.
"""

import biaseddoom as bd

import rogue_config as config
import rogue_runstate as runstate
import rogue_mutators as mutators


run_panel = None       # bd.ui panel; only exists while the stats view is open
row_cache = {}         # panel row label -> last rendered (value, color)
stats_visible = False  # USER4 toggles the full stats panel; slim HUD otherwise
prev_stats_key = False
slim_cache = {}        # slim HUD slot -> last rendered (text, color)
last_score = None      # score-increase detection drives the gold flash
popup_counter = 0


def safe_draw(func, *args, **kwargs):
    # Draw calls raise RuntimeError while the world mutates (no HUD before
    # level start, map unload) and ValueError for unknown fonts/textures.
    try:
        func(*args, **kwargs)
    except (RuntimeError, ValueError):
        pass


# --- slim strip ------------------------------------------------------------

def render_slim():
    """The always-on HUD: big score line + dim context line + toggle hint."""
    global last_score
    theme = bd.ui.theme
    current = runstate.score()
    flash = last_score is not None and current > last_score
    last_score = current
    if flash:
        # Decay the gold flash back to the normal color shortly after.
        def unflash():
            slim_cache.pop(config.SLIM_SCORE, None)
            render_slim()  # last_score == current now: normal color
            return False  # one-shot
        try:
            bd.schedule(unflash, delay=int(0.6 * bd.TICRATE))
        except RuntimeError:
            pass
    score_text = (f"LV {runstate.level()}  SCORE {current}  "
                  f"x{runstate.multiplier():.1f}")
    muts = " + ".join(mutators.MUTATORS[key]["title"]
                      for key in mutators.active) or "-"
    sub_text = (f"DEPTH {runstate.depth()}  \u00b7  {muts}  \u00b7  "
                f"SEED {runstate.seed()}")
    entries = ((config.SLIM_SCORE, score_text, 0.026,
                theme.gold if flash else theme.text, 0.012),
               (config.SLIM_SUB, sub_text, 0.015, theme.dim, 0.046),
               (config.SLIM_HINT, "USER4: RUN STATS", 0.012, theme.dim, 0.068))
    for slot, text, height, color, y in entries:
        key = (text, color)
        if slim_cache.get(slot) == key:
            continue
        slim_cache[slot] = key
        safe_draw(bd.draw_text, text, id=slot, x=0.012, y=y, height=height,
                  outline=True, color=color)


# --- full stats panel (USER4 toggles) ---------------------------------------

def render_row(label, value, color=None):
    """Update one stats-panel row, but only when its content actually changed
    (panel.row() re-renders the whole panel, so unchanged rows are skipped)."""
    if run_panel is None:
        return
    key = (value, color)
    if row_cache.get(label) == key:
        return
    row_cache[label] = key
    run_panel.row(label, value, value_color=color)


def render_bar(label, frac, fg=None):
    if run_panel is None:
        return
    key = (round(frac, 3), fg)
    if row_cache.get(label) == key:
        return
    row_cache[label] = key
    run_panel.bar(label, frac, fg=fg)


def update(force=False):
    """Refresh the HUD: slim strip always; framed panel only while toggled on.

    The force re-render happens before the visibility early-return so the
    slim strip also repaints on map load while the panel is hidden.
    """
    global run_panel, last_score
    if force:
        row_cache.clear()
        slim_cache.clear()
        last_score = None
    render_slim()
    if not stats_visible:
        return
    if run_panel is None:
        run_panel = bd.ui.panel(x=0.985, y=0.02, w=0.27, title="RUN",
                                anchor="tr")
    theme = bd.ui.theme
    render_row("SCORE", f"{runstate.score()}  x{runstate.multiplier():.1f}")
    render_bar("XP", min(1.0, bd.state.get("xp", 0) / runstate.xp_target()),
               fg=theme.accent)
    render_row("LEVEL", str(runstate.level()))
    render_row("DEPTH", f"{runstate.depth()}   SEED {runstate.seed()}")
    for slot in (0, 1):
        label = f"MUT {slot + 1}"
        if slot < len(mutators.active):
            entry = mutators.MUTATORS[mutators.active[slot]]
            render_row(label, entry["title"], entry["color"])
        else:
            render_row(label, "..." if slot == 0 else "-", theme.dim)
    render_row("TALLY", f"K {bd.state.get('kills', 0)}  "
                        f"S {bd.state.get('secrets', 0)}  "
                        f"U {bd.state.get('uniques', 0)}")


@bd.on("pre_tick")
def stats_toggle_watch(event):
    """USER4 edge-toggles the full stats panel (the slim HUD stays)."""
    global stats_visible, prev_stats_key, run_panel
    try:
        player = bd.player(0)
        pressed = bool(player and player.valid and
                       (player.buttons & bd.BT_USER4))
    except RuntimeError:
        return  # world mutating (map load/unload)
    if pressed and not prev_stats_key:
        stats_visible = not stats_visible
        bd.play_ui_sound("switches/normbutn", volume=0.6)
        if stats_visible:
            update(force=True)
        elif run_panel is not None:
            run_panel.close()
            run_panel = None
            row_cache.clear()
    prev_stats_key = pressed


# --- popups / toasts ------------------------------------------------------------

def popup(points, suffix="", color=None):
    """Floating '+N' near the crosshair (world items hide on dead actors,
    so kill/pickup popups are screen-space with a small jitter instead)."""
    global popup_counter
    popup_counter += 1
    jitter = bd.rng(runstate.seed() + popup_counter * 7919)
    x = 0.5 + (jitter.float() - 0.5) * 0.18
    y = 0.40 + (jitter.float() - 0.5) * 0.12
    theme = bd.ui.theme
    safe_draw(bd.draw_text, f"+{int(points)}{suffix}",
              id=config.POPUP_BASE + popup_counter % config.POPUP_SLOTS,
              x=x, y=y, font="smallfont",
              color=color or theme.gold, height=0.018, outline=True,
              align="center", duration=1.2)


# --- non-combat scoring events -----------------------------------------------------

@bd.on("secret_found")
def secret_found(event):
    bd.state["secrets"] = bd.state.get("secrets", 0) + 1
    gained = int(config.SCORE_PER_SECRET * runstate.multiplier())
    runstate.add_score(gained)
    popup(gained)
    bd.play_ui_sound("misc/secret")
    update()


@bd.on("item_picked")
def item_picked(event):
    if not any(keyword in event["class_name"].lower()
               for keyword in config.RARE_KEYWORDS):
        return
    points = config.SCORE_PER_RARE * (
        2 if "rich_pickup" in mutators.active else 1)
    gained = int(points * runstate.multiplier())
    runstate.add_score(gained)
    popup(gained)
    bd.log(f"run: rare pickup {event['name']} +{gained}")
    update()
