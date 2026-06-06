// Killable NPCs -- a Dusklight mod.
//
// Makes actors take real damage like enemies do: any player attack (sword, wolf
// bite, slingshot, arrows, bombs, ...) hurts them with configurable health and
// the weapon's own hit sound, and they die like a normal enemy. Only the player
// and ordinary enemies are excluded (enemies already handle their own damage).
//
// How: most actors carry the game's collision + `health` machinery but their
// target colliders reject player attacks at the At/Tg type gate and they never
// run a damage check. This mod post-hooks cCcS::ChkNoHitAtTg to force-allow
// player attacks against those colliders (noting the actor + its tg collider),
// then each frame (mod_tick, after collision) reads each noted collider's hit
// and runs the game's own cc_at_check -- with brief i-frames so one swing counts
// as one hit.

#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "dusk/hook.hpp"
#include "dusk/mod_api.h"

#include "f_op/f_op_actor.h"
#include "f_op/f_op_actor_mng.h"
#include "d/d_cc_uty.h"                 // cc_at_check, dCcU_AtInfo
#include "d/d_cc_d.h"                   // dCcD_GObjInf (GetAtSe, getHitSeID)
#include "d/d_com_inf_game.h"           // dComIfGp_getPlayer
#include "SSystem/SComponent/c_cc_d.h"  // cCcD_Obj, AT_TYPE_*
#include "SSystem/SComponent/c_cc_s.h"  // cCcS::ChkNoHitAtTg

DUSK_REQUIRE_API_VERSION

namespace {

constexpr u32 kPlayerWeaponMask = AT_TYPE_NORMAL_SWORD | AT_TYPE_MASTER_SWORD | AT_TYPE_8000 |
    AT_TYPE_SLINGSHOT | AT_TYPE_ARROW | AT_TYPE_BOMB | AT_TYPE_BOOMERANG | AT_TYPE_IRON_BALL |
    AT_TYPE_WOLF_ATTACK | AT_TYPE_WOLF_CUT_TURN;

// Frames of invulnerability after a hit, so a single swing (whose collider stays
// crossing for several frames) counts as one hit.
constexpr int kHitCooldown = 20;

const DuskSetting kSettings[] = {
    {"npc_hp", "NPC Health", "Health an actor has before dying, in the game's HP units.",
        DUSK_SETTING_INT, 40, 1, 500, 5},
    {"effect", "Death Effect", "0 = explosion poof, 1 = dark vanish.", DUSK_SETTING_INT, 0, 0, 1, 1},
    {"flee", "Flee When Hit", "Once struck, the actor keeps running away from you.",
        DUSK_SETTING_BOOL, 1, 0, 1, 1},
    {"flee_speed", "Flee Speed", "How fast a fleeing actor runs (units/frame).",
        DUSK_SETTING_FLOAT, 14, 1, 50, 1},
};

struct ActorState {
    s16 hp = 0;
    int iframes = 0;
    bool inited = false;
    bool fleeing = false;  // set on first hit -- runs away from then on
};

std::unordered_map<u32, ActorState> g_actors;             // per-actor HP + i-frames
std::vector<std::pair<u32, cCcD_Obj*>> g_candidates;      // (actor id, tg collider) this frame

// Settings are read in mod_tick (where the mod context is valid) and cached for
// use by the collision hook, which runs outside that context.
s16 g_maxHp = 40;
u8 g_effect = 0;
bool g_flee = true;
f32 g_fleeSpeed = 14.0f;

bool is_player_attack(cCcD_Obj* at) {
    fopAc_ac_c* attacker = at->GetAc();
    if (attacker == nullptr) {
        return false;
    }
    if (fopAcM_GetGroup(attacker) == fopAc_PLAYER_e) {
        return true;  // sword, wolf bite/claw, spin, ...
    }
    return (at->GetAtType() & kPlayerWeaponMask) != 0;  // arrows, bombs, ...
}

bool is_killable_target(fopAc_ac_c* actor) {
    if (actor == nullptr) {
        return false;
    }
    const u8 group = fopAcM_GetGroup(actor);
    return group != fopAc_PLAYER_e && group != fopAc_ENEMY_e;
}

// Post-hook on the At/Tg type gate. Record player-attack-vs-actor pairs and force
// the pair allowed so collision proceeds and records the tg hit.
void chk_post(void* args, void* retval) {
    cCcD_Obj* at = dusk::arg<cCcD_Obj*>(args, 1);
    cCcD_Obj* tg = dusk::arg<cCcD_Obj*>(args, 2);
    if (at == nullptr || tg == nullptr) {
        return;
    }
    fopAc_ac_c* atAc = at->GetAc();
    fopAc_ac_c* tgAc = tg->GetAc();
    if (atAc == nullptr || tgAc == nullptr || atAc == tgAc) {
        return;
    }
    if (is_killable_target(tgAc) && is_player_attack(at)) {
        g_candidates.emplace_back(fopAcM_GetID(tgAc), tg);
        *static_cast<bool*>(retval) = false;  // allow the hit
    }
}

void play_hit_sound(fopAc_ac_c* actor, cCcD_Obj* at) {
    auto* gobj = static_cast<dCcD_GObjInf*>(at);
    fopAcM_seStart(actor, dCcD_GObjInf::getHitSeID(gobj->GetAtSe(), 0), 0);
}

// Steer a fleeing actor directly away from the player, facing the run direction,
// using the engine's own move integration.
void drive_flee(fopAc_ac_c* actor, fopAc_ac_c* player) {
    f32 dx = actor->current.pos.x - player->current.pos.x;
    f32 dz = actor->current.pos.z - player->current.pos.z;
    f32 len = std::sqrt(dx * dx + dz * dz);
    if (len < 1.0f) {
        dx = 0.0f;  // standing on the player -- pick an arbitrary direction
        dz = 1.0f;
        len = 1.0f;
    }
    const f32 nx = dx / len;
    const f32 nz = dz / len;
    actor->shape_angle.y = static_cast<s16>(std::atan2(nx, nz) * (32768.0f / 3.14159265f));
    cXyz move(nx * g_fleeSpeed, 0.0f, nz * g_fleeSpeed);
    fopAcM_posMove(actor, &move);
}

}  // namespace

