#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_player_actions.inc
 * @brief Action head layout + player tick, mirroring inferno player_actions.
 *
 * Action heads (8): MOVE, PRAYER, TARGET, GEAR, EAT, POTION, SPEC, MODIFIER_SELECT.
 * The player tick reuses the shared osrs_encounter_player_step orchestrator for
 * target acquisition, movement, and chase-into-range.
 */

#define COLO_HEAD_MOVE            0  /* 25: idle + 8 walk + 16 run */
#define COLO_HEAD_PRAYER          1  /* overhead protect set */
#define COLO_HEAD_TARGET          2  /* COLO_OBS_NPCS+1: none or obs slot */
#define COLO_HEAD_GEAR            3  /* no_switch, melee, ranged */
#define COLO_HEAD_EAT             4  /* none, eat */
#define COLO_HEAD_POTION          5  /* none, drink */
#define COLO_HEAD_SPEC            6  /* no_change, toggle spec */
#define COLO_HEAD_MODIFIER_SELECT 7  /* none + pick draft option 0/1/2 */

#define COLO_GEAR_DIM 3
#define COLO_EAT_DIM 2
#define COLO_POTION_DIM 2
#define COLO_SPEC_DIM 2
#define COLO_MODIFIER_SELECT_DIM (1 + COLO_MODIFIER_DRAFT_OPTIONS)  /* 4 */

#define COLO_ACTION_DIMS_INIT { \
    ENCOUNTER_MOVE_ACTIONS, \
    ENCOUNTER_OVERHEAD_DIM_PVE, \
    COLO_OBS_NPCS + 1, \
    COLO_GEAR_DIM, \
    COLO_EAT_DIM, \
    COLO_POTION_DIM, \
    COLO_SPEC_DIM, \
    COLO_MODIFIER_SELECT_DIM \
}
static const int COLO_ACTION_DIMS[COLO_NUM_ACTION_HEADS] = COLO_ACTION_DIMS_INIT;
#define COLO_ACTION_MASK_SIZE (ENCOUNTER_MOVE_ACTIONS + ENCOUNTER_OVERHEAD_DIM_PVE + \
    COLO_OBS_NPCS + 1 + COLO_GEAR_DIM + COLO_EAT_DIM + COLO_POTION_DIM + COLO_SPEC_DIM + \
    COLO_MODIFIER_SELECT_DIM)

static int col_action_head_mask_offset(int head) {
    int offset = 0;
    for (int h = 0; h < head; h++)
        offset += COLO_ACTION_DIMS[h];
    return offset;
}


/** apply prayer actions + drain, before movement and attack. */
static void col_player_pretick(ColosseumState* s, ColosseumContext* ctx, const int* actions) {
    (void)ctx;
    if (encounter_apply_overhead_action(&s->player.prayer, actions[COLO_HEAD_PRAYER]))
        s->player.prayer_just_activated = 1;
    encounter_drain_all_prayers(&s->player, encounter_player_prayer_bonus(&s->player));
}


/** apply the active weapon set's loadout to the player. */
static void col_apply_weapon_set(ColosseumState* s, ColoWeaponSet set) {
    if (set < 0 || set >= COLO_NUM_WEAPON_SETS) return;
    if (s->weapon_set == set) return;
    GearSet gear = (set == COLO_GEAR_RANGED) ? GEAR_RANGED : GEAR_MELEE;
    encounter_apply_loadout(&s->player, COLO_LOADOUTS[set], gear);
    s->weapon_set = set;
}


typedef struct {
    ColosseumState* s;
} ColoWalkCtx;

static int col_tile_walkable(void* ctx, int x, int y) {
    ColoWalkCtx* wc = (ColoWalkCtx*)ctx;
    return col_player_walkable(wc->s, x, y);
}

static int col_pathfind_blocked(void* ctx, int abs_x, int abs_y) {
    ColoWalkCtx* wc = (ColoWalkCtx*)ctx;
    return col_player_walkable(wc->s, abs_x, abs_y) ? 0 : 1;
}

