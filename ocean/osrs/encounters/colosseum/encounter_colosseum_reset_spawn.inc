#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_reset_spawn.inc
 * @brief Episode setup + wave spawning, mirroring inferno reset_spawn.
 */

static int col_grid_index(int x, int y, int* gx, int* gy) {
    *gx = x - COLO_ARENA_MIN_X;
    *gy = y - COLO_ARENA_MIN_Y;
    return *gx >= 0 && *gx < COLO_ARENA_WIDTH && *gy >= 0 && *gy < COLO_ARENA_HEIGHT;
}

static int col_npc_effective_size(const ColoNPC* npc) {
    return npc->size > 0 ? npc->size : COLO_NPC_STATS[npc->type].size;
}

static void col_stamp_npc_collision_footprint(ColosseumState* s, int x, int y, int size, uint8_t v) {
    for (int dx = 0; dx < size; dx++) {
        for (int dy = 0; dy < size; dy++) {
            int gx, gy;
            if (col_grid_index(x + dx, y + dy, &gx, &gy))
                s->npc_collision_flags[gx][gy] = v;
        }
    }
}

static void col_rebuild_player_collision_flags(ColosseumState* s) {
    memset(s->player_collision_flags, 0, sizeof(s->player_collision_flags));
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        ColoNPC* npc = &s->npcs[i];
        if (!npc->active) continue;
        /* hazard entities never block the player (bees damage from BENEATH). */
        if (col_type_is_hazard_entity(npc->type)) continue;
        int size = col_npc_effective_size(npc);
        for (int dx = 0; dx < size; dx++) {
            for (int dy = 0; dy < size; dy++) {
                int gx, gy;
                if (col_grid_index(npc->x + dx, npc->y + dy, &gx, &gy))
                    s->player_collision_flags[gx][gy] = 1;
            }
        }
    }
}

/** find a free NPC slot, return index or -1. */
static int col_find_free_npc(ColosseumState* s) {
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        if (!s->npcs[i].active) return i;
    }
    return -1;
}

static void col_reset_current_wave_score_progress(ColosseumState* s) {
    s->current_wave_kills = 0;
    s->current_wave_total_killable = 0;
    s->current_wave_kill_progress = 0.0f;
}

static void col_record_score_spawn(ColosseumState* s, ColoNpcType type) {
    if (s->wave == COLO_WAVE_BOSS) return;
    if (col_type_is_hazard_entity(type)) return;
    s->current_wave_total_killable++;
}

static void col_deactivate_npc(ColosseumState* s, int idx) {
    ColoNPC* npc = &s->npcs[idx];
    /* hazard entities never stamped collision; unstamping would erase a real
       NPC's overlapping footprint. */
    if (!col_type_is_hazard_entity(npc->type)) {
        int size = col_npc_effective_size(npc);
        col_stamp_npc_collision_footprint(s, npc->x, npc->y, size, 0);
    }
    npc->active = 0;
}

/** Roll the persistent manticore cycle once at spawn. */
static void col_roll_manticore_fixed_orb_cycle(ColosseumState* s, ColoManticoreState* mc) {
    int mantimayhem = s->modifiers.tier[COLO_MOD_MANTIMAYHEM];
    if ((s->modifiers.active_mask & (1u << COLO_MOD_MANTIMAYHEM)) == 0) {
        mantimayhem = 0;
    }
    if (mantimayhem >= 3) {
        mc->fixed_orb_style[0] = ATTACK_STYLE_RANGED;
        mc->fixed_orb_style[1] = ATTACK_STYLE_MAGIC;
        mc->fixed_orb_style[2] = ATTACK_STYLE_MELEE;
        for (int o = 2; o > 0; o--) {
            int j = encounter_rand_int(&s->rng_state, o + 1);
            AttackStyle tmp = mc->fixed_orb_style[o];
            mc->fixed_orb_style[o] = mc->fixed_orb_style[j];
            mc->fixed_orb_style[j] = tmp;
        }
    } else if (encounter_rand_int(&s->rng_state, 2) == 0) {
        mc->fixed_orb_style[0] = ATTACK_STYLE_RANGED;
        mc->fixed_orb_style[1] = ATTACK_STYLE_MAGIC;
        mc->fixed_orb_style[2] = ATTACK_STYLE_MELEE;
    } else {
        mc->fixed_orb_style[0] = ATTACK_STYLE_MAGIC;
        mc->fixed_orb_style[1] = ATTACK_STYLE_RANGED;
        mc->fixed_orb_style[2] = ATTACK_STYLE_MELEE;
    }
}

