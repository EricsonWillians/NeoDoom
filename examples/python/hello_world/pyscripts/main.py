"""BiasedDoom embedded-Python example and runtime smoke test."""

import os
import biaseddoom as bd


helper = bd.import_script("pyscripts/helper.py", module_name="hello_helper")
load_requested_once = False
pre_tick_calls = 0
post_tick_calls = 0
controlled_for_destroy = None
filtered_spawn_reported = False
budget_reported = False


@bd.on("pre_tick", priority=1000)
def over_budget_probe(event):
    """Autotest-only callback that proves repeat offenders are contained."""
    if (
        os.environ.get("BIASEDDOOM_PYTHON_AUTOTEST") != "1"
        or bd.state.get("budget_probe_ran")
    ):
        return
    bd.state["budget_probe_ran"] = True
    import time

    deadline = time.perf_counter() + 0.006
    while time.perf_counter() < deadline:
        pass


@bd.on("pre_tick", priority=-1000)
def verify_budget_guard(event):
    global budget_reported
    if (
        budget_reported
        or os.environ.get("BIASEDDOOM_PYTHON_AUTOTEST") != "1"
        or not bd.state.get("budget_probe_ran")
    ):
        return
    profile = bd.profile()
    offender = next(
        entry for entry in profile["callbacks"] if entry["priority"] == 1000
    )
    observer = next(
        entry for entry in profile["callbacks"] if entry["priority"] == -1000
    )
    budget_reported = True
    bd.log(
        "PYTEST budget_guard "
        f"skipped={observer['budget_skips'] > 0} "
        f"disabled={bool(offender['budget_disabled'])} "
        f"overruns={offender['budget_overruns']}"
    )
    bd.set_cvar("py_tick_budget_ms", 3)
    bd.set_cvar("py_tick_overrun_limit", 3)


@bd.on("pre_tick", every=2, priority=100)
def control_phase(event):
    """Runs before native player thinking, at half the engine tic rate."""
    global pre_tick_calls
    pre_tick_calls += 1


@bd.on("post_tick", every=2, priority=-100)
def observation_phase(event):
    """Runs after actors and world specials have completed their tic."""
    global post_tick_calls
    post_tick_calls += 1


def on_engine_start(event):
    bd.state.setdefault("maps_loaded", 0)
    bd.state.setdefault("spawn_events", 0)
    bd.state.setdefault("death_events", 0)
    bd.state.setdefault("tick_reported", False)
    bd.state.setdefault("autotest_ticks", 0)
    bd.state.setdefault("autotest_saved", False)
    bd.state.setdefault("autotest_loaded", False)
    bd.state.setdefault("budget_probe_ran", False)
    bd.log(f"PYTEST engine_start api={bd.API_VERSION} runtime={bd.RUNTIME}")
    print("PYTEST stdout_redirect")

    if os.environ.get("BIASEDDOOM_PYTHON_AUTOTEST") == "1":
        bd.set_cvar("py_tick_budget_ms", 1)
        bd.set_cvar("py_tick_overrun_limit", 1)

        def scheduled_probe():
            bd.log("PYTEST schedule_ran")

        task_id = bd.schedule(scheduled_probe, delay=1)
        bd.log(f"PYTEST schedule_queued={task_id > 0 and bd.task_count() == 1}")

        # Engine-facing calls from Python worker threads must be rejected
        # instead of racing the playsim. join() releases the GIL while the
        # short probe runs; only the main callback writes the result.
        import threading

        rejected = []

        def background_probe():
            try:
                bd.current_map()
            except RuntimeError:
                rejected.append(True)

        probe = threading.Thread(target=background_probe)
        probe.start()
        probe.join()
        bd.log(f"PYTEST thread_guard={rejected == [True]}")

        try:
            bd.import_script("pyscripts/autotest_failure.py", module_name="autotest_failure")
        except RuntimeError as error:
            bd.log(f"PYTEST rollback_exception={error}")


@bd.on("map_load")
def map_loaded(event):
    bd.state["maps_loaded"] += 1
    bd.state["tick_reported"] = False
    gravity = bd.get_cvar("sv_gravity")
    player_count = len(bd.players())
    actor_count = len(bd.actors(limit=100000))
    message = helper.greeting(event["map"])
    bd.log(
        "PYTEST map_load "
        f"map={event['map']} save={event['from_savegame']} "
        f"gravity={gravity} players={player_count} actors={actor_count} {message}"
    )
    if event["from_savegame"] and os.environ.get("BIASEDDOOM_PYTHON_SAVE_AUTOTEST") == "1":
        bd.log(
            "PYTEST autotest_complete "
            f"spawns={bd.state['spawn_events']} deaths={bd.state['death_events']}"
        )
        bd.execute("quit")


