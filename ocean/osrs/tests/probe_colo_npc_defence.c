/**
 * @file probe_colo_npc_defence.c
 * @brief Measure NPC per-style defence / magic-level / HP vs the combat-triangle
 *        claim. Uses the real shared formulas from osrs_combat.h. No env build.
 *
 * BUILD:
 *   cc -std=c11 -O2 -I. -o /tmp/probe_npc_def \
 *       ocean/osrs/tests/probe_colo_npc_defence.c -lm
 *   /tmp/probe_npc_def
 */
#include <math.h>
#include <stdio.h>
#include <stdint.h>

#include "ocean/osrs/osrs_combat.h"

/* Per-style NPC def_roll EXACTLY as the real col_npc_target_def_roll
   (encounter_colosseum_player_actions.inc:995-1010):
   def_roll = (def_level + 9) * (style_def_bonus + 64).
   NOTE: the MAGIC branch uses def_level, NOT magic_level. style: 1=melee,2=ranged,3=magic. */
static int npc_def_roll(int def_level, int magic_level,
                        int stab, int slash, int crush,
                        int magic_def_bonus, int ranged_def_bonus,
                        int style, int melee_style) {
    (void)magic_level;
    int def_bonus;
    if (style == 3) def_bonus = magic_def_bonus;
    else if (style == 2) def_bonus = ranged_def_bonus;
    else def_bonus = (melee_style == 1) ? slash : (melee_style == 2) ? crush : stab;
    return (def_level + 9) * (def_bonus + 64);
}

static void report(const char* name, int hp, int def_level, int magic_level,
                   int stab, int slash, int crush,
                   int magic_def_bonus, int ranged_def_bonus) {
    printf("\n=== %s  (HP=%d def_lvl=%d magic_lvl=%d) ===\n",
           name, hp, def_level, magic_level);
    int dr_melee = npc_def_roll(def_level, magic_level, stab, slash, crush,
                                magic_def_bonus, ranged_def_bonus, 1, 0 /*stab*/);
    int dr_range = npc_def_roll(def_level, magic_level, stab, slash, crush,
                                magic_def_bonus, ranged_def_bonus, 2, 0);
    int dr_magic = npc_def_roll(def_level, magic_level, stab, slash, crush,
                                magic_def_bonus, ranged_def_bonus, 3, 0);
    printf("  def_roll  melee=%d  ranged=%d  magic=%d\n", dr_melee, dr_range, dr_magic);

    /* Player attack rolls for representative BIS setups.
       eff_level = base 99 + style prayer + stance ~= these; attack_bonus from gear.
       These are deliberately generous (BIS + offensive prayer) to test whether the
       correct style even lands reliably. */
    /* MAGIC: shadow+occult+confliction+augury. eff magic ~ 99*1.25(augury)+9 ~ 132.
       magic attack bonus: shadow ~ +(big). Shadow triples gear; magic_att gear ~ 100+.
       Use eff 132, att_bonus 170 (generous). */
    int mage_att = osrs_player_att_roll(132, 170);
    /* RANGED tbow: eff range 99*1.10(rigour)+9 ~ 117, range att bonus ~ +110. */
    int range_eff = (int)(99 * 1.10) + 9;
    int range_att_base = osrs_player_att_roll(range_eff, 110);
    /* MELEE tentacle+piety: eff att 99*1.20(piety)+11 ~ 129, stab bonus ~ +90. */
    int melee_att = osrs_player_att_roll(129, 90);

    /* tbow accuracy multiplier vs this target magic level */
    float tbow_acc_mult = osrs_tbow_acc_mult(magic_level);
    int tbow_att = (int)(range_att_base * tbow_acc_mult);

    float hc_mage  = osrs_hit_chance(mage_att, dr_magic);
    float hc_tbow  = osrs_hit_chance(tbow_att, dr_range);
    float hc_range = osrs_hit_chance(range_att_base, dr_range);
    float hc_melee = osrs_hit_chance(melee_att, dr_melee);

    printf("  hit_chance  magic(shadow)=%.3f  tbow=%.3f  ranged(bowfa/venator base)=%.3f  melee(tentacle)=%.3f\n",
           hc_mage, hc_tbow, hc_range, hc_melee);
    printf("  tbow acc_mult=%.3f  dmg_mult=%.3f\n",
           tbow_acc_mult, osrs_tbow_dmg_mult(magic_level));

    /* Max hits. Shadow magic: base 34, gear magic_dmg% tripled. Occult5+Confliction7+treads2=14 -> *3=42 -> +Augury? augury adds acc, not %dmg in this sim. */
    int shadow_max = osrs_player_magic_max_hit(34, 42);
    /* tbow base ~ ranged str. eff range 117, ranged str bonus ~ +75 (masori+ammo). */
    int tbow_base_max = osrs_player_ranged_max_hit(range_eff, 75);
    int tbow_max = (int)(tbow_base_max * osrs_tbow_dmg_mult(magic_level));
    printf("  shadow_max_hit~%d   tbow_max_hit~%d (base %d * dmg_mult)\n",
           shadow_max, tbow_max, tbow_base_max);
    printf("  -> shadow one-shot HP%d? %s   tbow one-shot? %s\n",
           hp, shadow_max >= hp ? "YES" : "no",
           tbow_max >= hp ? "YES" : "no");
}

int main(void) {
    printf("NPC per-style defence / triangle probe (shared osrs_combat.h formulas)\n");
    printf("magic def_roll formula = (def_level+9)*(magic_def_bonus+64)  <-- uses DEF_LEVEL, magic_level UNUSED for player-vs-NPC defence\n");

    /* values copied from COLO_NPC_BASE in encounter_colosseum_model.inc */
    report("Fremennik BERSERKER", 48, 80, 110, 50,50,50, /*magdef*/0, /*rngdef*/75);
    report("Fremennik ARCHER",    50, 80, 110, 0,0,0,   /*magdef*/75, /*rngdef*/50);
    report("Fremennik SEER",      50, 80, 110, 50,50,50, /*magdef*/30, /*rngdef*/0);
    report("SERPENT SHAMAN",     125, 90, 220, 30,30,30, /*magdef*/15, /*rngdef*/50);
    report("JAGUAR WARRIOR",     125,125, 100, 30,30,30, /*magdef*/15, /*rngdef*/45);

    printf("\nNOTE: warband one-shot in sim is via force-max (player_style_that_max_hits),\n");
    printf("      NOT via the def_roll/hit-chance above. The hit_chance columns show what\n");
    printf("      the agent WOULD face if force-max were absent. Serpent has NO force-max.\n");
    return 0;
}
