#ifndef OSRS_ASSET_FORMATS_H
#define OSRS_ASSET_FORMATS_H

#include <stdint.h>

#define TERR_MAGIC 0x54455252
#define OBJS_MAGIC 0x4F424A53
#define OBJ2_MAGIC 0x4F424A32
#define ATLS_MAGIC 0x41544C53
#define MDL4_MAGIC 0x4D444C34
#define TANM_MAGIC 0x4D4E4154
#define TANM_VERSION 1
#define ANIM2_MAGIC 0x324D4E41

#define OSRS_SPOTANIM_MAGIC 0x544F5053u
#define OSRS_SPOTANIM_VERSION 1u
#define OSRS_NPC_MODEL_BASE 0x000C0000u
#define OSRS_SPOTANIM_MODEL_BASE 0x000D0000u

typedef struct {
    uint32_t id;
    int32_t model_id;
    int32_t animation_id;
    uint32_t resize_xy;
    uint32_t resize_z;
    uint32_t rotation;
    int32_t brightness;
    int32_t shadow;
} OsrsSpotAnimDef;

#endif
