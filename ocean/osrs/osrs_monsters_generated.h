#ifndef OSRS_MONSTERS_GENERATED_H
#define OSRS_MONSTERS_GENERATED_H

#include <stdint.h>

typedef enum {
    MON_JAL_NIB = 0,
    MON_JAL_MEJRAH = 1,
    MON_JAL_AK = 2,
    MON_JAL_AKREK_MEJ = 3,
    MON_JAL_AKREK_XIL = 4,
    MON_JAL_AKREK_KET = 5,
    MON_JAL_IMKOT = 6,
    MON_JAL_XIL = 7,
    MON_JAL_ZEK = 8,
    MON_JALTOK_JAD = 9,
    MON_YT_HURKOT = 10,
    MON_TZKAL_ZUK = 11,
    MON_ZUK_SHIELD = 12,
    MON_JAL_MEJJAK = 13,
    MON_ZULRAH_GREEN = 14,
    MON_ZULRAH_RED = 15,
    MON_ZULRAH_BLUE = 16,
    MON_ZULRAH_SNAKELING_MELEE = 17,
    MON_ZULRAH_SNAKELING_MAGIC = 18,
    MON_TFA_ARCHIVE_GUARDIAN = 19,
    MON_TFA_BORK = 20,
    MON_TFA_SMORKS = 21,
    MON_TFA_EZEKIEL = 22,
    MON_TFA_DREADNOTH = 23,
    MON_TFA_MOIA = 24,
    MON_TFA_XARTH = 25,
    MON_TFA_OCELLUS_P1 = 26,
    MON_TFA_OCELLUS_P3 = 27,
    MON_TFA_VOID_RIFT = 28,
    MON_TFA_VOID_AUGMENTER = 29,
    MON_TFA_VOID_VINE = 30,
    NUM_MONSTERS = 31
} MonsterIndex;

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
    int16_t melee_att_bonus;
    int16_t melee_str_bonus;
    int16_t magic_att_bonus;
    int16_t magic_str_bonus;
    int16_t range_att_bonus;
    int16_t ranged_str_bonus;
    int16_t stab_def;
    int16_t slash_def;
    int16_t crush_def;
    int16_t magic_def;
    int16_t ranged_def;
} MonsterStats;

