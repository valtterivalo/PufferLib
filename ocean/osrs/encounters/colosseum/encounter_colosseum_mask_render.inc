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

    /* HEAD_PRIMARY (49): noop always valid, move actions follow old movement
       legality, attack actions require a live targetable obs slot. */
    int frozen = s->modifiers.draft_pending && !s->modifiers.draft_free_movement;
    mask[offset++] = 1.0f;
    for (int d = 1; d < ENCOUNTER_MOVE_ACTIONS; d++) {
        int nx = s->player.x + ENCOUNTER_MOVE_TARGET_DX[d];
        int ny = s->player.y + ENCOUNTER_MOVE_TARGET_DY[d];
        mask[offset++] = (!frozen && col_player_walkable(s, nx, ny)) ? 1.0f : 0.0f;
    }
    /* Inventory clicks run before primary and can clear the lock, so the held
       target stays valid for gear switch plus re-attack ticks. */
    for (int n = 0; n < COLO_OBS_NPCS; n++)
        mask[offset++] = (!frozen && col_obs_slot_is_targetable(s, n)) ? 1.0f : 0.0f;

    /* HEAD_PRAYER (5): no_change always valid; off valid iff one is active;
       set melee/ranged/magic valid iff prayer points remain. */
    mask[offset++] = 1.0f;
    mask[offset++] = s->player.prayer != PRAYER_NONE ? 1.0f : 0.0f;
    mask[offset++] = s->player.current_prayer > 0 ? 1.0f : 0.0f;
    mask[offset++] = s->player.current_prayer > 0 ? 1.0f : 0.0f;
    mask[offset++] = s->player.current_prayer > 0 ? 1.0f : 0.0f;

    /* Per-category inventory heads. Classify each cell once by its click-action
       (EQUIP -> gear slot, EAT, DRINK). The equip heads (one per gear slot) then
       only unmask the cells whose item belongs to that slot, so head g can equip
       at most one item into slot g; eat/drink are one head each. This makes a
       one-tick full switch possible (all slot heads fire in parallel) while
       "5 helmets" is structurally impossible. Validity uses the same gates as
       col_inventory_cell_actionable so the mask never unmasks a useless action. */
    int inventory_heads_enabled = !ctx->config.mask_inventory_heads;
    int cell_equip_slot[COLO_INVENTORY_DISPLAY_SLOTS];  /* gear slot if equippable, else -1 */
    int cell_can_eat[COLO_INVENTORY_DISPLAY_SLOTS];
    int cell_can_drink[COLO_INVENTORY_DISPLAY_SLOTS];
    for (int cell = 0; cell < COLO_INVENTORY_DISPLAY_SLOTS; cell++) {
        cell_equip_slot[cell] = -1;
        cell_can_eat[cell] = 0;
        cell_can_drink[cell] = 0;
        OsrsInventoryClickResolution r =
            osrs_inventory_cell_click_interpret(&s->inventory_cells[cell], OSRS_CLICK_TICK_FIRST);
        if (r.click_action == OSRS_CLICK_EQUIP) {
            if (col_can_equip_from_cell(s, cell))
                cell_equip_slot[cell] = osrs_item_gear_slot(s->inventory_cells[cell].item_idx);
        } else if (r.click_action == OSRS_CLICK_EAT) {
            cell_can_eat[cell] = col_can_eat_consumable_kind(s, r.consumable_kind);
        } else if (r.click_action == OSRS_CLICK_DRINK) {
            cell_can_drink[cell] = s->inventory_cells[cell].dose > 0 &&
                s->player.potion_timer == 0 &&
                col_drink_has_effect(s, r.consumable_kind);
        }
    }
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
        mask[offset++] = 1.0f;  /* noop */
        for (int cell = 0; cell < COLO_INVENTORY_DISPLAY_SLOTS; cell++)
            mask[offset++] = (inventory_heads_enabled && cell_equip_slot[cell] == slot)
                ? 1.0f : 0.0f;
    }
    mask[offset++] = 1.0f;  /* EAT noop */
    for (int cell = 0; cell < COLO_INVENTORY_DISPLAY_SLOTS; cell++)
        mask[offset++] = (inventory_heads_enabled && cell_can_eat[cell]) ? 1.0f : 0.0f;
    mask[offset++] = 1.0f;  /* DRINK noop */
    for (int cell = 0; cell < COLO_INVENTORY_DISPLAY_SLOTS; cell++)
        mask[offset++] = (inventory_heads_enabled && cell_can_drink[cell]) ? 1.0f : 0.0f;

    /* HEAD_SPEC (3): none, arm equipped spec weapon, disarm. */
    mask[offset++] = 1.0f;
    int spec_cost = osrs_spec_cost(s->player.equipped[GEAR_SLOT_WEAPON]);
    mask[offset++] = (spec_cost > 0 &&
                      (s->player.special_energy >= spec_cost || s->player.spec_armed))
        ? 1.0f : 0.0f;
    mask[offset++] = s->player.spec_armed ? 1.0f : 0.0f;

    /* HEAD_MODIFIER_SELECT (4): no-op valid EXCEPT while a draft is pending, when a
       pick is forced -- otherwise an argmax policy that prefers the no-op latches on
       it every (frozen, static-obs) tick and soft-locks the draft until timeout.
       col_modifier_open_draft only sets draft_pending with >=1 real option, so this
       never produces an all-masked head. pick option k valid ONLY while a draft is
       pending and that option slot holds a real modifier id. */
    mask[offset++] = s->modifiers.draft_pending ? 0.0f : 1.0f;
    for (int o = 0; o < COLO_MODIFIER_DRAFT_OPTIONS; o++)
        mask[offset++] = (s->modifiers.draft_pending && s->modifiers.draft_options[o] >= 0)
            ? 1.0f : 0.0f;

    /* HEAD_GRAPPLE_PARRY (COLO_NUM_GRAPPLE_SLOTS+1): none always valid;
       clicking a callout slot valid ONLY while a Sol grapple is active (A12). */
    mask[offset++] = 1.0f;
    for (int g = 0; g < COLO_NUM_GRAPPLE_SLOTS; g++)
        mask[offset++] = s->sol.grapple_active ? 1.0f : 0.0f;

    /* HEAD_OFFENSIVE (5): no_change always valid; off valid iff one is active;
       piety/rigour/augury all points-only (no weapon-style gate). Augury was
       formerly gated on the currently-equipped weapon being magic, which the
       harness builds against END-of-tick state, so the agent could never select
       Augury on the same tick it equipped a magic weapon (it had to wait one tick,
       costing the first magic attack its accuracy + damage). The lazy loadout
       recompute already reads post-equip equipped[] + offensive_prayer, so a
       same-tick equip+Augury lands shadow x3 + Augury +4% on that tick's attack;
       Augury on a non-magic weapon is a harmless accuracy no-op exactly as
       Piety/Rigour already are off-style. Matches the zulrah precedent. */
    mask[offset++] = 1.0f;
    mask[offset++] = (s->player.offensive_prayer != OFFENSIVE_PRAYER_NONE) ? 1.0f : 0.0f;
    mask[offset++] = (s->player.current_prayer > 0 &&
                      s->player.offensive_prayer != OFFENSIVE_PRAYER_PIETY) ? 1.0f : 0.0f;
    mask[offset++] = (s->player.current_prayer > 0 &&
                      s->player.offensive_prayer != OFFENSIVE_PRAYER_RIGOUR) ? 1.0f : 0.0f;
    mask[offset++] = (s->player.current_prayer > 0 &&
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
    GearSet gear = col_gear_set_for_attack_style(col_equipped_weapon_attack_style(&s->player));
    out->current_gear = gear;
    out->visible_gear = gear;
    if (spec_fired_kind > 0 && spec_fired_kind < NUM_ITEMS) {
        out->equipped[GEAR_SLOT_WEAPON] = (uint8_t)spec_fired_kind;
        out->equipped[GEAR_SLOT_SHIELD] = osrs_suppress_shield_for_two_handed_weapon(
            out->equipped[GEAR_SLOT_WEAPON], out->equipped[GEAR_SLOT_SHIELD]);
        out->current_gear = GEAR_MELEE;
        out->visible_gear = GEAR_MELEE;
    }
}

