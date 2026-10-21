/* Rooms with a boss implementation; start_room = -1 samples one per episode. */
static const int TFA_IMPLEMENTED_ROOMS[] = { TFA_ROOM_GUARDIAN, TFA_ROOM_XARTH, TFA_ROOM_DREADNOTH };
#define TFA_NUM_IMPLEMENTED_ROOMS ((int)(sizeof(TFA_IMPLEMENTED_ROOMS) / sizeof(*TFA_IMPLEMENTED_ROOMS)))

static void tfa_boss_enter(TfaState* s) {
    switch ((TfaRoom)s->room) {
        case TFA_ROOM_GUARDIAN: tfa_guardian_enter(s); return;
        case TFA_ROOM_XARTH: tfa_xarth_enter(s); return;
        case TFA_ROOM_DREADNOTH: tfa_dread_enter(s); return;
        default: break;
    }
    fprintf(stderr, "tfa room %d is not implemented\n", s->room);
    abort();
}

static void tfa_boss_tick(TfaState* s, TfaContext* ctx) {
    switch ((TfaRoom)s->room) {
        case TFA_ROOM_GUARDIAN: tfa_guardian_tick(s, ctx); return;
        case TFA_ROOM_XARTH: tfa_xarth_tick(s, ctx); return;
        case TFA_ROOM_DREADNOTH: tfa_dread_tick(s, ctx); return;
        default: break;
    }
    abort();
}

static int tfa_boss_npc_damage(TfaState* s, TfaNpc* n, int damage, int attacker, AttackStyle style) {
    switch ((TfaRoom)s->room) {
        case TFA_ROOM_GUARDIAN: return tfa_guardian_npc_damage(s, n, damage, attacker, style);
        case TFA_ROOM_XARTH: return tfa_xarth_npc_damage(s, n, damage, attacker, style);
        case TFA_ROOM_DREADNOTH: return tfa_dread_npc_damage(s, n, damage, attacker, style);
        default: break;
    }
    abort();
}

static void tfa_boss_npc_died(TfaState* s, TfaNpc* n, int attacker) {
    switch ((TfaRoom)s->room) {
        case TFA_ROOM_GUARDIAN: tfa_guardian_npc_died(s, n, attacker); return;
        case TFA_ROOM_XARTH: tfa_xarth_npc_died(s, n, attacker); return;
        case TFA_ROOM_DREADNOTH: tfa_dread_npc_died(s, n, attacker); return;
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
