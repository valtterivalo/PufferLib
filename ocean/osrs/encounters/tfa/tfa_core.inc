#define TFA_PI 3.14159265358979323846

static int tfa_rand(TfaState* s, int n) {
    return encounter_rand_int(&s->rng_state, n);
}

static int tfa_rand_range(TfaState* s, int lo, int hi) {
    return lo + encounter_rand_int(&s->rng_state, hi - lo + 1);
}

static int tfa_alive(const TfaState* s, int p) {
    return p >= 0 && p < s->team_size && s->players[p].life == TFA_LIFE_ALIVE;
}

/* Boss HP and shields scale with party size: duo values from the wiki, trio values from recorded kills, a solo is scaled as a duo.
   Every measured boss steps duo to trio by 1.45 (Ezekiel 3000 to 4350, Xarth 2600 to 3770, Dreadnoth 2100 to 3050, Guardian 1100 to 1575)
   and each raider past three adds 0.419 duo (5-raider kills: Xarth 5950, Dreadnoth 4810, Bork 3440, Ocellus P1 replay 5698-5728). */
#define TFA_TRIO_SCALE_PCT 145
#define TFA_EXTRA_RAIDER_PERMILLE 419

static int tfa_party_hp(int party, int duo, int trio) {
    return party <= 2 ? duo : trio + duo * TFA_EXTRA_RAIDER_PERMILLE * (party - 3) / 1000;
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
    uint16_t weapon = tp->player.equipped[GEAR_SLOT_WEAPON];
    AttackStyle style = (AttackStyle)get_item_attack_style(weapon);
    if (style == ATTACK_STYLE_MAGIC && !osrs_item_is_powered_staff(weapon)) return ATTACK_STYLE_MELEE;
    return style == ATTACK_STYLE_NONE ? ATTACK_STYLE_MELEE : style;
}

static FightStyle tfa_default_fight_style(AttackStyle style) {
    return style == ATTACK_STYLE_RANGED ? FIGHT_STYLE_RAPID : FIGHT_STYLE_ACCURATE;
}

static const EncounterLoadoutStats* tfa_player_stats(TfaPlayer* tp) {
    if (tp->stats_dirty) {
        AttackStyle style = tfa_player_weapon_style(tp);
        uint16_t weapon = tp->player.equipped[GEAR_SLOT_WEAPON];
        int spell = style == ATTACK_STYLE_MAGIC ? osrs_powered_staff_base_damage(weapon, tp->player.current_magic) : 0;
        encounter_compute_player_equipped_stats(&tp->player, style, tfa_default_fight_style(style), spell, &tp->stats);
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

static void tfa_boss_npc_hit_player(TfaState* s, TfaNpc* src, int raw, int dealt);

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
        if (h.check_at_land && h.source_slot >= 0) tfa_boss_npc_hit_player(s, &s->npcs[h.source_slot], h.damage, dmg);
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
        int freezes = h->spell_type == ENCOUNTER_SPELL_ICE;
        encounter_pending_hit_queue_remove(q, i, "tfa-npc");
        i--;
        if (!n->active) continue;
        if (freezes && n->frozen_ticks < BARRAGE_FREEZE_TICKS) n->frozen_ticks = BARRAGE_FREEZE_TICKS;
        tfa_npc_take_damage(s, n, damage, attacker, style);
    }
}