/** attack-target lookup callback: returns the NPC at an obs slot. */
static int col_lookup_player_attack_target(void* ctx, int target_slot, OsrsAttackTarget* out) {
    ColosseumState* s = (ColosseumState*)ctx;
    if (target_slot < 0 || target_slot >= COLO_MAX_NPCS) return 0;
    ColoNPC* npc = &s->npcs[target_slot];
    if (!npc->active) return 0;
    out->slot = target_slot;
    out->x = npc->x;
    out->y = npc->y;
    out->size = col_npc_effective_size(npc);
    out->attack_range = COLO_NPC_STATS[npc->type].attack_range > 0
        ? COLO_NPC_STATS[npc->type].attack_range : 1;
    return 1;
}

/** map an obs slot (action target index) to an NPC array index, or -1. */
static int col_obs_slot_to_npc(const ColosseumState* s, int obs_slot) {
    if (obs_slot < 0 || obs_slot >= COLO_OBS_NPCS) return -1;
    return s->current_obs_slots[obs_slot];
}


/**
 * Resolve a player attack on the targeted NPC. Rolls accuracy + damage via the
 * shared combat math and queues a pending hit. Honors the Fremennik RPS weakness:
 * when the player's style matches the NPC's player_style_that_max_hits, the hit is
 * a forced max (full accuracy + max damage).
 */
static void col_player_attack_target(ColosseumState* s, int npc_slot) {
    ColoNPC* npc = &s->npcs[npc_slot];
    const ColoNpcStats* ns = &COLO_NPC_STATS[npc->type];
    const EncounterLoadoutStats* ls = &s->loadout_stats[s->weapon_set];

    int rps_max_hit = ns->player_style_that_max_hits != ATTACK_STYLE_NONE &&
                      ns->player_style_that_max_hits == ls->style;

    int damage;
    if (rps_max_hit) {
        damage = ls->max_hit;
    } else {
        int def_bonus = ls->style == ATTACK_STYLE_MAGIC ? ns->magic_def_bonus
            : ls->style == ATTACK_STYLE_RANGED ? ns->ranged_def_bonus
            : ns->stab_def;
        int target_def_roll = (ns->def_level + 9) * (def_bonus + 64);
        int att_roll = osrs_player_att_roll(ls->eff_level, ls->attack_bonus);
        damage = encounter_npc_roll_attack(att_roll, target_def_roll, ls->max_hit, &s->rng_state);
    }

    int ticks = ls->attack_range > 1 ? 3 : 1;
    col_queue_npc_pending_hit(s, npc_slot, damage, ticks, ls->style, ENCOUNTER_SPELL_NONE);

    s->player.attack_timer = ls->attack_speed > 0 ? ls->attack_speed : 4;
    s->player_attack_npc_idx = npc_slot;
    s->player_attack_dmg = damage;
    s->player_attack_style_id = ls->style;
    s->tick_scratch.player_attacked = 1;
    s->tick_scratch.attacks_fired++;
}