def on_tick(event):
    global load_requested_once, controlled_for_destroy

    if not bd.state["tick_reported"]:
        bd.state["tick_reported"] = True
        bd.log(f"PYTEST tick map={event['map']} tic={event['level_time']}")

    # The packaged example remains a normal playable mod unless the test
    # harness supplies this environment variable. In that mode it exercises
    # actor mutation and asks the engine to exit cleanly after several tics.
    if os.environ.get("BIASEDDOOM_PYTHON_AUTOTEST") != "1":
        return

    bd.state["autotest_ticks"] += 1
    if bd.state["autotest_ticks"] == 1:
        active_players = bd.players()
        player_actor = active_players[0]["actor"] if active_players else None
        live_player = bd.player(0)
        live_pawn = live_player.actor if live_player else None
        if player_actor is None or live_pawn is None:
            bd.log("PYTEST autotest no_player", level="error")
        else:
            spawned = bd.spawn_actor(
                "ZombieMan",
                player_actor["x"] + 128.0,
                player_actor["y"],
                player_actor["z"],
                tid=9901,
                force=True,
            )
            bd.set_actor_velocity(9901, 0.0, 0.0, 0.0)
            damage_done = bd.damage_actor(9901, 10000, damage_type="PythonTest")
            # Spawn/death events run synchronously. Reading the manifest after
            # both operations verifies that nested events restored this mod's
            # same-container VFS context.
            manifest_ok = "pyscripts/main.py" in bd.read_text("PYTHON")
            bd.log(
                "PYTEST mutation "
                f"tid={spawned['tid']} class={spawned['class_name']} "
                f"damage={damage_done} vfs_after_nested={manifest_ok}"
            )

            # API v2 uses native handles for real-time logic. These changes
            # happen synchronously in the playsim without dictionary rebuilds
            # or queued console commands.
            controlled = bd.spawn(
                "PythonBridgeProbe",
                live_pawn.x + 192.0,
                live_pawn.y,
                live_pawn.z,
                tid=9902,
                force=True,
            )
            controlled.health = 75
            zscript_health = controlled.call_zscript(
                "PythonMutate", 5, (4.0, 5.0, 6.0), live_pawn
            )
            zscript_velocity = controlled.call_zscript("PythonVelocity")
            zscript_target = controlled.call_zscript("PythonTarget")
            zscript_echo = controlled.call_zscript("PythonEcho", "bridge-ok")
            try:
                controlled.call_zscript("PythonSecret")
                private_rejected = False
            except PermissionError:
                private_rejected = True
            zscript_ok = (
                zscript_health == 80
                and zscript_velocity == (4.0, 5.0, 6.0)
                and zscript_target == live_pawn
                and zscript_echo == "bridge-ok"
                and private_rejected
            )
            controlled.set_flag("FRIENDLY", True)
            bd.apply_actor_batch(
                [
                    ("velocity", controlled, 1.0, 2.0, 0.0),
                    ("add_velocity", controlled, 2.0, -2.0, 1.0),
                ]
            )
            special_result = bd.execute_special(
                "Thing_Activate", [controlled.tid], activator=live_pawn
            )

            before_clip = live_pawn.inventory_count("Clip")
            after_give = live_pawn.give_inventory("Clip", 3)
            after_take = live_pawn.take_inventory("Clip", 2)

            sector = bd.sector(0)
            old_light = sector.light
            sector.light = max(0, old_light - 1)
            light_changed = sector.light != old_light or old_light == 0
            sector.light = old_light

            line = bd.line(0)
            line_ok = line is not None and len(line.args) == 5
            refs_ok = controlled in bd.actor_refs(tid=9902, limit=4)
            target_ok = controlled.target == live_pawn
            velocity_ok = controlled.velocity == (3.0, 0.0, 1.0)

            disposable = bd.spawn(
                "ZombieMan",
                live_pawn.x + 256.0,
                live_pawn.y,
                live_pawn.z,
                tid=9903,
                force=True,
            )
            disposable.destroy()
            invalidated = not disposable.valid

            bd.log(
                "PYTEST realtime "
                f"handle={controlled.valid} refs={refs_ok} target={target_ok} "
                f"velocity={velocity_ok} invalidated={invalidated} "
                f"inventory={before_clip},{after_give},{after_take} "
                f"sector={light_changed} line={line_ok} special={special_result}"
            )
            bd.log(f"PYTEST zscript_bridge={zscript_ok}")
            profile = bd.profile()
            profile_ok = any(
                entry["event"] == "pre_tick" and entry["calls"] > 0
                for entry in profile["callbacks"]
            )
            bd.log(
                "PYTEST performance "
                f"profile={profile_ok} budget={profile['tick_budget_ms']} "
                f"hard={profile['hard_budget']}"
            )
            # Keep this handle across tics. Destroying it on the next tic
            # proves that native references remain safe and that the engine's
            # normal WorldThingDestroyed event reaches Python.
            controlled_for_destroy = controlled

    if bd.state["autotest_ticks"] == 2 and controlled_for_destroy:
        controlled_for_destroy.destroy()

    if os.environ.get("BIASEDDOOM_PYTHON_SAVE_AUTOTEST") == "1":
        if bd.state["autotest_ticks"] == 3 and not bd.state["autotest_saved"]:
            # Set this before queueing the command so the flag is present in
            # the serialized JSON state written by the save callback.
            bd.state["autotest_saved"] = True
            bd.execute('save python-runtime-autotest "Python runtime test"')
            bd.log("PYTEST save_requested")
        elif bd.state["autotest_saved"] and not bd.state["autotest_loaded"]:
            expected_save = os.environ.get("BIASEDDOOM_PYTHON_SAVE_FILE")
            save_is_ready = not expected_save or os.path.isfile(expected_save)
            if bd.state["autotest_ticks"] >= 5 and save_is_ready and not load_requested_once:
                # This module global deliberately is not serialized. It keeps
                # the loaded tick-3 snapshot from requesting the load twice.
                load_requested_once = True
                bd.execute("load python-runtime-autotest")
                bd.log("PYTEST load_requested")

    ready_to_exit = (
        os.environ.get("BIASEDDOOM_PYTHON_SAVE_AUTOTEST") != "1"
        or bd.state["autotest_loaded"]
    )
    if ready_to_exit and bd.state["autotest_ticks"] >= 8:
        bd.log(
            "PYTEST autotest_complete "
            f"spawns={bd.state['spawn_events']} deaths={bd.state['death_events']}"
        )
        bd.execute("quit")


