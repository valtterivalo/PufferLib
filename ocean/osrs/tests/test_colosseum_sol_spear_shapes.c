/**
 * @file test_colosseum_sol_spear_shapes.c
 * @brief Tile-exact parity of Sol spear hazard shapes against colosim.
 *
 * Reference = colosim's doFirstSpear/doSecondSpear stamping (fillRect +
 * fillLine, LINE_LENGTH 7, size 5) reimplemented verbatim in colosim's own
 * frame and mapped to ours via (x, y) -> (x, -y). The sim's analytic
 * col_sol_aoe_tile_is_hazard must mark exactly the same tiles for all 8
 * attack directions on both spear variants. Also pins the direction rule:
 * clamp-to-footprint + per-axis sign, under-boss falls to SW.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ocean/osrs/encounters/encounter_colosseum.h"

#define WIN 48
#define BX 100
#define BY 100
#define SIZE 5
#define LINE_LENGTH 7

static uint8_t ref[WIN][WIN];

static void ref_mark(int x_col, int y_col) {
    int x = x_col - BX + WIN / 2;
    int y = -y_col - BY + WIN / 2;
    if (x < 0 || x >= WIN || y < 0 || y >= WIN) {
        fprintf(stderr, "reference stamp out of window (%d, %d)\n", x, y);
        assert(0);
    }
    ref[x][y] = 1;
}

static void ref_fill_rect(int fx, int fy, int tx, int ty) {
    for (int xx = fx; xx < tx; xx++)
        for (int yy = ty; yy > fy; yy--)
            ref_mark(xx, yy);
}

typedef enum {
    DIR_WEST, DIR_EAST, DIR_NORTH, DIR_SOUTH,
    DIR_NORTHEAST, DIR_NORTHWEST, DIR_SOUTHEAST, DIR_SOUTHWEST,
} ColosimDir;

static const int COLOSIM_DIRS[8][2] = {
    { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 },
    { 1, -1 }, { -1, -1 }, { 1, 1 }, { -1, 1 },
};

static void ref_fill_line(int fx, int fy, ColosimDir dir, int length) {
    for (int n = 0; n <= length; n++)
        ref_mark(fx + COLOSIM_DIRS[dir][0] * n, fy + COLOSIM_DIRS[dir][1] * n);
}

/** colosim frame: L = (BX, -BY); footprint x in [Lx, Lx+4], y in [Ly-4, Ly]. */
static void ref_stamp_spear1(ColosimDir dir) {
    int lx = BX, ly = -BY;
    memset(ref, 0, sizeof(ref));
    ref_fill_rect(lx, ly - SIZE, lx + SIZE, ly);
    switch (dir) {
        case DIR_WEST:
            ref_fill_rect(lx - 1, ly - SIZE, lx, ly);
            ref_fill_line(lx - 2, ly - 1, dir, LINE_LENGTH);
            ref_fill_line(lx - 2, ly - 3, dir, LINE_LENGTH);
            break;
        case DIR_EAST:
            ref_fill_rect(lx + SIZE, ly - SIZE, lx + SIZE + 1, ly);
            ref_fill_line(lx + SIZE + 1, ly - 1, dir, LINE_LENGTH);
            ref_fill_line(lx + SIZE + 1, ly - 3, dir, LINE_LENGTH);
            break;
        case DIR_NORTH:
            ref_fill_rect(lx, ly - SIZE - 1, lx + SIZE, ly - SIZE);
            ref_fill_line(lx + 1, ly - SIZE - 1, dir, LINE_LENGTH);
            ref_fill_line(lx + 3, ly - SIZE - 1, dir, LINE_LENGTH);
            break;
        case DIR_SOUTH:
            ref_fill_rect(lx, ly, lx + SIZE, ly + 1);
            ref_fill_line(lx + 1, ly + 2, dir, LINE_LENGTH);
            ref_fill_line(lx + 3, ly + 2, dir, LINE_LENGTH);
            break;
        case DIR_NORTHEAST:
            ref_fill_line(lx + SIZE - 1, ly - SIZE, dir, LINE_LENGTH);
            ref_fill_line(lx + SIZE, ly - SIZE + 1, dir, LINE_LENGTH);
            break;
        case DIR_SOUTHEAST:
            ref_fill_line(lx + SIZE, ly, dir, LINE_LENGTH);
            ref_fill_line(lx + SIZE - 1, ly + 1, dir, LINE_LENGTH);
            break;
        case DIR_SOUTHWEST:
            ref_fill_line(lx - 1, ly, dir, LINE_LENGTH);
            ref_fill_line(lx, ly + 1, dir, LINE_LENGTH);
            break;
        case DIR_NORTHWEST:
            ref_fill_line(lx - 1, ly - SIZE + 1, dir, LINE_LENGTH);
            ref_fill_line(lx, ly - SIZE, dir, LINE_LENGTH);
            break;
    }
}

