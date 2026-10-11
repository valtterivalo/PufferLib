#ifndef GS_WIRE_H
#define GS_WIRE_H
/* The wire the Light replay writes with -Pwiretap: one line per plugin input, fed to the C inference port. */
#include <ctype.h>
#include <stdio.h>
#include "gs_infer.h"
#include "gs_java_items.h"

enum { GS_WIRE_TOKENS = 1024 };

typedef struct { int hp_xp_known, hp_xp, oracle_block; } GsWire;

static void gs_register_java_items(void) {
    for (size_t i = 0; i < sizeof(GS_JAVA_ITEMS) / sizeof(*GS_JAVA_ITEMS); i++) {
        const GsJavaItemRow* r = &GS_JAVA_ITEMS[i];
        int stats[14] = {r->astab, r->aslash, r->acrush, r->arange, r->amagic, r->dstab, r->dslash, r->dcrush, r->drange, r->dmagic, r->str, r->rstr, 0, 0};
        gs_item_register(r->id, r->name, r->has_stats, stats, r->mdmg, r->aspeed, r->two_handed);
        const GsItem* item = gs_item(r->id);
        assert((int)item->weapon == r->weapon);
        assert(item->style == r->style);
        assert(gs_base_speed(r->id) == r->base_speed);
    }
}

static int gs_wire_split(char* line, char** tokens) {
    int n = 0;
    for (char* t = strtok(line, " \n"); t; t = strtok(NULL, " \n")) {
        if (n == GS_WIRE_TOKENS) abort();
        tokens[n++] = t;
    }
    return n;
}

static GsHeadIcon gs_wire_head(const char* name) {
    return !strcmp(name, "NONE") ? GS_HEAD_NONE : !strcmp(name, "MELEE") ? GS_HEAD_MELEE : !strcmp(name, "RANGED") ? GS_HEAD_RANGED : !strcmp(name, "MAGIC") ? GS_HEAD_MAGIC : GS_HEAD_OTHER;
}

