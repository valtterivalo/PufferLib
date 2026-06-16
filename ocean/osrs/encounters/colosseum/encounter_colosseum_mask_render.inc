#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_mask_render.inc
 * @brief Action mask + reward/terminal getters + render-entity fill.
 *
 * Split out of encounter_colosseum_obs_mask.inc to keep each .inc under ~500
 * lines. The mask is per-head validity (1 = valid); col_write_mask_ctx
 * hard-asserts the running offset against COLO_ACTION_MASK_SIZE, like inferno.
 */

static void col_write_mask_ctx(EncounterState* state, EncounterContext* context, float* mask) {
    ColosseumState* s = (ColosseumState*)state;
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
    col_refresh_current_obs_slots_ctx(s, ctx);
    int offset = 0;

    /* HEAD_MOVE (25): idle always valid; each walk/run valid iff the target tile
       is walkable (in-arena, boss-arena bound on wave 12, not pillar/NPC-blocked).
       B6: the player is frozen while a draft is open — only idle is valid. */
    int frozen = s->modifiers.draft_pending;
    mask[offset++] = 1.0f;
    for (int d = 1; d < ENCOUNTER_MOVE_ACTIONS; d++) {
        int nx = s->player.x + ENCOUNTER_MOVE_TARGET_DX[d];
        int ny = s->player.y + ENCOUNTER_MOVE_TARGET_DY[d];
        mask[offset++] = (!frozen && col_player_walkable(s, nx, ny)) ? 1.0f : 0.0f;
    }

    /* HEAD_PRAYER (5): no_change always valid; set melee/ranged/magic valid iff
       prayer points remain; off valid iff a prayer is active. */
    mask[offset++] = 1.0f;
    mask[offset++] = s->player.prayer != PRAYER_NONE ? 1.0f : 0.0f;
    mask[offset++] = s->player.current_prayer > 0 ? 1.0f : 0.0f;
    mask[offset++] = s->player.current_prayer > 0 ? 1.0f : 0.0f;
    mask[offset++] = s->player.current_prayer > 0 ? 1.0f : 0.0f;

    /* HEAD_TARGET (COLO_OBS_NPCS+1): none always valid; slot valid iff it holds a
       live attackable NPC and the player is not draft-frozen (B6). */
    mask[offset++] = 1.0f;
    for (int n = 0; n < COLO_OBS_NPCS; n++)
        mask[offset++] = (!frozen && col_obs_slot_is_targetable(s, n)) ? 1.0f : 0.0f;

    /* HEAD_GEAR (4): no_switch always valid; set-X valid iff not already set X. */
    mask[offset++] = 1.0f;
    mask[offset++] = (s->weapon_set != COLO_GEAR_MELEE) ? 1.0f : 0.0f;
    mask[offset++] = (s->weapon_set != COLO_GEAR_RANGED) ? 1.0f : 0.0f;
    mask[offset++] = (s->weapon_set != COLO_GEAR_MAGIC) ? 1.0f : 0.0f;

    /* HEAD_EAT (2): none always valid; brew valid iff doses + off-timer + hurt. */
    mask[offset++] = 1.0f;
    mask[offset++] = (s->player.brew_doses > 0 && s->player.potion_timer == 0 &&
                      s->player.current_hitpoints < s->player.base_hitpoints) ? 1.0f : 0.0f;

    /* HEAD_POTION (5): none, restore, super combat, ranging, surge (L15).
       restore unmasks when a stat is drained, prayer is meaningfully low, or
       (sanfew) venom is ticking. Boost pots mirror inferno's bastion window
       [base, base+5] so a drained player restores first. E13 divine boost
       pots also require more than 10 Hitpoints. Surge needs a dose, no
       cooldown, and missing spec energy. */
    mask[offset++] = 1.0f;
    {
        int pray_missing = s->player.base_prayer - s->player.current_prayer;
        int restore_amount = (s->full_supplies.restore_kind == COLO_RESTORE_SANFEW)
            ? osrs_sanfew_restore_amount(s->player.base_prayer)
            : osrs_super_restore_amount(s->player.base_prayer);
        int stats_drained =
            s->player.current_attack < s->player.base_attack ||
            s->player.current_strength < s->player.base_strength ||
            s->player.current_defence < s->player.base_defence ||
            s->player.current_ranged < s->player.base_ranged ||
            s->player.current_magic < s->player.base_magic;
        int pray_worth = pray_missing >= (restore_amount + 1) / 2;
        int venom_cure = (s->full_supplies.restore_kind == COLO_RESTORE_SANFEW &&
                          s->player_venom > 0);
        mask[offset++] = (s->player.restore_doses > 0 &&
                          s->player.potion_timer == 0 &&
                          (stats_drained || pray_worth || venom_cure)) ? 1.0f : 0.0f;
    }
    mask[offset++] = (s->player.combat_potion_doses > 0 && s->player.potion_timer == 0 &&
                      (!col_combat_potion_is_divine(s) || s->player.current_hitpoints > 10) &&
                      s->player.current_attack >= s->player.base_attack &&
                      s->player.current_attack <= s->player.base_attack + 5) ? 1.0f : 0.0f;
    mask[offset++] = (s->player.ranged_potion_doses > 0 && s->player.potion_timer == 0 &&
                      (!col_ranged_potion_is_divine(s) || s->player.current_hitpoints > 10) &&
                      s->player.current_ranged >= s->player.base_ranged &&
                      s->player.current_ranged <= s->player.base_ranged + 5) ? 1.0f : 0.0f;
    mask[offset++] = (s->surge_doses > 0 && s->player.potion_timer == 0 &&
                      s->surge_cooldown == 0 &&
                      s->player.special_energy < 100) ? 1.0f : 0.0f;

    /* HEAD_SPEC (3): none always valid; arm spec k valid iff energy covers that
       weapon's cost, or it is already armed (so the press disarms). */
    mask[offset++] = 1.0f;
    for (int k = 1; k <= COLO_NUM_SPEC_WEAPONS; k++) {
        uint8_t spec_item = COLO_SPEC_WEAPONS[s->active_loadout_profile][k - 1];
        int can_arm = s->player.special_energy >= osrs_spec_cost(spec_item);
        mask[offset++] = (can_arm || s->spec_armed_kind == k) ? 1.0f : 0.0f;
    }

    /* HEAD_MODIFIER_SELECT (4): none always valid; pick option k valid ONLY while a
       draft is pending and that option slot holds a real modifier id. */
    mask[offset++] = 1.0f;
    for (int o = 0; o < COLO_MODIFIER_DRAFT_OPTIONS; o++)
        mask[offset++] = (s->modifiers.draft_pending && s->modifiers.draft_options[o] >= 0)
            ? 1.0f : 0.0f;

    /* HEAD_GRAPPLE_PARRY (COLO_NUM_GRAPPLE_SLOTS+1): none always valid;
       clicking a callout slot valid ONLY while a Sol grapple is active (A12). */
    mask[offset++] = 1.0f;
    for (int g = 0; g < COLO_NUM_GRAPPLE_SLOTS; g++)
        mask[offset++] = s->sol.grapple_active ? 1.0f : 0.0f;

    /* HEAD_OFFENSIVE (5): no_change always valid; off valid iff one is active;
       piety/rigour valid with prayer points; augury valid only in the magic set
       (it boosts magic accuracy + damage). */
    mask[offset++] = 1.0f;
    mask[offset++] = (s->player.offensive_prayer != OFFENSIVE_PRAYER_NONE) ? 1.0f : 0.0f;
    mask[offset++] = (s->player.current_prayer > 0 &&
                      s->player.offensive_prayer != OFFENSIVE_PRAYER_PIETY) ? 1.0f : 0.0f;
    mask[offset++] = (s->player.current_prayer > 0 &&
                      s->player.offensive_prayer != OFFENSIVE_PRAYER_RIGOUR) ? 1.0f : 0.0f;
    mask[offset++] = (s->weapon_set == COLO_GEAR_MAGIC && s->player.current_prayer > 0 &&
                      s->player.offensive_prayer != OFFENSIVE_PRAYER_AUGURY) ? 1.0f : 0.0f;

    /* HEAD_SPELL (3): none always valid; summon-thrall valid iff not draft-frozen,
       the recast gate is up, and the current target is a thrall-eligible (live,
       non-Sol) NPC (a summon replaces a live thrall); death-charge valid iff not
       frozen, ready (cd==0), and not already armed. */
    mask[offset++] = 1.0f;
    {
        int target = col_player_target_slot(s);
        mask[offset++] = (!frozen && s->thrall_recast_cd == 0 &&
                          col_npc_is_thrall_target(s, target)) ? 1.0f : 0.0f;
    }
    mask[offset++] = (!frozen && s->death_charge_cd == 0 &&
                      s->death_charge_window_left == 0) ? 1.0f : 0.0f;

    if (offset != COLO_ACTION_MASK_SIZE) {
        fprintf(stderr, "colosseum mask index mismatch: wrote %d, expected %d\n",
            offset, COLO_ACTION_MASK_SIZE);
        abort();
    }
}

