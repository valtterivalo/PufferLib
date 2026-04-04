/**
 * @file osrs_monsters_generated.h
 * @brief AUTO-GENERATED monster database from monsters.json
 *
 * DO NOT EDIT — regenerate with:
 *   python pufferlib/ocean/osrs/tools/generate_monsters.py
 */

#ifndef OSRS_MONSTERS_GENERATED_H
#define OSRS_MONSTERS_GENERATED_H

#include <stdint.h>

typedef enum {
    GEN_MON_JAL_NIB = 0,  /* Nibbler */
    GEN_MON_JAL_MEJRAH = 1,  /* Bat */
    GEN_MON_JAL_AK = 2,  /* Blob */
    GEN_MON_JAL_AKREK_MEJ = 3,  /* Blob mage split */
    GEN_MON_JAL_AKREK_XIL = 4,  /* Blob range split */
    GEN_MON_JAL_AKREK_KET = 5,  /* Blob melee split */
    GEN_MON_JAL_IMKOT = 6,  /* Meleer */
    GEN_MON_JAL_XIL = 7,  /* Ranger */
    GEN_MON_JAL_ZEK = 8,  /* Mager */
    GEN_MON_JALTOK_JAD = 9,  /* Jad */
    GEN_MON_YT_HURKOT = 10,  /* Jad healer */
    GEN_MON_TZKAL_ZUK = 11,  /* Zuk */
    GEN_MON_ZUK_SHIELD = 12,  /* Ancestral Glyph */
    GEN_MON_JAL_MEJJAK = 13,  /* Zuk healer */
    GEN_NUM_MONSTERS = 14
} GenMonsterIndex;

typedef struct {
    uint16_t npc_id;
    char name[32];
    int16_t hp;
    int16_t att_level;
    int16_t str_level;
    int16_t def_level;
    int16_t magic_level;
    int16_t range_level;
    uint8_t attack_speed;
    uint8_t size;
    int16_t max_hit;
    /* offensive bonuses */
    int16_t melee_att_bonus;
    int16_t melee_str_bonus;
    int16_t magic_att_bonus;
    int16_t magic_str_bonus;
    int16_t range_att_bonus;
    int16_t ranged_str_bonus;
    /* defensive bonuses */
    int16_t stab_def;
    int16_t slash_def;
    int16_t crush_def;
    int16_t magic_def;
    int16_t ranged_def;
} GenMonster;

