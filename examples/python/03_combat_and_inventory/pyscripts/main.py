"""Interactive native combat, missile, inventory, healing, and sound example."""

import math

import biaseddoom as bd


previous_buttons = 0
practice_target = None


def in_front(actor, distance):
    radians = math.radians(actor.angle)
    return (
        actor.x + math.cos(radians) * distance,
        actor.y + math.sin(radians) * distance,
        actor.z,
    )


@bd.on("pre_tick")
def combat_controls(event):
    global previous_buttons, practice_target
    player = bd.player()
    pawn = player.actor if player else None
    if pawn is None:
        return

    buttons = player.buttons
    pressed = buttons & ~previous_buttons
    previous_buttons = buttons

    if pressed & bd.BT_USER1:
        before = pawn.inventory_count("Clip")
        after = pawn.give_inventory("Clip", 10)
        pawn.heal(10, maximum=100)
        pawn.play_sound("weapons/pistol", volume=0.5)
        bd.center_message(f"Clip ammo: {before} -> {after}")

    if pressed & bd.BT_USER2:
        practice_target = bd.spawn("ZombieMan", *in_front(pawn, 256), force=True)
        practice_target.target = pawn
        hit = bd.line_attack(pawn, damage=20, damage_type="PythonExample")
        missile = bd.spawn_missile(
            pawn, practice_target, "Rocket", owner=pawn, check=False
        )
        bd.log(
            f"combat: hitscan target={hit['target']} damage={hit['damage']} "
            f"missile={missile}"
        )

    if pressed & bd.BT_USER3 and practice_target and practice_target.valid:
        dealt = practice_target.damage(15, damage_type="PythonExample", source=pawn)
        bd.radius_damage(
            practice_target,
            damage=32,
            distance=96,
            source=pawn,
            hurt_source=False,
        )
        bd.log(f"combat: direct damage result={dealt}")


@bd.on("map_load")
def show_help(event):
    bd.center_message("User1: ammo/heal  User2: target/attack  User3: damage")