static float col_get_reward_ctx(EncounterState* state, EncounterContext* context) {
    (void)context;
    return ((ColosseumState*)state)->reward;
}

static int col_is_terminal_ctx(EncounterState* state, EncounterContext* context) {
    (void)context;
    return ((ColosseumState*)state)->episode_over;
}

static int col_get_entity_count_ctx(EncounterState* state, EncounterContext* context) {
    (void)context;
    ColosseumState* s = (ColosseumState*)state;
    int count = 1;
    for (int i = 0; i < COLO_MAX_NPCS; i++)
        if (s->npcs[i].active) count++;
    return count;
}

static void* col_get_entity_ctx(EncounterState* state, EncounterContext* context, int index) {
    (void)context;
    ColosseumState* s = (ColosseumState*)state;
    if (index == 0) return &s->player;
    return NULL;
}

static void col_prepare_render_player(const ColosseumState* s, int spec_fired_kind, Player* out) {
    *out = s->player;
    GearSet gear = s->weapon_set == COLO_GEAR_RANGED ? GEAR_RANGED
                 : s->weapon_set == COLO_GEAR_MAGIC ? GEAR_MAGE
                 : GEAR_MELEE;
    out->current_gear = gear;
    out->visible_gear = gear;
    if (spec_fired_kind > 0 && spec_fired_kind <= COLO_NUM_SPEC_WEAPONS) {
        out->equipped[GEAR_SLOT_WEAPON] = col_spec_weapon_for_kind(s, spec_fired_kind);
        out->equipped[GEAR_SLOT_SHIELD] = osrs_suppress_shield_for_two_handed_weapon(
            out->equipped[GEAR_SLOT_WEAPON], out->equipped[GEAR_SLOT_SHIELD]);
        out->current_gear = GEAR_MELEE;
        out->visible_gear = GEAR_MELEE;
    }
}

