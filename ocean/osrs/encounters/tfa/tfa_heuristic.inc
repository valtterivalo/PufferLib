static int tfa_bork_safe_tile(const TfaState* s, int p, int x, int y) {
    return tfa_bork_walkable(x, y) && tfa_tile_danger(s, p, x, y) < 0 && !(s->npcs[0].active && tfa_bork_covers(&s->npcs[0], x, y)) &&
        !(s->npcs[1].active && tfa_bork_covers(&s->npcs[1], x, y));
}

/* The move (a step or a 2-tile run target) whose tile is nearest (gx, gy) along 8-way steps over safe Bork-arena tiles, 0 when none. */
static int tfa_bork_step(const TfaState* s, int p, int gx, int gy) {
    enum { W = 15, H = 17 };
    int dist[W * H], queue[W * H], head = 0, tail = 0;
    for (int i = 0; i < W * H; i++) dist[i] = -1;
    dist[(gy - TFA_BORK.arena_y0) * W + gx - TFA_BORK.arena_x0] = 0;
    queue[tail++] = (gy - TFA_BORK.arena_y0) * W + gx - TFA_BORK.arena_x0;
    while (head < tail) {
        int c = queue[head++], cx = c % W + TFA_BORK.arena_x0, cy = c / W + TFA_BORK.arena_y0;
        for (int a = 1; a <= 8; a++) {
            int nx = cx + ENCOUNTER_MOVE_TARGET_DX[a], ny = cy + ENCOUNTER_MOVE_TARGET_DY[a];
            if (!tfa_bork_in_arena(nx, ny) || !tfa_bork_safe_tile(s, p, nx, ny)) continue;
            int n = (ny - TFA_BORK.arena_y0) * W + nx - TFA_BORK.arena_x0;
            if (dist[n] >= 0) continue;
            dist[n] = dist[c] + 1;
            queue[tail++] = n;
        }
    }
    const Player* pl = &s->players[p].player;
    int best = 1 << 30, move = 0;
    for (int a = 1; a < OSRS_PRIMARY_MOVE_ACTIONS; a++) {
        int nx = pl->x + ENCOUNTER_MOVE_TARGET_DX[a], ny = pl->y + ENCOUNTER_MOVE_TARGET_DY[a];
        if (!tfa_bork_in_arena(nx, ny)) continue;
        int d = dist[(ny - TFA_BORK.arena_y0) * W + nx - TFA_BORK.arena_x0];
        if (d >= 0 && d < best) { best = d; move = a; }
    }
    return move;
}

