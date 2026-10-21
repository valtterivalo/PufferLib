#ifndef OSRS_MINIMAP_H
#define OSRS_MINIMAP_H

#include <assert.h>
#include <math.h>
#include <stdint.h>

#define OSRS_MINIMAP_TILE_PX 4
#define OSRS_MINIMAP_MAX_ROWS 256
#define OSRS_MINIMAP_BLACK 0xFF010000u
#define OSRS_MINIMAP_WHITE 0xFFFFFFFFu

/* RGBA8 pixels in raylib byte order, alpha 0 is transparent. */
typedef struct {
    int width, height;
    const uint32_t* pixels;
} OsrsPixels;

/* Widget.getSpriteMask: per row, the span between the first and last transparent pixel. */
typedef struct {
    int width, height;
    int x_starts[OSRS_MINIMAP_MAX_ROWS];
    int x_widths[OSRS_MINIMAP_MAX_ROWS];
} OsrsSpriteMask;

/* Minimap focal point in local units, 128 per tile. */
typedef struct {
    int x, y;
    int angle;
} OsrsMinimapView;

static inline int osrs_sine(int angle) { return (int)(65536.0 * sin((angle & 2047) * 0.0030679615)); }
static inline int osrs_cosine(int angle) { return (int)(65536.0 * cos((angle & 2047) * 0.0030679615)); }

static inline int osrs_camera_angle(float yaw_radians) {
    return (int)lroundf(yaw_radians * (2048.0f / 6.28318530718f)) & 2047;
}

static OsrsSpriteMask osrs_sprite_mask(OsrsPixels sprite) {
    assert(sprite.height <= OSRS_MINIMAP_MAX_ROWS);
    OsrsSpriteMask m = {.width = sprite.width, .height = sprite.height};
    for (int y = 0; y < sprite.height; y++) {
        const uint32_t* row = sprite.pixels + y * sprite.width;
        int start = 0, end = sprite.width;
        for (int x = 0; x < sprite.width; x++)
            if (!(row[x] >> 24)) { start = x; break; }
        for (int x = sprite.width - 1; x >= start; x--)
            if (!(row[x] >> 24)) { end = x + 1; break; }
        m.x_starts[y] = start;
        m.x_widths[y] = end - start;
    }
    return m;
}

static inline int osrs_sprite_mask_contains(const OsrsSpriteMask* m, int x, int y) {
    return y >= 0 && y < m->height && x >= m->x_starts[y] && x < m->x_starts[y] + m->x_widths[y];
}

/* SpritePixels.drawRotatedMaskedCenteredAround at zoom 256: src is sampled around (center_x, center_y)
 * into the mask's spans of dst. Samples outside src write `outside`; transparent samples are skipped. */
static void osrs_draw_rotated_masked(
    uint32_t* dst, const OsrsSpriteMask* m, OsrsPixels src, int center_x, int center_y, int angle, uint32_t outside
) {
    int sn = (int)(sin(angle / 326.11) * 65536.0);
    int cs = (int)(cos(angle / 326.11) * 65536.0);
    int left = -m->width / 2, top = -m->height / 2;
    int row_u = top * sn + left * cs + (center_x << 16);
    int row_v = top * cs - left * sn + (center_y << 16);
    for (int y = 0; y < m->height; y++, row_u += sn, row_v += cs) {
        int u = row_u, v = row_v;
        int x0 = m->x_starts[y], x1 = x0 + m->x_widths[y];
        for (int x = 0; x < m->width; x++, u += cs, v -= sn) {
            if (x < x0 || x >= x1) continue;
            int su = u >> 16, sv = v >> 16;
            uint32_t c = su >= 0 && su < src.width && sv >= 0 && sv < src.height
                ? src.pixels[sv * src.width + su] : outside;
            if (c >> 24) dst[y * m->width + x] = c;
        }
    }
}

/* drawSpriteOnMinimap: (dx, dy) is the sprite's minimap pixel offset from the focal point, north positive.
 * Within 50 px the sprite clips to the widget, out to 80 px to the mask, beyond that it is not drawn. */
static void osrs_minimap_draw_sprite(uint32_t* dst, const OsrsSpriteMask* m, OsrsPixels sprite, int dx, int dy, int angle) {
    int d2 = dx * dx + dy * dy;
    if (d2 > 6400) return;
    int sn = osrs_sine(angle), cs = osrs_cosine(angle);
    int rx = (cs * dx + dy * sn) >> 16;
    int ry = (dy * cs - sn * dx) >> 16;
    int ox = rx + m->width / 2 - sprite.width / 2;
    int oy = m->height / 2 - ry - sprite.height / 2;
    for (int y = 0; y < sprite.height; y++) {
        int py = oy + y;
        if (py < 0 || py >= m->height) continue;
        int x0 = d2 > 2500 ? m->x_starts[py] : 0;
        int x1 = d2 > 2500 ? x0 + m->x_widths[py] : m->width;
        for (int x = 0; x < sprite.width; x++) {
            int px = ox + x;
            uint32_t c = sprite.pixels[y * sprite.width + x];
            if (px >= x0 && px < x1 && (c >> 24)) dst[py * m->width + px] = c;
        }
    }
}

static inline void osrs_minimap_draw_sprite_at(
    uint32_t* dst, const OsrsSpriteMask* m, OsrsPixels sprite, OsrsMinimapView view, int local_x, int local_y
) {
    osrs_minimap_draw_sprite(dst, m, sprite, local_x / 32 - view.x / 32, local_y / 32 - view.y / 32, view.angle);
}

static inline void osrs_minimap_draw_tile_sprite(
    uint32_t* dst, const OsrsSpriteMask* m, OsrsPixels sprite, OsrsMinimapView view, int tile_x, int tile_y
) {
    osrs_minimap_draw_sprite(dst, m, sprite, tile_x * 4 + 2 - view.x / 32, tile_y * 4 + 2 - view.y / 32, view.angle);
}

/* The local player's 3x3 white square. */
static void osrs_minimap_draw_local_player(uint32_t* dst, const OsrsSpriteMask* m, OsrsMinimapView view, int local_x, int local_y) {
    int dx = local_x / 32 - view.x / 32, dy = local_y / 32 - view.y / 32;
    int sn = osrs_sine(view.angle), cs = osrs_cosine(view.angle);
    int rx = (cs * dx + sn * dy) >> 16;
    int ry = (cs * dy - sn * dx) >> 16;
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 3; x++) {
            int px = m->width / 2 + rx - 1 + x, py = m->height / 2 - ry - 1 + y;
            if (px >= 0 && py >= 0 && px < m->width && py < m->height) dst[py * m->width + px] = OSRS_MINIMAP_WHITE;
        }
}

/* checkIfMinimapClicked: widget-relative (x, y) inside the mask to the clicked tile. */
static int osrs_minimap_click_tile(const OsrsSpriteMask* m, OsrsMinimapView view, int x, int y, int* tile_x, int* tile_y) {
    if (!osrs_sprite_mask_contains(m, x, y)) return 0;
    int dx = x - m->width / 2, dy = y - m->height / 2;
    int sn = osrs_sine(view.angle), cs = osrs_cosine(view.angle);
    int rx = (sn * dy + dx * cs) >> 11;
    int ry = (dy * cs - dx * sn) >> 11;
    *tile_x = (rx + view.x) >> 7;
    *tile_y = (view.y - ry) >> 7;
    return 1;
}

#endif
