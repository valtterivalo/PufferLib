#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include "riskfight_oppfit_bridge.h"
#include "../osrs/tests/riskfight_observe.h"

typedef struct {
    Hist hist;
    uint32_t seed;
    int step_damage, step_max_hit, vw, maul;
} OppFitBridge;

static OppFit g_fit;
static int g_loaded;

static void oppfit_hit(void* context, const OsrsPvpHitEvent* event) {
    OppFitBridge* b = context;
    if (event->target == 1) {
        b->step_damage += event->hitpoints_lost;
        if (event->hitpoints_lost > b->step_max_hit) b->step_max_hit = event->hitpoints_lost;
    }
}

void* oppfit_create(void) {
    if (!g_loaded) {
        const char* model = getenv("RF_OPPFIT_MODEL");
        const char* eat = getenv("RF_OPPFIT_EAT");
        if (!model || !eat) { fprintf(stderr, "oppfit: RF_OPPFIT_MODEL and RF_OPPFIT_EAT must name the fit files\n"); abort(); }
        opp_load(model, eat, &g_fit, &g_eat_model);
        g_ablation.hp = HP_UNIFORM; g_ablation.escape_model = 1; g_ablation.his_defence = 99; g_ablation.panic = 0.8; g_ablation.panic_hp = 25;
        observer_register_items();
        g_loaded = 1;
    }
    OppFitBridge* b = calloc(1, sizeof(*b));
    if (!b) abort();
    return b;
}

void oppfit_destroy(void* bridge) { free(bridge); }

void oppfit_reset(void* bridge, void* state, uint32_t seed) {
    OppFitBridge* b = bridge;
    RiskfightState* s = state;
    memset(b, 0, sizeof(*b));
    b->hist = hist_start();
    b->seed = seed;
    s->env.pvp_runtime.hit_observer = oppfit_hit;
    s->env.pvp_runtime.hit_observer_context = b;
}

void oppfit_actions(void* bridge, const void* state, int* actions) {
    OppFitBridge* b = bridge;
    const RiskfightState* s = state;
    g_rollout_seed = b->seed;
    b->step_damage = b->step_max_hit = 0;
    b->vw = s->spec_voidwaker[1];
    b->maul = s->spec_maul[1];
    int step = s->env.tick;
    opp_fit_actions(&g_fit, s, &b->hist, 1 + step, b->seed + (uint32_t)step * 2654435761u, actions);
}

void oppfit_after(void* bridge, void* state) {
    OppFitBridge* b = bridge;
    RiskfightState* s = state;
    const Player* him = &s->env.players[1];
    int ate_true = him->ate_food_this_tick || him->ate_karambwan_this_tick || s->inventory_use[1].potion_animation_tick_plus_one == s->env.tick;
    StepEvents events = {.ate_visible = riskfight_meal_visible(s, 1), .ate = ate_true,
        .swung = s->visible[0].last_attack_tick == s->env.tick - 1, .vw = s->spec_voidwaker[1] - b->vw, .maul = s->spec_maul[1] - b->maul,
        .damage_on_him = b->step_damage, .max_hit_on_him = b->step_max_hit, .hit_on_him = him->hit_landed_this_tick};
    /* The list of his unseen animations is pruned only when his bar is read; a 500-tick fight can outlast its capacity and nothing reads more than its count. */
    Warning* w = &b->hist.warning;
    if (w->unseen_count >= WARNING_UNSEEN - 2) {
        int drop = w->unseen_count / 2;
        memmove(w->unseen, w->unseen + drop, (size_t)(w->unseen_count - drop) * sizeof(int));
        w->unseen_count -= drop;
    }
    hist_step(&b->hist, s, s->env.tick, &events);
}
