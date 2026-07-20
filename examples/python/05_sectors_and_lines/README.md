# Sectors And Lines

The first sector and line are inspected on map load. Bind User buttons for:

- **User2** — temporarily pulse sector light;
- **User3** — move the floor eight map units through scheduled native steps;
- **User4** — execute the selected line's special with the player as activator.

Real mods should select sectors by tag and lines by line ID rather than array
index. This sample uses index zero so it works on ordinary unmodified maps.
