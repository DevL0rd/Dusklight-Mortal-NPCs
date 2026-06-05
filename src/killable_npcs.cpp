// Killable NPCs -- an example native Dusklight mod (detour-based).
//
// Makes NPCs take damage like real enemies: any player attack (sword, wolf bite,
// slingshot, arrows, bombs, ...) hurts them, they have configurable HP, play the
// weapon's hit sound, and die when HP runs out.
//
// How: NPCs carry the game's collision + `health` machinery (they're fopAc_ac_c)
// but their target colliders reject player attacks at the At/Tg type gate and
// they never run a damage check. This mod hooks cCcS::ChkNoHitAtTg to force-allow
// player attacks against NPC tg colliders (and note the NPC + its tg collider),
// then once per frame (fapGm_Execute hook) reads each NPC's tg-hit and runs the
// game's own cc_at_check to apply real damage, plays the hit sound, and defeats
// it at 0 HP -- with brief i-frames so one swing = one hit.

#include <dlfcn.h>

#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <funchook.h>

#include "dusk/mod_sdk.h"

#include "f_op/f_op_actor.h"
#include "f_op/f_op_actor_mng.h"
#include "d/d_cc_uty.h"                 // cc_at_check, dCcU_AtInfo
#include "d/d_cc_d.h"                   // dCcD_GObjInf (GetAtSe, getHitSeID)
#include "SSystem/SComponent/c_cc_d.h"  // cCcD_Obj, AT_TYPE_*