@bd.on("actor_spawned")
def actor_spawned(event):
    bd.state["spawn_events"] += 1
    if event["actor"]["tid"] == 9901:
        bd.log("PYTEST actor_spawned tid=9901")


@bd.on("actor_spawned", class_name="ZombieMan", priority=50)
def filtered_actor_spawned(event):
    global filtered_spawn_reported
    if (
        os.environ.get("BIASEDDOOM_PYTHON_AUTOTEST") == "1"
        and not filtered_spawn_reported
    ):
        filtered_spawn_reported = True
        bd.log(f"PYTEST filtered_spawn handle={event['actor_ref'].valid}")


@bd.on("actor_damaged", class_name="ZombieMan", tid=9901)
def filtered_actor_damaged(event):
    bd.log(
        f"PYTEST actor_damaged tid={event['actor_ref'].tid} damage={event['damage']}"
    )


@bd.on("actor_destroyed")
def filtered_actor_destroyed(event):
    if event["actor"]["tid"] == 9902:
        bd.log(f"PYTEST actor_destroyed tid={event['actor_ref'].tid}")


def on_actor_died(event):
    bd.state["death_events"] += 1
    if event["actor"]["tid"] == 9901:
        bd.log("PYTEST actor_died tid=9901")


def on_save(event):
    bd.state["last_saved_map"] = event["map"]
    bd.log(f"PYTEST save state={dict(bd.state)}")


def on_load(event):
    bd.state["autotest_loaded"] = True
    bd.log(f"PYTEST load restored_state={dict(bd.state)}")


def on_map_unload(event):
    bd.log(f"PYTEST map_unload next={event['next_map']}")


def on_engine_shutdown(event):
    bd.log(
        "PYTEST engine_shutdown "
        f"pre_ticks={pre_tick_calls} post_ticks={post_tick_calls} "
        f"state={dict(bd.state)}"
    )
