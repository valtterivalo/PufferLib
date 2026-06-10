# Drop-in Colosseum NPC stats (shared MonsterStats struct)

Ground truth from refs/osrs-dps-calc (cross-checked vs wiki). Self-contained: combat fns
(`osrs_npc_attack_roll(att_level,att_bonus)`, `osrs_npc_max_hit(...)`, `encounter_npc_roll_attack(att_roll,def_roll,maxhit,rng)`)
take plain ints, so Colosseum needs NO entry in the shared generated MONSTER_DATABASE and NO monsters.json
regeneration (the generator is not in this checkout anyway). Keep this local table in the encounter.

Field order = MonsterStats (osrs_monsters_generated.h:37): npc_id,name,hp,att,str,def,magic,range,
attack_speed,size,max_hit, melee_att,melee_str,magic_att,magic_str,range_att,ranged_str, stab_def,slash_def,crush_def,magic_def,ranged_def.

```c
/** Canonical Fortis Colosseum NPC combat stats (refs/osrs-dps-calc, cross-checked vs OSRS wiki). */
static const MonsterStats COLO_NPC_STATS[COLO_NUM_NPC_TYPES] = {
    [COLO_BERSERKER] = { .npc_id=12816, .name="Fremennik berserker",
        .hp=48, .att_level=110,.str_level=110,.def_level=80,.magic_level=110,.range_level=110,
        .attack_speed=6,.size=1,.max_hit=29,
        .melee_att_bonus=150,.melee_str_bonus=90,.magic_att_bonus=150,.magic_str_bonus=0,.range_att_bonus=0,.ranged_str_bonus=0,
        .stab_def=50,.slash_def=50,.crush_def=50,.magic_def=0,.ranged_def=75 },
    [COLO_ARCHER] = { .npc_id=12814, .name="Fremennik archer",
        .hp=50, .att_level=110,.str_level=110,.def_level=80,.magic_level=110,.range_level=110,
        .attack_speed=6,.size=1,.max_hit=14,
        .melee_att_bonus=150,.melee_str_bonus=150,.magic_att_bonus=150,.magic_str_bonus=0,.range_att_bonus=150,.ranged_str_bonus=10,
        .stab_def=0,.slash_def=0,.crush_def=0,.magic_def=75,.ranged_def=50 },
    [COLO_SEER] = { .npc_id=12815, .name="Fremennik seer",
        .hp=50, .att_level=110,.str_level=110,.def_level=80,.magic_level=110,.range_level=110,
        .attack_speed=6,.size=1,.max_hit=12,
        .melee_att_bonus=0,.melee_str_bonus=0,.magic_att_bonus=150,.magic_str_bonus=0,.range_att_bonus=0,.ranged_str_bonus=0,
        .stab_def=50,.slash_def=50,.crush_def=50,.magic_def=30,.ranged_def=0 },
    [COLO_SERPENT_SHAMAN] = { .npc_id=12811, .name="Serpent shaman",
        .hp=125, .att_level=100,.str_level=90,.def_level=90,.magic_level=220,.range_level=160,
        .attack_speed=5,.size=1,.max_hit=28,
        .melee_att_bonus=0,.melee_str_bonus=0,.magic_att_bonus=50,.magic_str_bonus=15,.range_att_bonus=0,.ranged_str_bonus=0,
        .stab_def=30,.slash_def=30,.crush_def=30,.magic_def=15,.ranged_def=50 },
    [COLO_JAGUAR_WARRIOR] = { .npc_id=12810, .name="Jaguar warrior",
        .hp=125, .att_level=200,.str_level=330,.def_level=125,.magic_level=100,.range_level=160,
        .attack_speed=5,.size=2,.max_hit=47,
        .melee_att_bonus=0,.melee_str_bonus=25,.magic_att_bonus=0,.magic_str_bonus=0,.range_att_bonus=0,.ranged_str_bonus=0,
        .stab_def=30,.slash_def=30,.crush_def=30,.magic_def=15,.ranged_def=45 },
    [COLO_JAVELIN_COLOSSUS] = { .npc_id=12817, .name="Javelin Colossus",
        .hp=220, .att_level=200,.str_level=300,.def_level=190,.magic_level=225,.range_level=360,
        .attack_speed=5,.size=3,.max_hit=48,
        .melee_att_bonus=0,.melee_str_bonus=0,.magic_att_bonus=0,.magic_str_bonus=0,.range_att_bonus=25,.ranged_str_bonus=20,
        .stab_def=15,.slash_def=15,.crush_def=15,.magic_def=20,.ranged_def=30 },
    [COLO_SHOCKWAVE_COLOSSUS] = { .npc_id=12819, .name="Shockwave Colossus",
        .hp=125, .att_level=120,.str_level=190,.def_level=150,.magic_level=350,.range_level=220,
        .attack_speed=5,.size=3,.max_hit=56,
        .melee_att_bonus=0,.melee_str_bonus=0,.magic_att_bonus=55,.magic_str_bonus=35,.range_att_bonus=0,.ranged_str_bonus=0,
        .stab_def=15,.slash_def=15,.crush_def=15,.magic_def=5,.ranged_def=35 },
    [COLO_MINOTAUR] = { .npc_id=12812, .name="Minotaur",
        .hp=225, .att_level=300,.str_level=360,.def_level=190,.magic_level=250,.range_level=120,
        .attack_speed=5,.size=3,.max_hit=74,
        .melee_att_bonus=15,.melee_str_bonus=64,.magic_att_bonus=0,.magic_str_bonus=0,.range_att_bonus=0,.ranged_str_bonus=0,
        .stab_def=0,.slash_def=0,.crush_def=0,.magic_def=0,.ranged_def=12 },
    [COLO_MANTICORE] = { .npc_id=12818, .name="Manticore",
        .hp=250, .att_level=300,.str_level=300,.def_level=250,.magic_level=300,.range_level=350,
        .attack_speed=10,.size=3,.max_hit=36, /* per-style: rng36/mag31/melee31, handled in barrage logic */
        .melee_att_bonus=0,.melee_str_bonus=0,.magic_att_bonus=0,.magic_str_bonus=0,.range_att_bonus=0,.ranged_str_bonus=0,
        .stab_def=0,.slash_def=0,.crush_def=0,.magic_def=10,.ranged_def=25 },
    [COLO_SOL_HEREDIT] = { .npc_id=12821, .name="Sol Heredit",
        .hp=1500, .att_level=350,.str_level=400,.def_level=200,.magic_level=300,.range_level=350,
        .attack_speed=5,.size=5,.max_hit=44, /* AoE/grapple 44; TP combos 15/25/35 & 15/30/45 in boss logic */
        .melee_att_bonus=250,.melee_str_bonus=5,.magic_att_bonus=0,.magic_str_bonus=0,.range_att_bonus=150,.ranged_str_bonus=5,
        .stab_def=65,.slash_def=5,.crush_def=30,.magic_def=750,.ranged_def=825 },
};
```

Per-style max hits (used by combat/barrage logic, not the single max_hit field):
- Manticore: ranged 36, magic 31, melee 31.
- Jaguar: 47 per hit, 3 independent hits per attack.
- Javelin: 48 base (49/51/54 under Relentless I/II/III).
- Sol: AoE 44, grapple 44, Triple Parry 15/25/35 and 15/30/45.

Sol weakness is encoded by defence bonuses (slash 5 vs magic 750 / ranged 825), so the shared accuracy
roll naturally makes slash the only viable style. No special-casing needed.