static void gs_wire_state(char** t, int n, GsClient* c) {
    int i = 2;
    memset(c, 0, sizeof(*c));
    c->hitpoints = atoi(t[i++]); c->attack = atoi(t[i++]); c->strength = atoi(t[i++]); c->defence = atoi(t[i++]);
    c->ranged = atoi(t[i++]); c->magic = atoi(t[i++]); c->prayer = atoi(t[i++]);
    c->prayers = strtoull(t[i++], NULL, 10);
    c->spec_energy_varp = atoi(t[i++]); c->sa_attack = atoi(t[i++]); c->com_mode = atoi(t[i++]);
    c->lx = atoi(t[i++]); c->ly = atoi(t[i++]); c->local_interacting = atoi(t[i++]);
    for (int k = 0; k < GS_WORN_SLOTS; k++) c->worn[k] = atoi(t[i++]);
    for (int k = 0; k < 28; k++) c->inventory[k] = atoi(t[i++]);
    c->player_count = atoi(t[i++]);
    assert(c->player_count <= GS_MAX_PLAYERS);
    for (int k = 0; k < c->player_count; k++) { c->player_id[k] = atoi(t[i++]); c->player_interacting[k] = atoi(t[i++]); }
    c->tracked = atoi(t[i++]);
    if (c->tracked >= 0) {
        c->tracked_interacting = atoi(t[i++]);
        c->ox = atoi(t[i++]); c->oy = atoi(t[i++]); c->ratio = atoi(t[i++]); c->scale = atoi(t[i++]); c->oweapon = atoi(t[i++]);
        c->overhead = gs_wire_head(t[i++]);
        c->equipment_count = atoi(t[i++]);
        assert(c->equipment_count <= GS_MAX_EQUIPMENT);
        for (int k = 0; k < c->equipment_count; k++) c->equipment[k] = atoi(t[i++]);
    } else c->ratio = c->scale = -1;
    if (i < n && !strcmp(t[i], "ANIMATION")) {
        i++;
        assert(i < n);
        if (!strcmp(t[i], "OBSERVED")) {
            i++;
            assert(i < n);
            char* end;
            long id = strtol(t[i++], &end, 10);
            assert(!*end && id >= -1 && id <= INT_MAX);
            c->opponent_animation = (GsCurrentAnimation){GS_ANIMATION_OBSERVED, (int)id};
        } else {
            assert(!strcmp(t[i++], "UNKNOWN"));
        }
    }
    if (i < n) {
        assert(!strcmp(t[i++], "GEAR") && i < n);
        if (!strcmp(t[i++], "OBSERVED")) {
            int end_gear = i;
            while (end_gear < n && strcmp(t[end_gear], "COMBAT_LEVEL")) end_gear++;
            assert(end_gear - i == 2 || end_gear - i == 2 + GS_ARMOR_SLOTS);
            char* end;
            long weapon = strtol(t[i++], &end, 10);
            assert(!*end && weapon >= -1 && weapon <= INT_MAX);
            long shield = strtol(t[i++], &end, 10);
            assert(!*end && shield >= -1 && shield <= INT_MAX);
            c->opponent_gear = (GsObservedGear){.source = GS_GEAR_WEAPON_SHIELD_OBSERVED, .weapon = (int)weapon, .shield = (int)shield};
            if (i < end_gear) {
                c->opponent_gear.source = GS_GEAR_COMPOSITION_OBSERVED;
                for (int slot = 0; slot < GS_ARMOR_SLOTS; slot++) {
                    long raw = strtol(t[i++], &end, 10);
                    assert(!*end && raw >= -1 && raw <= INT_MAX);
                    c->opponent_gear.armor[slot] = (int)raw;
                }
            }
        } else assert(!strcmp(t[i - 1], "UNKNOWN"));
    }
    while (i < n) {
        if (!strcmp(t[i], "LOCAL_BASE")) {
            i++;
            if (i == n) abort();
            if (!strcmp(t[i], "OBSERVED")) {
                i++;
                if (n - i < 7) abort();
                c->local_base_known = 1;
                for (int skill = 0; skill < 7; skill++) {
                    char* end;
                    long base = strtol(t[i++], &end, 10);
                    if (*end || base < (skill ? 1 : 10) || base > 99) abort();
                    c->local_base[skill] = (int)base;
                }
            } else {
                if (strcmp(t[i++], "UNKNOWN")) abort();
            }
            continue;
        }
        if (!strcmp(t[i], "FORECAST")) {
            i++;
            assert(i < n && (!strcmp(t[i], "AVAILABLE") || !strcmp(t[i], "UNKNOWN")));
            c->forecast_available = !strcmp(t[i++], "AVAILABLE");
            continue;
        }
        int* level;
        if (!strcmp(t[i], "COMBAT_LEVEL")) level = &c->opponent_combat_level;
        else { assert(!strcmp(t[i], "LOCAL_LEVEL")); level = &c->local_combat_level; }
        i++;
        assert(i < n);
        if (!strcmp(t[i], "OBSERVED")) {
            i++;
            assert(i < n);
            char* end;
            long value = strtol(t[i++], &end, 10);
            assert(!*end && value >= 3 && value <= 126);
            *level = (int)value;
        } else assert(!strcmp(t[i++], "UNKNOWN"));
    }
    assert(i == n);
}

static void gs_wire_strip_tags(const char* in, char* out) {
    int skipping = 0;
    for (; *in; in++) {
        if (*in == '<') skipping = 1;
        else if (*in == '>') skipping = 0;
        else if (!skipping) *out++ = *in;
    }
    *out = 0;
}

static void gs_wire_finish(const GsWire* wire) {
    if (wire->oracle_block) {
        fprintf(stderr, "unterminated source wire oracle block\n");
        abort();
    }
}

/* Applies causal inputs only; END-framed S/SS oracle records never replay their
 * target-history T rows as game ticks. Streaming callers must gs_wire_finish. */
