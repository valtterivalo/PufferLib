/* Prints the opponent pool as a table: kit, group, draw weight in percent, source, weapons, armour, and what the build left out.
 * cc -std=gnu11 -O2 -I. -o /tmp/print_riskfight_pool ocean/osrs/tests/print_riskfight_pool.c -lm ; /tmp/print_riskfight_pool [top N empirical rows, default all] */
#include <stdio.h>
#include <stdlib.h>
#include "../encounters/encounter_riskfight.h"

static const char* item_name(uint16_t item) { return item == ITEM_NONE ? "-" : ITEM_DATABASE[item].name; }

int main(int argc, char** argv) {
    int top = argc > 1 ? atoi(argv[1]) : RISKFIGHT_POOL_COUNT;
    static const char* KIND[] = {"empirical", "archetype", "edge"};
    float mass[3] = {0};
    int count[3] = {0};
    for (int i = 0; i < RISKFIGHT_POOL_COUNT; i++) { mass[RISKFIGHT_POOL[i].kind] += RISKFIGHT_POOL[i].weight; count[RISKFIGHT_POOL[i].kind]++; }
    printf("pool: %d kits; empirical %d (%.1f%%), archetype %d (%.1f%%), edge %d (%.1f%%); left out: %d empirical rows / %d fights (%d for a magic weapon)\n", RISKFIGHT_POOL_COUNT,
        count[0], 100 * mass[0], count[1], 100 * mass[1], count[2], 100 * mass[2], RISKFIGHT_POOL_EXCLUDED_ROWS, RISKFIGHT_POOL_EXCLUDED_FIGHTS, RISKFIGHT_POOL_EXCLUDED_MAGIC_ROWS);
    printf("| kit | group | weight % | source | weapons (started with first) | head / body / legs / shield | bag |\n|---|---|---|---|---|---|---|\n");
    int emitted_empirical = 0;
    for (int pass = 0; pass < 3; pass++) {
        /* heaviest first inside the group */
        int order[RF_POOL_MAX], n = 0;
        for (int i = 0; i < RISKFIGHT_POOL_COUNT; i++) if ((int)RISKFIGHT_POOL[i].kind == pass) order[n++] = i;
        for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++) if (RISKFIGHT_POOL[order[b]].weight > RISKFIGHT_POOL[order[a]].weight) { int t = order[a]; order[a] = order[b]; order[b] = t; }
        for (int a = 0; a < n; a++) {
            const RiskfightKit* k = &RISKFIGHT_POOL[order[a]];
            if (pass == 0 && emitted_empirical++ >= top) break;
            printf("| %s | %s | %.3f | %s | ", k->name, KIND[k->kind], 100 * k->weight, k->source);
            for (int w = 0; w < k->weapon_count; w++) printf("%s%s", w ? ", " : "", item_name(k->weapons[w]));
            printf(" | %s / %s / %s / %s | %s |\n", item_name(k->equipment[GEAR_SLOT_HEAD]), item_name(k->equipment[GEAR_SLOT_BODY]), item_name(k->equipment[GEAR_SLOT_LEGS]),
                item_name(k->equipment[GEAR_SLOT_SHIELD]), k->bag == RF_BAG_MELEE ? "melee" : k->bag == RF_BAG_RANGED ? "ranged" : "bare");
        }
    }
    return 0;
}