static int col_npc_render_attack_anim_id(
    const ColosseumContext* ctx,
    int npc_slot,
    const ColoNPC* npc,
    int def_id
) {
    assert(ctx);
    if (npc->type == COLO_MANTICORE && ctx->manticore_charge_started[npc_slot])
        return 10868;
    switch (ctx->npc_render_attack_anim[npc_slot]) {
        case COLO_NPC_RENDER_ATTACK_NORMAL:
            return col_npc_attack_anim_id(def_id);
        case COLO_NPC_RENDER_ATTACK_JAVELIN_SKYFALL:
            assert(npc->type == COLO_JAVELIN_COLOSSUS);
            return COLO_JAVELIN_SKYFALL_ANIM_ID;
    }
    abort();
}

/** Render-only: a deterministic free tile adjacent to the thrall's current sim
    target NPC (s->thrall_target_slot), so the viewer shows it standing to attack
    the enemy it auto-attacks rather than glued to the player. The sim never
    positions the thrall (its damage is not range-gated), so the viewer scans the
    ring around the target footprint and picks the walkable tile nearest the
    player (the near side a melee/ranged actor would stand on). Falls back to the
    target's own tile, then to the player-adjacent scan, then the player tile when
    the thrall has no live target. Both the render-entity and projectile emitters
    call this so the model and its bolt share one source tile. */
