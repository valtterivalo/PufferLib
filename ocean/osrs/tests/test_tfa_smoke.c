#include "../encounters/encounter_tfa.h"
#include "osrs_test_check.h"

/* Scripted raiders: attack the boss, pray by phase, eat and drink when low. */
static void tfa_scripted_actions(TfaState* s, int p, int* acts) {
    memset(acts, 0, sizeof(int) * TFA_NUM_ACTION_HEADS);
    TfaPlayer* tp = &s->players[p];
    if (tp->life != TFA_LIFE_ALIVE) return;
    const TfaNpc* g = tfa_npc_first(s, TFA_NPC_GUARDIAN);
    if (!g) return;
    int grounded = s->boss.guardian.phase == TFA_GUARDIAN_GROUNDED;
    OverheadPrayer want = grounded ? PRAYER_PROTECT_MELEE : PRAYER_PROTECT_RANGED;
    if (tp->player.prayer != want)
        acts[OSRS_HEAD_OVERHEAD] = grounded ? ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE : ENCOUNTER_OVERHEAD_SET_REFRESH_RANGED;
    acts[OSRS_HEAD_PRIMARY] = TFA_PRIMARY_ATTACK_BASE + tfa_npc_slot(s, g);
    int here = tfa_tile_danger(s, p, tp->player.x, tp->player.y);
    if (here >= 0 && here <= 1) {
        for (int a = 1; a < OSRS_PRIMARY_MOVE_ACTIONS; a++) {
            int nx = tp->player.x + ENCOUNTER_MOVE_TARGET_DX[a], ny = tp->player.y + ENCOUNTER_MOVE_TARGET_DY[a];
            if (tfa_layout_blocked(tfa_room_layout(s->room), nx, ny) || tfa_tile_danger(s, p, nx, ny) >= 0) continue;
            acts[OSRS_HEAD_PRIMARY] = a;
            break;
        }
    }
    for (int cell = 0; cell < OSRS_INVENTORY_SIZE; cell++) {
        OsrsInventoryClickResolution r =
            osrs_inventory_cell_click_interpret(&tp->player.inventory_cells[cell], OSRS_CLICK_TICK_FIRST);
        if (r.click_action == OSRS_CLICK_EAT && tp->player.current_hitpoints < 60 && !acts[OSRS_HEAD_EAT] &&
                osrs_can_eat_consumable_kind(&tp->player, r.consumable_kind))
            acts[OSRS_HEAD_EAT] = cell + 1;
        if (r.click_action == OSRS_CLICK_DRINK && tp->player.potion_timer == 0 && !acts[OSRS_HEAD_DRINK] &&
                ((r.consumable_kind == OSRS_CONSUMABLE_SUPER_RESTORE && tp->player.current_prayer < 30) ||
                 (r.consumable_kind == OSRS_CONSUMABLE_BREW && tp->player.current_hitpoints < 70)))
            acts[OSRS_HEAD_DRINK] = cell + 1;
    }
}

static void run_team(int team_size, int episodes) {
    TfaState* s = calloc(1, sizeof(TfaState));
    TfaContext* ctx = calloc(1, sizeof(TfaContext));
    tfa_init_context((EncounterContext*)ctx);
    ctx->config.team_size = team_size;
    tfa_finalize_context((EncounterState*)s, (EncounterContext*)ctx);
    int clears = 0, wipes = 0, timeouts = 0;
    long ticks = 0;
    float obs[TFA_NUM_OBS];
    unsigned char mask[TFA_ACTION_MASK_SIZE];
    for (int e = 0; e < episodes; e++) {
        tfa_reset_typed(s, ctx, 1000 + e);
        int acts[TFA_MAX_PLAYERS * TFA_NUM_ACTION_HEADS];
        while (!s->episode_over) {
            for (int p = 0; p < TFA_MAX_PLAYERS; p++) {
                tfa_scripted_actions(s, p, acts + p * TFA_NUM_ACTION_HEADS);
                if (p >= team_size) continue;
                tfa_write_mask_for(s, ctx, p, mask);
                int off = 0;
                for (int h = 0; h < TFA_NUM_ACTION_HEADS; h++) {
                    int a = acts[p * TFA_NUM_ACTION_HEADS + h];
                    if (!mask[off + a]) acts[p * TFA_NUM_ACTION_HEADS + h] = 0;
                    off += TFA_ACTION_DIMS[h];
                }
            }
            tfa_step_team(s, ctx, acts);
            for (int p = 0; p < team_size; p++) tfa_write_obs_for(s, ctx, p, obs);
        }
        if (getenv("TFA_DEBUG") && e < 3) {
            const TfaNpc* g = &s->npcs[0];
            printf("  ep %d tick %d outcome %d phase %d kd %d enraged %d hp %d/%d shield %d/%d",
                e, s->tick, s->outcome, s->boss.guardian.phase, s->boss.guardian.knockdowns,
                s->boss.guardian.enraged, g->hp, g->max_hp, g->shield, g->max_shield);
            for (int p = 0; p < team_size; p++)
                printf(" | p%d dealt %.0f taken %.0f", p, s->players[p].damage_dealt_total, s->players[p].damage_taken_total);
            printf("\n");
        }
        ticks += s->tick;
        clears += s->outcome == TFA_OUTCOME_CLEARED;
        wipes += s->outcome == TFA_OUTCOME_WIPED;
        timeouts += s->outcome == TFA_OUTCOME_TIMEOUT;
    }
    printf("team %d: clears %d wipes %d timeouts %d mean ticks %.1f\n",
        team_size, clears, wipes, timeouts, (double)ticks / episodes);
    CHECK("every episode ends with an outcome", clears + wipes + timeouts == episodes);
    free(s);
    free(ctx);
}

int main(void) {
    for (int team = 1; team <= TFA_MAX_PLAYERS; team++) run_team(team, 50);
    return osrs_test_summary();
}
