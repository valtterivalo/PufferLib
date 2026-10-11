#include "../gearswapper/gs_wire.h"

int main(void) {
    gs_register_java_items();
    const char* legacy = "S 394 121 118 118 119 99 99 90 0 1000 0 0 3162 3503 -1 10828 21295 1725 27690 10551 22322 -1 4087 -1 7462 11840 -1 2550 -1 24592 24592 24595 24595 24598 24595 24595 24595 24598 24592 24595 24595 24592 6685 24592 6685 -1 24592 24592 24592 24225 12695 24592 24592 24592 24592 24621 8013 1 0 -1 -1";
    char line[1 << 16];
    GsInfer* infer = malloc(sizeof(*infer));
    gs_infer_init(infer);
    GsWire wire = {0};
    assert(gs_wire_apply(infer, &wire, legacy) == 'S');
    assert(infer->c.opponent_animation.source == GS_ANIMATION_UNKNOWN);
    GsClient baseline = infer->c;
    for (int id = -1; id <= 829; id += 830) {
        snprintf(line, sizeof(line), "%s ANIMATION OBSERVED %d", legacy, id);
        assert(gs_wire_apply(infer, &wire, line) == 'S');
        assert(infer->c.opponent_animation.source == GS_ANIMATION_OBSERVED && infer->c.opponent_animation.id == id);
        GsClient current = infer->c;
        current.opponent_animation = baseline.opponent_animation;
        assert(!memcmp(&baseline, &current, sizeof(current)));
    }
    snprintf(line, sizeof(line), "%s ANIMATION UNKNOWN", legacy);
    assert(gs_wire_apply(infer, &wire, line) == 'S');
    assert(infer->c.opponent_animation.source == GS_ANIMATION_UNKNOWN);
    infer->c.ratio = 15; infer->c.scale = 30;
    infer->c.opponent_combat_level = 126;
    gs_eater_observe(infer, 20);
    assert(infer->unseen_count == 0);
    infer->c.opponent_animation = (GsCurrentAnimation){GS_ANIMATION_OBSERVED, -1};
    gs_eater_observe(infer, 20);
    assert(infer->unseen_count == 0);
    infer->c.opponent_animation = (GsCurrentAnimation){GS_ANIMATION_OBSERVED, 11275};
    gs_eater_observe(infer, 20);
    gs_eater_observe(infer, 20);
    assert(infer->unseen_count == 1 && infer->unseen[0] == 20);
    gs_eater_opponent_animated(infer, GS_KIND_EAT, 829, 21);
    infer->c.opponent_animation = (GsCurrentAnimation){GS_ANIMATION_OBSERVED, 829};
    gs_eater_observe(infer, 21);
    assert(infer->unseen_count == 0);
    gs_eater_observe(infer, 22);
    assert(infer->unseen_count == 1 && infer->unseen[0] == 22);
    assert(infer->health->target.has_history && infer->health->target.has_belief);
    gs_eater_opponent_hitsplat(infer, 23);
    infer->c.opponent_animation = (GsCurrentAnimation){GS_ANIMATION_OBSERVED, 11275};
    gs_eater_observe(infer, 23);
    assert(infer->unseen_count == 1 && infer->unseen[0] == 23);
    gs_eater_reset(infer);
    assert(infer->unseen_count == 0);
    puts("current animation wire and observe contracts passed");
    free(infer);
    return 0;
}
