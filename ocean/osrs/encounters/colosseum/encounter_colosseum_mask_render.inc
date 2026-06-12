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

    /* HEAD_GEAR (3): no_switch always valid; set-X valid iff not already set X. */
    mask[offset++] = 1.0f;
    mask[offset++] = (s->weapon_set != COLO_GEAR_MELEE) ? 1.0f : 0.0f;
    mask[offset++] = (s->weapon_set != COLO_GEAR_RANGED) ? 1.0f : 0.0f;

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
       piety/rigour valid with prayer points; augury stays masked — the kits
       carry no magic weapon set (L1). */
    mask[offset++] = 1.0f;
    mask[offset++] = (s->player.offensive_prayer != OFFENSIVE_PRAYER_NONE) ? 1.0f : 0.0f;
    mask[offset++] = (s->player.current_prayer > 0 &&
                      s->player.offensive_prayer != OFFENSIVE_PRAYER_PIETY) ? 1.0f : 0.0f;
    mask[offset++] = (s->player.current_prayer > 0 &&
                      s->player.offensive_prayer != OFFENSIVE_PRAYER_RIGOUR) ? 1.0f : 0.0f;
    mask[offset++] = 0.0f;

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

/** populate render entities (player + active NPCs). */
static void col_fill_render_entities_ctx(
    EncounterState* state,
    EncounterContext* context,
    RenderEntity* out,
    int max_entities,
    int* count
) {
    (void)context;
    ColosseumState* s = (ColosseumState*)state;
    const EncounterLoadoutStats* ls = &s->loadout_stats[s->weapon_set];
    s->player.gui_max_hit = ls->max_hit;
    s->player.gui_attack_speed = ls->attack_speed;
    s->player.gui_attack_range = ls->attack_range;
    s->player.gui_strength_bonus = ls->strength_bonus;

    int n = 0;
    if (n < max_entities)
        osrs_render_entity_from_player_entity(&s->player, &out[n++]);

    for (int i = 0; i < COLO_MAX_NPCS && n < max_entities; i++) {
        ColoNPC* npc = &s->npcs[i];
        if (!npc->active) continue;
        int def_id = COLO_NPC_DEF_IDS[npc->type];
        if (npc->type == COLO_MINOTAUR && col_mod_active(s, COLO_MOD_RED_FLAG))
            def_id = COLO_NPC_DEF_ID_MINOTAUR_ROUTEFIND;
        RenderEntity* re = &out[n++];
        osrs_render_entity_from_npc_spec(&(OsrsNpcRenderEntitySpec){
            .npc_def_id = def_id,
            .npc_slot = i,
            .npc_instance_id = npc->render_id,
            .npc_visible = npc->active,
            .npc_size = npc->size,
            .npc_anim_id = -1,
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
        }, re);
    }

    encounter_resolve_attack_target(out, n, s->interaction.target_slot);
    *count = n;
}
