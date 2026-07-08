#!/usr/bin/env bash
# Copy the glTF GLTFModel mixin into a mod folder so the mod's ZSCRIPT.zs can use it.
# Usage: ./tools/copy_gltf_mixin_to_mod.sh /path/to/mod_dir
# If you have a .pk3 file, unzip it to a folder first (unzip -d BDPlayer_cube BDPlayer_cube.pk3)

set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 /path/to/mod_folder (unzipped PK3 or mod directory)"
  exit 2
fi

MODDIR="$1"
ROOT="$(dirname "$0")/.."
GLTF_SRC="$ROOT/wadsrc/static/zscript/models/gltf_model.zs"

if [ ! -f "$GLTF_SRC" ]; then
  echo "Error: could not find gltf_model.zs at $GLTF_SRC"
  exit 3
fi

# Target zscript directory inside mod
TARGET_ZSCRIPT_DIR="$MODDIR/zscript"
TARGET_ZSCRIPT_FILE="$MODDIR/ZSCRIPT.zs"

mkdir -p "$TARGET_ZSCRIPT_DIR"
cp -v "$GLTF_SRC" "$TARGET_ZSCRIPT_DIR/gltf_model.zs"

# The mixin calls prefixed Actor methods (GLTF_*). Those natives are declared by
# BiasedDoom's engine-side Actor class, so no local native declarations need to be
# patched into the copied mixin.

# Ensure ZSCRIPT.zs exists; if not, create a minimal one that includes the mixin first
if [ ! -f "$TARGET_ZSCRIPT_FILE" ]; then
  cat > "$TARGET_ZSCRIPT_FILE" <<'EOF'
version "4.15.1"
#include "zscript/gltf_model.zs"

// Include remaining zscript files
#include "zscript/BDPlayer.zs"
EOF
  echo "Created $TARGET_ZSCRIPT_FILE with gltf_model include."
else
  # If the file already includes gltf_model.zs, do nothing. Otherwise insert it after the version declaration.
  if grep -q "gltf_model.zs" "$TARGET_ZSCRIPT_FILE"; then
    echo "ZSCRIPT.zs already includes gltf_model.zs"
  else
    # Try to insert after the version "..." line, otherwise add at top
    if grep -q '^version ' "$TARGET_ZSCRIPT_FILE"; then
      awk 'BEGIN{inserted=0}
      /^version / && !inserted {print; print "#include \"zscript/gltf_model.zs\""; inserted=1; next}
      {print}
      END{if(!inserted) print "#include \"zscript/gltf_model.zs\""}' "$TARGET_ZSCRIPT_FILE" > "$TARGET_ZSCRIPT_FILE.tmp" && mv "$TARGET_ZSCRIPT_FILE.tmp" "$TARGET_ZSCRIPT_FILE"
      echo "Inserted gltf_model include into existing ZSCRIPT.zs"
    else
      # Prepend include
      (echo '#include "zscript/gltf_model.zs"'; cat "$TARGET_ZSCRIPT_FILE") > "$TARGET_ZSCRIPT_FILE.tmp" && mv "$TARGET_ZSCRIPT_FILE.tmp" "$TARGET_ZSCRIPT_FILE"
      echo "Prepended gltf_model include into existing ZSCRIPT.zs"
    fi
  fi
fi

echo "Done. Your mod folder ($MODDIR) now contains zscript/gltf_model.zs and ZSCRIPT.zs includes it."

echo "Next: rezip into a .pk3 or point the engine at the folder. Then run ./build.sh (or launch ./biaseddoom -file /path/to/your/mod) to test." 

exit 0
