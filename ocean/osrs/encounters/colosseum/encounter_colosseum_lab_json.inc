#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_lab_json.inc
 * @brief JSON scenario dump for the Colosseum lab (split from the parser to keep
 *        each .inc under ~500 lines). Reuses the name tables defined in
 *        encounter_colosseum_lab_parse.inc, which is included before this file.
 */

static void col_lab_string_init(ColoLabString* out) {
    out->len = 0;
    out->cap = 4096;
    out->data = (char*)malloc(out->cap);
    if (!out->data) col_lab_abort("out of memory");
    out->data[0] = '\0';
}

static void col_lab_string_reserve(ColoLabString* out, size_t need) {
    if (need <= out->cap) return;
    size_t next = out->cap;
    while (next < need) {
        if (next > SIZE_MAX / 2) col_lab_abort("json output too large");
        next *= 2;
    }
    char* data = (char*)realloc(out->data, next);
    if (!data) col_lab_abort("out of memory");
    out->data = data;
    out->cap = next;
}

static void col_lab_string_append(ColoLabString* out, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) col_lab_abort("json formatting failed");
    col_lab_string_reserve(out, out->len + (size_t)needed + 1);
    int written = vsnprintf(out->data + out->len, out->cap - out->len, fmt, args);
    va_end(args);
    if (written != needed) col_lab_abort("json formatting length mismatch");
    out->len += (size_t)written;
}

static void col_lab_append_npcs_json(ColosseumState* s, ColoLabString* out) {
    col_lab_string_append(out, "\"npcs\":[");
    int emitted = 0;
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        ColoNPC* npc = &s->npcs[i];
        if (!npc->active) continue;
        if (emitted > 0) col_lab_string_append(out, ",");
        const EncounterPendingHit* pending =
            encounter_pending_hit_queue_earliest(&npc->pending_hits);
        int pending_ticks = pending ? pending->ticks_remaining : 0;
        col_lab_string_append(out,
            "{\"slot\":%d,\"type\":\"%s\",\"type_id\":%d,\"def_id\":%d,"
            "\"x\":%d,\"y\":%d,\"size\":%d,\"hp\":%d,\"max_hp\":%d,"
            "\"timer\":%d,\"stun\":%d,\"frozen\":%d,\"death_ticks\":%d,"
            "\"pending_count\":%d,\"pending_earliest_ticks\":%d}",
            i, col_lab_npc_type_name(npc->type), npc->type,
            COLO_NPC_DEF_IDS[npc->type], npc->x, npc->y,
            col_npc_effective_size(npc), npc->hp, npc->max_hp,
            npc->attack_timer, npc->stun_timer, npc->frozen_ticks,
            npc->death_ticks, npc->pending_hits.count, pending_ticks);
        emitted++;
    }
    col_lab_string_append(out, "]");
}

static void col_lab_append_modifiers_json(const ColosseumState* s, ColoLabString* out) {
    col_lab_string_append(out, "\"modifiers\":{\"active\":[");
    int emitted = 0;
    for (int m = 0; m < COLO_NUM_REAL_MODIFIERS; m++) {
        if (!(s->modifiers.active_mask & (1u << (unsigned)m))) continue;
        if (emitted > 0) col_lab_string_append(out, ",");
        col_lab_string_append(out, "{\"id\":%d,\"name\":\"%s\",\"tier\":%d}",
            m, col_lab_modifier_name(m), s->modifiers.tier[m]);
        emitted++;
    }
    col_lab_string_append(out, "],\"draft_pending\":%d,\"draft_options\":[",
        s->modifiers.draft_pending);
    for (int o = 0; o < COLO_MODIFIER_DRAFT_OPTIONS; o++) {
        if (o > 0) col_lab_string_append(out, ",");
        col_lab_string_append(out, "%d", s->modifiers.draft_options[o]);
    }
    col_lab_string_append(out, "]}");
}

static char* col_lab_alloc_json(ColosseumState* s) {
    ColoLabString out;
    col_lab_string_init(&out);
    col_lab_string_append(&out,
        "{\"tick\":%d,\"wave\":%d,"
        "\"player\":{\"x\":%d,\"y\":%d,\"hp\":%d,\"max_hp\":%d,\"prayer\":\"%s\","
        "\"attack_timer\":%d,\"weapon_set\":%d,\"attacked\":%d,"
        "\"attack_target\":%d,\"attack_damage\":%d},",
        s->tick, s->wave + 1,
        s->player.x, s->player.y, s->player.current_hitpoints,
        s->player.base_hitpoints, col_lab_overhead_name(s->player.prayer),
        s->player.attack_timer, s->weapon_set, s->tick_scratch.player_attacked,
        s->player_attack_npc_idx, s->player_attack_dmg);
    col_lab_append_modifiers_json(s, &out);
    col_lab_string_append(&out, ",");
    col_lab_string_append(&out,
        "\"boss\":{\"started\":%d,\"phase\":%d,\"grapple_active\":%d,"
        "\"grapple_slot\":%d,\"parry_active\":%d},",
        s->sol.started, s->sol.phase, s->sol.grapple_active,
        s->sol.grapple_body_slot, s->sol.parry_sequence_active);
    col_lab_append_npcs_json(s, &out);
    col_lab_string_append(&out, "}");
    return out.data;
}