/** initialize a fresh NPC of `type` at (x, y) from the placeholder stat table. */
static void col_init_npc(ColosseumState* s, int idx, ColoNpcType type, int x, int y) {
    ColoNPC* npc = &s->npcs[idx];
    memset(npc, 0, sizeof(*npc));
    npc->type = type;
    colo_npc_init_type_state(npc);
    if (type == COLO_MANTICORE) {
        col_roll_manticore_fixed_orb_cycle(s, colo_npc_manticore(npc));
    }
    const ColoNpcStats* st = &COLO_NPC_STATS[type];
    npc->x = x;
    npc->y = y;
    npc->hp = st->hp;
    npc->max_hp = st->hp;
    npc->size = st->size;
    npc->render_id = ++s->next_npc_render_id;
    npc->attack_timer = st->attack_speed;
    npc->attack_style = st->default_style;
    npc->active = 1;
    npc->target_x = x;
    npc->target_y = y;
    npc->stun_timer = st->stun_on_spawn;
    npc->aggro_target = -1;
    npc->attack_visual_target = -1;
    if (!col_type_is_hazard_entity(type) && col_npc_effective_size(npc) > 0)
        col_stamp_npc_collision_footprint(s, x, y, col_npc_effective_size(npc), 1);
}

/** spawn an NPC of `type` with its SW tile at (x, y); asserts a free slot
    instead of silently dropping (A29). Returns the slot. */
static int col_spawn_npc_at(ColosseumState* s, ColoNpcType type, int x, int y) {
    int slot = col_find_free_npc(s);
    assert(slot >= 0 && "colosseum NPC slots exhausted");
    col_init_npc(s, slot, type, x, y);
    col_record_score_spawn(s, type);
    return slot;
}

/** B3: true if any tile of a size x size footprint at SW (x, y) lies within
    Chebyshev COLO_SPAWN_EXCLUSION_CHEB of the player's tile. */
static int col_spawn_excluded_near_player(const ColosseumState* s, int x, int y, int size) {
    return encounter_entity_footprint_distance(
        s->player.x, s->player.y, 1, x, y, size) <= COLO_SPAWN_EXCLUSION_CHEB;
}

/** a size x size footprint at SW (x, y) is spawnable: off the static mask,
    inside the boss clamp when active, and not overlapping NPCs or the player. */
static int col_spawn_footprint_free(const ColosseumState* s, int x, int y, int size) {
    for (int dx = 0; dx < size; dx++) {
        for (int dy = 0; dy < size; dy++) {
            int tx = x + dx, ty = y + dy;
            if (col_static_blocked(tx, ty)) return 0;
            if (col_sol_clamp_active(s) && !col_in_boss_arena(s, tx, ty)) return 0;
            int gx, gy;
            if (!col_grid_index(tx, ty, &gx, &gy)) return 0;
            if (s->npc_collision_flags[gx][gy]) return 0;
            if (tx == s->player.x && ty == s->player.y) return 0;
        }
    }
    return 1;
}

/** D4: candidate anchors = zones fully spawnable AND outside the B3
    player-proximity exclusion; the caller draws uniformly without replacement. */
static int col_spawn_anchor_candidates(const ColosseumState* s, int* out) {
    int n = 0;
    for (int a = 0; a < COLO_NUM_SPAWN_ANCHORS; a++) {
        int ax = COLO_SPAWN_ANCHORS[a][0], ay = COLO_SPAWN_ANCHORS[a][1];
        if (col_spawn_excluded_near_player(s, ax, ay, COLO_SPAWN_ZONE_SIZE)) continue;
        if (!col_spawn_footprint_free(s, ax, ay, COLO_SPAWN_ZONE_SIZE)) continue;
        out[n++] = a;
    }
    return n;
}

