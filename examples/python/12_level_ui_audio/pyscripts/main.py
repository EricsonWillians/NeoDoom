"""Level/UI/audio operations behind a tiny on-screen menu panel.

Try it: USER1/USER2 move the highlight, USER3 runs the item, USER4 exits the level.

The menu is a themed bd.ui panel: the selected row shows a '<' marker in
the accent color, the rest are dimmed.
"""

import biaseddoom as bd

previous_buttons = 0
selection = 0
menu_panel = None  # bd.ui panel, recreated per map

MENU = (
    "Center message + sound",
    "Change music (D_RUNNIN)",
    "Light special (tag 0 -> 160)",
    "Exit level",
)


def feedback(message, color="gold"):
    """Short HUD confirmation; reusing id=1 replaces the previous message."""
    try:
        bd.hud_text(message, id=1, x=0.5, y=0.66, color=color, hold=2.0, fade=0.5)
    except RuntimeError:
        pass  # no status bar -> no HUD messages


def draw_menu():
    """(Re)render the menu panel with the current row highlighted."""
    global menu_panel
    if menu_panel is None:
        menu_panel = bd.ui.panel(x=0.31, y=0.24, w=0.38, title="COMMANDS")
    theme = bd.ui.theme
    for i, label in enumerate(MENU):
        selected = i == selection
        # The label stays constant so the row updates in place; selection
        # shows as an accent-colored '<' marker on the value side.
        menu_panel.row(label, "<" if selected else "",
                       value_color=theme.accent if selected else theme.dim)


def close_menu():
    global menu_panel
    if menu_panel is not None:
        menu_panel.close()
        menu_panel = None


def run_selection(pawn):
    """USER3: execute the highlighted menu item."""
    bd.play_ui_sound("misc/pistol")  # confirm click
    if selection == 0:
        bd.center_message("Immediate Python center message", bold=True)
        pawn.play_sound("misc/chat", volume=0.75, local=True)
    elif selection == 1:
        # Immediate music switch; the lump name is Doom II specific.
        bd.set_music("D_RUNNIN", looping=True, force=True)
        feedback("Music changed to D_RUNNIN")
        bd.log("level: music changed to D_RUNNIN")
    elif selection == 2:
        try:
            bd.execute_special("Light_ChangeToValue", [0, 160], activator=pawn)
            feedback("Ran Light_ChangeToValue(tag 0, light 160)")
        except (ValueError, RuntimeError) as error:
            feedback("This map cannot run that special", color="orange")
            bd.log(f"level: sample special failed: {error}", level="warning")
    elif selection == 3:
        bd.exit_level(keep_facing=True)


@bd.on("map_load")
def show_menu(event):
    global previous_buttons
    previous_buttons = 0
    draw_menu()
    try:
        bd.hud_text("USER1/2: move | USER3: select | USER4: exit level",
                    id=9, x=0.5, y=0.12, color="gold", hold=4.0, fade=1.0)
    except RuntimeError:
        pass


@bd.on("map_unload")
def hide_menu(event):
    close_menu()


@bd.on("pre_tick")
def menu_controls(event):
    global previous_buttons, selection
    player = bd.player()
    pawn = player.actor if player else None
    if pawn is None:
        return
    pressed = player.buttons & ~previous_buttons
    previous_buttons = player.buttons

    if pressed & bd.BT_USER1:
        selection = (selection - 1) % len(MENU)
        bd.play_ui_sound("switches/normbutn")  # navigate click
        draw_menu()
    if pressed & bd.BT_USER2:
        selection = (selection + 1) % len(MENU)
        bd.play_ui_sound("switches/normbutn")
        draw_menu()
    if pressed & bd.BT_USER3:
        run_selection(pawn)
    if pressed & bd.BT_USER4:
        bd.exit_level(keep_facing=True)
