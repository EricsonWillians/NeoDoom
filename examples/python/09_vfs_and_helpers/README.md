# VFS Data And Helpers

Read JSON config and import a Python helper from the mod's own PK3 with
`bd.read_text` and `bd.import_script`, then show both on the in-game HUD.

PK3 Python directories are not mounted on `sys.path`; `import_script` executes
a sibling file from the same container and returns it as a real module object,
preserving same-container ownership even when other mods contain identical
paths.

## Try it

Load any map. A centered `bd.ui` announcement shows the greeting returned by
`helper.greeting()` with the parsed `settings.json` values (monster class,
report interval) as its subtitle — with a switch sound confirming the import
succeeded. Every few seconds a green `VFS REPORT` toast shows the helper's
live `actor_summary` of the configured monster class.

## What it demonstrates

- `bd.read_text` loading UTF-8 data from the current container at module load.
- Parsing it with the bundled `json` standard-library module.
- `bd.import_script` returning a full module: main calls two different helper
  functions (`greeting`, `actor_summary`).
- Driving both immediate and repeating reports from the loaded config values.

## Notes

- Both VFS reads run once at module load, not per callback — static data
  should be loaded and parsed a single time.
- `bd.ui.announce`/`bd.ui.toast` self-guard their draws, so no status bar has
  to be active yet.
