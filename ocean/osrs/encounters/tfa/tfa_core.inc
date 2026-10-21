static int tfa_rand(TfaState* s, int n) {
    return encounter_rand_int(&s->rng_state, n);
}

static int tfa_rand_range(TfaState* s, int lo, int hi) {
    return lo + encounter_rand_int(&s->rng_state, hi - lo + 1);
}

static int tfa_alive(const TfaState* s, int p) {
    return p >= 0 && p < s->team_size && s->players[p].life == TFA_LIFE_ALIVE;
}

static int tfa_alive_count(const TfaState* s) {
    int n = 0;
    for (int p = 0; p < s->team_size; p++) n += tfa_alive(s, p);
    return n;
}

/* Targetable NPCs live in slots [0, TFA_NUM_TARGET_SLOTS) so a target slot names
   the same NPC for its whole life; scenery NPCs fill the rest. */
static TfaNpc* tfa_npc_alloc(TfaState* s, TfaNpcKind kind, int npc_id, int size, int x, int y, int targetable) {
    int lo = targetable ? 0 : TFA_NUM_TARGET_SLOTS;
    int hi = targetable ? TFA_NUM_TARGET_SLOTS : TFA_MAX_NPCS;
    for (int i = lo; i < hi; i++) {
        TfaNpc* n = &s->npcs[i];
        if (n->active) continue;
        *n = (TfaNpc){
            .active = 1,
            .kind = (uint8_t)kind,
            .size = (uint8_t)size,
            .target = -1,
            .monster = -1,
            .npc_id = npc_id,
            .x = (int16_t)x,
            .y = (int16_t)y,
            .anim_id = -1,
            .instance_id = ++s->next_instance_id,
        };
        return n;
    }
    fprintf(stderr, "tfa npc slots [%d, %d) full spawning kind %d\n", lo, hi, kind);
    abort();
}

/* A combat NPC: stats from MONSTER_DATABASE. */
static TfaNpc* tfa_npc_spawn(TfaState* s, TfaNpcKind kind, MonsterIndex monster, int npc_id, int x, int y) {
    const MonsterStats* m = &MONSTER_DATABASE[monster];
    TfaNpc* n = tfa_npc_alloc(s, kind, npc_id, m->size, x, y, 1);
    n->monster = (int16_t)monster;
    n->attackable = 1;
    n->hp = m->hp;
    n->max_hp = m->hp;
    return n;
}

/* A scenery-like NPC that players cannot attack: pylons, telegraph actors. */
static TfaNpc* tfa_npc_spawn_inert(TfaState* s, TfaNpcKind kind, int npc_id, int size, int x, int y) {
    return tfa_npc_alloc(s, kind, npc_id, size, x, y, 0);
}

static int tfa_npc_slot(const TfaState* s, const TfaNpc* n) {
    return (int)(n - s->npcs);
}

static TfaNpc* tfa_npc_first(TfaState* s, TfaNpcKind kind) {
    for (int i = 0; i < TFA_MAX_NPCS; i++)
        if (s->npcs[i].active && s->npcs[i].kind == kind) return &s->npcs[i];
    return NULL;
}

static void tfa_npc_anim(TfaState* s, TfaNpc* n, int anim_id) {
    n->anim_id = anim_id;
    n->anim_tick = s->tick;
}

static int tfa_player_npc_distance(const TfaPlayer* tp, const TfaNpc* n) {
    return encounter_rect_distance(tp->player.x, tp->player.y, 1, n->x, n->y, n->size);
}

static void tfa_mark_stats_dirty(TfaPlayer* tp) {
    tp->stats_dirty = 1;
}

/* An empty weapon slot punches: melee. */
static AttackStyle tfa_player_weapon_style(const TfaPlayer* tp) {
    AttackStyle style = (AttackStyle)get_item_attack_style(tp->player.equipped[GEAR_SLOT_WEAPON]);
    return style == ATTACK_STYLE_NONE ? ATTACK_STYLE_MELEE : style;
}

static FightStyle tfa_default_fight_style(AttackStyle style) {
    return style == ATTACK_STYLE_RANGED ? FIGHT_STYLE_RAPID : FIGHT_STYLE_ACCURATE;
}

static const EncounterLoadoutStats* tfa_player_stats(TfaPlayer* tp) {
    if (tp->stats_dirty) {
        AttackStyle style = tfa_player_weapon_style(tp);
        assert(style != ATTACK_STYLE_MAGIC && "tfa loadouts carry no magic weapon until powered staff damage is shared");
        encounter_compute_player_equipped_stats(
            &tp->player, style, tfa_default_fight_style(style), 0, &tp->stats);
        tp->stats_dirty = 0;
    }
    return &tp->stats;
}

