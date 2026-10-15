/* Prints the two fight-start loadouts (ours from the recording, his from the declared priors) of a tape as C initializers.
 *   cc -std=c11 -O2 -I. -o /tmp/export_loadout ocean/osrs/tests/export_loadout.c -lm
 *   /tmp/export_loadout TAPE [TICK_OFFSET] */
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include "riskfight_rollout.h"
static void print_loadout(const RiskfightLoadout* l, const char* name) {
    printf("static const RiskfightLoadout %s = {.equipment = {", name);
    for (int i = 0; i < NUM_GEAR_SLOTS; i++) printf("%d%s", l->equipment[i], i + 1 < NUM_GEAR_SLOTS ? ", " : "");
    printf("}, .inventory = {");
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) printf("%d%s", l->inventory[i], i + 1 < OSRS_INVENTORY_SIZE ? ", " : "");
    printf("}, .hitpoints = %d, .vengeance_sacks = %d};\n", l->hitpoints, l->vengeance_sacks);
}
int main(int argc, char** argv) {
    assert(argc >= 2);
    static Tape tape;
    if (!tape_load(&tape, argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
    int t = tape.lo + (argc > 2 ? atoi(argv[2]) : 1);
    if (t > tape.hi) t = tape.hi;
    observer_register_items();
    const TapeTick* tick = tape_tick(&tape, t);
    RiskfightSnapshot snap = {0};
    RiskfightLoadout* us = &snap.loadout[0];
    for (int i = 0; i < NUM_GEAR_SLOTS; i++) us->equipment[i] = ITEM_NONE;
    for (int i = 0; i < TAPE_WORN; i++)
        if (WORN_TO_GEAR[i] >= 0 && tick->worn[i] >= 0) us->equipment[WORN_TO_GEAR[i]] = raw_id_to_item(tick->worn[i]);
    for (int i = 0; i < TAPE_BAG; i++) us->inventory[i] = raw_id_to_content(tick->bag[i]);
    us->hitpoints = tick->hp;
    us->vengeance_sacks = tick->sacks;
    printf("/* %s tick %d */\n", tape.name, t);
    print_loadout(&snap.loadout[0], "RISKFIGHT_LIVE_OURS");
    for (int side = 0; side < 1; side++) {
        printf("/* side %d worn:", side);
        for (int i = 0; i < NUM_GEAR_SLOTS; i++) printf(" %s", snap.loadout[side].equipment[i] == ITEM_NONE ? "-" : ITEM_DATABASE[snap.loadout[side].equipment[i]].name);
        printf("\n   bag:");
        for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) {
            const OsrsItemContentMetadata* m = osrs_item_content_metadata(snap.loadout[side].inventory[i]);
            printf(" %d(kind %d dose %d item %s)", snap.loadout[side].inventory[i], m->consumable_kind, m->dose_count, m->item_idx != ITEM_NONE ? ITEM_DATABASE[m->item_idx].name : "-");
        }
        printf(" */\n");
    }
    return 0;
}