static void col_thrall_render_tile(const ColosseumState* s, int* out_x, int* out_y) {
    static const int ADJ[8][2] = {
        {0, 1}, {1, 0}, {-1, 0}, {0, -1},        /* cardinals */
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1} };    /* diagonals */
    int slot = s->thrall_target_slot;
    if (col_npc_is_thrall_target(s, slot)) {
        const ColoNPC* target = &s->npcs[slot];
        int size = col_npc_effective_size(target);
        int best_x = -1, best_y = -1, best_dist = 0;
        for (int dx = -1; dx <= size; dx++) {
            for (int dy = -1; dy <= size; dy++) {
                int on_footprint = dx >= 0 && dx < size && dy >= 0 && dy < size;
                if (on_footprint) continue;
                int x = target->x + dx;
                int y = target->y + dy;
                if (!col_player_walkable((void*)s, x, y)) continue;
                int ddx = x - s->player.x;
                int ddy = y - s->player.y;
                int dist = (ddx < 0 ? -ddx : ddx) > (ddy < 0 ? -ddy : ddy)
                    ? (ddx < 0 ? -ddx : ddx) : (ddy < 0 ? -ddy : ddy);
                if (best_x < 0 || dist < best_dist) {
                    best_x = x;
                    best_y = y;
                    best_dist = dist;
                }
            }
        }
        if (best_x >= 0) {
            *out_x = best_x;
            *out_y = best_y;
            return;
        }
        *out_x = target->x;
        *out_y = target->y;
        return;
    }
    for (int a = 0; a < 8; a++) {
        int x = s->player.x + ADJ[a][0];
        int y = s->player.y + ADJ[a][1];
        if (col_player_walkable((void*)s, x, y)) {
            *out_x = x;
            *out_y = y;
            return;
        }
    }
    *out_x = s->player.x;
    *out_y = s->player.y;
}

