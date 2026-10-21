#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../encounters/encounter_riskfight.h"
#include "riskfight_tape.h"

/* Rows of ocean/osrs/items/wiki_item_rows.tsv: each resolves by raw id and by alias to its own index, carries the slot, speed, range, style and
 * bonuses of the TSV, and the generated attack-style lookup agrees with the style column. Run from the repository root:
 *   cc -std=c11 -O2 -I. -o /tmp/test_wiki_item_rows ocean/osrs/tests/test_wiki_item_rows.c -lm && /tmp/test_wiki_item_rows */

static_assert(OSRS_STAT_ITEM_SLOTS >= 40, "the live server keeps at least 40 stat-priced rows");
static_assert(NUM_ITEMS + OSRS_STAT_ITEM_SLOTS < ITEM_NONE, "every row index stays below ITEM_NONE");
static_assert(NUM_ITEMS == NUM_HAND_ITEMS + OSRS_ITEM_WIKI_ROW_COUNT, "wiki rows follow the frozen hand rows");

#define FIELDS 25
#define STAT_FIELDS 14

static int slot_of(const char* name) {
    static const struct { const char* name; int slot; } SLOTS[] = {
        {"head", SLOT_HEAD}, {"cape", SLOT_CAPE}, {"neck", SLOT_NECK}, {"weapon", SLOT_WEAPON}, {"body", SLOT_BODY}, {"shield", SLOT_SHIELD},
        {"legs", SLOT_LEGS}, {"hands", SLOT_HANDS}, {"feet", SLOT_FEET}, {"ring", SLOT_RING}, {"ammo", SLOT_AMMO}};
    for (size_t i = 0; i < sizeof(SLOTS) / sizeof(*SLOTS); i++)
        if (!strcmp(SLOTS[i].name, name)) return SLOTS[i].slot;
    fprintf(stderr, "unknown slot %s\n", name);
    abort();
}

static int style_of(const char* name) {
    if (!strcmp(name, "none")) return ATTACK_STYLE_NONE;
    if (!strcmp(name, "melee")) return ATTACK_STYLE_MELEE;
    if (!strcmp(name, "ranged")) return ATTACK_STYLE_RANGED;
    if (!strcmp(name, "magic")) return ATTACK_STYLE_MAGIC;
    fprintf(stderr, "unknown style %s\n", name);
    abort();
}

static void check_row(char* line, int order) {
    char* f[FIELDS];
    int n = 0;
    for (char* p = strtok(line, "\t\n"); p && n < FIELDS; p = strtok(NULL, "\t\n")) f[n++] = p;
    /* aliases and notes may be empty and strtok collapses empty fields: only the first 22 are positional */
    assert(n >= 22 + 1);
    int raw = atoi(f[0]);
    int idx = osrs_item_index_from_raw_id(raw);
    assert(idx == NUM_HAND_ITEMS + order);          /* appended in TSV order, indexes of existing rows never move */
    assert(idx < NUM_ITEMS && idx < ITEM_NONE && osrs_item_row_valid(idx));
    const Item* it = &ITEM_DATABASE[idx];
    assert(it->item_id == raw && !strcmp(it->name, f[2]));
    assert(it->slot == slot_of(f[3]) && it->two_handed == atoi(f[4]));
    assert(it->attack_speed == atoi(f[5]) && it->attack_range == atoi(f[6]));
    assert(get_item_attack_style((uint16_t)idx) == style_of(f[7]));
    /* specials stay unmodelled until osrs_special_attacks.h has them; the Saradomin sword's, the Dragon warhammer's and the Tonalztics' are */
    int modelled = idx == ITEM_SARADOMIN_SWORD || idx == ITEM_DRAGON_WARHAMMER || idx == ITEM_TONALZTICS_OF_RALOS;
    assert((osrs_spec_cost(idx) > 0) == modelled && it->effect_mask == 0);
    const int16_t got[STAT_FIELDS] = {it->attack_stab, it->attack_slash, it->attack_crush, it->attack_magic, it->attack_ranged,
        it->defence_stab, it->defence_slash, it->defence_crush, it->defence_magic, it->defence_ranged,
        it->melee_strength, it->ranged_strength, it->magic_damage, it->prayer};
    for (int k = 0; k < STAT_FIELDS; k++) assert(got[k] == atoi(f[8 + k]));
    if (it->slot == SLOT_WEAPON) assert(it->attack_speed > 0 && get_item_attack_style((uint16_t)idx) != ATTACK_STYLE_NONE);
    else assert(it->attack_speed == 0 && it->attack_range == 0 && get_item_attack_style((uint16_t)idx) == ATTACK_STYLE_NONE);
    if (n >= 23 && f[22][0] >= '0' && f[22][0] <= '9') {          /* aliases column: comma separated raw ids */
        for (char* a = strtok(f[22], ","); a; a = strtok(NULL, ",")) {
            assert(osrs_item_index_from_raw_id(atoi(a)) == idx);
            assert(osrs_item_canonical_raw_id((uint16_t)atoi(a)) == raw);
        }
    }
}

