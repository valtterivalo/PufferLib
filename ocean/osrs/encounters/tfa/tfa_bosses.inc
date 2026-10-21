static void tfa_boss_enter(TfaState* s) {
    switch ((TfaRoom)s->room) {
        case TFA_ROOM_GUARDIAN: tfa_guardian_enter(s); return;
        case TFA_ROOM_BORK: tfa_bork_enter(s); return;
        case TFA_ROOM_MOIA: tfa_moia_enter(s); return;
        case TFA_ROOM_XARTH: tfa_xarth_enter(s); return;
        default: break;
    }
    fprintf(stderr, "tfa room %d is not implemented\n", s->room);
    abort();
}

static void tfa_boss_tick(TfaState* s, TfaContext* ctx) {
    switch ((TfaRoom)s->room) {
        case TFA_ROOM_GUARDIAN: tfa_guardian_tick(s, ctx); return;
        case TFA_ROOM_BORK: tfa_bork_tick(s, ctx); return;
        case TFA_ROOM_MOIA: tfa_moia_tick(s, ctx); return;
        case TFA_ROOM_XARTH: tfa_xarth_tick(s, ctx); return;
        default: break;
    }
    abort();
}

static int tfa_boss_npc_damage(TfaState* s, TfaNpc* n, int damage, int attacker, AttackStyle style) {
    switch ((TfaRoom)s->room) {
        case TFA_ROOM_GUARDIAN: return tfa_guardian_npc_damage(s, n, damage, attacker, style);
        case TFA_ROOM_BORK: return tfa_bork_npc_damage(s, n, damage, attacker, style);
        case TFA_ROOM_MOIA: return tfa_moia_npc_damage(s, n, damage, attacker, style);
        case TFA_ROOM_XARTH: return tfa_xarth_npc_damage(s, n, damage, attacker, style);
        default: break;
    }
    abort();
}

static void tfa_boss_npc_died(TfaState* s, TfaNpc* n, int attacker) {
    switch ((TfaRoom)s->room) {
        case TFA_ROOM_GUARDIAN: tfa_guardian_npc_died(s, n, attacker); return;
        case TFA_ROOM_BORK: tfa_bork_npc_died(s, n, attacker); return;
        case TFA_ROOM_MOIA: tfa_moia_npc_died(s, n, attacker); return;
        case TFA_ROOM_XARTH: tfa_xarth_npc_died(s, n, attacker); return;
        default: break;
    }
    abort();
}

static int tfa_npc_targets(const TfaState* s, const TfaNpc* n, int p) {
    (void)s;
    return n->target == p;
}

static int tfa_npc_act_ticks(const TfaState* s, const TfaNpc* n) {
    (void)s;
    return n->attack_timer;
}