extern "C" {

void mod_init(DuskModAPI* api) {
    dusk::init(api);
    api->define_settings(kSettings, sizeof(kSettings) / sizeof(kSettings[0]));
    dusk::hookAddPost<&cCcS::ChkNoHitAtTg>(chk_post);
    api->log_info("Killable NPCs enabled");
}

void mod_tick(DuskModAPI* api) {
    g_maxHp = static_cast<s16>(api->setting_get("npc_hp"));
    g_effect = static_cast<u8>(api->setting_get("effect"));
    g_flee = api->setting_get("flee") != 0.0;
    g_fleeSpeed = static_cast<f32>(api->setting_get("flee_speed"));

    for (auto& [id, state] : g_actors) {
        if (state.iframes > 0) {
            --state.iframes;
        }
    }

    std::unordered_set<u32> hitThisFrame;  // one hit per actor per frame across its colliders
    for (auto& [id, tg] : g_candidates) {
        if (!tg->ChkTgHit()) {
            continue;  // this collider didn't actually cross
        }
        fopAc_ac_c* actor = fopAcM_SearchByID(id);
        if (actor == nullptr) {
            continue;
        }
        ActorState& state = g_actors[id];
        if (!state.inited) {
            state.hp = g_maxHp;
            state.inited = true;
        }
        if (state.iframes > 0 || !hitThisFrame.insert(id).second) {
            continue;
        }

        cCcD_Obj* at = tg->GetTgHitObj();
        if (at == nullptr) {
            continue;
        }

        // Apply real damage through the game's own attack check: it reads the
        // weapon's attack power from the hitting collider and subtracts it from
        // the actor's health, exactly as enemies take damage.
        dCcU_AtInfo info{};
        info.mpCollider = at;
        actor->health = state.hp;
        cc_at_check(actor, &info);
        state.hp = actor->health;
        state.iframes = kHitCooldown;
        play_hit_sound(actor, at);

        if (actor->health <= 0) {
            fopAcM_createDisappear(actor, &actor->current.pos, 10, g_effect, 0xFF);
            fopAcM_delete(actor);
            g_actors.erase(id);
        } else if (g_flee) {
            state.fleeing = true;  // struck and survived -- flee from now on
        }
    }
    g_candidates.clear();

    // Keep fleeing actors running away from the player every frame.
    if (g_flee) {
        if (fopAc_ac_c* player = dComIfGp_getPlayer(0)) {
            for (auto it = g_actors.begin(); it != g_actors.end();) {
                if (!it->second.fleeing) {
                    ++it;
                    continue;
                }
                fopAc_ac_c* actor = fopAcM_SearchByID(it->first);
                if (actor == nullptr) {
                    it = g_actors.erase(it);
                    continue;
                }
                drive_flee(actor, player);
                ++it;
            }
        }
    }
}

void mod_cleanup(DuskModAPI* api) {
    (void)api;
    g_actors.clear();
    g_candidates.clear();
}

}  // extern "C"