/** uniform free tile inside the warband centre box (B2(d)): rejection-sample,
    then fall back to a deterministic scan; asserts the box is not full. */
static void col_draw_warband_box_tile(ColosseumState* s, int* ox, int* oy) {
    int span_x = COLO_WARBAND_BOX_MAX_X - COLO_WARBAND_BOX_MIN_X + 1;
    int span_y = COLO_WARBAND_BOX_MAX_Y - COLO_WARBAND_BOX_MIN_Y + 1;
    for (int t = 0; t < 32; t++) {
        int x = COLO_WARBAND_BOX_MIN_X + encounter_rand_int(&s->rng_state, span_x);
        int y = COLO_WARBAND_BOX_MIN_Y + encounter_rand_int(&s->rng_state, span_y);
        if (col_spawn_footprint_free(s, x, y, 1)) { *ox = x; *oy = y; return; }
    }
    for (int y = COLO_WARBAND_BOX_MIN_Y; y <= COLO_WARBAND_BOX_MAX_Y; y++)
        for (int x = COLO_WARBAND_BOX_MIN_X; x <= COLO_WARBAND_BOX_MAX_X; x++)
            if (col_spawn_footprint_free(s, x, y, 1)) { *ox = x; *oy = y; return; }
    assert(0 && "warband centre box full");
}

/** place a 1x1 warbander on the first free tile adjacent to (ax, ay)
    (deterministic offsets per the B2(d) note); falls back to a box tile. */
static void col_spawn_warbander_adjacent(ColosseumState* s, ColoNpcType type, int ax, int ay) {
    static const int OFF[8][2] = {
        {-1, 0}, {1, 0}, {0, 1}, {0, -1}, {-1, 1}, {1, 1}, {-1, -1}, {1, -1} };
    for (int o = 0; o < 8; o++) {
        int x = ax + OFF[o][0], y = ay + OFF[o][1];
        if (!col_spawn_footprint_free(s, x, y, 1)) continue;
        col_spawn_npc_at(s, type, x, y);
        return;
    }
    int x, y;
    col_draw_warband_box_tile(s, &x, &y);
    col_spawn_npc_at(s, type, x, y);
}

/** A26: the Dynamic Duo pair spawns NEAR its primary, off-anchor allowed ("The
    paired Colossus spawns near the main Colossus, but not necessarily on one of
    the 12 default spawns"). Every fallback stays near the partner: the 8
    footprints flush with the primary's first, then outward SW-anchor rings of
    growing Chebyshev radius, and as a last resort the free anchor NEAREST the
    primary (never the table-order-first one). */
static void col_spawn_pair_adjacent(ColosseumState* s, ColoNpcType type, int px, int py) {
    int size = COLO_NPC_STATS[type].size;
    const int off[8][2] = {
        {size, 0}, {-size, 0}, {0, size}, {0, -size},
        {size, size}, {-size, -size}, {size, -size}, {-size, size} };
    for (int o = 0; o < 8; o++) {
        int x = px + off[o][0], y = py + off[o][1];
        if (!col_spawn_footprint_free(s, x, y, size)) continue;
        col_spawn_npc_at(s, type, x, y);
        return;
    }
    for (int r = 1; r <= size + 2; r++) {
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                if (dx > -r && dx < r && dy > -r && dy < r) continue;  /* shell only */
                int x = px + dx, y = py + dy;
                if (!col_spawn_footprint_free(s, x, y, size)) continue;
                col_spawn_npc_at(s, type, x, y);
                return;
            }
        }
    }
    int best = -1, best_dist = 1 << 30;
    for (int a = 0; a < COLO_NUM_SPAWN_ANCHORS; a++) {
        int ax = COLO_SPAWN_ANCHORS[a][0], ay = COLO_SPAWN_ANCHORS[a][1];
        if (col_spawn_excluded_near_player(s, ax, ay, COLO_SPAWN_ZONE_SIZE)) continue;
        if (!col_spawn_footprint_free(s, ax, ay, COLO_SPAWN_ZONE_SIZE)) continue;
        int dx = ax > px ? ax - px : px - ax;
        int dy = ay > py ? ay - py : py - ay;
        int cheb = dx > dy ? dx : dy;
        if (cheb < best_dist) { best_dist = cheb; best = a; }
    }
    assert(best >= 0 && "no spawn spot for the Dynamic Duo pair");
    col_spawn_npc_at(s, type, COLO_SPAWN_ANCHORS[best][0], COLO_SPAWN_ANCHORS[best][1]);
}