/* Scripted raiders: attack the boss, pray by phase, eat and drink when low. */
static void tfa_heuristic_actions(TfaState* s, int p, int* acts) {
    memset(acts, 0, sizeof(int) * TFA_NUM_ACTION_HEADS);
    TfaPlayer* tp = &s->players[p];
    if (tp->life != TFA_LIFE_ALIVE) return;
    OverheadPrayer want = PRAYER_NONE;
    const TfaNpc* target = NULL;
    int hide_x = -1, hide_y = -1, flee_x = -1, flee_y = -1, go_x = -1, go_y = -1;
    int flee = 0;
    if (s->room == TFA_ROOM_GUARDIAN) {
        const TfaGuardianState* gs = &s->boss.guardian;
        target = tfa_npc_first(s, TFA_NPC_GUARDIAN);
        want = gs->phase == TFA_GUARDIAN_GROUNDED ? PRAYER_PROTECT_MELEE : PRAYER_PROTECT_RANGED;
        /* Alone, P0 flicks under the flying Guardian for the slam it can dodge: in on the tick before a ranged slot, out on the slot itself (the recorded recipe,
           4/4 in the solo fight; team fights show one flick each). */
        int slot_is_ranged = gs->phase == TFA_GUARDIAN_FLYING && !gs->enraged && gs->shield_broke_tick == 0 &&
            gs->special_timer > TFA_GUARDIAN.ranged_interval;
        if (s->team_size == 1 && target && slot_is_ranged && (gs->ranged_timer == 2 || gs->ranged_timer == 1)) {
            go_x = target->x + 2;
            go_y = gs->ranged_timer == 2 ? target->y : target->y - 1;
        }
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
        /* P0 barraged jellies whenever one was up and Dreadnoth otherwise (449 casts, 62 crossbow shots), the bow raiders only close jellies. */
        int best = target && target->attackable && tp->kit == TFA_KIT_BOW ? 4 : 1 << 30;
        for (int i = 0; i < TFA_NUM_TARGET_SLOTS; i++) {
            const TfaNpc* j = &s->npcs[i];
            if (!j->active || j->kind != TFA_NPC_JELLY || !j->attackable) continue;
            int d = chebyshev_distance(j->x, j->y, tp->player.x, tp->player.y);
            if (d < best) { best = d; target = j; }
        }
        const TfaNpc* boss = tfa_npc_first(s, TFA_NPC_DREADNOTH);
        if (boss && encounter_rect_distance(tp->player.x, tp->player.y, 1, boss->x, boss->y, boss->size) <= 1)
            flee_x = boss->x, flee_y = boss->y;
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
        int t = s->boss.moia.clock - TFA_MOIA.first_attack;
        int soon = 0;
        for (int k = 1; k <= 2; k++) {
            int ph = (t + k) % TFA_MOIA.binder_cycle;
            soon |= t + k >= TFA_MOIA.binder_attack_first &&
                (ph == TFA_MOIA.binder_attack_phase_a || ph == TFA_MOIA.binder_attack_phase_b);
        }
        if (soon && here_min <= TFA_MOIA.binder_reach) {
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
        /* The recorded two-tank play (bork_02, Talk:Bork): the tank stands next to Bork praying Melee, the rest stack 1 tile beyond the tank on the
           side of the Smorks and pray against the soonest landing volley. When the bomb flies the tank steps 2 back so Bork walks under it and the
           stack steps 2 aside; the stun is free damage; everyone hides behind the Smorks for the stomp. */
        const TfaBorkState* bs = &s->boss.bork;
        const TfaNpc* bork = tfa_npc_first(s, TFA_NPC_BORK);
        const TfaNpc* smorks = tfa_npc_first(s, TFA_NPC_SMORKS);
        target = bork ? bork : smorks;
        int tank = bork && p == bs->tank;
        want = tank ? PRAYER_PROTECT_MELEE : PRAYER_PROTECT_RANGED;
        int soonest = 1 << 30;
        for (int h = 0; h < tp->incoming.count; h++) {
            const TfaIncomingHit* hit = &tp->incoming.hits[h];
            if (hit->ticks_remaining < 2 || hit->ticks_remaining >= soonest) continue;
            soonest = hit->ticks_remaining;
            want = hit->attack_style == ATTACK_STYLE_MAGIC ? PRAYER_PROTECT_MAGIC : PRAYER_PROTECT_RANGED;
        }
        if (smorks) {
            int stomp_soon = bork && bs->stomp_tick >= 0 && s->tick >= bs->stomp_tick - 6 && s->tick <= bs->stomp_tick + TFA_BORK.stomp_pulse_first +
                (TFA_BORK.stomp_pulses - 1) * TFA_BORK.stomp_pulse_gap;
            int land_x = -1, land_y = -1;
            for (int i = 0; bork && i < bs->pending_count; i++)
                if (bs->pending[i].kind == TFA_BORK_PEND_BOMB_LAND && !tfa_bork_covers(bork, bs->pending[i].x, bs->pending[i].y))
                    land_x = bs->pending[i].x, land_y = bs->pending[i].y;
            int want_x = tp->player.x, want_y = tp->player.y, cx2 = 0, cy2 = 0;
            if (bork) {
                const Player* tk = &s->players[bs->tank].player;
                cx2 = 2 * bork->x + bork->size, cy2 = 2 * bork->y + bork->size;
                int tx2 = 2 * tk->x + 1 - cx2, ty2 = 2 * tk->y + 1 - cy2;
                int ux = abs(tx2) >= abs(ty2) ? (tx2 > 0) - (tx2 < 0) : 0, uy = ux ? 0 : (ty2 > 0) - (ty2 < 0);
                want_x = tank ? tk->x : tk->x + ux;
                want_y = tank ? tk->y : tk->y + uy;
            }
            if (land_x >= 0) {
                int lx2 = 2 * land_x + 1 - cx2, ly2 = 2 * land_y + 1 - cy2;
                int bx = abs(lx2) >= abs(ly2) ? (lx2 > 0) - (lx2 < 0) : 0, by = bx ? 0 : (ly2 > 0) - (ly2 < 0);
                want_x = tank ? land_x + bx : land_x + 2 * by;
                want_y = tank ? land_y + by : land_y + 2 * bx;
            }
            int best = 1 << 30;
            for (int x = TFA_BORK.arena_x0; x <= TFA_BORK.arena_x1; x++)
                for (int y = TFA_BORK.arena_y0; y <= TFA_BORK.arena_y1; y++) {
                    if (!tfa_bork_safe_tile(s, p, x, y)) continue;
                    if (stomp_soon && !tfa_bork_shadowed(bork->x, bork->y, smorks, x, y)) continue;
                    if (bork && !stomp_soon && !tank && !bs->stunned && encounter_rect_distance(x, y, 1, bork->x, bork->y, bork->size) <= 1) continue;
                    int landing = 0;
                    for (int i = 0; i < bs->pending_count && !landing; i++) {
                        const TfaBorkPending* e = &bs->pending[i];
                        int napalm = e->kind == TFA_BORK_PEND_NAPALM_LAND;
                        if (!napalm && e->kind != TFA_BORK_PEND_BOMB_LAND) continue;
                        landing = x - e->x >= (napalm ? -3 : -1) && x - e->x <= 1 && y - e->y >= (napalm ? -2 : -1) && y - e->y <= (napalm ? 3 : 1);
                    }
                    if (landing) continue;
                    if (!stomp_soon && tank && land_x < 0 && encounter_rect_distance(x, y, 1, bork->x, bork->y, bork->size) != 1) continue;
                    int d = stomp_soon ? chebyshev_distance(x, y, tp->player.x, tp->player.y) : chebyshev_distance(x, y, want_x, want_y);
                    if (d < best) { best = d; go_x = x; go_y = y; }
                }
            if (bs->stunned && !stomp_soon && go_x == tp->player.x && go_y == tp->player.y) go_x = go_y = -1;
        }
    } else if (s->room == TFA_ROOM_EZEKIEL) {
        const TfaNpc* boss = tfa_npc_first(s, TFA_NPC_EZEKIEL);
        target = boss;
        want = PRAYER_PROTECT_MELEE;
        for (int i = 0; i < TFA_NUM_TARGET_SLOTS; i++) {
            const TfaNpc* w = &s->npcs[i];
            if (!w->active || w->kind != TFA_NPC_EZEKIEL_SWORD || w->hp >= TFA_EZEKIEL.sword_charge_cap || w->state_ticks <= 0) continue;
            int line = w->state ? w->y : w->x;
            if ((w->state ? boss->y : boss->x) == line) { target = w; break; }
        }
    } else if (s->room == TFA_ROOM_OCELLUS) {
        const TfaOcellusState* os = &s->boss.ocellus;
        want = (OverheadPrayer)os->want_prayer;
        target = s->npcs[0].attackable ? &s->npcs[0] : NULL;
        for (int i = 0; i < TFA_NUM_TARGET_SLOTS; i++) {
            const TfaNpc* c = &s->npcs[i];
            if (!c->active) continue;
            if (c->kind == TFA_NPC_OCELLUS_PORTAL && oc_quadrant(tp->player.x, tp->player.y) == c->state) go_x = c->x, go_y = c->y;
            int add = c->kind == TFA_NPC_VOID_SIPHON || c->kind == TFA_NPC_VOID_AUGMENTER || c->kind == TFA_NPC_VOID_RIFT ||
                (c->kind == TFA_NPC_VOID_VINE && c->target != p);
            if (c->kind == TFA_NPC_OCELLUS_BLADE || (add && c->attackable)) {
                if (!target || target->kind == TFA_NPC_OCELLUS || c->kind != TFA_NPC_OCELLUS_BLADE) target = c;
            }
        }
        const TfaNpc* boss = &s->npcs[0];
        if (p == 0 && os->phase >= TFA_OCELLUS_P3 && boss->attackable && go_x < 0) {
            go_x = boss->x - 1;
            go_y = boss->y + 1;
            if (tfa_layout_blocked(tfa_room_layout(s->room), go_x, go_y) || oc_quadrant(go_x, go_y) != oc_quadrant(boss->x + 1, boss->y + 1)) go_x = boss->x + 3;
        }
        /* The Untether slam: inside his 3x3 for the S check of who is outside (decided by S-2, moved before S), out for the S+1 check of who is inside. */
        if (os->slam_tick > 0 && s->tick >= os->slam_tick - 5 && s->tick <= os->slam_tick - 1) {
            int best = 1 << 30, inside = s->tick <= os->slam_tick - 2;
            for (int x = boss->x - 1; x <= boss->x + boss->size; x++)
                for (int y = boss->y - 1; y <= boss->y + boss->size; y++) {
                    int under = x >= boss->x && x < boss->x + boss->size && y >= boss->y && y < boss->y + boss->size;
                    if (under != inside || tfa_layout_blocked(tfa_room_layout(s->room), x, y) || oc_quadrant(x, y) != oc_quadrant(boss->x + 1, boss->y + 1)) continue;
                    int d = chebyshev_distance(x, y, tp->player.x, tp->player.y);
                    if (d < best) { best = d; go_x = x; go_y = y; }
                }
        }
        /* No hits that would land into his soul split: hold for the 4 ticks before it as well. */
        int split_soon = os->phase == TFA_OCELLUS_P4 && (os->p4_step == OC4_QUAD_A || os->p4_step == OC4_QUAD_B) && os->next_attack_tick - s->tick <= 4;
        if ((os->deflect == ATTACK_STYLE_RANGED || os->soul_split || split_soon) && target && target->kind == TFA_NPC_OCELLUS) target = NULL;
        if (!target) {
            if (tp->player.prayer != want && want == PRAYER_NONE) acts[OSRS_HEAD_OVERHEAD] = ENCOUNTER_OVERHEAD_OFF;
            if (osrs_interaction_active(&tp->player.interaction)) {
                for (int a = 1; a < OSRS_PRIMARY_MOVE_ACTIONS && !acts[OSRS_HEAD_PRIMARY]; a++)
                    if (!tfa_layout_blocked(tfa_room_layout(s->room), tp->player.x + ENCOUNTER_MOVE_TARGET_DX[a], tp->player.y + ENCOUNTER_MOVE_TARGET_DY[a]))
                        acts[OSRS_HEAD_PRIMARY] = a;
            }
            return;
        }
    } else if (s->room == TFA_ROOM_XARTH) {
        const TfaXarthState* xs = &s->boss.xarth;
        target = tfa_npc_first(s, TFA_NPC_XARTH);
        want = xs->p2_started ? PRAYER_PROTECT_RANGED : PRAYER_PROTECT_MAGIC;
        /* Corpses are barraged as they walk in (1307 recorded hits on corpses, 135 of 139 killed before reaching Xarth). */
        const TfaNpc* boss = target;
        int best = 1 << 30;
        for (int i = 0; i < TFA_MAX_NPCS && boss; i++) {
            const TfaNpc* c = &s->npcs[i];
            if (!c->active || c->kind != TFA_NPC_XARTH_CORPSE) continue;
            int d = encounter_rect_distance(c->x, c->y, 1, boss->x, boss->y, boss->size);
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
    } else if (s->room == TFA_ROOM_DAGONHAI) {
        /* Provisional until a recording: barrage a channeller and the imps, shoot the Elder, pray at the soonest landing hit, and the raider
           nearest the gust tile blocks the tome. */
        const TfaDagonhaiState* ds = &s->boss.dagonhai;
        want = PRAYER_PROTECT_MAGIC;
        int soonest = 1 << 30;
        for (int h = 0; h < tp->incoming.count; h++) {
            const TfaIncomingHit* hit = &tp->incoming.hits[h];
            if (hit->ticks_remaining >= soonest || hit->damage == 0) continue;
            soonest = hit->ticks_remaining;
            want = hit->attack_style == ATTACK_STYLE_RANGED ? PRAYER_PROTECT_RANGED
                : hit->attack_style == ATTACK_STYLE_MELEE ? PRAYER_PROTECT_MELEE : PRAYER_PROTECT_MAGIC;
        }
        for (int i = 0; i < 3 && !target; i++)
            if (s->npcs[i].active && s->npcs[i].attackable) target = &s->npcs[i];
        if (ds->caster_slot >= 0) target = &s->npcs[ds->caster_slot];
        int best = 1 << 30;
        for (int i = 0; i < TFA_NUM_TARGET_SLOTS && ds->phase == TFA_DAGONHAI_IMPS; i++) {
            const TfaNpc* imp = &s->npcs[i];
            if (!imp->active || imp->kind != TFA_NPC_IMP) continue;
            int d = chebyshev_distance(imp->x, imp->y, tp->player.x, tp->player.y);
            if (d < best) { best = d; target = imp; }
        }
        if (ds->gust_slot >= 0) {
            const TfaNpc* g = &s->npcs[ds->gust_slot];
            int nearest = -1, nd = 1 << 30;
            for (int q = 0; q < s->team_size; q++) {
                if (!tfa_alive(s, q)) continue;
                int d = chebyshev_distance(g->x, g->y, s->players[q].player.x, s->players[q].player.y);
                if (d < nd) { nd = d; nearest = q; }
            }
            if (nearest == p) go_x = g->x, go_y = g->y;
        }
    }
    if (!target) return;
    if (tp->player.prayer != want)
        acts[OSRS_HEAD_OVERHEAD] = want == PRAYER_NONE ? ENCOUNTER_OVERHEAD_OFF : want == PRAYER_PROTECT_MELEE ? ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE
            : want == PRAYER_PROTECT_MAGIC ? ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC : ENCOUNTER_OVERHEAD_SET_REFRESH_RANGED;
    /* Weapons per room from the recordings: P0 meleed Ezekiel (1043 Soulreaper attacks) and Ocellus phase 3 (80 Soulreaper hits to 37 ranged in runs 4
       and 5, full damage: he does not protect from melee), blowpiped the Guardian (364 of 852
       attacks) and the Blade (44 of 44 hits in the two floored runs), crossbowed every other boss and barraged corpses and jellies (577 casts); the bow raiders shot everything but Ezekiel (no
       arrows there). Openers: the Zaryte special (75%) on the first crossbow shot, two Dragon warhammer specials on Ezekiel, two Tonalztics
       specials six ticks apart thrown from one tile (Guardian 4/4 fights, Xarth 3/3) by one bow raider, who throws another whenever 50% energy
       is back (2727 again at +126 to +256). Bork (one fight, 7 Soulreaper swings while P0 also carried the cannon remains) stays ranged. */
    int crossbow = tp->kit == TFA_KIT_CROSSBOW;
    int barrage = target->kind == TFA_NPC_XARTH_CORPSE || target->kind == TFA_NPC_JELLY || target->kind == TFA_NPC_IMP ||
        (crossbow && s->room == TFA_ROOM_DREADNOTH) || (s->room == TFA_ROOM_DAGONHAI && tfa_npc_slot(s, target) == s->boss.dagonhai.caster_slot);
    int p3_melee = crossbow && s->room == TFA_ROOM_OCELLUS && target->kind == TFA_NPC_OCELLUS && s->boss.ocellus.phase >= TFA_OCELLUS_P3;
    AttackStyle stance = barrage ? ATTACK_STYLE_MAGIC
        : s->room == TFA_ROOM_EZEKIEL || p3_melee ? ATTACK_STYLE_MELEE
        : ATTACK_STYLE_RANGED;
    uint16_t spec_weapon = ITEM_NONE;
    if (stance == ATTACK_STYLE_MELEE && crossbow && tp->player.special_energy >= 50) spec_weapon = ITEM_DRAGON_WARHAMMER;
    if (stance == ATTACK_STYLE_RANGED && crossbow && tp->player.special_energy == 100) spec_weapon = ITEM_ZARYTE_CROSSBOW;
    if (stance == ATTACK_STYLE_RANGED && p == 1 && tp->player.special_energy >= 50) spec_weapon = ITEM_TONALZTICS_OF_RALOS;
    uint16_t wanted[NUM_GEAR_SLOTS];
    memset(wanted, ITEM_NONE, sizeof(wanted));
    if (stance == ATTACK_STYLE_RANGED) {
        memcpy(wanted, TFA_KIT_WORN[tp->kit], sizeof(wanted));
        if (crossbow && (s->room == TFA_ROOM_GUARDIAN || target->kind == TFA_NPC_OCELLUS_BLADE)) {
            wanted[GEAR_SLOT_WEAPON] = ITEM_TOXIC_BLOWPIPE;
            wanted[GEAR_SLOT_SHIELD] = ITEM_NONE;
        }
    } else if (stance == ATTACK_STYLE_MAGIC) {
        wanted[GEAR_SLOT_WEAPON] = ITEM_ICE_ANCIENT_SCEPTRE_L;
        wanted[GEAR_SLOT_BODY] = ITEM_ANCESTRAL_TOP;
        wanted[GEAR_SLOT_LEGS] = ITEM_ANCESTRAL_BOTTOM;
    } else {
        wanted[GEAR_SLOT_WEAPON] = ITEM_SOULREAPER_AXE;
        wanted[GEAR_SLOT_CAPE] = ITEM_INFERNAL_CAPE;
        wanted[GEAR_SLOT_NECK] = ITEM_AMULET_OF_RANCOUR;
        if (spec_weapon != ITEM_NONE) wanted[GEAR_SLOT_SHIELD] = ITEM_AVERNIC_DEFENDER;
    }
    if (spec_weapon != ITEM_NONE) wanted[GEAR_SLOT_WEAPON] = spec_weapon;
    OffensivePrayer offensive = encounter_offensive_prayer_for_style(stance);
    if (tp->player.offensive_prayer != offensive)
        acts[OSRS_HEAD_OFFENSIVE] = stance == ATTACK_STYLE_MELEE ? ENCOUNTER_OFFENSIVE_SET_REFRESH_PIETY
            : stance == ATTACK_STYLE_MAGIC ? ENCOUNTER_OFFENSIVE_SET_REFRESH_AUGURY : ENCOUNTER_OFFENSIVE_SET_REFRESH_RIGOUR;
    for (int cell = 0; cell < OSRS_INVENTORY_SIZE; cell++) {
        const OsrsItemContentMetadata* md = osrs_inventory_cell_metadata(&tp->player.inventory_cells[cell]);
        if (md->gear_slot >= 0 && md->item_idx != ITEM_NONE && wanted[md->gear_slot] == md->item_idx && !acts[OSRS_HEAD_EQUIP_SLOT(md->gear_slot)])
            acts[OSRS_HEAD_EQUIP_SLOT(md->gear_slot)] = cell + 1;
    }
    if (spec_weapon != ITEM_NONE && tp->player.equipped[GEAR_SLOT_WEAPON] == spec_weapon && !tp->player.spec_armed)
        acts[OSRS_HEAD_SPECIAL] = 1;
    if (barrage) acts[OSRS_HEAD_SPELL] = OSRS_SPELL_ICE_BARRAGE;
    /* A special opens the fight: no attack until the special weapon is worn (equipping disarms the bar). */
    if (spec_weapon == ITEM_NONE || tp->player.equipped[GEAR_SLOT_WEAPON] == spec_weapon)
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
    if (s->room == TFA_ROOM_BORK && go_x >= 0 && (tp->player.x != go_x || tp->player.y != go_y)) {
        int move = tfa_bork_step(s, p, go_x, go_y);
        if (move) acts[OSRS_HEAD_PRIMARY] = move;
    } else if (go_x >= 0 && (tp->player.x != go_x || tp->player.y != go_y)) {
        int best = 1 << 30;
        for (int a = 1; a < OSRS_PRIMARY_MOVE_ACTIONS; a++) {
            int nx = tp->player.x + ENCOUNTER_MOVE_TARGET_DX[a], ny = tp->player.y + ENCOUNTER_MOVE_TARGET_DY[a];
            if (tfa_layout_blocked(tfa_room_layout(s->room), nx, ny)) continue;
            int d = chebyshev_distance(nx, ny, go_x, go_y);
            if (d < best) { best = d; acts[OSRS_HEAD_PRIMARY] = a; }
        }
    }
    if (flee) acts[OSRS_HEAD_PRIMARY] = flee;
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
    int react = s->room == TFA_ROOM_EZEKIEL ? 3 : s->room == TFA_ROOM_OCELLUS ? 4 : 1;
    if (here >= 0 && here <= react) {
        const TfaRoomLayout* layout = tfa_room_layout(s->room);
        int safe[289][2], safe_count = 0;
        for (int dx = -8; dx <= 8; dx++)
            for (int dy = -8; dy <= 8; dy++) {
                int tx = tp->player.x + dx, ty = tp->player.y + dy;
                if (!tfa_layout_blocked(layout, tx, ty) && tfa_tile_danger(s, p, tx, ty) < 0 &&
                        !(s->room == TFA_ROOM_GUARDIAN && target->kind == TFA_NPC_GUARDIAN && tfa_guardian_covers(target, tx, ty)) &&
                        !(s->room == TFA_ROOM_BORK && !tfa_bork_safe_tile(s, p, tx, ty))) {
                    safe[safe_count][0] = tx;
                    safe[safe_count++][1] = ty;
                }
            }
        int best = 1 << 30;
        for (int a = 1; a < OSRS_PRIMARY_MOVE_ACTIONS; a++) {
            int nx = tp->player.x + ENCOUNTER_MOVE_TARGET_DX[a], ny = tp->player.y + ENCOUNTER_MOVE_TARGET_DY[a];
            if (tfa_layout_blocked(layout, nx, ny)) continue;
            for (int k = 0; k < safe_count; k++) {
                int d = chebyshev_distance(nx, ny, safe[k][0], safe[k][1]);
                if (d < best) { best = d; acts[OSRS_HEAD_PRIMARY] = a; }
            }
        }
    }
    /* Restores also bring Ranged back from the brews: P0's boosted Ranged sat at 99 (p10 78-99) through every recorded fight. */
    for (int cell = 0; cell < OSRS_INVENTORY_SIZE; cell++) {
        OsrsInventoryClickResolution r =
            osrs_inventory_cell_click_interpret(&tp->player.inventory_cells[cell], OSRS_CLICK_TICK_FIRST);
        if (r.click_action == OSRS_CLICK_EAT && tp->player.current_hitpoints < 60 && !acts[OSRS_HEAD_EAT] &&
                osrs_can_eat_consumable_kind(&tp->player, r.consumable_kind))
            acts[OSRS_HEAD_EAT] = cell + 1;
        if (r.click_action == OSRS_CLICK_DRINK && tp->player.potion_timer == 0 && !acts[OSRS_HEAD_DRINK] &&
                ((r.consumable_kind == OSRS_CONSUMABLE_SUPER_RESTORE && (tp->player.current_prayer < 30 || tp->player.current_ranged < 90)) ||
                 (r.consumable_kind == OSRS_CONSUMABLE_BREW && tp->player.current_hitpoints < 70)))
            acts[OSRS_HEAD_DRINK] = cell + 1;
    }
}
