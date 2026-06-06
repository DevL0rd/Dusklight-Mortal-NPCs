#!/usr/bin/env bash
# Build this mod and package it as killable_npcs.dusk straight into the game's
# mods folder (so the running game hot-reloads it). Override paths with env vars:
#   DUSK_DIR   Dusklight source checkout (default ~/Github/dusklight)
#   MODS_DIR   the game's mods folder    (default the Dusklight data dir's mods/)
set -euo pipefail
cd "$(dirname "$0")"

DUSK_DIR="${DUSK_DIR:-$HOME/Github/dusklight}"
MODS_DIR="${MODS_DIR:-$HOME/.local/share/TwilitRealm/Dusklight/mods}"

mkdir -p "$MODS_DIR"
cmake -S . -B staging -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DDUSK_DIR="$DUSK_DIR" \
    -DDUSK_ENABLE_CODE_MODS=ON \
    -DDUSK_MODS_OUTPUT_DIR="$MODS_DIR"
cmake --build staging --target killable_npcs
echo "Packaged -> $MODS_DIR/killable_npcs.dusk"