/** A6+D19: the Quartet warbander on wave 12 spawns on a random walkable
    interior tile outside the B3-mirrored player exclusion. B2(c): the Quartet
    extra holds the SOUTH diamond slot. */
static void col_spawn_quartet_boss_wave(ColosseumState* s, ColoNpcType type) {
    int span = COLO_BOSS_ARENA_SIZE;
    int qx = -1, qy = -1;
    for (int t = 0; t < 64 && qx < 0; t++) {
        int x = COLO_BOSS_ARENA_MIN_X + encounter_rand_int(&s->rng_state, span);
        int y = COLO_BOSS_ARENA_MIN_Y + encounter_rand_int(&s->rng_state, span);
        if (col_spawn_excluded_near_player(s, x, y, 1)) continue;
        if (!col_spawn_footprint_free(s, x, y, 1)) continue;
        qx = x; qy = y;
    }
    for (int y = COLO_BOSS_ARENA_MIN_Y; y <= COLO_BOSS_ARENA_MAX_Y && qx < 0; y++) {
        for (int x = COLO_BOSS_ARENA_MIN_X; x <= COLO_BOSS_ARENA_MAX_X && qx < 0; x++) {
            if (col_spawn_excluded_near_player(s, x, y, 1)) continue;
            if (!col_spawn_footprint_free(s, x, y, 1)) continue;
            qx = x; qy = y;
        }
    }
    assert(qx >= 0 && "no spawn tile for the wave-12 Quartet warbander");
    int slot = col_spawn_npc_at(s, type, qx, qy);
    colo_npc_warband(&s->npcs[slot])->formation_dir = COLO_WARBAND_FORM_SOUTH;
}

static const ColoNpcType COLO_WARBAND_TYPES[3] = {
    COLO_FREMENNIK_BERSERKER, COLO_FREMENNIK_ARCHER, COLO_FREMENNIK_SEER };

/**
 * Spawn the current wave's roster.
 *
 * Waves 1-11: the warband trio spawns at the arena centre — ranger uniformly in
 * the B2(d) box, berserker + seer adjacent. Primaries (non-warband) draw spawn
 * anchors uniformly without replacement (A4+D4) under the B3 player-proximity
 * exclusion, SW tile ON the anchor; Dynamic Duo pairs place adjacent to their
 * primary (A26); the Quartet extra warbander takes its own centre-box tile.
 * Wave 12: the boss arena starts first (clamp + player teleport), Sol spawns at
 * his researched SW tile, and a Quartet warbander spawns on a random interior
 * tile (A6+D19).
 */