/** populate render entities (player + active NPCs). */
static void col_fill_render_entities_ctx(
    EncounterState* state,
    EncounterContext* context,
    RenderEntity* out,
    int max_entities,
    int* count
) {
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
    ColosseumState* s = (ColosseumState*)state;
    const EncounterLoadoutStats* ls = col_live_loadout_stats(s);
    s->player.gui_max_hit = ls->max_hit;
    s->player.gui_attack_speed = ls->attack_speed;
    s->player.gui_attack_range = ls->attack_range;
    s->player.gui_strength_bonus = ls->strength_bonus;

    int n = 0;
    if (n < max_entities) {
        Player render_player;
        col_prepare_render_player(s, ctx->player_spec_fired_kind, &render_player);
        RenderEntity* re = &out[n++];
        osrs_render_entity_from_player_entity(&render_player, re);
        if (ctx->player_render_hit_count < 0 ||
                ctx->player_render_hit_count > ENCOUNTER_RENDER_HITS_MAX) {
            fprintf(stderr, "invalid player render hit count\n");
            abort();
        }
        re->render_hit_count = ctx->player_render_hit_count;
        for (int h = 0; h < ctx->player_render_hit_count; h++)
            re->render_hit_damage[h] = ctx->player_render_hit_damage[h];
    }

    for (int i = 0; i < COLO_MAX_NPCS && n < max_entities; i++) {
        ColoNPC* npc = &s->npcs[i];
        if (!npc->active) continue;
        int def_id = COLO_NPC_DEF_IDS[npc->type];
        if (npc->type == COLO_MINOTAUR && col_mod_active(s, COLO_MOD_RED_FLAG))
            def_id = COLO_NPC_DEF_ID_MINOTAUR_ROUTEFIND;
        int npc_anim_id = -1;
        if (npc->death_ticks > 0)
            npc_anim_id = col_npc_death_anim_id(npc->type);
        else if (npc->type == COLO_MANTICORE && ctx->manticore_charge_started[i])
            npc_anim_id = col_npc_render_attack_anim_id(ctx, i, npc, def_id);
        else if (npc->attacked_this_tick)
            npc_anim_id = col_npc_render_attack_anim_id(ctx, i, npc, def_id);
        RenderEntity* re = &out[n++];
        OsrsNpcRenderEntitySpec npc_spec = {
            .npc_def_id = def_id,
            .npc_slot = i,
            .npc_instance_id = npc->render_id,
            .npc_visible = npc->active,
            .npc_size = npc->size,
            .npc_anim_id = npc_anim_id,
            .x = npc->x,
            .y = npc->y,
            .dest_x = npc->target_x,
            .dest_y = npc->target_y,
            .current_hitpoints = npc->hp,
            .base_hitpoints = npc->max_hp,
            .attack_style_this_tick = npc->attacked_this_tick
                ? npc->attack_style_this_tick : ATTACK_STYLE_NONE,
            .hit_landed_this_tick = npc->hit_landed_this_tick,
            .hit_damage = npc->hit_damage,
            .hit_was_successful = npc->hit_was_successful_this_tick,
            .hit_spell_type = npc->hit_spell_type,
            .attack_target_entity_idx = 0,
        };
        osrs_render_entity_from_npc_spec(&npc_spec, re);
        if (ctx->npc_render_hit_count[i] < 0 ||
                ctx->npc_render_hit_count[i] > ENCOUNTER_RENDER_HITS_MAX) {
            fprintf(stderr, "invalid npc render hit count\n");
            abort();
        }
        re->render_hit_count = ctx->npc_render_hit_count[i];
        for (int h = 0; h < ctx->npc_render_hit_count[i]; h++)
            re->render_hit_damage[h] = ctx->npc_render_hit_damage[i][h];
        re->is_running = col_type_is_warbander(npc->type) ? 1 : 0;
        re->frozen_ticks = npc->frozen_ticks;
        re->debug_npc_type_name = colo_npc_type_name(npc->type);
        re->debug_attack_timer = npc->attack_timer;
        re->debug_attack_style = npc->attack_style;
        if (npc->type == COLO_MANTICORE) {
            const ColoManticoreState* mc = &npc->type_state.manticore;
            re->debug_manticore_state_active = 1;
            re->debug_manticore_cycle_step = mc->cycle_step;
            for (int orb = 0; orb < 3; orb++)
                re->debug_manticore_orb_style[orb] = mc->orb_style[orb];
        }
    }

    if (s->thrall_active && n < max_entities) {
        int tx, ty;
        col_thrall_render_tile(s, &tx, &ty);
        /* hide the thrall when the player stands on its render tile: the tile
           picker falls back to the player's own tile when no walkable neighbour
           exists, and drawing the thrall under the player looks wrong. */
        if (tx != s->player.x || ty != s->player.y) {
            RenderEntity* re = &out[n++];
            OsrsNpcRenderEntitySpec thrall_spec = {
                .npc_def_id = COLO_NPC_DEF_ID_THRALL,
                .npc_slot = COLO_NPC_SLOT_THRALL,
                .npc_instance_id = COLO_THRALL_RENDER_INSTANCE_ID,
                .npc_visible = 1,
                .npc_size = 1,
                .npc_anim_id = ctx->thrall_attacked_this_tick ? 11101 : -1,
                .x = tx,
                .y = ty,
                .dest_x = tx,
                .dest_y = ty,
                .current_hitpoints = 1,
                .base_hitpoints = 1,
                .attack_style_this_tick = ATTACK_STYLE_NONE,
                .hit_landed_this_tick = 0,
                .hit_damage = 0,
                .hit_was_successful = 0,
                .hit_spell_type = ENCOUNTER_SPELL_NONE,
                .attack_target_entity_idx = 0,
            };
            osrs_render_entity_from_npc_spec(&thrall_spec, re);
            re->debug_npc_type_name = "Arceuus Thrall";
        }
    }

    if (s->solarflare.active && n < max_entities) {
        RenderEntity* re = &out[n++];
        OsrsNpcRenderEntitySpec orb_spec = {
            .npc_def_id = COLO_NPC_DEF_ID_SOLARFLARE,
            .npc_slot = COLO_NPC_SLOT_SOLARFLARE,
            .npc_instance_id = COLO_SOLARFLARE_RENDER_INSTANCE_ID,
            .npc_visible = 1,
            .npc_size = 1,
            .npc_anim_id = COLO_SOLARFLARE_IDLE_ANIM_ID,
            .x = s->solarflare.x,
            .y = s->solarflare.y,
            .dest_x = s->solarflare.x,
            .dest_y = s->solarflare.y,
            .current_hitpoints = 1,
            .base_hitpoints = 1,
            .attack_style_this_tick = ATTACK_STYLE_NONE,
            .hit_landed_this_tick = 0,
            .hit_damage = 0,
            .hit_was_successful = 0,
            .hit_spell_type = ENCOUNTER_SPELL_NONE,
            .attack_target_entity_idx = 0,
        };
        osrs_render_entity_from_npc_spec(&orb_spec, re);
        re->debug_npc_type_name = "Solar flare";
    }

    encounter_resolve_attack_target(out, n, s->interaction.target_slot);
    *count = n;
}