int main(void) {
    osrs_stat_items_clear();
    FILE* fh = fopen("ocean/osrs/items/wiki_item_rows.tsv", "r");
    assert(fh && "run from the repository root");
    char line[2048];
    int rows = 0, header = 0;
    while (fgets(line, sizeof(line), fh)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if (!header) { header = 1; assert(!strncmp(line, "raw_id\tenum\tname", 16)); continue; }
        check_row(line, rows++);
    }
    fclose(fh);
    assert(rows == OSRS_ITEM_WIKI_ROW_COUNT && rows > 0);

    /* Exact wiki numbers of three weapons (OSRS Wiki Combat stats infoboxes). Belle's folly: one-handed stab sword, no special attack. */
    int belle = osrs_item_index_from_raw_id(31248);
    assert(belle == ITEM_BELLES_FOLLY);
    const Item* b = &ITEM_DATABASE[belle];
    assert(b->slot == SLOT_WEAPON && !b->two_handed && b->attack_speed == 5 && b->attack_range == 1);
    assert(b->attack_stab == 100 && b->attack_slash == 78 && b->attack_crush == -3 && b->attack_magic == 0 && b->attack_ranged == 0);
    assert(b->defence_stab == 20 && b->defence_slash == 15 && b->defence_crush == -9 && b->defence_magic == 0 && b->defence_ranged == 0);
    assert(b->melee_strength == 102 && b->ranged_strength == 0 && b->magic_damage == 0 && b->prayer == 0);
    assert(get_item_attack_style(ITEM_BELLES_FOLLY) == ATTACK_STYLE_MELEE && osrs_spec_cost(ITEM_BELLES_FOLLY) == 0);

    int halberd = osrs_item_index_from_raw_id(29796);
    const Item* h = &ITEM_DATABASE[halberd];
    assert(halberd == ITEM_NOXIOUS_HALBERD && h->two_handed && h->attack_speed == 5 && h->attack_range == 2);
    assert(h->attack_stab == 80 && h->attack_slash == 132 && h->attack_crush == 0 && h->melee_strength == 142 && h->prayer == 0);
    assert(item_is_two_handed(ITEM_NOXIOUS_HALBERD) && get_item_attack_style(ITEM_NOXIOUS_HALBERD) == ATTACK_STYLE_MELEE);

    int atlatl = osrs_item_index_from_raw_id(29000);
    const Item* a = &ITEM_DATABASE[atlatl];
    assert(atlatl == ITEM_ECLIPSE_ATLATL && a->two_handed && a->attack_speed == 4 && a->attack_range == 5);
    assert(a->attack_ranged == 87 && a->melee_strength == 40 && a->ranged_strength == 0);    /* the atlatl is struck with melee strength */
    assert(get_item_attack_style(ITEM_ECLIPSE_ATLATL) == ATTACK_STYLE_RANGED);

    /* Hand rows stayed where they were; variants with identical wiki stats resolve to them. */
    assert(ITEM_ELITE_BLACK_PLATELEGS == 162 && NUM_HAND_ITEMS == 163 && ITEM_BELLES_FOLLY >= NUM_HAND_ITEMS);
    assert(osrs_item_index_from_raw_id(26484) == ITEM_ABYSSAL_TENTACLE && osrs_item_canonical_raw_id(26484) == 12006);
    assert(osrs_item_index_from_raw_id(1706) == ITEM_GLORY && osrs_item_index_from_raw_id(11978) == ITEM_GLORY);
    assert(osrs_item_index_from_raw_id(28951) == ITEM_DIZANAS_QUIVER && osrs_item_index_from_raw_id(29605) == ITEM_AGS);

    /* No raw id names two rows. */
    for (int i = 0; i < NUM_ITEMS; i++)
        for (int j = i + 1; j < NUM_ITEMS; j++) assert(ITEM_DATABASE[i].item_id != ITEM_DATABASE[j].item_id);

    printf("Wiki item rows passed: %d rows, NUM_ITEMS %d, stat slots %d\n", rows, NUM_ITEMS, OSRS_STAT_ITEM_SLOTS);
    return 0;
}
