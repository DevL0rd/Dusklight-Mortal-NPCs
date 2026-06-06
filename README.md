# Killable NPCs

A [Dusklight](https://github.com/TwilitRealm/dusklight) code mod that lets you
defeat almost any actor. Any player attack — sword, wolf bite, slingshot, arrows,
bombs — deals real damage with configurable health and the weapon's own hit
sound, and the target dies like a normal enemy. Only the player and ordinary
enemies are excluded (enemies already handle their own damage).

## How it works

Most actors carry the game's collision + `health` machinery, but their target
colliders reject player attacks at the At/Tg type gate and never run a damage
check. The mod hooks `cCcS::ChkNoHitAtTg` (via the Dusklight hook API) to
force-allow player attacks against those colliders, then each frame runs the
game's own `cc_at_check` on the actors that were actually struck — so damage,
knockback and death are exactly the game's, not a reimplementation. Brief
i-frames make one swing count as one hit.

## Settings

Declared in code (`define_settings` at init) and shown in the **Mods** tab
(left = mod list, right = its settings):

| Setting | Default | Description |
| --- | --- | --- |
| NPC Health | 40 | Health an actor has before dying, in the game's HP units. |
| Death Effect | 0 | `0` = explosion poof (works anywhere); `1` = dark vanish (only shows in Twilight areas). |

## Building

This is a code mod built with the Dusklight mod SDK. It needs a Dusklight source
checkout (for the game headers + the `add_dusk_mod()` packaging helper); by
default it expects `~/Github/dusklight`.

```sh
./build.sh
```

That packages `killable_npcs.dusk` straight into the game's mods folder
(`~/.local/share/TwilitRealm/Dusklight/mods` by default — override with
`MODS_DIR`, and the Dusklight path with `DUSK_DIR`). The running game
**hot-reloads** the mod when the package changes, so you can iterate without
restarting. The mod is enabled by default; its enabled state and settings are
saved by the game under `mods/.config/`.

## Files

| File | Purpose |
| --- | --- |
| `src/killable_npcs.cpp` | Mod source (`mod_init`/`mod_tick` + the collision hook). |
| `src/mod.json` | Metadata (id, name, version, author, description, has_code). |
| `CMakeLists.txt` | Declares the mod via the Dusklight SDK's `add_dusk_mod()`. |
| `build.sh` | Build + package into the game's mods folder. |