static const GenMonster GEN_MONSTER_DATABASE[GEN_NUM_MONSTERS] = {
    [GEN_MON_JAL_NIB] = { /* Nibbler */
        .npc_id = 7691, .name = "Jal-Nib",
        .hp = 10, .att_level = 1, .str_level = 1, .def_level = 15,
        .magic_level = 15, .range_level = 1,
        .attack_speed = 4, .size = 1, .max_hit = 4,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 0, .magic_str_bonus = 0,
        .range_att_bonus = 0, .ranged_str_bonus = 0,
        .stab_def = -20, .slash_def = -20, .crush_def = -20,
        .magic_def = -20, .ranged_def = -20
    },
    [GEN_MON_JAL_MEJRAH] = { /* Bat */
        .npc_id = 7692, .name = "Jal-MejRah",
        .hp = 25, .att_level = 0, .str_level = 0, .def_level = 55,
        .magic_level = 120, .range_level = 120,
        .attack_speed = 3, .size = 2, .max_hit = 19,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 0, .magic_str_bonus = 0,
        .range_att_bonus = 30, .ranged_str_bonus = 30,
        .stab_def = 30, .slash_def = 30, .crush_def = 30,
        .magic_def = -20, .ranged_def = 45
    },
    [GEN_MON_JAL_AK] = { /* Blob */
        .npc_id = 7693, .name = "Jal-Ak",
        .hp = 40, .att_level = 160, .str_level = 160, .def_level = 95,
        .magic_level = 160, .range_level = 160,
        .attack_speed = 6, .size = 3, .max_hit = 29,
        .melee_att_bonus = 0, .melee_str_bonus = 45, .magic_att_bonus = 45, .magic_str_bonus = 45,
        .range_att_bonus = 45, .ranged_str_bonus = 45,
        .stab_def = 25, .slash_def = 25, .crush_def = 25,
        .magic_def = 25, .ranged_def = 25
    },
    [GEN_MON_JAL_AKREK_MEJ] = { /* Blob mage split */
        .npc_id = 7694, .name = "Jal-AkRek-Mej",
        .hp = 15, .att_level = 1, .str_level = 1, .def_level = 95,
        .magic_level = 120, .range_level = 1,
        .attack_speed = 4, .size = 1, .max_hit = 18,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 25, .magic_str_bonus = 25,
        .range_att_bonus = 0, .ranged_str_bonus = 0,
        .stab_def = 0, .slash_def = 0, .crush_def = 0,
        .magic_def = 25, .ranged_def = 0
    },
    [GEN_MON_JAL_AKREK_XIL] = { /* Blob range split */
        .npc_id = 7695, .name = "Jal-AkRek-Xil",
        .hp = 15, .att_level = 1, .str_level = 1, .def_level = 95,
        .magic_level = 1, .range_level = 120,
        .attack_speed = 4, .size = 1, .max_hit = 18,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 0, .magic_str_bonus = 0,
        .range_att_bonus = 25, .ranged_str_bonus = 25,
        .stab_def = 0, .slash_def = 0, .crush_def = 0,
        .magic_def = 0, .ranged_def = 25
    },
    [GEN_MON_JAL_AKREK_KET] = { /* Blob melee split */
        .npc_id = 7696, .name = "Jal-AkRek-Ket",
        .hp = 15, .att_level = 120, .str_level = 120, .def_level = 95,
        .magic_level = 1, .range_level = 1,
        .attack_speed = 4, .size = 1, .max_hit = 18,
        .melee_att_bonus = 0, .melee_str_bonus = 25, .magic_att_bonus = 0, .magic_str_bonus = 0,
        .range_att_bonus = 0, .ranged_str_bonus = 0,
        .stab_def = 25, .slash_def = 25, .crush_def = 25,
        .magic_def = 0, .ranged_def = 0
    },
    [GEN_MON_JAL_IMKOT] = { /* Meleer */
        .npc_id = 7697, .name = "Jal-ImKot",
        .hp = 75, .att_level = 210, .str_level = 290, .def_level = 120,
        .magic_level = 120, .range_level = 220,
        .attack_speed = 4, .size = 4, .max_hit = 49,
        .melee_att_bonus = 0, .melee_str_bonus = 40, .magic_att_bonus = 0, .magic_str_bonus = 0,
        .range_att_bonus = 0, .ranged_str_bonus = 0,
        .stab_def = 65, .slash_def = 65, .crush_def = 65,
        .magic_def = 30, .ranged_def = 50
    },
    [GEN_MON_JAL_XIL] = { /* Ranger */
        .npc_id = 7698, .name = "Jal-Xil",
        .hp = 125, .att_level = 140, .str_level = 180, .def_level = 60,
        .magic_level = 90, .range_level = 250,
        .attack_speed = 4, .size = 3, .max_hit = 46,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 0, .magic_str_bonus = 0,
        .range_att_bonus = 40, .ranged_str_bonus = 50,
        .stab_def = 0, .slash_def = 0, .crush_def = 0,
        .magic_def = 0, .ranged_def = 0
    },
    [GEN_MON_JAL_ZEK] = { /* Mager */
        .npc_id = 7699, .name = "Jal-Zek",
        .hp = 220, .att_level = 370, .str_level = 510, .def_level = 260,
        .magic_level = 300, .range_level = 510,
        .attack_speed = 4, .size = 4, .max_hit = 70,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 80, .magic_str_bonus = 0,
        .range_att_bonus = 0, .ranged_str_bonus = 0,
        .stab_def = 0, .slash_def = 0, .crush_def = 0,
        .magic_def = 0, .ranged_def = 0
    },
    [GEN_MON_JALTOK_JAD] = { /* Jad */
        .npc_id = 7700, .name = "JalTok-Jad",
        .hp = 350, .att_level = 750, .str_level = 1020, .def_level = 480,
        .magic_level = 510, .range_level = 1020,
        .attack_speed = 8, .size = 5, .max_hit = 113,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 100, .magic_str_bonus = 75,
        .range_att_bonus = 80, .ranged_str_bonus = 0,
        .stab_def = 0, .slash_def = 0, .crush_def = 0,
        .magic_def = 0, .ranged_def = 0
    },
    [GEN_MON_YT_HURKOT] = { /* Jad healer */
        .npc_id = 7701, .name = "Yt-HurKot",
        .hp = 90, .att_level = 165, .str_level = 125, .def_level = 100,
        .magic_level = 150, .range_level = 150,
        .attack_speed = 4, .size = 1, .max_hit = 18,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 100, .magic_str_bonus = 0,
        .range_att_bonus = 80, .ranged_str_bonus = 0,
        .stab_def = 0, .slash_def = 0, .crush_def = 0,
        .magic_def = 130, .ranged_def = 130
    },
    [GEN_MON_TZKAL_ZUK] = { /* Zuk */
        .npc_id = 7706, .name = "TzKal-Zuk",
        .hp = 1200, .att_level = 350, .str_level = 600, .def_level = 260,
        .magic_level = 150, .range_level = 400,
        .attack_speed = 10, .size = 7, .max_hit = 0,
        .melee_att_bonus = 0, .melee_str_bonus = 200, .magic_att_bonus = 550, .magic_str_bonus = 450,
        .range_att_bonus = 550, .ranged_str_bonus = 200,
        .stab_def = 0, .slash_def = 0, .crush_def = 0,
        .magic_def = 350, .ranged_def = 100
    },
    [GEN_MON_ZUK_SHIELD] = { /* Ancestral Glyph (manual) */
        .npc_id = 7707, .name = "Ancestral Glyph",
        .hp = 600, .att_level = 0, .str_level = 0, .def_level = 0,
        .magic_level = 0, .range_level = 0,
        .attack_speed = 0, .size = 5, .max_hit = 0,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 0, .magic_str_bonus = 0, .range_att_bonus = 0, .ranged_str_bonus = 0,
        .stab_def = 0, .slash_def = 0, .crush_def = 0, .magic_def = 0, .ranged_def = 0
    },
    [GEN_MON_JAL_MEJJAK] = { /* Zuk healer */
        .npc_id = 7708, .name = "Jal-MejJak",
        .hp = 75, .att_level = 1, .str_level = 1, .def_level = 100,
        .magic_level = 1, .range_level = 1,
        .attack_speed = 3, .size = 1, .max_hit = 10,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 0, .magic_str_bonus = 0,
        .range_att_bonus = 0, .ranged_str_bonus = 0,
        .stab_def = 0, .slash_def = 0, .crush_def = 0,
        .magic_def = 0, .ranged_def = 0
    },
};

#endif /* OSRS_MONSTERS_GENERATED_H */
