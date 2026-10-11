#include "../gearswapper/gs_wire.h"

int main(int argc, char** argv) {
    assert(argc == 2);
    gs_register_java_items();
    FILE* source = fopen(argv[1], "r");
    assert(source);
    GsInfer inf;
    gs_infer_init(&inf);
    GsWire wire = {0};
    GsInfer unchanged = inf;
    char legacy_food[] = "F 2 22 -2147483648 12 UNCHANGED\n";
    assert(gs_wire_apply(&inf, &wire, legacy_food) == 0);
    assert(memcmp(&inf, &unchanged, sizeof(inf)) == 0 && !wire.hp_xp_known);
    char line[1 << 16];
    int real = 0, fake = 0, witness = 0, after_witness = 0;
    char prior = 0;
    while (fgets(line, sizeof(line), source)) {
        GsWire before = wire;
        int tick = 0, gained = 0;
        if (strncmp(line, "FHP ", 4) == 0) {
            assert(sscanf(line, "FHP %d %d", &tick, &gained) == 2 && prior == 'S');
        }
        char applied = gs_wire_apply(&inf, &wire, line);
        if (applied == 'X') {
            real++;
            if (witness) {
                assert(wire.hp_xp == 30629081 && wire.hp_xp_known);
                after_witness++;
            }
        }
        if (applied == 'F') {
            fake++;
            assert(wire.hp_xp == before.hp_xp && wire.hp_xp_known == before.hp_xp_known);
            if (tick == 633) {
                assert(gained == 79 && wire.hp_xp == 30629081 && wire.hp_xp_known);
                assert(inf.c.opponent_combat_level == 126 && inf.c.local_combat_level == 0);
                assert(inf.own_hit_damage == 53 && inf.local_xp_tick == 633);
                witness++;
            }
        }
        if (applied) prior = applied;
    }
    assert(!ferror(source));
    gs_wire_finish(&wire);
    gs_infer_clear(&inf);
    fclose(source);
    assert(fake > 0 && real > 0 && witness == 1 && after_witness > 0);
    printf("fake HP inputs %d, real XP observations %d, SS2 fake gain79 at633, cumulative XP preserved\n", fake, real);
    return 0;
}
