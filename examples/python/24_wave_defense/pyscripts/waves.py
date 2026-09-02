"""The wave state machine: start/clear/breather, mutator rolls, boss waves
(one boss, twins from wave 20), milestone bonuses, and best-wave tracking.
"""

import math

import biaseddoom as bd

import horde_affixes as affixes
import horde_config as config
import horde_loot as loot
import horde_monsters as monsters
import horde_runstate as runstate
import horde_visuals as visuals


def roll_mutators():
    """Wave-scoped rules twists, rolled on an entropy-mixed stream: one
    roll from wave 2, stacking to two from wave 20 and three from wave
    30. Distinct mutators only."""
    if runstate.wave < 2:
        return ()
    rolls = 1 + sum(runstate.wave >= w for w in config.MUTATORS_STACK_FROM)
    stream = runstate.fresh_stream(900 + runstate.wave)
    picks = []
    for _ in range(rolls):
        if stream.float() < config.MUTATOR_CHANCE:
            mut = stream.choice(config.MUTATORS)
            if mut not in picks:
                picks.append(mut)
    return tuple(picks)


def start_wave():
    if runstate.phase == "fight":
        return False  # duplicate schedule; a wave is already running
    body = runstate.player_body()
    if body is None:
        return False  # smoke harness: no player, no waves
    runstate.wave += 1
    runstate.fit_failures = 0
    runstate.last_positions = {}
    runstate.uniques_pending = (
        0 if runstate.wave < config.UNIQUE_FROM_WAVE
        else 2 if runstate.wave >= config.UNIQUE_TWO_FROM_WAVE else 1)
    stream = bd.rng(runstate.wave * 31337 + 7)
    # Deterministic per-wave shuffle of the commandeered spawn pool.
    order = [i for i, point in enumerate(runstate.spawn_points)
             if point is not None]
    for i in range(len(order) - 1, 0, -1):
        j = stream.int(0, i)
        order[i], order[j] = order[j], order[i]
    runstate.spawn_order = order
    runstate.spawn_cursor = 0
    runstate.mutators = roll_mutators()
    quota_mult = 1.0
    for mut in runstate.mutators:
        quota_mult *= mut["quota"]
    mutator_names = " + ".join(mut["name"] for mut in runstate.mutators)
    if runstate.wave % config.BOSS_EVERY == 0:
        start_boss_wave(stream)
    else:
        runstate.quota_left = min(
            config.QUOTA_MAX,
            int(math.ceil((4 + runstate.wave * 2) * quota_mult)))
        runstate.phase = "fight"
        bd.ui.announce(f"WAVE {runstate.wave}",
                       subtitle=("if it's tinted, it counts"
                                 if runstate.wave == 1
                                 else mutator_names if mutator_names
                                 else "they keep coming"),
                       color=bd.ui.theme.bad, duration=2.0)
    if mutator_names and runstate.wave % config.BOSS_EVERY == 0:
        bd.ui.toast(f"MUTATOR: {mutator_names}", color=bd.ui.theme.warn,
                    duration=2.0)
    if runstate.wave % 10 == 0:
        bd.ui.toast(f"MILESTONE WAVE {runstate.wave}: DOUBLE CLEAR BONUS",
                    color=bd.ui.theme.gold, duration=2.5)
    threats = config.NEW_THREATS.get(runstate.wave)
    if threats and runstate.wave > 1:
        bd.ui.toast(f"NEW THREATS: {threats}", color=bd.ui.theme.warn,
                    duration=2.5)
    maybe_best_wave()
    visuals.render_hud()
    bd.log(f"horde: wave {runstate.wave} (quota {runstate.quota_left}"
           f"{', mutators ' + mutator_names if mutator_names else ''})")
    return False  # one-shot


