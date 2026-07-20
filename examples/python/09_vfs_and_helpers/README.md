# VFS Data And Helpers

At module load this example:

1. reads `pyscripts/settings.json` from its own PK3 with `bd.read_text`;
2. parses it with the bundled `json` standard-library module;
3. loads `pyscripts/helper.py` through `bd.import_script`;
4. uses both from immediate and repeating actor reports.

PK3 Python directories are not mounted on `sys.path`; `import_script` preserves
same-container ownership even when different mods contain identical paths.
