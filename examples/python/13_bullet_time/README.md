# Bullet Time

This interactive example implements hold-to-slow-mo on the **User1** button.
While held, `bd.set_timescale(0.35)` slows the world, a cool blue
`bd.screen_fade` tints the view, and a persistent `bd.hud_text` banner (fixed
id 100) shows `BULLET TIME`. Releasing restores timescale 1.0, pulses the tint
back, plays a different `bd.play_ui_sound`, and clears the banner with
`bd.hud_clear`.

Button edges are detected in `pre_tick` like the player-controller example.
`hud_text` raises `RuntimeError` before a level's status bar exists, so the
helper guards it; `map_unload` restores the timescale defensively because the
`i_timescale` CVar survives map changes.