def spawn_boss(body, stream, rank, ids):
    """Spawn one boss at its entrance point; returns its name or None.
    Boss health follows the same curve language as everything else:
    (1.2 + 0.5 x boss index) x the wave health multiplier."""
    point = monsters.boss_spawn_point(body, rank)
    if point is not None:
        x, y, z, angle = point
        force = False
    else:
        x, y, z, angle = monsters.ring_position(
            body, stream, config.SPAWN_MIN_RADIUS + 100.0,
            config.SPAWN_MAX_RADIUS + 150.0)
        force = True
    boss_class = config.BOSS_POOL[(runstate.boss_index() - 1 + rank)
                                  % len(config.BOSS_POOL)]
    ref = runstate.safe_call(bd.spawn, boss_class, x, y, z, angle=angle,
                             force=force)
    if ref is None or not ref.valid:
        return None
    health = int(ref.health
                 * (config.BOSS_HEALTH_BASE
                    + config.BOSS_HEALTH_PER_INDEX * runstate.boss_index())
                 * affixes.wave_health_mult())
    runstate.safe_call(bd.apply_actor_batch, [
        ("health", ref, health),
        ("scale", ref, 1.4),
        ("damage_multiply", ref, ref.damage_multiply * 1.5
         * affixes.wave_dealt_mult()),
        ("tint", ref, config.BOSS_TINT[0], config.BOSS_TINT[1],
         config.BOSS_TINT[2]),
    ])
    runstate.bosses.add(ref)
    runstate.defenders.add(ref)
    epithet = config.BOSS_EPITHETS[(runstate.boss_index() - 1 + rank)
                                   % len(config.BOSS_EPITHETS)]
    boss_name = f"{affixes.prettify(boss_class)} {epithet}"
    runstate.defender_info[ref] = {
        "tier": 3, "prefix": None, "prefix2": None, "suffix": None,
        "mods": {}, "score": 1, "boss": True, "max_health": health,
        "title": boss_name.upper(),
        "bar": (ids[2], 3.0, 0.09, 0.01),
        "name": (ids[1], 0.024, "bigfont")}
    visuals.update_overhead(ref)
    return boss_name


def start_boss_wave(stream):
    """Escort wave with one boss — two from boss index 4 (wave 20). Bosses
    don't consume quota; they simply must die before the wave clears."""
    runstate.phase = "fight"
    escorts = min(6, 3 + runstate.boss_index())
    runstate.quota_left = escorts
    body = runstate.player_body()
    count = 2 if runstate.boss_index() >= config.TWIN_BOSSES_FROM else 1
    names = []
    for rank in range(count):
        name = spawn_boss(body, stream, rank, config.BOSS_ID_SETS[rank])
        if name is not None:
            names.append(name)
    if not names:
        # No room even for a boss: convert to a plain big wave.
        runstate.quota_left = min(config.QUOTA_MAX, 4 + runstate.wave * 2)
        bd.log(f"horde: boss spawn failed, wave {runstate.wave}"
               " stays a trash wave")
        return
    bd.ui.announce(f"BOSS WAVE {runstate.wave}",
                   subtitle=(f"{' and '.join(names)} approach"
                             if len(names) == 2
                             else f"{names[0]} approaches"),
                   color=config.BOSS_TINT, duration=2.5)
    bd.play_ui_sound("misc/secret")


def maybe_best_wave():
    if runstate.wave > runstate.best_wave():
        bd.state["hd_best_wave"] = runstate.wave
        if runstate.wave > 1:
            bd.ui.toast(f"NEW BEST WAVE: {runstate.wave}",
                        color=bd.ui.theme.gold, duration=2.0)


def wave_cleared():
    """Quota exhausted and every defender dead: pay out, breathe, repeat."""
    if runstate.phase != "fight":
        return  # guard against a double clear (died + destroyed races)
    runstate.phase = "breather"
    runstate.breather_left = config.BREATHER_SECONDS
    runstate.bosses.clear()
    visuals.update_tracker()  # clears the tracker on the final kills
    milestone = runstate.wave % 10 == 0
    gained = config.SCORE_PER_CLEAR * runstate.wave * (2 if milestone else 1)
    runstate.add_score(gained)
    runstate.save_best_score()
    visuals.popup(gained)
    runstate.safe_call(bd.draw_text, "WAVE CLEAR", id=config.CLEAR_TEXT,
                       x=0.5, y=0.35, font="bigfont", height=0.03,
                       color=bd.ui.theme.good,
                       outline=True, align="center", duration=1.2)
    bd.play_ui_sound("misc/secret")
    bd.log(f"horde: wave {runstate.wave} cleared (+{gained})")
    loot.drop_supplies()
    bd.schedule(breather_tick, delay=bd.TICRATE, repeat=bd.TICRATE)


def breather_tick():
    """Countdown between waves; True keeps the repeating task alive."""
    if runstate.phase != "breather":
        return False  # stale task from a cleared/dead attempt: cancel
    runstate.breather_left -= 1
    if runstate.breather_left <= 0:
        bd.schedule(start_wave, delay=1)
        return False
    visuals.hud_cache = (None, None)  # force the countdown line to refresh
    visuals.render_hud()
    return True
