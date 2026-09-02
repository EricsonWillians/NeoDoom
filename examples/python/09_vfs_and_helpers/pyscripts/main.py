"""Read JSON config and import a helper module from this PK3 through the VFS.

Try it: load a map — the HUD shows the config values and the helper's greeting.
"""

import json

import biaseddoom as bd


# Both lines run at module load, straight out of this mod's own container.
SETTINGS = json.loads(bd.read_text("pyscripts/settings.json"))
helper = bd.import_script("pyscripts/helper.py", module_name="vfs_example_helper")


def report():
    """Log and display how many configured monsters are alive right now."""
    actors = bd.actor_refs(class_name=SETTINGS["monster_class"], limit=4096)
    summary = helper.actor_summary(actors)
    bd.log(f"vfs: {summary}")
    bd.ui.toast(f"VFS REPORT: {summary}", color=bd.ui.theme.good,
                duration=3.0, y=0.60)
    return True


@bd.on("map_load")
def map_loaded(event):
    # Prove the VFS reads worked: config values and the helper module's
    # greeting, both pulled from this PK3, in one centered announcement
    # (toasts share a single display-list id, so two banners would collide).
    bd.ui.announce(
        helper.greeting(),
        subtitle=f"settings.json -> monster={SETTINGS['monster_class']} "
                 f"report_every={SETTINGS['report_every_seconds']}s",
        color=bd.ui.theme.gold,
        duration=6.0,
    )
    bd.center_message(SETTINGS["message"])
    report()
    period = SETTINGS["report_every_seconds"] * bd.TICRATE
    bd.schedule(report, delay=period, repeat=period)
