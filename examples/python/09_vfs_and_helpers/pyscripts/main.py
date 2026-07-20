"""Read JSON and import a helper module from the current PK3 container."""

import json

import biaseddoom as bd


SETTINGS = json.loads(bd.read_text("pyscripts/settings.json"))
helper = bd.import_script("pyscripts/helper.py", module_name="vfs_example_helper")


def report():
    actors = bd.actor_refs(class_name=SETTINGS["monster_class"], limit=4096)
    bd.log(f"vfs: {helper.actor_summary(actors)}")
    return True


@bd.on("map_load")
def map_loaded(event):
    bd.center_message(SETTINGS["message"])
    report()
    bd.schedule(
        report,
        delay=SETTINGS["report_every_seconds"] * bd.TICRATE,
        repeat=SETTINGS["report_every_seconds"] * bd.TICRATE,
    )