static const MonsterStats MONSTER_DATABASE[NUM_MONSTERS] = {
    [MON_JAL_NIB] = {
        .npc_id = 7691, .name = "Jal-Nib",
        .hp = 10, .att_level = 1, .str_level = 1, .def_level = 15,
        .magic_level = 15, .range_level = 1,
        .attack_speed = 4, .size = 1, .max_hit = 4,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 0, .magic_str_bonus = 0,
        .range_att_bonus = 0, .ranged_str_bonus = 0,
        .stab_def = -20, .slash_def = -20, .crush_def = -20,
        .magic_def = -20, .ranged_def = -20
    },
    [MON_JAL_MEJRAH] = {
        .npc_id = 7692, .name = "Jal-MejRah",
        .hp = 25, .att_level = 0, .str_level = 0, .def_level = 55,
        .magic_level = 120, .range_level = 120,
        .attack_speed = 3, .size = 2, .max_hit = 19,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 0, .magic_str_bonus = 0,
        .range_att_bonus = 30, .ranged_str_bonus = 30,
        .stab_def = 30, .slash_def = 30, .crush_def = 30,
        .magic_def = -20, .ranged_def = 45
    },
    [MON_JAL_AK] = {
        .npc_id = 7693, .name = "Jal-Ak",
        .hp = 40, .att_level = 160, .str_level = 160, .def_level = 95,
        .magic_level = 160, .range_level = 160,
        .attack_speed = 6, .size = 3, .max_hit = 29,
        .melee_att_bonus = 0, .melee_str_bonus = 45, .magic_att_bonus = 45, .magic_str_bonus = 45,
        .range_att_bonus = 45, .ranged_str_bonus = 45,
        .stab_def = 25, .slash_def = 25, .crush_def = 25,
        .magic_def = 25, .ranged_def = 25
    },
    [MON_JAL_AKREK_MEJ] = {
        .npc_id = 7694, .name = "Jal-AkRek-Mej",
        .hp = 15, .att_level = 1, .str_level = 1, .def_level = 95,
        .magic_level = 120, .range_level = 1,
        .attack_speed = 4, .size = 1, .max_hit = 18,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 25, .magic_str_bonus = 25,
        .range_att_bonus = 0, .ranged_str_bonus = 0,
        .stab_def = 0, .slash_def = 0, .crush_def = 0,
        .magic_def = 25, .ranged_def = 0
    },
    [MON_JAL_AKREK_XIL] = {
        .npc_id = 7695, .name = "Jal-AkRek-Xil",
        .hp = 15, .att_level = 1, .str_level = 1, .def_level = 95,
        .magic_level = 1, .range_level = 120,
        .attack_speed = 4, .size = 1, .max_hit = 18,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 0, .magic_str_bonus = 0,
        .range_att_bonus = 25, .ranged_str_bonus = 25,
        .stab_def = 0, .slash_def = 0, .crush_def = 0,
        .magic_def = 0, .ranged_def = 25
    },
    [MON_JAL_AKREK_KET] = {
        .npc_id = 7696, .name = "Jal-AkRek-Ket",
        .hp = 15, .att_level = 120, .str_level = 120, .def_level = 95,
        .magic_level = 1, .range_level = 1,
        .attack_speed = 4, .size = 1, .max_hit = 18,
        .melee_att_bonus = 0, .melee_str_bonus = 25, .magic_att_bonus = 0, .magic_str_bonus = 0,
        .range_att_bonus = 0, .ranged_str_bonus = 0,
        .stab_def = 25, .slash_def = 25, .crush_def = 25,
        .magic_def = 0, .ranged_def = 0
    },
    [MON_JAL_IMKOT] = {
        .npc_id = 7697, .name = "Jal-ImKot",
        .hp = 75, .att_level = 210, .str_level = 290, .def_level = 120,
        .magic_level = 120, .range_level = 220,
        .attack_speed = 4, .size = 4, .max_hit = 49,
        .melee_att_bonus = 0, .melee_str_bonus = 40, .magic_att_bonus = 0, .magic_str_bonus = 0,
        .range_att_bonus = 0, .ranged_str_bonus = 0,
        .stab_def = 65, .slash_def = 65, .crush_def = 65,
        .magic_def = 30, .ranged_def = 50
    },
    [MON_JAL_XIL] = {
        .npc_id = 7698, .name = "Jal-Xil",
        .hp = 125, .att_level = 140, .str_level = 180, .def_level = 60,
        .magic_level = 90, .range_level = 250,
        .attack_speed = 4, .size = 3, .max_hit = 46,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 0, .magic_str_bonus = 0,
        .range_att_bonus = 40, .ranged_str_bonus = 50,
        .stab_def = 0, .slash_def = 0, .crush_def = 0,
        .magic_def = 0, .ranged_def = 0
    },
    [MON_JAL_ZEK] = {
        .npc_id = 7699, .name = "Jal-Zek",
        .hp = 220, .att_level = 370, .str_level = 510, .def_level = 260,
        .magic_level = 300, .range_level = 510,
        .attack_speed = 4, .size = 4, .max_hit = 70,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 80, .magic_str_bonus = 0,
        .range_att_bonus = 0, .ranged_str_bonus = 0,
        .stab_def = 0, .slash_def = 0, .crush_def = 0,
        .magic_def = 0, .ranged_def = 0
    },
    [MON_JALTOK_JAD] = {
        .npc_id = 7700, .name = "JalTok-Jad",
        .hp = 350, .att_level = 750, .str_level = 1020, .def_level = 480,
        .magic_level = 510, .range_level = 1020,
        .attack_speed = 8, .size = 5, .max_hit = 113,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 100, .magic_str_bonus = 75,
        .range_att_bonus = 80, .ranged_str_bonus = 0,
        .stab_def = 0, .slash_def = 0, .crush_def = 0,
        .magic_def = 0, .ranged_def = 0
    },
    [MON_YT_HURKOT] = {
        .npc_id = 7701, .name = "Yt-HurKot",
        .hp = 90, .att_level = 165, .str_level = 125, .def_level = 100,
        .magic_level = 150, .range_level = 150,
        .attack_speed = 4, .size = 1, .max_hit = 18,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 100, .magic_str_bonus = 0,
        .range_att_bonus = 80, .ranged_str_bonus = 0,
        .stab_def = 0, .slash_def = 0, .crush_def = 0,
        .magic_def = 130, .ranged_def = 130
    },
    [MON_TZKAL_ZUK] = {
        .npc_id = 7706, .name = "TzKal-Zuk",
        .hp = 1200, .att_level = 350, .str_level = 600, .def_level = 260,
        .magic_level = 150, .range_level = 400,
        .attack_speed = 10, .size = 7, .max_hit = 148,
        .melee_att_bonus = 0, .melee_str_bonus = 200, .magic_att_bonus = 550, .magic_str_bonus = 450,
        .range_att_bonus = 550, .ranged_str_bonus = 200,
        .stab_def = 0, .slash_def = 0, .crush_def = 0,
        .magic_def = 350, .ranged_def = 100
    },
    [MON_ZUK_SHIELD] = {
        .npc_id = 7707, .name = "Ancestral Glyph",
        .hp = 600, .att_level = 0, .str_level = 0, .def_level = 0,
        .magic_level = 0, .range_level = 0,
        .attack_speed = 0, .size = 5, .max_hit = 0,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 0, .magic_str_bonus = 0, .range_att_bonus = 0, .ranged_str_bonus = 0,
        .stab_def = 0, .slash_def = 0, .crush_def = 0, .magic_def = 0, .ranged_def = 0
    },
    [MON_JAL_MEJJAK] = {
        .npc_id = 7708, .name = "Jal-MejJak",
        .hp = 75, .att_level = 1, .str_level = 1, .def_level = 100,
        .magic_level = 1, .range_level = 1,
        .attack_speed = 3, .size = 1, .max_hit = 10,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 0, .magic_str_bonus = 0,
        .range_att_bonus = 0, .ranged_str_bonus = 0,
        .stab_def = 0, .slash_def = 0, .crush_def = 0,
        .magic_def = 0, .ranged_def = 0
    },
    [MON_ZULRAH_GREEN] = {
        .npc_id = 2042, .name = "Zulrah",
        .hp = 500, .att_level = 1, .str_level = 1, .def_level = 300,
        .magic_level = 300, .range_level = 300,
        .attack_speed = 3, .size = 5, .max_hit = 41,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 50, .magic_str_bonus = 20,
        .range_att_bonus = 50, .ranged_str_bonus = 20,
        .stab_def = 0, .slash_def = 0, .crush_def = 0,
        .magic_def = -45, .ranged_def = 50
    },
    [MON_ZULRAH_RED] = {
        .npc_id = 2043, .name = "Zulrah",
        .hp = 500, .att_level = 1, .str_level = 1, .def_level = 300,
        .magic_level = 300, .range_level = 300,
        .attack_speed = 3, .size = 5, .max_hit = 30,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 50, .magic_str_bonus = 20,
        .range_att_bonus = 50, .ranged_str_bonus = 20,
        .stab_def = 0, .slash_def = 0, .crush_def = 0,
        .magic_def = 0, .ranged_def = 300
    },
    [MON_ZULRAH_BLUE] = {
        .npc_id = 2044, .name = "Zulrah",
        .hp = 500, .att_level = 1, .str_level = 1, .def_level = 300,
        .magic_level = 300, .range_level = 300,
        .attack_speed = 3, .size = 5, .max_hit = 41,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 50, .magic_str_bonus = 20,
        .range_att_bonus = 50, .ranged_str_bonus = 20,
        .stab_def = 0, .slash_def = 0, .crush_def = 0,
        .magic_def = 300, .ranged_def = 0
    },
    [MON_ZULRAH_SNAKELING_MELEE] = {
        .npc_id = 2045, .name = "Snakeling",
        .hp = 1, .att_level = 140, .str_level = 138, .def_level = 1,
        .magic_level = 1, .range_level = 1,
        .attack_speed = 3, .size = 1, .max_hit = 15,
        .melee_att_bonus = 120, .melee_str_bonus = 0, .magic_att_bonus = 0, .magic_str_bonus = 0,
        .range_att_bonus = 0, .ranged_str_bonus = 0,
        .stab_def = -40, .slash_def = -40, .crush_def = -40,
        .magic_def = -40, .ranged_def = -40
    },
    [MON_ZULRAH_SNAKELING_MAGIC] = {
        .npc_id = 2046, .name = "Snakeling",
        .hp = 1, .att_level = 1, .str_level = 1, .def_level = 1,
        .magic_level = 185, .range_level = 1,
        .attack_speed = 3, .size = 1, .max_hit = 13,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 80, .magic_str_bonus = -20,
        .range_att_bonus = 0, .ranged_str_bonus = 0,
        .stab_def = -40, .slash_def = -40, .crush_def = -40,
        .magic_def = -40, .ranged_def = -40
    },
    /* The Fractured Archive, OSRS Wiki infoboxes 2026-10-20 (ocean/osrs/encounters/tfa/fixtures/tfa_wiki_stats.md).
       ranged_def is the standard ranged defence. Blank wiki fields are 0. */
    [MON_TFA_ARCHIVE_GUARDIAN] = {
        .npc_id = 16659, .name = "Archive Guardian",
        .hp = 550, .att_level = 250, .str_level = 240, .def_level = 260,
        .magic_level = 250, .range_level = 280,
        .attack_speed = 5, .size = 5, .max_hit = 25,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 350, .magic_str_bonus = 55,
        .range_att_bonus = 350, .ranged_str_bonus = 55,
        .stab_def = 120, .slash_def = 110, .crush_def = 20,
        .magic_def = 130, .ranged_def = 120
    },
    [MON_TFA_BORK] = {
        .npc_id = 16640, .name = "Bork",
        .hp = 1500, .att_level = 200, .str_level = 200, .def_level = 300,
        .magic_level = 90, .range_level = 200,
        .attack_speed = 7, .size = 4, .max_hit = 0,
        .melee_att_bonus = 100, .melee_str_bonus = 40, .magic_att_bonus = 0, .magic_str_bonus = 0,
        .range_att_bonus = 0, .ranged_str_bonus = 0,
        .stab_def = 140, .slash_def = 130, .crush_def = 10,
        .magic_def = 300, .ranged_def = 50
    },
    [MON_TFA_SMORKS] = {
        .npc_id = 16641, .name = "The Smorks",
        .hp = 600, .att_level = 1, .str_level = 1, .def_level = 220,
        .magic_level = 185, .range_level = 185,
        .attack_speed = 7, .size = 3, .max_hit = 0,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 185, .magic_str_bonus = 35,
        .range_att_bonus = 250, .ranged_str_bonus = 30,
        .stab_def = 220, .slash_def = 170, .crush_def = 160,
        .magic_def = 400, .ranged_def = 150
    },
    [MON_TFA_EZEKIEL] = {
        .npc_id = 16652, .name = "Ezekiel",
        .hp = 3000, .att_level = 250, .str_level = 150, .def_level = 130,
        .magic_level = 200, .range_level = 80,
        .attack_speed = 5, .size = 3, .max_hit = 0,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 250, .magic_str_bonus = 0,
        .range_att_bonus = 0, .ranged_str_bonus = 0,
        .stab_def = 150, .slash_def = 90, .crush_def = 35,
        .magic_def = 200, .ranged_def = 370
    },
    [MON_TFA_DREADNOTH] = {
        .npc_id = 16665, .name = "Dreadnoth the Jellied",
        .hp = 2100, .att_level = 280, .str_level = 285, .def_level = 125,
        .magic_level = 220, .range_level = 250,
        .attack_speed = 4, .size = 5, .max_hit = 0,
        .melee_att_bonus = 185, .melee_str_bonus = 35, .magic_att_bonus = 0, .magic_str_bonus = 0,
        .range_att_bonus = 180, .ranged_str_bonus = 20,
        .stab_def = 15, .slash_def = 20, .crush_def = 5,
        .magic_def = 20, .ranged_def = 80
    },
    [MON_TFA_MOIA] = {
        .npc_id = 16667, .name = "Moia",
        .hp = 785, .att_level = 1, .str_level = 1, .def_level = 195,
        .magic_level = 350, .range_level = 200,
        .attack_speed = 5, .size = 3, .max_hit = 0,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 300, .magic_str_bonus = 0,
        .range_att_bonus = 350, .ranged_str_bonus = 35,
        .stab_def = 20, .slash_def = 110, .crush_def = 15,
        .magic_def = 30, .ranged_def = 120
    },
    [MON_TFA_XARTH] = {
        .npc_id = 16684, .name = "Xarth",
        .hp = 2600, .att_level = 250, .str_level = 225, .def_level = 170,
        .magic_level = 350, .range_level = 220,
        .attack_speed = 4, .size = 3, .max_hit = 0,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 250, .magic_str_bonus = 0,
        .range_att_bonus = 250, .ranged_str_bonus = 0,
        .stab_def = 115, .slash_def = 105, .crush_def = 20,
        .magic_def = 205, .ranged_def = 230
    },
    [MON_TFA_OCELLUS_P1] = {
        .npc_id = 16674, .name = "Ocellus",
        .hp = 2500, .att_level = 1, .str_level = 1, .def_level = 215,
        .magic_level = 350, .range_level = 250,
        .attack_speed = 5, .size = 3, .max_hit = 0,
        .melee_att_bonus = 1, .melee_str_bonus = 1, .magic_att_bonus = 150, .magic_str_bonus = 0,
        .range_att_bonus = 150, .ranged_str_bonus = 0,
        .stab_def = 150, .slash_def = 95, .crush_def = 60,
        .magic_def = 100, .ranged_def = 135
    },
    [MON_TFA_OCELLUS_P3] = {
        .npc_id = 16677, .name = "Ocellus",
        .hp = 5250, .att_level = 325, .str_level = 250, .def_level = 250,
        .magic_level = 300, .range_level = 300,
        .attack_speed = 6, .size = 3, .max_hit = 0,
        .melee_att_bonus = 150, .melee_str_bonus = 10, .magic_att_bonus = 350, .magic_str_bonus = 10,
        .range_att_bonus = 0, .ranged_str_bonus = 0,
        .stab_def = 150, .slash_def = 100, .crush_def = 30,
        .magic_def = 200, .ranged_def = 250
    },
    [MON_TFA_VOID_RIFT] = {
        .npc_id = 16678, .name = "Void Rift",
        .hp = 44, .att_level = 0, .str_level = 0, .def_level = 175,
        .magic_level = 300, .range_level = 0,
        .attack_speed = 0, .size = 1, .max_hit = 0,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 0, .magic_str_bonus = 0,
        .range_att_bonus = 0, .ranged_str_bonus = 0,
        .stab_def = 0, .slash_def = 0, .crush_def = 0,
        .magic_def = 0, .ranged_def = 0
    },
    [MON_TFA_VOID_AUGMENTER] = {
        .npc_id = 16680, .name = "Void Augmenter",
        .hp = 44, .att_level = 0, .str_level = 0, .def_level = 175,
        .magic_level = 300, .range_level = 0,
        .attack_speed = 0, .size = 1, .max_hit = 0,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 0, .magic_str_bonus = 0,
        .range_att_bonus = 0, .ranged_str_bonus = 0,
        .stab_def = 0, .slash_def = 0, .crush_def = 0,
        .magic_def = 0, .ranged_def = 0
    },
    [MON_TFA_VOID_VINE] = {
        .npc_id = 16681, .name = "Void Vine",
        .hp = 44, .att_level = 0, .str_level = 0, .def_level = 0,
        .magic_level = 0, .range_level = 0,
        .attack_speed = 0, .size = 1, .max_hit = 0,
        .melee_att_bonus = 0, .melee_str_bonus = 0, .magic_att_bonus = 0, .magic_str_bonus = 0,
        .range_att_bonus = 0, .ranged_str_bonus = 0,
        .stab_def = 0, .slash_def = 0, .crush_def = 0,
        .magic_def = 0, .ranged_def = 0
    },
};

#endif