/** the main per-tick player routine. OSRS order: target -> move -> attack. */
static void col_tick_player_ctx(
    ColosseumState* s,
    ColosseumContext* ctx,
    const int* actions,
    int can_attack
) {
    /* gear switch */
    int gear_act = actions[COLO_HEAD_GEAR];
    if (gear_act == 1) col_apply_weapon_set(s, COLO_GEAR_MELEE);
    else if (gear_act == 2) col_apply_weapon_set(s, COLO_GEAR_RANGED);

    /* eat / drink. shared eat transition self-gates on food count + timer. */
    if (s->player.potion_timer > 0) s->player.potion_timer--;
    if (actions[COLO_HEAD_EAT] == 1)
        osrs_player_eat_food_type(&s->player, FOOD_SHARK);
    if (actions[COLO_HEAD_POTION] == 1 &&
            s->player.restore_doses > 0 && s->player.potion_timer == 0) {
        encounter_restore_stats(&s->player);
        s->player.restore_doses--;
        s->player.potion_timer = 3;
    }

    /* spec toggle */
    if (actions[COLO_HEAD_SPEC] == 1)
        s->player.spec_armed = !s->player.spec_armed;

    /* target acquisition: translate the obs-slot target action into an NPC slot */
    int target_act = actions[COLO_HEAD_TARGET];
    int has_new_target = 0;
    int new_target_slot = -1;
    if (target_act > 0) {
        int npc_slot = col_obs_slot_to_npc(s, target_act - 1);
        if (npc_slot >= 0 && s->npcs[npc_slot].active) {
            has_new_target = 1;
            new_target_slot = npc_slot;
        }
    }
    if (has_new_target)
        osrs_interaction_set(&s->interaction, new_target_slot);

    int has_explicit_move = (actions[COLO_HEAD_MOVE] > 0 || s->player_dest_x >= 0);
    if (!has_new_target && has_explicit_move)
        osrs_interaction_check_interrupt(&s->interaction, OSRS_IACT_MOVE);
    if (osrs_interaction_active(&s->interaction)) {
        int slot = s->interaction.target_slot;
        if (slot < 0 || slot >= COLO_MAX_NPCS || !s->npcs[slot].active)
            osrs_interaction_clear(&s->interaction);
    }

    OsrsPlayerMoveKind move_kind = OSRS_PLAYER_MOVE_NONE;
    int move_act = actions[COLO_HEAD_MOVE];
    if (has_explicit_move && !osrs_interaction_active(&s->interaction)) {
        if (s->player_dest_x >= 0) move_kind = OSRS_PLAYER_MOVE_DESTINATION;
        else if (move_act > 0 && move_act < ENCOUNTER_MOVE_ACTIONS)
            move_kind = OSRS_PLAYER_MOVE_ACTION;
    }
    ColoWalkCtx wc = { s };
    (void)osrs_encounter_player_step(&(OsrsPlayerStepInput){
        .player = &s->player,
        .interaction = &s->interaction,
        .target_lookup = col_lookup_player_attack_target,
        .target_ctx = s,
        .has_new_target = has_new_target,
        .new_target_slot = new_target_slot,
        .move_kind = move_kind,
        .move_action = move_act,
        .dest_x = &s->player_dest_x,
        .dest_y = &s->player_dest_y,
        .arena = {
            .collision_map = ctx->collision_map,
            .world_offset_x = ctx->world_offset_x,
            .world_offset_y = ctx->world_offset_y,
            .is_walkable = col_tile_walkable,
            .walkable_ctx = &wc,
            .extra_blocked = col_pathfind_blocked,
            .blocked_ctx = &wc,
            .los_blockers = NULL,
            .los_blocker_count = 0,
            .arena_base_x = COLO_ARENA_MIN_X,
            .arena_base_y = COLO_ARENA_MIN_Y,
            .arena_w = COLO_ARENA_WIDTH,
            .arena_h = COLO_ARENA_HEIGHT,
        },
    });
    col_rebuild_player_collision_flags(s);

    /* attack */
    if (can_attack && s->player.attack_timer > 0) s->player.attack_timer--;
    if (can_attack && osrs_interaction_active(&s->interaction) && s->player.attack_timer == 0) {
        int slot = s->interaction.target_slot;
        ColoNPC* npc = &s->npcs[slot];
        if (npc->active) {
            const EncounterLoadoutStats* ls = &s->loadout_stats[s->weapon_set];
            if (encounter_player_can_attack(s->player.x, s->player.y,
                    npc->x, npc->y, col_npc_effective_size(npc),
                    ls->attack_range, NULL, 0)) {
                col_player_attack_target(s, slot);
            }
        }
    }
}

static void col_tick_player(ColosseumState* s, const int* actions, int can_attack) {
    col_tick_player_ctx(s, col_legacy_context(), actions, can_attack);
}
