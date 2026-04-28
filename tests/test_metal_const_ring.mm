/**
 * @file test_metal_const_ring.mm
 * @brief Tests constant-ring reservation bounds.
 */

#include "src/metal_platform.h"

int main(void) {
    NSUInteger next_offset = 0;

    if (!mtl_const_ring_reserve_range(0, 1, &next_offset)) return 1;
    if (next_offset != 16) return 2;

    if (!mtl_const_ring_reserve_range(MTL_CONST_RING_SIZE - 16, 16, &next_offset)) return 3;
    if (next_offset != MTL_CONST_RING_SIZE) return 4;

    if (mtl_const_ring_reserve_range(MTL_CONST_RING_SIZE - 16, 17, &next_offset)) return 5;

    return 0;
}
