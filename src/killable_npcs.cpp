// Killable NPCs -- an example native Dusklight mod (detour-based).
//
// The game exports its symbols, so this mod reaches straight into the game: it
// installs runtime function hooks (via funchook) and calls game functions
// directly through the game's own headers. No game-side per-feature support is
// needed.
//
// What it does: hooks daAlink_c::setSwordHitVibration -- which the game calls
// for every actor the sword's collider strikes -- and, when the struck actor is
// an NPC, "defeats" it: a short knockback launch (driven each frame by a hook on
// fapGm_Execute) followed by the standard enemy dark-vanish effect + removal.
// Knockback/effect are read from the mod's settings (settings.json / Mods tab).

#include <dlfcn.h>

#include <cmath>
#include <vector>

#include <funchook.h>

#include "dusk/mod_sdk.h"

// Game headers (resolved against the Dusklight source tree by the build).
#include "f_op/f_op_actor.h"
#include "f_op/f_op_actor_mng.h"
#include "d/d_cc_d.h"

namespace {

// Mangled symbols of the game functions we hook (from the exported executable).
constexpr const char* kSymSwordHitVibration = "_ZN9daAlink_c20setSwordHitVibrationEP12dCcD_GObjInf";
constexpr const char* kSymGameExecute = "_Z13fapGm_Executev";

using SwordHitFn = int (*)(void* self, dCcD_GObjInf* gobj);
using GameExecFn = void (*)(void);

// Settings this mod exposes. Declared to the host on init, which shows them in
// the Mods tab, persists their values, and writes the mod's settings.json.
const DuskSetting kSettings[] = {
    {"knockback", "Knockback Strength", "How far a defeated NPC is launched backward.",
        DUSK_SETTING_FLOAT, 18, 0, 60, 2},
    {"launch_up", "Launch Height", "How high a defeated NPC is launched.", DUSK_SETTING_FLOAT, 34, 0,
        80, 2},
    {"effect", "Death Effect",
        "0 = explosion poof (works anywhere); 1 = dark vanish (only shows in "
        "Twilight areas where that particle bank is loaded).",
        DUSK_SETTING_INT, 0, 0, 1, 1},
};

const DuskHost* g_host = nullptr;
DuskMod* g_self = nullptr;
funchook_t* g_funchook = nullptr;
SwordHitFn g_orig_sword = nullptr;  // becomes the trampoline after prepare
GameExecFn g_orig_exec = nullptr;

struct PendingDefeat {
    u32 id;     // victim process id (resolved each tick; never a stored pointer)
    cXyz vel;   // current velocity
    int ticks;  // ticks remaining before the vanish effect
    u8 effect;  // fopAcM_createDisappear type
};
std::vector<PendingDefeat> g_pending;

constexpr f32 kGravity = 5.5f;
constexpr int kDefeatTicks = 14;

void start_defeat(fopAc_ac_c* victim, fopAc_ac_c* source) {
    const u32 id = fopAcM_GetID(victim);
    for (const PendingDefeat& p : g_pending) {
        if (p.id == id) {
            return;  // already being defeated
        }
    }

    const f32 knockback = static_cast<f32>(g_host->setting_get(g_self, "knockback"));
    const f32 launchUp = static_cast<f32>(g_host->setting_get(g_self, "launch_up"));
    const u8 effect = static_cast<u8>(g_host->setting_get(g_self, "effect"));

    // Knockback direction: away from the attacker, falling back to the victim's
    // own facing if positions coincide.
    f32 dx = victim->current.pos.x - source->current.pos.x;
    f32 dz = victim->current.pos.z - source->current.pos.z;
    f32 len = std::sqrt(dx * dx + dz * dz);
    if (len < 1.0f) {
        const f32 ang = victim->shape_angle.y * (3.14159265f / 32768.0f);
        dx = std::sin(ang);
        dz = std::cos(ang);
        len = 1.0f;
    }

    PendingDefeat p{};
    p.id = id;
    p.vel.x = (dx / len) * knockback;
    p.vel.z = (dz / len) * knockback;
    p.vel.y = launchUp;
    p.ticks = kDefeatTicks;
    p.effect = effect;
    g_pending.push_back(p);
}

// Hook: called for every actor the sword collider hits.
int detour_sword(void* self, dCcD_GObjInf* gobj) {
    if (gobj->ChkAtHit()) {
        fopAc_ac_c* victim = gobj->GetAtHitAc();
        if (victim != nullptr && fopAcM_GetGroup(victim) == fopAc_NPC_e) {
            start_defeat(victim, reinterpret_cast<fopAc_ac_c*>(self));
            return 1;  // consume the hit
        }
    }
    return g_orig_sword(self, gobj);
}

// Hook: once per game simulation tick. Advance in-progress defeats after the
// game has run its own logic for the frame.
void detour_exec(void) {
    g_orig_exec();

    for (std::size_t i = 0; i < g_pending.size();) {
        PendingDefeat& p = g_pending[i];
        fopAc_ac_c* actor = fopAcM_SearchByID(p.id);
        bool finished = false;

        if (actor == nullptr) {
            finished = true;
        } else {
            actor->current.pos.x += p.vel.x;
            actor->current.pos.y += p.vel.y;
            actor->current.pos.z += p.vel.z;
            p.vel.y -= kGravity;
            if (--p.ticks <= 0) {
                fopAcM_createDisappear(actor, &actor->current.pos, 10, p.effect, 0xFF);
                fopAcM_delete(actor);
                finished = true;
            }
        }

        if (finished) {
            g_pending[i] = g_pending.back();
            g_pending.pop_back();
        } else {
            ++i;
        }
    }
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

    g_orig_sword = reinterpret_cast<SwordHitFn>(resolve(kSymSwordHitVibration));
    g_orig_exec = reinterpret_cast<GameExecFn>(resolve(kSymGameExecute));
    if (g_orig_sword == nullptr || g_orig_exec == nullptr) {
        host->log("Killable NPCs: could not resolve game symbols");
        return 1;
    }

    g_funchook = funchook_create();
    if (g_funchook == nullptr ||
        funchook_prepare(g_funchook, reinterpret_cast<void**>(&g_orig_sword),
            reinterpret_cast<void*>(detour_sword)) != 0 ||
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
    g_pending.clear();
    g_orig_sword = nullptr;
    g_orig_exec = nullptr;
    g_self = nullptr;
    g_host = nullptr;
}

}  // extern "C"