static void col_spawn_wave(ColosseumState* s) {
    col_reset_current_wave_score_progress(s);

    /* clear the old roster. Bee swarms survive wave transitions (they respawn
       forever, D34); totems despawn with the wave — their owners are gone —
       and all totem bookkeeping resets. */
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        if (!s->npcs[i].active) continue;
        if (s->npcs[i].type == COLO_BEE_SWARM) continue;
        col_deactivate_npc(s, i);
    }
    memset(s->totems, 0, sizeof(s->totems));
    /* heal any stale footprint stamps (e.g. NPCs force-killed without cleanup). */
    memset(s->npc_collision_flags, 0, sizeof(s->npc_collision_flags));

    /* B2(a): re-arm the shared warband cycle; the step loop anchors it to the
       wave's first actionable tick N. */
    s->warband_cycle_anchor = -1;

    if (s->wave < 0 || s->wave >= COLO_NUM_WAVES) return;
    const ColoWaveDef* def = &COLO_WAVES[s->wave];

    if (s->wave == COLO_WAVE_BOSS) {
        col_sol_begin_boss_arena(s);
        for (int i = 0; i < def->count; i++) {
            assert((ColoNpcType)def->types[i] == COLO_SOL_HEREDIT);
            col_spawn_npc_at(s, COLO_SOL_HEREDIT, COLO_SOL_SPAWN_X, COLO_SOL_SPAWN_Y);
        }
        if (col_mod_active(s, COLO_MOD_QUARTET)) {
            ColoNpcType extra = COLO_WARBAND_TYPES[encounter_rand_int(&s->rng_state, 3)];
            col_spawn_quartet_boss_wave(s, extra);
        }
    } else {
        int archer_x = -1, archer_y = -1;
        for (int i = 0; i < def->count; i++) {
            if ((ColoNpcType)def->types[i] != COLO_FREMENNIK_ARCHER) continue;
            col_draw_warband_box_tile(s, &archer_x, &archer_y);
            col_spawn_npc_at(s, COLO_FREMENNIK_ARCHER, archer_x, archer_y);
        }
        for (int i = 0; i < def->count; i++) {
            ColoNpcType type = (ColoNpcType)def->types[i];
            if (!col_type_is_warbander(type) || type == COLO_FREMENNIK_ARCHER) continue;
            assert(archer_x >= 0 && "warband def lists berserker/seer without an archer");
            col_spawn_warbander_adjacent(s, type, archer_x, archer_y);
        }

        int candidates[COLO_NUM_SPAWN_ANCHORS];
        int ncand = col_spawn_anchor_candidates(s, candidates);
        encounter_shuffle(candidates, ncand, &s->rng_state);
        int cursor = 0;
        for (int i = 0; i < def->count; i++) {
            ColoNpcType type = (ColoNpcType)def->types[i];
            if (col_type_is_warbander(type)) continue;
            int anchor = -1;
            while (cursor < ncand) {
                int a = candidates[cursor++];
                /* re-verify: a warband body or Duo pair may have claimed zone tiles. */
                if (col_spawn_footprint_free(
                        s, COLO_SPAWN_ANCHORS[a][0], COLO_SPAWN_ANCHORS[a][1],
                        COLO_SPAWN_ZONE_SIZE)) { anchor = a; break; }
            }
            assert(anchor >= 0 && "primary spawn ran out of anchor candidates");
            int ax = COLO_SPAWN_ANCHORS[anchor][0], ay = COLO_SPAWN_ANCHORS[anchor][1];
            col_spawn_npc_at(s, type, ax, ay);
            /* Dynamic Duo: Shockwave Colossi spawn in pairs. */
            if (type == COLO_SHOCKWAVE_COLOSSUS && col_mod_active(s, COLO_MOD_DYNAMIC_DUO))
                col_spawn_pair_adjacent(s, type, ax, ay);
        }

        /* Quartet: one extra random warbander every wave, also centre-box; it
           holds the SOUTH diamond slot (B2(c)). */
        if (col_mod_active(s, COLO_MOD_QUARTET)) {
            ColoNpcType extra = COLO_WARBAND_TYPES[encounter_rand_int(&s->rng_state, 3)];
            int qx, qy;
            col_draw_warband_box_tile(s, &qx, &qy);
            int slot = col_spawn_npc_at(s, extra, qx, qy);
            colo_npc_warband(&s->npcs[slot])->formation_dir = COLO_WARBAND_FORM_SOUTH;
        }
    }

    s->reinforcement_timer = COLO_REINFORCEMENT_TICKS;
    col_modifiers_on_wave_spawn(s);
}

/** sentinel reinforcement_timer value meaning this wave already reinforced. */
#define COLO_REINFORCE_FIRED (-1)

