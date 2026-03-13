/**
 * @fileoverview NPC model/animation mappings for Zulrah encounter rendering.
 *
 * Maps NPC definition IDs to cache model IDs and animation sequence IDs.
 * Generated data — edit export scripts to regenerate.
 */

#ifndef NPC_MODELS_H
#define NPC_MODELS_H

#include <stdint.h>

typedef struct {
    uint16_t npc_id;
    uint32_t model_id;
    uint32_t idle_anim;
    uint32_t attack_anim;
} NpcModelMapping;

/* zulrah forms + snakeling */
static const NpcModelMapping NPC_MODEL_MAP[] = {
    {2042, 14408, 5069, 5068},  /* green zulrah (ranged) */
    {2043, 14409, 5069, 5068},  /* red zulrah (melee) */
    {2044, 14407, 5069, 5068},  /* blue zulrah (magic) */
};

/* snakeling model + animations (NPC 2045 melee, 2046 magic — same model) */
#define SNAKELING_MODEL_ID 10415
#define SNAKELING_ANIM_IDLE    1721
#define SNAKELING_ANIM_MELEE   140   /* NPC 2045 melee attack */
#define SNAKELING_ANIM_MAGIC   185   /* NPC 2046 magic attack */
#define SNAKELING_ANIM_DEATH   138   /* NPC 2045 death */
#define SNAKELING_ANIM_WALK    2405  /* walk cycle */

/* zulrah spotanim (projectile/cloud) model IDs */
#define GFX_RANGED_PROJ_MODEL  20390  /* GFX 1044 ranged projectile */
#define GFX_CLOUD_PROJ_MODEL   11221  /* GFX 1045 cloud projectile (flies to target) */
#define GFX_MAGIC_PROJ_MODEL   26593  /* GFX 1046 magic projectile */
#define GFX_TOXIC_CLOUD_MODEL   4086  /* object 11700 — venom cloud on ground (3x3) */
#define GFX_SNAKELING_SPAWN_MODEL 20390  /* GFX 1047 spawn orb (same mesh as 1044, white recolor) */

/* zulrah animation sequence IDs */
#define ZULRAH_ANIM_ATTACK   5068
#define ZULRAH_ANIM_IDLE     5069
#define ZULRAH_ANIM_DIVE     5072  /* DIG_ANIM_ID from RuneLite plugin — sinks into water */
#define ZULRAH_ANIM_SURFACE  5071  /* INITIAL_RISE_ANIM_ID — first appearance */
#define ZULRAH_ANIM_RISE     5073  /* RISE_ANIM_ID — subsequent phase surfaces */
#define ZULRAH_ANIM_5070     5070  /* unknown — possibly unused */
#define ZULRAH_ANIM_5806     5806  /* melee whip variant 1 */
#define ZULRAH_ANIM_5807     5807  /* melee whip variant 2 */

/* GFX 1047 snakeling spawn orb animation */
#define GFX_SNAKELING_SPAWN_ANIM 5358

static const NpcModelMapping* npc_model_lookup(uint16_t npc_id) {
    for (int i = 0; i < (int)(sizeof(NPC_MODEL_MAP) / sizeof(NPC_MODEL_MAP[0])); i++) {
        if (NPC_MODEL_MAP[i].npc_id == npc_id) return &NPC_MODEL_MAP[i];
    }
    return NULL;
}

#endif /* NPC_MODELS_H */