/* Raw damage to a player after the throw-time protection rule: a correct
   overhead scales the hit to protected_pct percent (0 blocks it, 100 ignores prayer). */
static int tfa_protected_damage(int raw, int overhead, AttackStyle style, int protected_pct) {
    if (style == ATTACK_STYLE_NONE || !encounter_prayer_correct_for_style(overhead, style))
        return raw;
    return raw * protected_pct / 100;
}

static void tfa_damage_player_now(TfaState* s, int p, int damage, AttackStyle style) {
    TfaPlayer* tp = &s->players[p];
    if (tp->life != TFA_LIFE_ALIVE) return;
    int before = tp->player.current_hitpoints;
    encounter_damage_player(&tp->player, damage, NULL);
    int applied = before - tp->player.current_hitpoints;
    if (tp->player.render_hit_count < OSRS_RENDER_HITS_MAX)
        tp->player.render_hit_damage[tp->player.render_hit_count++] = damage;
    tp->player.hit_style = style;
    tp->damage_taken_tick += (float)applied;
    tp->damage_taken_total += (float)applied;
}

static void tfa_incoming_push(TfaState* s, int p, TfaIncomingHit hit) {
    TfaIncomingQueue* q = &s->players[p].incoming;
    if (q->count >= ENCOUNTER_MAX_PENDING_HITS) {
        fprintf(stderr, "tfa incoming hit queue full tick=%d player=%d\n", s->tick, p);
        abort();
    }
    q->hits[q->count++] = hit;
}

/* NPC attack on player p computed this tick (the throw), landing after delay ticks: the prayer
   active now decides it (Guardian ranged: a flick effective one tick after the anim does not count). */
static void tfa_queue_npc_hit(
    TfaState* s, int p, int raw, AttackStyle style, int delay, int protected_pct, const TfaNpc* src
) {
    TfaPlayer* tp = &s->players[p];
    if (tp->life != TFA_LIFE_ALIVE) return;
    int capped = raw > tp->player.current_hitpoints ? tp->player.current_hitpoints : raw;
    int dmg = tfa_protected_damage(capped, tp->player.prayer, style, protected_pct);
    if (delay <= 0) {
        tfa_damage_player_now(s, p, dmg, style);
        return;
    }
    tfa_incoming_push(s, p, (TfaIncomingHit){
        .ticks_remaining = (int8_t)delay,
        .attack_style = (int8_t)style,
        .source_slot = (int8_t)(src ? tfa_npc_slot(s, src) : -1),
        .damage = (int16_t)dmg,
    });
}

/* As tfa_queue_npc_hit, but the protection prayer active on the landing tick decides it
   (Xarth: a prayer effective on the landing tick blocked 3 of 3 recorded ranged hits). */
static void tfa_queue_npc_hit_land(
    TfaState* s, int p, int raw, AttackStyle style, int delay, int protected_pct, const TfaNpc* src
) {
    TfaPlayer* tp = &s->players[p];
    if (tp->life != TFA_LIFE_ALIVE) return;
    assert(delay > 0);
    int capped = raw > tp->player.current_hitpoints ? tp->player.current_hitpoints : raw;
    tfa_incoming_push(s, p, (TfaIncomingHit){
        .ticks_remaining = (int8_t)delay,
        .attack_style = (int8_t)style,
        .check_at_land = 1,
        .protected_pct = (int8_t)protected_pct,
        .source_slot = (int8_t)(src ? tfa_npc_slot(s, src) : -1),
        .damage = (int16_t)capped,
    });
}

/* Accuracy-rolled NPC attack against player p using the NPC's database stats. */
static int tfa_npc_roll(TfaState* s, const TfaNpc* n, int p, AttackStyle style, int max_hit) {
    const MonsterStats* m = &MONSTER_DATABASE[n->monster];
    TfaPlayer* tp = &s->players[p];
    const EncounterLoadoutStats* ls = tfa_player_stats(tp);
    int att_roll = style == ATTACK_STYLE_MAGIC
        ? osrs_npc_attack_roll(m->magic_level, m->magic_att_bonus)
        : style == ATTACK_STYLE_RANGED
            ? osrs_npc_attack_roll(m->range_level, m->range_att_bonus)
            : osrs_npc_attack_roll(m->att_level, m->melee_att_bonus);
    int def_roll = encounter_player_def_roll_from_loadout(
        tp->player.current_defence, tp->player.current_magic,
        ls->def_stab, ls->def_slash, ls->def_crush, ls->def_magic, ls->def_ranged,
        style, 2);
    return encounter_npc_roll_attack(att_roll, def_roll, max_hit, &s->rng_state);
}