/**
 * Spawn the wave's reinforcement species inside the chosen gate gap (A17+B8+D2).
 *
 * Side: the b5 yellow line — player y >= COLO_GATE_LINE_NORTH_MIN_Y takes the
 * north gate, else the south (fixed threshold, not nearest-distance). Species
 * pack side-by-side from the gap's west edge (sim x 15-18) on the innermost
 * walkable row of the chosen wall, multi-tile footprints extending inward; an
 * occupied base spot slides east within the gap, and a fully occupied gap spawns
 * overlapping (reinforcements stream in regardless). Sets reinforcement_timer to
 * COLO_REINFORCE_FIRED so an overrun reinforces exactly once per wave (D1).
 */
static void col_spawn_reinforcements(ColosseumState* s) {
    if (s->wave < 0 || s->wave >= COLO_NUM_WAVES) return;
    const ColoWaveDef* def = &COLO_WAVES[s->wave];
    if (def->reinforce_count <= 0) {
        s->reinforcement_timer = COLO_REINFORCE_FIRED;
        return;
    }

    int north = s->player.y >= COLO_GATE_LINE_NORTH_MIN_Y;
    int base_x = COLO_GATE_MIN_X;
    for (int i = 0; i < def->reinforce_count; i++) {
        ColoNpcType type = (ColoNpcType)def->reinforce[i];
        int size = COLO_NPC_STATS[type].size;
        int y = north ? COLO_GATE_NORTH_SPAWN_ROW - (size - 1) : COLO_GATE_SOUTH_SPAWN_ROW;
        int x = base_x;
        for (int try_x = base_x; try_x + size - 1 <= COLO_GATE_MAX_X; try_x++) {
            if (col_spawn_footprint_free(s, try_x, y, size)) { x = try_x; break; }
        }
        col_spawn_npc_at(s, type, x, y);
        base_x = x + size;
    }

    s->reinforcement_timer = COLO_REINFORCE_FIRED;
}

static void col_init_empty_inventory_cells(ColosseumState* s) {
    for (int i = 0; i < COLO_INVENTORY_DISPLAY_SLOTS; i++) {
        s->inventory_cells[i] = (ColoInvCell){
            .osrs_id = 0,
            .item_idx = ITEM_NONE,
            .dose = 0,
        };
    }
}

static void col_seed_inventory_cells_from_display(ColosseumState* s) {
    col_init_empty_inventory_cells(s);
    const int* kit = COLO_INVENTORY_DISPLAY[s->active_loadout_profile];
    for (int i = 0; i < COLO_INVENTORY_DISPLAY_SLOTS; i++) {
        uint16_t raw_id = (uint16_t)kit[i];
        if (raw_id == 0) continue;

        uint8_t item_idx = osrs_item_index_for_raw_osrs_id(raw_id);
        if (item_idx != ITEM_NONE) {
            s->inventory_cells[i] = (ColoInvCell){
                .osrs_id = raw_id,
                .item_idx = item_idx,
                .dose = 0,
            };
            continue;
        }

        OsrsConsumableClick consumable =
            osrs_consumable_click_lookup_raw_osrs_id(raw_id);
        if (consumable.click_action != OSRS_CLICK_NONE) {
            s->inventory_cells[i] = (ColoInvCell){
                .osrs_id = raw_id,
                .item_idx = ITEM_NONE,
                .dose = consumable.dose_count,
            };
            continue;
        }

        if (raw_id == 27281) {
            s->inventory_cells[i] = (ColoInvCell){
                .osrs_id = raw_id,
                .item_idx = ITEM_NONE,
                .dose = 0,
            };
            continue;
        }

        fprintf(stderr, "colosseum: inventory display raw id %u has no cell mapping\n",
            raw_id);
        abort();
    }
}


