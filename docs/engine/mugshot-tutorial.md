# Mugshot Customization Tutorial

BiasedDoom 4.15.4 lets players resize and reposition the status-bar portrait, and lets SBARINFO authors constrain high-resolution face graphics to a known layout box.

## For Players

Open:

`Options -> HUD Options -> Mugshot options`

The menu contains:

- **Mugshot scale:** 0.25x to 4.00x in 0.05 steps.
- **Horizontal offset:** -160 to 160 virtual pixels.
- **Vertical offset:** -100 to 100 virtual pixels.
- **Reset mugshot:** restores scale 1.00 and both offsets to zero.

The portrait scales around its horizontal center and bottom edge. Increase the scale first, then use the offsets for final alignment. These settings are archived and apply to both the stock Doom HUD and legacy SBARINFO status bars.

The same controls are available from the console:

```text
hud_mugshot_scale 1.5
hud_mugshot_xoffset 4
hud_mugshot_yoffset -3
```

To restore the defaults:

```text
resetcvar hud_mugshot_scale
resetcvar hud_mugshot_xoffset
resetcvar hud_mugshot_yoffset
```

## For SBARINFO Authors

Use the `drawmugshot` command. The final width and height are optional:

```c
drawmugshot ["<default face>"], <accuracy>, [<flags>,] <x>, <y> [, <width>, <height>];
```

The classic Doom form is unchanged:

```c
statusbar Normal
{
    drawmugshot "STF", 5, 143, 168;
}
```

To fit a high-resolution face pack into the classic 24×29 slot:

```c
statusbar Normal
{
    drawmugshot "STF", 5, 143, 168, 24, 29;
}
```

BiasedDoom first resolves the base size from the command, a matching mugshot-state override, or the texture's display dimensions. It then applies the player's scale and offsets. This means the author controls the intended HUD box while the player retains a final accessibility/customization transform.

### Optional flags

Inherited `drawmugshot` flags remain identifiers, not quoted strings. Multiple flags are separated with `|`:

```c
drawmugshot "STF", 5, disablegrin | disableouch, 143, 168, 24, 29;
```

Supported flags include `xdeathface`, `animatedgodmode`, `disablegrin`, `disableouch`, `disablepain`, `disablerampage`, and `custom`.

## Troubleshooting

- **The face is still huge:** give `drawmugshot` an explicit base width/height or reduce `hud_mugshot_scale`.
- **The face grows below the bar:** reset the vertical offset. Scaling itself is bottom-anchored.
- **Only one axis looks wrong:** width and height fall back independently, so verify that both target values are positive.
- **A mod ignores the target on `drawmugshot`:** check whether it defines a matching mugshot-state override with its own positive dimensions and coordinates.
- **You want untouched upstream behavior:** reset all three CVars and omit the optional width/height.

For implementation details, see [Mugshot Scaling And Positioning](mugshot-scaling.md).