static void ref_stamp_spear2(ColosimDir dir) {
    int lx = BX, ly = -BY;
    memset(ref, 0, sizeof(ref));
    ref_fill_rect(lx - 1, ly - SIZE - 1, lx + SIZE + 1, ly + 1);
    switch (dir) {
        case DIR_WEST:
            ref_fill_rect(lx - 1, ly - SIZE, lx, ly);
            ref_fill_line(lx - 2, ly, dir, LINE_LENGTH);
            ref_fill_line(lx - 2, ly - 2, dir, LINE_LENGTH);
            ref_fill_line(lx - 2, ly - 4, dir, LINE_LENGTH);
            break;
        case DIR_EAST:
            ref_fill_line(lx + SIZE + 1, ly, dir, LINE_LENGTH);
            ref_fill_line(lx + SIZE + 1, ly - 2, dir, LINE_LENGTH);
            ref_fill_line(lx + SIZE + 1, ly - 4, dir, LINE_LENGTH);
            break;
        case DIR_NORTH:
            ref_fill_line(lx, ly - SIZE - 1, dir, LINE_LENGTH);
            ref_fill_line(lx + 2, ly - SIZE - 1, dir, LINE_LENGTH);
            ref_fill_line(lx + 4, ly - SIZE - 1, dir, LINE_LENGTH);
            break;
        case DIR_SOUTH:
            ref_fill_line(lx, ly + 2, dir, LINE_LENGTH);
            ref_fill_line(lx + 2, ly + 2, dir, LINE_LENGTH);
            ref_fill_line(lx + 4, ly + 2, dir, LINE_LENGTH);
            break;
        case DIR_NORTHEAST:
            ref_fill_line(lx + SIZE + 1, ly - SIZE - 1, dir, LINE_LENGTH);
            ref_fill_line(lx + SIZE - 2, ly - SIZE - 1, dir, LINE_LENGTH);
            ref_fill_line(lx + SIZE + 1, ly - SIZE + 2, dir, LINE_LENGTH);
            break;
        case DIR_SOUTHEAST:
            ref_fill_line(lx + SIZE + 1, ly - 1, dir, LINE_LENGTH);
            ref_fill_line(lx + SIZE + 1, ly + 2, dir, LINE_LENGTH);
            ref_fill_line(lx + SIZE - 2, ly + 2, dir, LINE_LENGTH);
            break;
        case DIR_SOUTHWEST:
            ref_fill_line(lx - 2, ly + 2, dir, LINE_LENGTH);
            ref_fill_line(lx - 2, ly - 1, dir, LINE_LENGTH);
            ref_fill_line(lx + 1, ly + 2, dir, LINE_LENGTH);
            break;
        case DIR_NORTHWEST:
            ref_fill_line(lx - 2, ly - SIZE + 2, dir, LINE_LENGTH);
            ref_fill_line(lx - 2, ly - SIZE - 1, dir, LINE_LENGTH);
            ref_fill_line(lx + 1, ly - SIZE - 1, dir, LINE_LENGTH);
            break;
    }
}

