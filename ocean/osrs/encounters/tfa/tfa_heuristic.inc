/* Scripted raiders: attack the boss, pray by phase, eat and drink when low. */
static void tfa_heuristic_actions(TfaState* s, int p, int* acts) {
    memset(acts, 0, sizeof(int) * TFA_NUM_ACTION_HEADS);
    TfaPlayer* tp = &s->players[p];
    if (tp->life != TFA_LIFE_ALIVE) return;
    OverheadPrayer want = PRAYER_NONE;
    const TfaNpc* target = NULL;
    int hide_x = -1, hide_y = -1, flee_x = -1, flee_y = -1;
    if (s->room == TFA_ROOM_GUARDIAN) {
        target = tfa_npc_first(s, TFA_NPC_GUARDIAN);
        want = s->boss.guardian.phase == TFA_GUARDIAN_GROUNDED ? PRAYER_PROTECT_MELEE : PRAYER_PROTECT_RANGED;
    } else if (s->room == TFA_ROOM_DREADNOTH) {
        target = tfa_npc_first(s, TFA_NPC_DREADNOTH);
        want = PRAYER_PROTECT_MELEE;
        int soonest = 1 << 30;
        for (int h = 0; h < tp->incoming.count; h++) {
            const TfaIncomingHit* hit = &tp->incoming.hits[h];
            if (hit->ticks_remaining < 2 || hit->ticks_remaining >= soonest || hit->damage == 0) continue;
            soonest = hit->ticks_remaining;
            want = hit->attack_style == ATTACK_STYLE_RANGED ? PRAYER_PROTECT_RANGED : PRAYER_PROTECT_MELEE;
        }
        int best = target && target->attackable ? 4 : 1 << 30;
        for (int i = 1; i < TFA_NUM_TARGET_SLOTS; i++) {
            const TfaNpc* j = &s->npcs[i];
            if (!j->active || j->kind != TFA_NPC_JELLY || !j->attackable) continue;
            int d = chebyshev_distance(j->x, j->y, tp->player.x, tp->player.y);
            if (d < best) { best = d; target = j; }
        }
        const TfaNpc* boss = tfa_npc_first(s, TFA_NPC_DREADNOTH);
        if (boss && encounter_rect_distance(tp->player.x, tp->player.y, 1, boss->x, boss->y, boss->size) <= 1)
            flee_x = boss->x, flee_y = boss->y;
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
    acts[OSRS_HEAD_PRIMARY] = TFA_PRIMARY_ATTACK_BASE + tfa_npc_slot(s, target);
    if (hide_x >= 0 && (tp->player.x != hide_x || tp->player.y != hide_y) && tp->player.attack_timer > 0) {
        int best = 1 << 30;
        for (int a = 1; a < OSRS_PRIMARY_MOVE_ACTIONS; a++) {
            int nx = tp->player.x + ENCOUNTER_MOVE_TARGET_DX[a], ny = tp->player.y + ENCOUNTER_MOVE_TARGET_DY[a];
            if (tfa_layout_blocked(tfa_room_layout(s->room), nx, ny)) continue;
            int d = chebyshev_distance(nx, ny, hide_x, hide_y);
            if (d < best) { best = d; acts[OSRS_HEAD_PRIMARY] = a; }
        }
    }
    if (flee_x >= 0) {
        int best = encounter_rect_distance(tp->player.x, tp->player.y, 1, flee_x, flee_y, 5);
        for (int a = 1; a < OSRS_PRIMARY_MOVE_ACTIONS; a++) {
            int nx = tp->player.x + ENCOUNTER_MOVE_TARGET_DX[a], ny = tp->player.y + ENCOUNTER_MOVE_TARGET_DY[a];
            if (tfa_layout_blocked(tfa_room_layout(s->room), nx, ny) || tfa_tile_danger(s, p, nx, ny) >= 0) continue;
            int d = encounter_rect_distance(nx, ny, 1, flee_x, flee_y, 5);
            if (d > best) { best = d; acts[OSRS_HEAD_PRIMARY] = a; }
        }
    }
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
