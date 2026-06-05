# Killable NPCs

A native [Dusklight](https://github.com/TwilitRealm/dusklight) mod that lets you
defeat any NPC with your sword. Struck NPCs are launched back and vanish with the
standard enemy death effect.

It's a **detour mod**: Dusklight exports its symbols, so this mod hooks game
functions at runtime (via [funchook](https://github.com/kubo/funchook)) and calls
game code directly through the game's own headers — no game-side support code.
Specifically it hooks `daAlink_c::setSwordHitVibration` (detect the sword hitting
an NPC) and `fapGm_Execute` (drive the knockback each frame).

## Settings

The mod declares these in code (`define_settings` at init); the host writes them
to a generated `settings.json` and shows them in the **Mods** tab (left = mod
list, right = its settings):

| Setting | Default | Description |
| --- | --- | --- |
| Knockback Strength | 18 | How far a defeated NPC is launched backward. |
| Launch Height | 34 | How high a defeated NPC is launched. |
| Death Effect | 1 | `0` = explosion poof, `1` = dark vanish. |

## Building & deploying

Because a detour mod reaches directly into the game, it builds against the
Dusklight source tree (headers), a configured Dusklight build dir (generated +
fetched headers such as Tracy), and funchook. The paths live in this mod's
[`config.sh`](config.sh) and default to siblings under `~/Github`:

By default the build expects `dusklight` and `funchook` checked out under
`~/Github` (alongside your `TwilightPrinces/` mods); override the paths in
`config.sh` if your layout differs.

```
~/Github/
    dusklight/              built once, without LTO (e.g. linux-default-relwithdebinfo)
    funchook/               git clone https://github.com/kubo/funchook
    TwilightPrinces/        your Dusklight-related repos
        killable_npcs/      (this mod -- a self-contained repo)
            config.sh       DUSKLIGHT_DIR / DUSKLIGHT_BUILD_DIR / FUNCHOOK_DIR / DEPLOY_DIR
            build.sh        build + deploy
            deploy.sh       install the built mod into DEPLOY_DIR
            src/            killable_npcs.cpp + mod.json
            ...
```

Edit `config.sh` (or export the vars) for your machine, then:

```sh
./build.sh
```

That builds the mod and **auto-deploys** it. CMake compiles in `staging/` (its
scratch tree) and assembles the ready-to-copy folder at `build/killable_npcs/`
(the library + `mod.json`, laid out exactly as it belongs in `mods/`);
`build.sh` then calls `./deploy.sh`, which copies that folder into
`DEPLOY_DIR/killable_npcs/` (the game's `mods/` folder). Both `build/` and
`staging/` are git-ignored. Restart Dusklight; the mod is enabled by default.
`config.json` (enabled state + saved settings) and `settings.json` (the declared
settings) are generated there automatically.

To deploy an already-built mod without rebuilding: `./deploy.sh`.

> Dusklight must be built **without LTO** (the non-`release` presets already are)
> so the functions this mod hooks remain real, addressable symbols. Rebuild +
> redeploy whenever Dusklight is rebuilt — stale mod binaries will not load.

## Files

| File | Purpose |
| --- | --- |
| `src/killable_npcs.cpp` | Mod source (detours + direct game calls). |
| `src/mod.json` | Metadata (id, name, version, author, library). |
| `CMakeLists.txt` | CMake build (links funchook, points at the Dusklight tree). |
| `config.sh` | Paths: Dusklight tree/build dir, funchook, and deploy target. |
| `build.sh` | Build + deploy (uses `config.sh`, `deploy.sh`). |
| `deploy.sh` | Copy the built mod into the game's `mods/` folder. |