static void col_reset_ctx(EncounterState* state, EncounterContext* context, uint32_t seed) {
    col_build_npc_stats();
    ColosseumState* s = (ColosseumState*)state;
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
    int saved_start = ctx->config.start_wave;
    uint32_t saved_rng = s->rng_state;
    col_context_clear_render_events(ctx);
    memset(s, 0, sizeof(ColosseumState));
    s->start_wave = saved_start;
    s->rng_state = encounter_resolve_seed(saved_rng, seed);

    s->player_dest_x = -1;
    s->player_dest_y = -1;
    s->last_hit_by_type = -1;
    s->min_sol_hp_seen = COLO_SOL_HP_MAX;
    for (int i = 0; i < COLO_OBS_NPCS; i++) s->current_obs_slots[i] = -1;

    /* player: maxed combat stats + live equipped melee loadout */
    s->player.entity_type = ENTITY_PLAYER;
    encounter_init_maxed_player_combat_stats(&s->player, 99);
    osrs_item_effect_state_init(&s->player.item_effect_state);

    s->weapon_set = COLO_GEAR_MELEE;
    s->active_loadout_profile = col_sample_loadout_profile(
        ctx->config.loadout_profile_mode,
        ctx->config.beginner_loadout_fraction,
        &s->rng_state);
    switch (s->active_loadout_profile) {
        case COLO_LOADOUT_PROFILE_SPEEDRUN:
            s->episode_max_ticks = COLO_SPEEDRUN_MAX_TICKS;
            break;
        case COLO_LOADOUT_PROFILE_BEGINNER:
            s->episode_max_ticks = COLO_BEGINNER_MAX_TICKS;
            break;
        default:
            fprintf(stderr, "colosseum: invalid active_loadout_profile %d\n",
                (int)s->active_loadout_profile);
            abort();
    }
    const uint8_t* const* loadouts = col_loadouts_for_profile(s->active_loadout_profile);
    encounter_apply_loadout(&s->player, loadouts[COLO_GEAR_MELEE], GEAR_MELEE);
    memset(s->player.inventory, ITEM_NONE, sizeof(s->player.inventory));
    memset(s->player.num_items_in_slot, 0, sizeof(s->player.num_items_in_slot));
    col_seed_inventory_cells_from_display(s);

    /* researched supplies (doses); currents live on Player, denominators here */
    const ColoSupplyLoadout* supplies = &COLO_SUPPLY_LOADOUTS[s->active_loadout_profile];
    s->full_supplies = *supplies;
    s->player.brew_doses = supplies->brew_doses;
    s->player.restore_doses = supplies->restore_doses;
    s->player.combat_potion_doses = supplies->combat_doses;
    s->player.ranged_potion_doses = supplies->ranged_doses;
    s->surge_doses = supplies->surge_doses;
    s->surge_cooldown = 0;

    s->player.prayer = PRAYER_NONE;
    s->player.offensive_prayer = OFFENSIVE_PRAYER_NONE;
    osrs_interaction_init(&s->interaction);
    s->player.spec_armed = 0;
    s->player.special_energy = 100;
    s->player.run_energy = 10000;

    col_mark_live_loadout_dirty(s);
    col_ensure_live_loadout_stats(s);

    /* the thrall has no target until summoned (slot 0 is a real NPC index). */
    s->thrall_target_slot = -1;

    s->player.x = COLO_PLAYER_START_X;
    s->player.y = COLO_PLAYER_START_Y;
    col_rebuild_player_collision_flags(s);

    int effective_start = s->start_wave;
    if (effective_start < 0) effective_start = 0;
    if (effective_start >= COLO_NUM_WAVES) effective_start = COLO_NUM_WAVES - 1;
    s->wave = effective_start;
    s->wave_spawn_target = effective_start;
    s->wave_spawn_delay = 0;
    s->log.wave_reached = effective_start;

    /* A16+B6: a wave-1 run opens with the mandatory fixed draft (Relentless /
       Blasphemy / Frailty); the wave spawns only after the pick, then runs the
       6-tick ready delay. start_wave>1 runs (curriculum/lab) DETERMINISTICALLY
       SKIP every prior draft — they start modifier-free and meet their first
       draft after clearing the start wave (auto-rolling 1..11 random modifiers
       would make curriculum difficulty wildly seed-dependent). */
    if (effective_start == 0) {
        col_modifier_open_draft(s, 0);
        assert(s->modifiers.draft_pending && "wave-1 fixed draft failed to open");
    } else {
        s->wave_ready_delay = COLO_START_READY_TICKS;
        col_spawn_wave(s);
    }
}

static void col_reset(EncounterState* state, uint32_t seed) {
    col_reset_ctx(state, (EncounterContext*)col_legacy_context(), seed);
}