/* Sol Heredit plays a distinct animation per attack. The sim sets
   sol.last_attack_kind on the tick the attack fires (col_sol_pick_variant), so the
   renderer keys off it. The deob _TELEGRAPH-named sequences ARE the wind-up-and-
   execute attack anims (every Sol attack telegraphs its AoE); the plain
   MELEE_ATTACK 10882 is the instant jab, unused here. The triple lengthens (the
   delayed final hit) at phase >= 3 (50% HP and below), matching the sim's own
   col_sol_start_triple_parry gate. The generated single attack_anim used to be
   10876, his arena-entry JUMP, which is why every attack looked like the leap-in. */
static int col_sol_render_attack_anim_id(const SolHereditState* sol) {
    switch (sol->last_attack_kind) {
        case COLO_SOL_ATTACK_SPEAR:   return 10883;  /* MELEE_ATTACK_TELEGRAPH */
        case COLO_SOL_ATTACK_SHIELD:  return 10885;  /* SHIELDSLAM_TELEGRAPH */
        case COLO_SOL_ATTACK_GRAPPLE: return 10884;  /* GRAPPLE_ATTACK_TELEGRAPH */
        case COLO_SOL_ATTACK_TRIPLE:  return sol->phase >= 3 ? 10886 : 10887;
        default: break;
    }
    return 10883;  /* default to the spear attack, never the entry jump */
}

