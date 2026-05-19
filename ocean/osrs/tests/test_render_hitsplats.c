#include "../osrs_pvp_actions.h"
#include "../encounters/encounter_zulrah.h"
#include "../encounters/encounter_inferno.h"
#include "../osrs_render.h"
#include <assert.h>

static void assert_slot_offset(int slot, int expected_dx, int expected_dy) {
    int dx = 999;
    int dy = 999;

    render_splat_slot_offset(slot, &dx, &dy);

    assert(dx == expected_dx);
    assert(dy == expected_dy);
}

int main(void) {
    assert_slot_offset(0, 0, 0);
    assert_slot_offset(1, 0, -20);
    assert_slot_offset(2, -15, -10);
    assert_slot_offset(3, 15, -10);

    HitSplat s = {
        .active = 1,
        .damage = 31,
        .type = 1,
        .hitmark_move = 5.0,
        .hitmark_trans = 230,
        .ticks_remaining = 70,
    };

    int sx = 0;
    int sy = 0;
    render_splat_screen_pos(100, 200, 3, &s, &sx, &sy);
    assert(sx == 115);
    assert(sy == 195);

    float overhead_y = render_overhead_anchor_y(2.0f);
    assert(overhead_y > 2.117f);
    assert(overhead_y < 2.118f);

    ContextMenu menu = {
        .visible = 1,
        .screen_x = 10,
        .screen_y = 20,
        .width = CONTEXT_MENU_MIN_W,
        .item_count = 3,
    };
    assert(context_menu_height(&menu) == 84);
    assert(context_menu_row_at(&menu, 12, 24) == -1);
    assert(context_menu_row_at(&menu, 12, 43) == 0);
    assert(context_menu_row_at(&menu, 12, 63) == 1);
    assert(context_menu_row_at(&menu, 12, 83) == 2);
    assert(context_menu_row_at(&menu, 12, 104) == -1);

    static RenderClient rc;
    rc.entity_count = 1;
    rc.splats[0][0] = s;
    render_update_splats_client_tick(&rc);
    assert(rc.splats[0][0].active);
    assert(rc.splats[0][0].ticks_remaining == 69);
    assert(rc.splats[0][0].hitmark_move == 4.75);

    RenderEntity elysian_hit = {
        .hit_damage = 12,
        .elysian_proc_this_tick = 1,
    };
    assert(render_entity_hit_splat_type(&elysian_hit) == 1);

    RenderEntity elysian_zero = {
        .hit_damage = 0,
        .elysian_proc_this_tick = 1,
    };
    assert(render_entity_hit_splat_type(&elysian_zero) == 0);

    for (int i = 0; i < 40; i++) {
        render_update_splats_client_tick(&rc);
    }
    assert(rc.splats[0][0].hitmark_move == -5.0);

    for (int i = 0; i < 29; i++) {
        render_update_splats_client_tick(&rc);
    }
    assert(!rc.splats[0][0].active);

    static RenderClient slots;
    slots.entity_count = 1;
    render_push_splat_type(&slots, 1, 0, 1);
    render_push_splat_type(&slots, 2, 0, 1);
    render_push_splat_type(&slots, 3, 0, 1);
    render_push_splat_type(&slots, 4, 0, 1);
    for (int i = 0; i < RENDER_SPLATS_PER_PLAYER; i++) {
        assert(slots.splats[0][i].active);
        assert(slots.splats[0][i].damage == i + 1);
    }

    return 0;
}
