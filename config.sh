# Shared configuration for building & deploying Dusklight mods.
# Sourced by build.sh / deploy.sh. Every value can be overridden by exporting it
# in your environment before running a script.

# Dusklight source tree (game headers + the mod SDK).
: "${DUSKLIGHT_DIR:=$HOME/Github/dusklight}"

# A configured Dusklight build directory (generated + fetched headers, e.g. Tracy).
# Must be built WITHOUT LTO so hooked functions stay addressable.
: "${DUSKLIGHT_BUILD_DIR:=$DUSKLIGHT_DIR/build/linux-default-relwithdebinfo}"

# funchook source tree (runtime inline hooking).
: "${FUNCHOOK_DIR:=$HOME/Github/funchook}"

# Where the deploy script installs built mods: the "mods" folder the game scans
# at startup. Default is next to the dev executable; set this to your data
# folder's mods/ (e.g. ~/.local/share/TwilitRealm/Dusklight/mods) for an
# installed build.
: "${DEPLOY_DIR:=$DUSKLIGHT_BUILD_DIR/mods}"