/* Player hits on NPCs carry the attacking player's index in source_npc_slot; a landed ice spell freezes the NPC as it lands. */
static void tfa_queue_player_hit(TfaState* s, TfaNpc* n, int attacker, int damage, AttackStyle style, int delay, int freezes) {
    EncounterPendingHit hit = {
        .active = 1,
        .ticks_remaining = (int8_t)delay,
        .attack_style = (int8_t)style,
        .spell_type = (int8_t)(freezes ? ENCOUNTER_SPELL_ICE : ENCOUNTER_SPELL_NONE),
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

static int tfa_npc_def_level(const TfaNpc* n) {
    return MONSTER_DATABASE[n->monster].def_level - n->def_drain;
}

/* Special attack drains floor Defence at 0. */
/* Drains stop at 60% of defence on the Guardian, Bork, Ezekiel, Moia, Xarth and Ocellus (P1 129, P2/P3 150); Dreadnoth, the Smorks, the Blade and
   the Dagon'hai drain to 0 (Jagex hotfix, 21 Oct). Fights recorded before it drained past the caps (Xarth misses 0.00-0.07 after two Division
   specials); run 6 (15:19 UTC) misses Ocellus P1 at 0.29/0.37 after them, as at 129. Division drains an eighth of Magic per damaging throw. */
static void tfa_npc_drain_defence(TfaNpc* n, const SpecResult* sr) {
    const MonsterStats* m = &MONSTER_DATABASE[n->monster];
    int max_drain = m->def_level - m->def_level * m->def_floor_pct / 100;
    n->def_drain += sr->def_drain + m->magic_level / 8 * sr->def_drain_magic_hits;
    if (n->def_drain > max_drain) n->def_drain = max_drain;
}

/* A stunned NPC has no defence: the zero-hit fraction fell from 23% to 3% of player hits during Ezekiel's weakened window. */
static int tfa_npc_def_roll(const TfaNpc* n, AttackStyle style, int melee_style, uint16_t weapon) {
    if (n->act == TFA_ACT_STUNNED) return 0;
    const MonsterStats* m = &MONSTER_DATABASE[n->monster];
    return encounter_npc_target_def_roll(
        tfa_npc_def_level(n), m->magic_level, m->stab_def, m->slash_def, m->crush_def, m->magic_def,
        osrs_ranged_def_bonus_for_weapon(weapon, m->ranged_light_def, m->ranged_def, m->ranged_heavy_def), style, melee_style);
}

static void tfa_hazard_add(TfaState* s, TfaHazard h) {
    for (int i = 0; i < TFA_MAX_HAZARDS; i++) {
        if (s->hazards[i].kind != TFA_HAZ_NONE) continue;
        TfaHazard* d = &s->hazards[i];
        memset(d, 0, sizeof(*d));
        d->kind = h.kind; d->style = h.style; d->w = h.w; d->h = h.h; d->owner = h.owner; d->effect = h.effect;
        d->x = h.x; d->y = h.y; d->start_tick = h.start_tick; d->end_tick = h.end_tick; d->arm_ticks = h.arm_ticks;
        d->min_damage = h.min_damage; d->max_damage = h.max_damage; d->protected_pct = h.protected_pct; d->gfx_id = h.gfx_id; d->loc_id = h.loc_id; d->orientation = h.orientation;
        if (i >= s->hazard_hi) s->hazard_hi = i + 1;
        return;
    }
    int loc = s->hazards[0].loc_id, same = 0;
    for (int i = 0; i < TFA_MAX_HAZARDS; i++) same += s->hazards[i].loc_id == loc;
    fprintf(stderr, "tfa hazard capacity %d exceeded room %d tick %d (%d of loc %d)\n", TFA_MAX_HAZARDS, s->room, s->tick, same, loc);
    abort();
}

static int tfa_hazard_covers(const TfaHazard* h, int x, int y) {
    return x >= h->x && x < h->x + h->w && y >= h->y && y < h->y + h->h;
}

/* Ticks until a hazard can hit each tile (-1 none within TFA_DANGER_HORIZON), rebuilt after every step and reset so observations and
   the scripted raiders read tiles instead of scanning hazards: grid TFA_MAX_PLAYERS holds the hazards that hit everyone, grid p
   those owned by player p. Each build clears only the previous build's rects. */
static void tfa_danger_build(TfaState* s) {
    while (s->hazard_hi > 0 && s->hazards[s->hazard_hi - 1].kind == TFA_HAZ_NONE) s->hazard_hi--;
    for (int r = 0; r < s->danger_rect_count; r++) {
        const TfaDangerRect* d = &s->danger_rects[r];
        int8_t* grid = s->danger[d->owner < 0 ? TFA_MAX_PLAYERS : d->owner];
        for (int y = d->y0; y < d->y1; y++) memset(&grid[y * TFA_ROOM_SIZE + d->x0], -1, (size_t)(d->x1 - d->x0));
    }
    s->danger_rect_count = 0;
    for (int i = 0; i < s->hazard_hi; i++) {
        const TfaHazard* h = &s->hazards[i];
        if (h->kind == TFA_HAZ_NONE || h->kind == TFA_HAZ_LOC) continue;
        int t, x0 = h->x, y0 = h->y, x1 = h->x + h->w, y1 = h->y + h->h;
        if (h->kind == TFA_HAZ_MUSHROOM) {
            t = h->arm_ticks > 0 ? h->arm_ticks : 1;
            x0--; y0--; x1++; y1++;
        } else {
            t = h->start_tick > s->tick ? h->start_tick - s->tick : 0;
        }
        if (t > TFA_DANGER_HORIZON) continue;
        x0 = x0 < 0 ? 0 : x0; y0 = y0 < 0 ? 0 : y0;
        x1 = x1 > TFA_ROOM_SIZE ? TFA_ROOM_SIZE : x1; y1 = y1 > TFA_ROOM_SIZE ? TFA_ROOM_SIZE : y1;
        if (x0 >= x1 || y0 >= y1) continue;
        s->danger_rects[s->danger_rect_count++] = (TfaDangerRect){ (int8_t)x0, (int8_t)y0, (int8_t)x1, (int8_t)y1, h->owner };
        int8_t* grid = s->danger[h->owner < 0 ? TFA_MAX_PLAYERS : h->owner];
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++) {
                int8_t* d = &grid[y * TFA_ROOM_SIZE + x];
                if (*d < 0 || t < *d) *d = (int8_t)t;
            }
    }
}

static void tfa_ocellus_tendril_hit(TfaState* s, int p);

static void tfa_hazard_hit_player(TfaState* s, const TfaHazard* h, int p) {
    TfaPlayer* tp = &s->players[p];
    if (h->effect == TFA_EFFECT_TENDRIL) {
        tfa_ocellus_tendril_hit(s, p);
        return;
    }
    int raw = tfa_rand_range(s, h->min_damage, h->max_damage);
    int dmg = tfa_protected_damage(raw, tp->player.prayer, (AttackStyle)h->style, h->protected_pct);
    tfa_damage_player_now(s, p, dmg, (AttackStyle)h->style);
    if (h->effect == TFA_EFFECT_POISON && dmg > 0)
        osrs_poison_apply(&tp->poison, 6, OSRS_POISON_PERIOD);
    if (h->effect == TFA_EFFECT_BURN)
        tp->burn_ticks = 10;
}

/* A one-shot spotanim gfx_id on tile (x, y), starting delay client cycles after this tick. */
static void tfa_fx_ground(TfaState* s, int x, int y, int gfx_id, int delay) {
    if (s->fx_count >= TFA_MAX_FX) {
        fprintf(stderr, "tfa fx capacity %d exceeded tick=%d\n", TFA_MAX_FX, s->tick);
        abort();
    }
    s->fx[s->fx_count++] = (TfaFx){ .kind = TFA_FX_GROUND, .x = (int16_t)x, .y = (int16_t)y, .gfx_id = gfx_id, .delay = (int16_t)delay };
}

/* A projectile from NPC src to player p, launched delay ticks from now and flying duration ticks. */
static void tfa_fx_projectile(TfaState* s, const TfaNpc* src, int p, int gfx_id, uint32_t model, int anim, int delay, int duration) {
    if (s->fx_count >= TFA_MAX_FX) {
        fprintf(stderr, "tfa fx capacity %d exceeded tick=%d\n", TFA_MAX_FX, s->tick);
        abort();
    }
    s->fx[s->fx_count++] = (TfaFx){
        .kind = TFA_FX_PROJECTILE,
        .src_slot = (int8_t)tfa_npc_slot(s, src),
        .dst_player = (int8_t)p,
        .delay = (int16_t)delay,
        .duration = (int16_t)duration,
        .gfx_id = gfx_id,
        .model = model,
        .anim = anim,
    };
}
