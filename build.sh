#!/usr/bin/env bash
# Build this mod, then deploy it into the game's mods/ folder.
#
# Paths come from ./config.sh (DUSKLIGHT_DIR, DUSKLIGHT_BUILD_DIR, FUNCHOOK_DIR,
# DEPLOY_DIR); override any of them in your environment.
set -euo pipefail
cd "$(dirname "$0")"
source ./config.sh

# CMake's scratch/compile tree lives in staging/; the finished, ready-to-copy
# mod folder is written to build/<mod-id>/.
cmake -B staging -DCMAKE_BUILD_TYPE=Release \
    -DDUSKLIGHT_DIR="$DUSKLIGHT_DIR" \
    -DDUSKLIGHT_BUILD_DIR="$DUSKLIGHT_BUILD_DIR" \
    -DFUNCHOOK_DIR="$FUNCHOOK_DIR"
cmake --build staging

# Auto-deploy into the game's mods/ folder.
./deploy.sh