static void tfa_resolve_player_hits(TfaState* s, int p) {
    TfaPlayer* tp = &s->players[p];
    TfaIncomingQueue* q = &tp->incoming;
    int kept = 0;
    for (int i = 0; i < q->count; i++) {
        TfaIncomingHit h = q->hits[i];
        if (--h.ticks_remaining > 0) {
            q->hits[kept++] = h;
            continue;
        }
        int dmg = h.check_at_land
            ? tfa_protected_damage(h.damage, tp->player.prayer, (AttackStyle)h.attack_style, h.protected_pct)
            : h.damage;
        tfa_damage_player_now(s, p, dmg, (AttackStyle)h.attack_style);
    }
    q->count = kept;
}

static int tfa_npc_take_damage(TfaState* s, TfaNpc* n, int damage, int attacker, AttackStyle style);

static void tfa_resolve_npc_hits(TfaState* s, TfaNpc* n) {
    EncounterPendingHitQueue* q = &n->hits;
    for (int i = 0; i < q->count; i++) {
        EncounterPendingHit* h = &q->hits[i];
        if (--h->ticks_remaining > 0) continue;
        int attacker = h->source_npc_slot;
        int damage = h->damage;
        AttackStyle style = (AttackStyle)h->attack_style;
        encounter_pending_hit_queue_remove(q, i, "tfa-npc");
        i--;
        if (n->active) tfa_npc_take_damage(s, n, damage, attacker, style);
    }
}

/* Player hits on NPCs carry the attacking player's index in source_npc_slot. */
static void tfa_queue_player_hit(TfaState* s, TfaNpc* n, int attacker, int damage, AttackStyle style, int delay) {
    EncounterPendingHit hit = {
        .active = 1,
        .ticks_remaining = (int8_t)delay,
        .attack_style = (int8_t)style,
        .spell_type = ENCOUNTER_SPELL_NONE,
        .source_npc_type = -1,
        .source_npc_slot = (int8_t)attacker,
        .hit_success = (int8_t)(damage > 0),
        .damage = (int16_t)damage,
    };
    if (delay <= 0) {
        tfa_npc_take_damage(s, n, damage, attacker, style);
        return;
    }
    encounter_pending_hit_queue_push(&n->hits, hit, "tfa-npc", s->tick, tfa_npc_slot(s, n), style);
}

static void tfa_credit_damage(TfaState* s, int attacker, int applied) {
    if (attacker < 0) return;
    s->players[attacker].damage_dealt_tick += (float)applied;
    s->players[attacker].damage_dealt_total += (float)applied;
    s->team_damage_dealt_tick += (float)applied;
}

static int tfa_npc_def_roll(const TfaNpc* n, AttackStyle style, int melee_style) {
    const MonsterStats* m = &MONSTER_DATABASE[n->monster];
    return encounter_npc_target_def_roll(
        m->def_level, m->magic_level, m->stab_def, m->slash_def, m->crush_def,
        m->magic_def, m->ranged_def, style, melee_style);
}

static void tfa_hazard_add(TfaState* s, TfaHazard h) {
    for (int i = 0; i < TFA_MAX_HAZARDS; i++) {
        if (s->hazards[i].kind != TFA_HAZ_NONE) continue;
        s->hazards[i] = h;
        return;
    }
    fprintf(stderr, "tfa hazard capacity %d exceeded\n", TFA_MAX_HAZARDS);
    abort();
}

static int tfa_hazard_covers(const TfaHazard* h, int x, int y) {
    return x >= h->x && x < h->x + h->w && y >= h->y && y < h->y + h->h;
}

static void tfa_hazard_hit_player(TfaState* s, const TfaHazard* h, int p) {
    TfaPlayer* tp = &s->players[p];
    int raw = tfa_rand_range(s, h->min_damage, h->max_damage);
    int dmg = tfa_protected_damage(raw, tp->player.prayer, (AttackStyle)h->style, h->protected_pct);
    tfa_damage_player_now(s, p, dmg, (AttackStyle)h->style);
    if (h->effect == TFA_EFFECT_POISON && dmg > 0)
        osrs_poison_apply(&tp->poison, 6, OSRS_POISON_PERIOD);
    if (h->effect == TFA_EFFECT_BURN)
        tp->burn_ticks = 10;
}