/** our (dx, dy) for each colosim direction under the (x, -y) frame map. */
static const int OUR_DIR[8][2] = {
    { -1, 0 }, { 1, 0 }, { 0, 1 }, { 0, -1 },
    { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 },
};

static const char* DIR_NAME[8] = {
    "W", "E", "N", "S", "NE", "NW", "SE", "SW",
};

static int compare_shape(int variant, ColosimDir dir) {
    SolHereditState sol;
    memset(&sol, 0, sizeof(sol));
    sol.aoe_attack = variant == 1 ? COLO_SOL_AOE_SPEAR1 : COLO_SOL_AOE_SPEAR2;
    sol.aoe_x = BX;
    sol.aoe_y = BY;
    sol.aoe_dir_x = OUR_DIR[dir][0];
    sol.aoe_dir_y = OUR_DIR[dir][1];

    int mismatches = 0;
    for (int x = 0; x < WIN; x++) {
        for (int y = 0; y < WIN; y++) {
            int wx = BX + x - WIN / 2;
            int wy = BY + y - WIN / 2;
            int got = col_sol_aoe_tile_is_hazard(&sol, wx, wy);
            int want = ref[x][y];
            if (got != want) {
                if (mismatches < 6)
                    fprintf(stderr,
                        "spear%d %s mismatch at rel (%d, %d): sim=%d colosim=%d\n",
                        variant, DIR_NAME[dir],
                        wx - BX, wy - BY, got, want);
                mismatches++;
            }
        }
    }
    return mismatches;
}

static void test_direction_rule(void) {
    ColosseumState* s = (ColosseumState*)calloc(1, sizeof(ColosseumState));
    ColoNPC boss;
    memset(&boss, 0, sizeof(boss));
    boss.type = COLO_SOL_HEREDIT;
    boss.size = SIZE;
    boss.x = BX;
    boss.y = BY;

    struct { int px, py, dx, dy; } cases[] = {
        { BX - 3, BY + 2, -1, 0 },   /* west of footprint, inside y-band */
        { BX + 7, BY,      1, 0 },   /* east */
        { BX + 2, BY + 9,  0, 1 },   /* north */
        { BX,     BY - 1,  0, -1 },  /* south */
        { BX + 6, BY + 5,  1, 1 },   /* past the NE corner */
        { BX - 1, BY + 5, -1, 1 },   /* past the NW corner */
        { BX + 5, BY - 2,  1, -1 },  /* past the SE corner */
        { BX - 4, BY - 4, -1, -1 },  /* past the SW corner */
        { BX + 2, BY + 2, -1, -1 },  /* under the boss: colosim SW fallback */
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        s->player.x = cases[i].px;
        s->player.y = cases[i].py;
        int dx = 0, dy = 0;
        col_sol_aoe_direction(s, &boss, &dx, &dy);
        if (dx != cases[i].dx || dy != cases[i].dy) {
            fprintf(stderr,
                "direction case %zu: player (%d, %d) -> got (%d, %d) want (%d, %d)\n",
                i, cases[i].px, cases[i].py, dx, dy, cases[i].dx, cases[i].dy);
            assert(0);
        }
    }
    free(s);
}

int main(void) {
    int total_mismatches = 0;
    for (int dir = 0; dir < 8; dir++) {
        ref_stamp_spear1((ColosimDir)dir);
        total_mismatches += compare_shape(1, (ColosimDir)dir);
        ref_stamp_spear2((ColosimDir)dir);
        total_mismatches += compare_shape(2, (ColosimDir)dir);
    }
    if (total_mismatches) {
        fprintf(stderr, "%d hazard tile mismatches vs colosim\n", total_mismatches);
        return 1;
    }
    test_direction_rule();
    printf("sol spear shapes: 16/16 direction x variant sets match colosim\n");
    return 0;
}
