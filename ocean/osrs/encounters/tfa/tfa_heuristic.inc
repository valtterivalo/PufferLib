/* Scripted raiders: attack the boss, pray by phase, eat and drink when low. */
static void tfa_heuristic_actions(TfaState* s, int p, int* acts) {
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
