#include "ocean/osrs/osrs_inventory.h"

int main(int argc, char** argv) {
    assert(argc == 2);
    FILE* output = fopen(argv[1], "w");
    assert(output);
    fprintf(output, "#ifndef OSRS_NH_ACTIONS_GENERATED_H\n#define OSRS_NH_ACTIONS_GENERATED_H\n\n");
    int total = 0;
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
        fprintf(output, "static const uint8_t NH_PVP_EQUIPMENT_SLOT_%d[] = {", slot);
        int count = 0;
        for (int item = 0; item < NUM_ITEMS; item++) {
            if (osrs_item_gear_slot(item) != slot) continue;
            fprintf(output, "%s%d", count ? ", " : "", item);
            count++;
        }
        assert(count > 0);
        total += count + 2;
        fprintf(output, "};\n#define NH_PVP_EQUIPMENT_DIM_%d %d\n", slot, count + 2);
    }
    fprintf(output, "\nstatic const uint8_t* const NH_PVP_EQUIPMENT_ITEMS[NUM_GEAR_SLOTS] = {\n");
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++)
        fprintf(output, "    NH_PVP_EQUIPMENT_SLOT_%d%s\n", slot, slot + 1 == NUM_GEAR_SLOTS ? "" : ",");
    fprintf(output, "};\n#define NH_PVP_EQUIPMENT_LOGITS %d\n\n#endif\n", total);
    assert(fclose(output) == 0);
    return 0;
}
