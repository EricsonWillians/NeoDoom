"""Player-centered effects: aura rings around the player's feet,
full-screen frame flashes and affix-colored damage feedback.

Rings use the same bd.draw_world_ring primitive as the monster auras,
anchored to the player body with a duration so they auto-expire (mostly a
third-person/multiplayer cue — first person barely sees its own floor),
while bd.draw_frame paints a colored screen frame for first-person
feedback. Re-registering the shared canvas ids refreshes effects on rapid
procs. Screen flashes use bd.screen_flash and stay subtle: vanilla Doom
already flashes red on any damage, so only AFFIXED attackers add their
color on top.
"""

import biaseddoom as bd

import rogue_config as config
import rogue_runstate as runstate
import rogue_affixes as affixes
import rogue_hud as hud


def ring(color, radius, duration, alpha=0.95):
    """Flash a colored ground ring around the player's feet (mostly a
    third-person/multiplayer cue; first person barely sees its own floor)."""
    body = runstate.player_body()
    if body is None:
        return
    hud.safe_draw(bd.draw_world_ring, body, id=config.PLAYER_RING,
                  radius=radius, color=color, alpha=alpha, offset_z=2.0,
                  segments=28, duration=duration)


def frame_flash(color, thickness, duration, alpha=0.85):
    """Full-screen colored frame: the first-person half of an effect."""
    hud.safe_draw(bd.draw_frame, id=config.PLAYER_FRAME, x=0.0, y=0.0,
                  w=1.0, h=1.0, color=color, thickness=thickness,
                  alpha=alpha, duration=duration)


def level_up_fx():
    """Gold aura ring + screen frame + flash on level-up (the announce
    itself is handled by the caller)."""
    ring(config.UNIQUE_COLOR, 24.0, 1.4)
    frame_flash(config.UNIQUE_COLOR, 6, 0.8)
    bd.screen_flash(255, 215, 90, 0.22)
    bd.play_ui_sound("misc/pkup", volume=0.8)


def heal_proc_fx():
    """Brief green pulse when the vampire mutator feeds a kill to you."""
    ring(bd.ui.theme.good, 16.0, 0.5, alpha=0.8)
    frame_flash(bd.ui.theme.good, 4, 0.4, alpha=0.6)


@bd.on("actor_damaged")
def player_damaged(event):
    """Flash the screen in the attacker's affix color when the player takes
    a hit from an affixed monster; plain monsters keep vanilla feedback."""
    victim = event["actor_ref"]
    if victim is None or not victim.valid or not victim.is_player:
        return
    source = event["source_ref"]
    if source is None or not source.valid:
        return
    record = affixes.read(source)
    if record is None:
        return
    color = (config.UNIQUE_COLOR if record["tier"] == 3
             else affixes.color(record))
    # Intensity scales with the hit, capped subtle.
    alpha = min(0.25, 0.08 + max(0, event["damage"]) * 0.008)
    bd.screen_flash(color[0], color[1], color[2], alpha)
