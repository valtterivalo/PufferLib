/* Scripted raiders: attack the boss, pray by phase, eat and drink when low. */
static void tfa_heuristic_actions(TfaState* s, int p, int* acts) {
    memset(acts, 0, sizeof(int) * TFA_NUM_ACTION_HEADS);
    TfaPlayer* tp = &s->players[p];
    if (tp->life != TFA_LIFE_ALIVE) return;
    OverheadPrayer want = PRAYER_NONE;
    const TfaNpc* target = NULL;
    int hide_x = -1, hide_y = -1;
    int flee = 0;
    if (s->room == TFA_ROOM_GUARDIAN) {
        target = tfa_npc_first(s, TFA_NPC_GUARDIAN);
        want = s->boss.guardian.phase == TFA_GUARDIAN_GROUNDED ? PRAYER_PROTECT_MELEE : PRAYER_PROTECT_RANGED;
    } else if (s->room == TFA_ROOM_MOIA) {
        target = tfa_npc_first(s, TFA_NPC_MOIA);
        want = s->boss.moia.magic_land_tick == s->tick + 1 ? PRAYER_PROTECT_MAGIC : PRAYER_PROTECT_RANGED;
        int here_min = 1 << 30, best_min = -1;
        for (int i = 0; i < TFA_MAX_NPCS; i++) {
            const TfaNpc* b = &s->npcs[i];
            if (!b->active || b->kind != TFA_NPC_BINDER) continue;
            int d = chebyshev_distance(b->x, b->y, tp->player.x, tp->player.y);
            if (d < here_min) here_min = d;
        }
        if (here_min <= 3) {
            for (int a = 1; a < OSRS_PRIMARY_MOVE_ACTIONS; a++) {
                int nx = tp->player.x + ENCOUNTER_MOVE_TARGET_DX[a], ny = tp->player.y + ENCOUNTER_MOVE_TARGET_DY[a];
                if (tfa_layout_blocked(tfa_room_layout(s->room), nx, ny)) continue;
                int nmin = 1 << 30;
                for (int i = 0; i < TFA_MAX_NPCS; i++) {
                    const TfaNpc* b = &s->npcs[i];
                    if (!b->active || b->kind != TFA_NPC_BINDER) continue;
                    int d = chebyshev_distance(b->x, b->y, nx, ny);
                    if (d < nmin) nmin = d;
                }
                if (nmin > best_min) { best_min = nmin; flee = nmin > here_min ? a : 0; }
            }
        }
    } else if (s->room == TFA_ROOM_BORK) {
        const TfaBorkState* bs = &s->boss.bork;
        const TfaNpc* bork = tfa_npc_first(s, TFA_NPC_BORK);
        const TfaNpc* smorks = tfa_npc_first(s, TFA_NPC_SMORKS);
        target = bork ? bork : smorks;
        want = PRAYER_PROTECT_MELEE;
        if (smorks && bs->next_smork == s->tick + 1 && bs->next_gong != s->tick + 1) want = PRAYER_PROTECT_RANGED;
        int best = 1 << 30;
        int wave_soon = bs->next_gong - s->tick <= 3 || (bs->stomp_tick >= 0 && s->tick <= bs->stomp_tick + 11);
        if (bork && smorks && wave_soon) {
            for (int x = TFA_BORK.arena_x0; x <= TFA_BORK.arena_x1; x++)
                for (int y = TFA_BORK.arena_y0; y <= TFA_BORK.arena_y1; y++) {
                    if (tfa_layout_blocked(tfa_room_layout(s->room), x, y) || tfa_tile_danger(s, p, x, y) >= 0 || !tfa_bork_shadowed(bork, smorks, x, y)) continue;
                    int d = chebyshev_distance(x, y, tp->player.x, tp->player.y);
                    if (d < best) { best = d; hide_x = x; hide_y = y; }
                }
        }
    } else if (s->room == TFA_ROOM_XARTH) {
        const TfaXarthState* xs = &s->boss.xarth;
        target = tfa_npc_first(s, TFA_NPC_XARTH);
        want = xs->p2_started ? PRAYER_PROTECT_RANGED : PRAYER_PROTECT_MAGIC;
        int best = 1 << 30;
        for (int i = 0; i < TFA_MAX_NPCS; i++) {
            const TfaNpc* c = &s->npcs[i];
            if (!c->active || c->kind != TFA_NPC_XARTH_CORPSE) continue;
            int d = chebyshev_distance(c->x, c->y, tp->player.x, tp->player.y);
            if (d < best) { best = d; target = c; }
        }
        if (!xs->p2_started && target && target->kind == TFA_NPC_XARTH) {
            for (int i = 0; i < TFA_MAX_NPCS && hide_x < 0; i++) {
                const TfaNpc* pi = &s->npcs[i];
                if (!pi->active || pi->kind != TFA_NPC_XARTH_PILLAR || pi->npc_id != XARTH_NPC_PILLAR) continue;
                int dx = pi->x - target->x - 1, dy = pi->y - target->y - 1;
                hide_x = pi->x + (dx > 0 ? 2 : dx < 0 ? -1 : 0);
                hide_y = pi->y + (dy > 0 ? 2 : dy < 0 ? -1 : 0);
            }
        }
    }
    if (!target) return;
    if (tp->player.prayer != want)
        acts[OSRS_HEAD_OVERHEAD] = want == PRAYER_PROTECT_MELEE ? ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE
            : want == PRAYER_PROTECT_MAGIC ? ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC : ENCOUNTER_OVERHEAD_SET_REFRESH_RANGED;
    if (s->room == TFA_ROOM_BORK) {
        if (tp->player.offensive_prayer != OFFENSIVE_PRAYER_PIETY) acts[OSRS_HEAD_OFFENSIVE] = ENCOUNTER_OFFENSIVE_SET_REFRESH_PIETY;
        for (int cell = 0; cell < OSRS_INVENTORY_SIZE; cell++) {
            const OsrsItemContentMetadata* md = osrs_inventory_cell_metadata(&tp->player.inventory_cells[cell]);
            if ((md->item_idx == ITEM_INQUISITORS_MACE || md->item_idx == ITEM_AVERNIC_DEFENDER) && tp->player.equipped[md->gear_slot] != md->item_idx)
                acts[OSRS_HEAD_EQUIP_SLOT(md->gear_slot)] = cell + 1;
        }
    }
    acts[OSRS_HEAD_PRIMARY] = TFA_PRIMARY_ATTACK_BASE + tfa_npc_slot(s, target);
    if (hide_x >= 0 && (tp->player.x != hide_x || tp->player.y != hide_y) && tp->player.attack_timer > 0) {
        int best = 1 << 30;
        for (int a = 1; a < OSRS_PRIMARY_MOVE_ACTIONS; a++) {
            int nx = tp->player.x + ENCOUNTER_MOVE_TARGET_DX[a], ny = tp->player.y + ENCOUNTER_MOVE_TARGET_DY[a];
            if (tfa_layout_blocked(tfa_room_layout(s->room), nx, ny) || (s->room == TFA_ROOM_BORK && tfa_tile_danger(s, p, nx, ny) >= 0)) continue;
            int d = chebyshev_distance(nx, ny, hide_x, hide_y);
            if (d < best) { best = d; acts[OSRS_HEAD_PRIMARY] = a; }
        }
    }
    if (flee) acts[OSRS_HEAD_PRIMARY] = flee;
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
