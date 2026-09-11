#include <assert.h>
#include "../osrs_models.h"
#include "../osrs_anim.h"

int main(void) {
    assert(item_render_ready_anim(4718) == 2065);
    assert(item_render_walk_anim(4718) == 2064);
    assert(item_render_run_anim(4718) == ITEM_RENDER_MODEL_MISSING);
    assert(item_render_ready_anim(24225) == item_render_ready_anim(4153));
    assert(item_render_walk_anim(24225) == item_render_walk_anim(4153));
    assert(item_render_run_anim(24225) == item_render_run_anim(4153));
    AnimCache* cache = anim_cache_load("ocean/osrs/data/equipment.anims");
    const int sequences[] = {2065, 2064, 1662, 1663, 1664};
    for (int i = 0; i < 5; i++) {
        AnimSequence* sequence = anim_get_sequence(cache, sequences[i]);
        assert(sequence && sequence->frame_count > 0);
        for (int frame = 0; frame < sequence->frame_count; frame++)
            assert(anim_get_framebase(cache, sequence->frames[frame].frame.framebase_id));
    }
    anim_cache_free(cache);
    puts("Riskfight stance assets passed");
}
