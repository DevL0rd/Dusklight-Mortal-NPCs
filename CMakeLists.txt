cmake_minimum_required(VERSION 3.25)
project(killable_npcs CXX)

# A Dusklight code mod. It uses the Dusklight mod SDK: include the game source
# tree (for game headers + the add_dusk_mod() packaging helper), then declare the
# mod. The result is a single `killable_npcs.dusk` package the game can load.
# Override DUSK_DIR if the Dusklight checkout lives elsewhere.
set(DUSK_DIR "$ENV{HOME}/Github/dusklight" CACHE PATH "Path to the Dusklight source root")
add_subdirectory("${DUSK_DIR}" dusk EXCLUDE_FROM_ALL)

add_dusk_mod(killable_npcs
    SOURCES  src/killable_npcs.cpp
    MOD_JSON src/mod.json
)
