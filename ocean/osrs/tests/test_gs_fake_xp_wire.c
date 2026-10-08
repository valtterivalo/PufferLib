#include "../gearswapper/gs_wire.h"

int main(int argc, char** argv) {
    assert(argc == 2);
    gs_register_java_items();
    FILE* source = fopen(argv[1], "r");
    assert(source);
    GsInfer inf;
    gs_infer_init(&inf);
    GsWire wire = {0};
    char line[1 << 16];
    int real = 0, fake = 0, witness = 0, after_witness = 0;
    char prior = 0;
    while (fgets(line, sizeof(line), source)) {
        GsInfer expected = inf;
        GsWire before = wire;
        int tick = 0, gained = 0;
        if (line[0] == 'F' && line[1] == ' ') {
            assert(sscanf(line, "F %d %d", &tick, &gained) == 2 && prior == 'S');
            if (gained > 0) gs_on_hitpoints_gain(&expected, tick, gained, GS_WIRE_COMBAT_LEVEL);
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
            assert(inf.own_hit_damage == expected.own_hit_damage && inf.own_hit_lands == expected.own_hit_lands);
            assert(inf.struck_damage == expected.struck_damage && inf.struck_tick == expected.struck_tick);
            if (tick == 633) {
                assert(gained == 79 && wire.hp_xp == 30629081 && wire.hp_xp_known);
                assert(inf.own_hit_damage == 53 && inf.own_hit_lands == 633);
                witness++;
            }
        }
        if (applied) prior = applied;
    }
    assert(!ferror(source));
    fclose(source);
    assert(fake > 0 && real > 0 && witness == 1 && after_witness > 0);
    printf("fake HP inputs %d, real XP observations %d, SS2 damage53 landing633, cumulative XP preserved\n", fake, real);
    return 0;
}