static char gs_wire_apply(GsInfer* inf, GsWire* w, const char* line) {
    char copy[1 << 16];
    char* t[GS_WIRE_TOKENS];
    strcpy(copy, line);
    int n = gs_wire_split(copy, t);
    if (n == 0) return 0;
    const char* tag = t[0];
    if (w->oracle_block) {
        if (!strcmp(tag, "END")) {
            if (n != 1) abort();
            w->oracle_block = 0;
        } else if (!strcmp(tag, "RECORDING") || !strcmp(tag, "SS") || !strcmp(tag, "S")) {
            fprintf(stderr, "source wire oracle block missing END before %s\n", tag);
            abort();
        }
        return 0;
    }
    if (!strcmp(tag, "SS")) {
        if (n != 13) abort();
        w->oracle_block = 1;
        return 0;
    }
    if (!strcmp(tag, "END")) {
        fprintf(stderr, "source wire END without oracle block\n");
        abort();
    }
    if (!strcmp(tag, "S")) {
        if (n > 3 && isalpha((unsigned char)t[3][0])) {
            if (strcmp(t[3], "UNKNOWN") && strcmp(t[3], "LOCAL") && strcmp(t[3], "OPPONENT")) abort();
            w->oracle_block = 1;
            return 0;
        }
        gs_wire_state(t, n, &inf->c);
        return 'S';
    }
    if (!strcmp(tag, "I")) gs_on_interacting(inf, atoi(t[2]), atoi(t[3]));
    else if (!strcmp(tag, "A")) gs_on_animation(inf, atoi(t[1]), atoi(t[2]), atoi(t[3]));
    else if (!strcmp(tag, "H")) {
        assert(n == 7 && (!strcmp(t[6], "true") || !strcmp(t[6], "false")));
        gs_on_hitsplat(inf, atoi(t[1]), atoi(t[2]), atoi(t[3]), atoi(t[4]), !strcmp(t[5], "true"), !strcmp(t[6], "true"));
    }
    else if (!strcmp(tag, "O")) { if (!strcmp(t[3], "true")) gs_on_overhead_text(inf, atoi(t[1]), atoi(t[2])); }
    else if (!strcmp(tag, "G")) gs_on_graphic(inf, atoi(t[1]), atoi(t[2]));
    else if (!strcmp(tag, "U")) gs_on_surge(inf, atoi(t[1]));
    else if (!strcmp(tag, "Z")) gs_on_sound(inf, atoi(t[1]), atoi(t[2]), atoi(t[3]), atoi(t[4]));
    else if (!strcmp(tag, "C")) {
        if (!strcmp(t[2], "GAMEMESSAGE") || !strcmp(t[2], "SPAM")) {
            const char* text = strstr(line, t[2]) + strlen(t[2]) + 1;
            char clean[1 << 16];
            gs_wire_strip_tags(text, clean);
            clean[strcspn(clean, "\n")] = 0;
            gs_on_chat(inf, clean);
        }
    }
    else if (!strcmp(tag, "X")) {
        int xp = atoi(t[2]);
        int gained = xp - (w->hp_xp_known ? w->hp_xp : xp);
        w->hp_xp_known = 1;
        w->hp_xp = xp;
        if (gained > 0) {
            if (inf->c.opponent_combat_level) gs_on_hitpoints_gain(inf, atoi(t[1]), gained, inf->c.opponent_combat_level);
            else inf->local_launch_unknown = 1;
        }
    }
    else if (!strcmp(tag, "FHP")) {
        assert(n == 3);
        int gained = atoi(t[2]);
        if (gained > 0) {
            if (inf->c.opponent_combat_level) gs_on_hitpoints_gain(inf, atoi(t[1]), gained, inf->c.opponent_combat_level);
            else inf->local_launch_unknown = 1;
        }
    }
    else if (!strcmp(tag, "N")) { int units[3] = {atoi(t[2]), atoi(t[3]), atoi(t[4])}; gs_on_inventory(inf, atoi(t[1]), units); }
    else if (!strcmp(tag, "T")) {
        int tick = atoi(t[1]);
        gs_on_game_tick(inf, tick);
        if (inf->timed >= 0 && inf->c.ratio != 0) gs_eater_observe(inf, tick);
    }
    else return 0;
    return tag[0];
}
#endif