namespace {

constexpr const char* kSymChkNoHit = "_ZN4cCcS12ChkNoHitAtTgEP8cCcD_ObjS1_";
constexpr const char* kSymGameExec = "_Z13fapGm_Executev";

using ChkNoHitFn = bool (*)(void* self, cCcD_Obj* at, cCcD_Obj* tg);
using GameExecFn = void (*)(void);

constexpr u32 kPlayerWeaponMask = AT_TYPE_NORMAL_SWORD | AT_TYPE_MASTER_SWORD | AT_TYPE_8000 |
    AT_TYPE_SLINGSHOT | AT_TYPE_ARROW | AT_TYPE_BOMB | AT_TYPE_BOOMERANG | AT_TYPE_IRON_BALL |
    AT_TYPE_WOLF_ATTACK | AT_TYPE_WOLF_CUT_TURN;

// Frames of invulnerability after a hit, so a single swing (whose collider
// stays crossing for several frames) only counts as one hit.
constexpr int kHitCooldown = 20;

const DuskSetting kSettings[] = {
    {"npc_hp", "NPC Health", "Health an NPC has before dying, in the game's HP units.",
        DUSK_SETTING_INT, 40, 1, 500, 5},
    {"effect", "Death Effect", "0 = explosion poof, 1 = dark vanish.", DUSK_SETTING_INT, 0, 0, 1, 1},
};

const DuskHost* g_host = nullptr;
DuskMod* g_self = nullptr;
funchook_t* g_funchook = nullptr;
ChkNoHitFn g_orig_chk = nullptr;
GameExecFn g_orig_exec = nullptr;

struct NpcState {
    s16 hp = 0;
    int iframes = 0;
    bool inited = false;
};
std::unordered_map<u32, NpcState> g_npcs;                  // per-NPC HP + i-frames
std::vector<std::pair<u32, cCcD_Obj*>> g_candidates;      // (NPC id, tg collider) pairs this frame
                                                          // -- an NPC may have several tg colliders

bool is_player_attack(cCcD_Obj* at) {
    fopAc_ac_c* attacker = at->GetAc();
    if (attacker == nullptr) {
        return false;
    }
    if (fopAcM_GetGroup(attacker) == fopAc_PLAYER_e) {
        return true;  // sword, wolf bite/claw, spin, etc.
    }
    return (at->GetAtType() & kPlayerWeaponMask) != 0;  // arrows, bombs, ...
}

// Any actor a player attack can reach is killable -- NPCs, animals, generic
// actors, etc. The only exclusions are the player itself and normal enemies
// (enemies already take damage through their own AI; intercepting them here
// would override their real HP and death).
bool is_killable_target(fopAc_ac_c* actor) {
    if (actor == nullptr) {
        return false;
    }
    const u8 group = fopAcM_GetGroup(actor);
    return group != fopAc_PLAYER_e && group != fopAc_ENEMY_e;
}

// Hook: the At/Tg type gate. Record + force-allow player attacks vs NPC tg
// colliders; the actual hit is read back after collision in detour_exec.
bool detour_chk(void* self, cCcD_Obj* at, cCcD_Obj* tg) {
    const bool noHit = g_orig_chk(self, at, tg);
    fopAc_ac_c* atAc = at->GetAc();
    fopAc_ac_c* tgAc = tg->GetAc();
    if (atAc == nullptr || tgAc == nullptr || atAc == tgAc) {
        return noHit;  // skip null + self-collisions
    }

    // Diagnostic: log every distinct cross-actor (attacker -> target) pair the
    // gate sees, so we can see the wolf attack (atType 0x80000000) pair with an
    // NPC and learn that NPC's group. Bounded by the distinct set + a hard cap.
    {
        static std::unordered_set<u32> seen;
        const u32 key = (static_cast<u32>(static_cast<u16>(fopAcM_GetName(atAc))) << 16) |
            static_cast<u16>(fopAcM_GetName(tgAc));
        if (seen.size() < 80 && seen.insert(key).second) {
            char b[176];
            std::snprintf(b, sizeof(b),
                "Killable: AT %d grp %d (0x%x) -> TG %d grp %d  noHit=%d",
                fopAcM_GetName(atAc), fopAcM_GetGroup(atAc), at->GetAtType(),
                fopAcM_GetName(tgAc), fopAcM_GetGroup(tgAc), (int)noHit);
            g_host->log(b);
        }
    }

    if (is_killable_target(tgAc) && is_player_attack(at)) {
        g_candidates.emplace_back(fopAcM_GetID(tgAc), tg);
        return false;  // allow the geometry check + hit
    }
    return noHit;
}

void play_hit_sound(fopAc_ac_c* npc, cCcD_Obj* at) {
    auto* gobj = static_cast<dCcD_GObjInf*>(at);
    fopAcM_seStart(npc, dCcD_GObjInf::getHitSeID(gobj->GetAtSe(), 0), 0);
}

// Hook: once per game tick. Read each candidate NPC's tg-hit and apply damage.
void detour_exec(void) {
    g_orig_exec();

    for (auto& [id, state] : g_npcs) {
        if (state.iframes > 0) {
            --state.iframes;
        }
    }

    const s16 maxHp = static_cast<s16>(g_host->setting_get(g_self, "npc_hp"));
    const u8 effect = static_cast<u8>(g_host->setting_get(g_self, "effect"));

    std::unordered_set<u32> hitThisFrame;  // one hit per NPC per frame across its colliders
    for (auto& [id, tg] : g_candidates) {
        const bool tgHit = tg->ChkTgHit();

        // One-time diagnostic per (actor, hit-state): log the actor's native
        // health so we can tell living creatures (dog/NPC) from props (signs).
        {
            static std::unordered_set<u32> logged;
            if (logged.insert(id * 2u + (tgHit ? 1u : 0u)).second) {
                fopAc_ac_c* a = fopAcM_SearchByID(id);
                char d[128];
                std::snprintf(d, sizeof(d),
                    "Killable: cand id %d name %d grp %d nativeHealth %d tgHit=%d", id,
                    a ? fopAcM_GetName(a) : -1, a ? fopAcM_GetGroup(a) : -1,
                    a ? (int)a->health : -999, (int)tgHit);
                g_host->log(d);
            }
        }

        if (!tgHit) {
            continue;  // this collider didn't cross
        }
        fopAc_ac_c* npc = fopAcM_SearchByID(id);
        if (npc == nullptr) {
            continue;
        }
        NpcState& state = g_npcs[id];
        if (!state.inited) {
            state.hp = maxHp;
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
        const s16 before = state.hp;
        npc->health = state.hp;
        cc_at_check(npc, &info);
        state.hp = npc->health;
        state.iframes = kHitCooldown;
        play_hit_sound(npc, at);

        char b[128];
        std::snprintf(b, sizeof(b), "Killable: damage npc %d hp %d->%d (power %d)",
            fopAcM_GetName(npc), static_cast<int>(before), static_cast<int>(state.hp),
            static_cast<int>(info.mAttackPower));
        g_host->log(b);

        if (state.hp <= 0) {
            fopAcM_createDisappear(npc, &npc->current.pos, 10, effect, 0xFF);
            fopAcM_delete(npc);
            g_npcs.erase(id);
        }
    }
    g_candidates.clear();
}

void* resolve(const char* symbol) {
    void* self = dlopen(nullptr, RTLD_NOW | RTLD_GLOBAL);
    return self ? dlsym(self, symbol) : nullptr;
}

}  // namespace

extern "C" {

int dusk_mod_init(const DuskHost* host, DuskMod* self) {
    if (host == nullptr || host->abi_version != DUSK_MOD_ABI_VERSION) {
        return 1;
    }
    g_host = host;
    g_self = self;
    host->define_settings(self, kSettings, sizeof(kSettings) / sizeof(kSettings[0]));

    g_orig_chk = reinterpret_cast<ChkNoHitFn>(resolve(kSymChkNoHit));
    g_orig_exec = reinterpret_cast<GameExecFn>(resolve(kSymGameExec));
    if (g_orig_chk == nullptr || g_orig_exec == nullptr) {
        host->log("Killable NPCs: could not resolve game symbols");
        return 1;
    }

    g_funchook = funchook_create();
    if (g_funchook == nullptr ||
        funchook_prepare(g_funchook, reinterpret_cast<void**>(&g_orig_chk),
            reinterpret_cast<void*>(detour_chk)) != 0 ||
        funchook_prepare(g_funchook, reinterpret_cast<void**>(&g_orig_exec),
            reinterpret_cast<void*>(detour_exec)) != 0 ||
        funchook_install(g_funchook, 0) != 0) {
        host->log("Killable NPCs: failed to install hooks");
        if (g_funchook != nullptr) {
            funchook_destroy(g_funchook);
            g_funchook = nullptr;
        }
        return 1;
    }

    host->log("Killable NPCs enabled");
    return 0;
}

void dusk_mod_dispose(void) {
    if (g_funchook != nullptr) {
        funchook_uninstall(g_funchook, 0);
        funchook_destroy(g_funchook);
        g_funchook = nullptr;
    }
    g_npcs.clear();
    g_candidates.clear();
    g_orig_chk = nullptr;
    g_orig_exec = nullptr;
    g_self = nullptr;
    g_host = nullptr;
}

}  // extern "C"
