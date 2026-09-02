# Killstreak Announcer

This example keeps a kill-streak combo meter on `actor_died`. Only monster
deaths count, and only kills credited to a player: the engine points the dying
actor's `target` at its killer before the event fires, so a live player target
on `event["actor_ref"]` is the credit check.

Kills within `3 * bd.TICRATE` tics of each other chain a streak (timed with
`bd.level_time()`); the window expiring resets the counter first. Milestones
at 3/5/8/10 announce `N KILL STREAK!` through `bd.ui.announce` with a flavor
subtitle and escalating colors (gold, orange, red), play `misc/chat` via
`bd.play_ui_sound`, and add a `bd.screen_flash` from 8 onward. A small
`bd.ui` **STREAK** panel (top-left) tracks CURRENT and BEST. A map-local
repeating task every 5 tics resets the counter when the combo dies of old
age.