static int col_npc_render_attack_anim_id(
    const ColosseumState* s,
    const ColosseumContext* ctx,
    int npc_slot,
    const ColoNPC* npc,
    int def_id
) {
    assert(ctx);
    if (npc->type == COLO_SOL_HEREDIT)
        return col_sol_render_attack_anim_id(&s->sol);
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

/** Render-only: a free, visible tile next to the PLAYER for the allied thrall to
    stand on. In OSRS the thrall loosely follows its caster and lobs its bolt at
    whatever the caster fights, so it lives beside the player rather than glued to
    (or inside) an enemy. Scans the player-adjacent ring first, then widens one
    tile out, rejecting static blocks, NPC footprints, and the player's own tile
    (col_npc_blocked over a 1x1 footprint covers all three), so the thrall is
    always drawn on a clear tile instead of hidden under the player or behind a
    colossus. Both the render-entity and projectile emitters call this so the
    model and its bolt share one source tile. */
static void col_thrall_render_tile(const ColosseumState* s, int* out_x, int* out_y) {
    static const int RING[16][2] = {
        {0, 1}, {1, 0}, {-1, 0}, {0, -1},          /* adjacent cardinals */
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1},        /* adjacent diagonals */
        {0, 2}, {2, 0}, {-2, 0}, {0, -2},          /* one tile further out */
        {1, 2}, {2, 1}, {-1, 2}, {-2, 1} };
    for (int a = 0; a < 16; a++) {
        int x = s->player.x + RING[a][0];
        int y = s->player.y + RING[a][1];
        if (!col_npc_blocked((void*)s, x, y, 1)) {
            *out_x = x;
            *out_y = y;
            return;
        }
    }
    /* a tight pile-in left no clear neighbour: still draw the thrall off the
       player tile rather than dropping it. */
    *out_x = s->player.x;
    *out_y = s->player.y + 1;
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
            npc_anim_id = col_npc_render_attack_anim_id(s, ctx, i, npc, def_id);
        else if (npc->attacked_this_tick)
            npc_anim_id = col_npc_render_attack_anim_id(s, ctx, i, npc, def_id);
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

    if (s->solarflare.active) {
        for (int p = 0; p < COLO_NUM_PILLARS && n < max_entities; p++) {
            int x, y;
            col_solarflare_tile(s, p, s->solarflare.step, &x, &y);
            RenderEntity* re = &out[n++];
            OsrsNpcRenderEntitySpec orb_spec = {
                .npc_def_id = COLO_NPC_DEF_ID_SOLARFLARE,
                .npc_slot = COLO_NPC_SLOT_SOLARFLARE + p,
                .npc_instance_id = COLO_SOLARFLARE_RENDER_INSTANCE_ID + (uint32_t)p,
                .npc_visible = 1,
                .npc_size = 1,
                .npc_anim_id = COLO_SOLARFLARE_IDLE_ANIM_ID,
                .x = x,
                .y = y,
                .dest_x = x,
                .dest_y = y,
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
    }

    encounter_resolve_attack_target(out, n, s->interaction.target_slot);
    *count = n;
}
