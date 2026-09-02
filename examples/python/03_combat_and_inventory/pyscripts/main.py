"""Native combat and inventory abilities with on-screen cooldown bars.

Try it: USER1 supply, USER2 strike, USER3 nova (bind +user1/+user2/+user3).

Cooldowns are shown as readiness bars (0..1) on a themed bd.ui panel,
one bar per ability, filled in the ability's color when ready.
"""

import math

import biaseddoom as bd

# (button mask, display name, cooldown tics, ui sound, ready-bar color)
ABILITIES = (
    (bd.BT_USER1, "SUPPLY", 2 * bd.TICRATE, "misc/chat", (90, 200, 90)),
    (bd.BT_USER2, "STRIKE", 4 * bd.TICRATE, "misc/pistol", (230, 200, 60)),
    (bd.BT_USER3, "NOVA", 6 * bd.TICRATE, "misc/secret", (220, 90, 60)),
)

previous_buttons = 0
practice_target = None
cooldown_until = [0, 0, 0]  # level_time() at which each ability is ready again
ability_panel = None  # bd.ui panel, recreated per map
last_fracs = None  # per-ability last drawn readiness; None forces a redraw


def say(message, color="gold"):
    """Brief HUD feedback; reusing id=1 replaces the previous message."""
    try:
        bd.hud_text(message, id=1, x=0.5, y=0.75, color=color, hold=1.5, fade=0.5)
    except RuntimeError:
        pass  # no status bar -> no HUD messages


def in_front(actor, distance):
    radians = math.radians(actor.angle)
    return (
        actor.x + math.cos(radians) * distance,
        actor.y + math.sin(radians) * distance,
        actor.z,
    )


def draw_cooldown_bars(now, force=False):
    """One bd.ui bar row per ability: frac is readiness (0 = just fired,
    1 = ready), filled with the ability color.

    panel.bar() re-renders the whole panel, so rows only update when the
    displayed frac actually changes (rounded to 5% steps).
    """
    global ability_panel, last_fracs
    if ability_panel is None:
        ability_panel = bd.ui.panel(x=0.02, y=0.68, w=0.30,
                                    title="ABILITIES", anchor="bl")
        last_fracs = None
    if last_fracs is None:
        last_fracs = [None] * len(ABILITIES)
    for i, (button, name, cooldown, _, ready_color) in enumerate(ABILITIES):
        remaining = cooldown_until[i] - now
        frac = 1.0 if remaining <= 0 else 1.0 - remaining / cooldown
        frac = min(max(frac, 0.0), 1.0)
        shown = round(frac * 20) / 20  # 5% steps: redraw only on real change
        if force or shown != last_fracs[i]:
            last_fracs[i] = shown
            key = {bd.BT_USER1: "USER1", bd.BT_USER2: "USER2",
                   bd.BT_USER3: "USER3", bd.BT_USER4: "USER4"}.get(button, "?")
            ability_panel.bar(f"{key} {name}", shown, fg=ready_color)


# --- the three abilities ----------------------------------------------------

def supply(pawn):
    """USER1: native inventory give/count plus healing through the pawn."""
    before = pawn.inventory_count("Clip")
    pawn.give_inventory("Clip", 10)
    after = pawn.inventory_count("Clip")
    pawn.heal(10, maximum=100)
    bd.play_ui_sound("misc/chat")
    say(f"SUPPLY: clips {before} -> {after}, +10 health", color="green")


def strike(pawn):
    """USER2: spawn a practice target, then native hitscan + aimed missile."""
    global practice_target
    practice_target = bd.spawn("ZombieMan", *in_front(pawn, 256), force=True)
    practice_target.target = pawn
    hit = bd.line_attack(pawn, damage=20, damage_type="PythonExample")
    bd.spawn_missile(pawn, practice_target, "Rocket", owner=pawn, check=False)
    bd.play_ui_sound("misc/pistol")
    say(f"STRIKE: hitscan {hit['damage']} dmg + rocket inbound", color="yellow")
    bd.log(f"combat: hitscan target={hit['target']} damage={hit['damage']}")


def nova(pawn):
    """USER3: direct damage plus a radius blast centered on the target."""
    if practice_target is None or not practice_target.valid:
        say("NOVA needs a target - press USER2 first", color="orange")
        bd.play_ui_sound("switches/normbutn")
        return False  # refused: do not consume the cooldown
    dealt = practice_target.damage(15, damage_type="PythonExample", source=pawn)
    bd.radius_damage(practice_target, damage=32, distance=96,
                     source=pawn, hurt_source=False)
    bd.play_ui_sound("misc/secret")
    bd.screen_flash(255, 180, 80, 0.35)  # one-frame impact flash
    say(f"NOVA: {dealt} direct + 32 blast damage", color="red")


ABILITY_HANDLERS = (supply, strike, nova)


@bd.on("pre_tick")
def combat_controls(event):
    global previous_buttons
    player = bd.player()
    pawn = player.actor if player else None
    if pawn is None:
        return

    now = bd.level_time()
    draw_cooldown_bars(now)  # internally skips unchanged bars

    buttons = player.buttons
    pressed = buttons & ~previous_buttons
    previous_buttons = buttons

    for i, (button, name, cooldown, _, _) in enumerate(ABILITIES):
        if not (pressed & button):
            continue
        if now < cooldown_until[i]:
            left = (cooldown_until[i] - now + bd.TICRATE - 1) // bd.TICRATE
            bd.ui.toast(f"{name} recharging ({left}s)",
                        color=bd.ui.theme.warn, y=0.62)
            bd.play_ui_sound("switches/normbutn")
        elif ABILITY_HANDLERS[i](pawn) is not False:
            cooldown_until[i] = now + cooldown
            draw_cooldown_bars(now)


@bd.on("map_load")
def show_help(event):
    global previous_buttons, ability_panel, last_fracs
    previous_buttons = 0
    cooldown_until[:] = [0, 0, 0]
    if ability_panel is not None:
        ability_panel.close()  # drop the previous map's panel rows
        ability_panel = None
    last_fracs = None
    # The panel is (re)created lazily on the first pre_tick, in-level, where
    # the HUD is guaranteed to exist (draws during map_load can be dropped).
    try:
        bd.hud_text("USER1: supply | USER2: strike | USER3: nova",
                    id=9, x=0.5, y=0.12, color="gold", hold=4.0, fade=1.0)
    except RuntimeError:
        pass
