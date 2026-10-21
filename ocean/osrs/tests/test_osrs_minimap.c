#include <stdlib.h>
#include <string.h>

#include "ocean/osrs/osrs_minimap.h"
#include "ocean/osrs/tests/osrs_test_check.h"

enum { W = 152, H = 152, SRC = 512 };

static OsrsSpriteMask circle_mask(void) {
    static uint32_t px[W * H];
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int dx = 2 * x + 1 - W, dy = 2 * y + 1 - H;
            px[y * W + x] = dx * dx + dy * dy < W * W ? 0 : 0xFF000000u;
        }
    return osrs_sprite_mask((OsrsPixels){W, H, px});
}

static int drawn_pixel(const uint32_t* dst, int* x, int* y) {
    for (int i = 0; i < W * H; i++)
        if (dst[i]) {
            *x = i % W, *y = i / W;
            return 1;
        }
    return 0;
}

int main(void) {
    OsrsSpriteMask mask = circle_mask();
    static uint32_t src[SRC * SRC], dst[W * H];
    for (int i = 0; i < SRC * SRC; i++) src[i] = 0xFF000000u | (uint32_t)rand();
    OsrsPixels scene = {SRC, SRC, src};

    osrs_draw_rotated_masked(dst, &mask, scene, 200, 300, 0, OSRS_MINIMAP_BLACK);
    int crop = 1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (osrs_sprite_mask_contains(&mask, x, y))
                crop &= dst[y * W + x] == src[(300 - H / 2 + y) * SRC + 200 - W / 2 + x];
    CHECK("angle 0 blits an exact crop", crop);

    OsrsMinimapView west = {100 * 128 + 64, 100 * 128 + 64, 512};
    uint32_t dot = OSRS_MINIMAP_WHITE;
    memset(dst, 0, sizeof(dst));
    osrs_minimap_draw_tile_sprite(dst, &mask, (OsrsPixels){1, 1, &dot}, west, 95, 100);
    int px, py;
    CHECK("camera facing west puts west tiles above centre",
        drawn_pixel(dst, &px, &py) && py < H / 2 - 15 && abs(px - W / 2) <= 1);

    int roundtrips = 0, misses = 0;
    for (int angle = 0; angle < 2048; angle += 37)
        for (int ty = 85; ty <= 115; ty++)
            for (int tx = 85; tx <= 115; tx++) {
                OsrsMinimapView view = {100 * 128 + 70, 100 * 128 + 40, angle};
                memset(dst, 0, sizeof(dst));
                osrs_minimap_draw_tile_sprite(dst, &mask, (OsrsPixels){1, 1, &dot}, view, tx, ty);
                if (!drawn_pixel(dst, &px, &py)) continue;
                int cx, cy;
                roundtrips++;
                misses += !osrs_minimap_click_tile(&mask, view, px, py, &cx, &cy) || cx != tx || cy != ty;
            }
    CHECK("tiles are drawn across the whole map", roundtrips > 40000);
    ASSERT_INT_EQ("clicking a drawn tile walks to that tile", misses, 0);

    return osrs_test_summary();
}
