/**
 * @fileoverview osrs_damage.h — shared OSRS damage application pipeline.
 *
 * unified pending hit queue and damage pipeline used by all encounters (PvE)
 * and PvP. replaces duplicated logic across osrs_pvp_combat.h, osrs_encounter.h,
 * and encounter_zulrah.h with a single source of truth.
 *
 * PENDING HITS:
 *   osrs_queue_pending_hit(queue, ...)         queue a hit with delay
 *   osrs_tick_pending_hits(queue, max)         tick all, return landed count
 *
 * DAMAGE PIPELINE:
 *   osrs_apply_damage_pipeline(...)            prayer -> veng -> recoil -> smite
 *
 * HELPERS:
 *   osrs_has_recoil_ring(equipped)             check ring of recoil / suffering (i)
 *
 * the pipeline is a pure function — computes damage chain without modifying state.
 * callers apply the returned DamageResult to their own game state.
 *
 * ref: osrs wiki "protection prayers", "vengeance", "ring of recoil", "smite"
 * ref: osrs_pvp_combat.h:570-691 (original PvP apply_damage)
 * ref: encounter_zulrah.h:651-675 (original zulrah recoil)
 */

#ifndef OSRS_DAMAGE_H
#define OSRS_DAMAGE_H

#include "osrs_combat.h"
#include "osrs_items.h"

/* ======================================================================== */
/* pending hit queue                                                         */
/* ======================================================================== */

/* spell types for barrage freeze/heal effects on pending hits.
   duplicated from osrs_encounter.h — these live here because osrs_damage.h
   owns the pending hit struct. osrs_encounter.h will migrate to use these. */
#ifndef ENCOUNTER_SPELL_NONE
#define ENCOUNTER_SPELL_NONE  0
#define ENCOUNTER_SPELL_ICE   1   /* ice barrage: freeze on hit */
#define ENCOUNTER_SPELL_BLOOD 2   /* blood barrage: heal 25% of AoE damage */
#endif

#define OSRS_MAX_PENDING_HITS 16

typedef struct {
    int active;
    int damage;
    int ticks_remaining;      /* countdown to landing */
    int attack_style;         /* ATTACK_STYLE_* for prayer check */
    int check_prayer;         /* 1 = re-check prayer when hit lands (jad) */
    int spell_type;           /* ENCOUNTER_SPELL_* for freeze/heal effects */
    int is_pvp;               /* 1 = 40% prayer reduction, 0 = 100% block */
    int source_is_player;     /* 1 = attacker is a player (for veng/recoil) */
} OsrsPendingHit;

/* queue a new pending hit. returns slot index or -1 if full. */
static inline int osrs_queue_pending_hit(OsrsPendingHit* queue, int max_hits,
                                         int damage, int ticks, int attack_style,
                                         int check_prayer, int spell_type,
                                         int is_pvp, int source_is_player) {
    for (int i = 0; i < max_hits; i++) {
        if (!queue[i].active) {
            queue[i].active = 1;
            queue[i].damage = damage;
            queue[i].ticks_remaining = ticks;
            queue[i].attack_style = attack_style;
            queue[i].check_prayer = check_prayer;
            queue[i].spell_type = spell_type;
            queue[i].is_pvp = is_pvp;
            queue[i].source_is_player = source_is_player;
            return i;
        }
    }
    return -1;  /* queue full */
}

/* tick all pending hits: decrement ticks_remaining, return count of hits that
   landed this tick (reached ticks_remaining == 0). landed hits remain active
   so the caller can iterate and process them, then clear active = 0. */
static inline int osrs_tick_pending_hits(OsrsPendingHit* queue, int max_hits) {
    int landed = 0;
    for (int i = 0; i < max_hits; i++) {
        if (!queue[i].active) continue;
        queue[i].ticks_remaining--;
        if (queue[i].ticks_remaining <= 0) {
            landed++;
        }
    }
    return landed;
}

/* ======================================================================== */
/* damage pipeline result                                                    */
/* ======================================================================== */

typedef struct {
    int final_damage;       /* damage after prayer reduction */
    int veng_damage;        /* reflected by vengeance (0 if inactive) */
    int recoil_damage;      /* reflected by recoil ring (0 if no ring) */
    int smite_drain;        /* prayer drained from target (0 if no smite) */
    int prayer_blocked;     /* 1 if correct prayer was active */
} DamageResult;

/* apply the full OSRS damage pipeline to a hit.
   pure function — does NOT modify any state. caller applies the result.

   raw_damage: damage before any reduction
   attack_style: ATTACK_STYLE_MELEE/RANGED/MAGIC
   target_prayer: target's active overhead prayer (OverheadPrayer enum)
   is_pvp: 1 = 40% prayer reduction, 0 = 100% block
   target_veng_active: 1 if target has vengeance active
   target_has_recoil: 1 if target has ring of recoil / ring of suffering (i)
   attacker_smite_active: 1 if attacker has smite prayer active

   pipeline order:
     1. prayer reduction (osrs_prayer_reduce_damage from osrs_combat.h)
     2. vengeance: floor(final_damage * 0.75) reflected to attacker
     3. recoil: floor(final_damage * 0.1) + 1 reflected to attacker
     4. smite: floor(final_damage / 4) drained from target prayer

   ref: osrs_pvp_combat.h:570-691, encounter_zulrah.h:651-675 */
static inline DamageResult osrs_apply_damage_pipeline(
    int raw_damage, int attack_style,
    int target_prayer, int is_pvp,
    int target_veng_active,
    int target_has_recoil,
    int attacker_smite_active
) {
    DamageResult r = {0, 0, 0, 0, 0};

    /* 1. prayer reduction */
    int prayer_correct = encounter_prayer_correct_for_style(target_prayer, attack_style);
    r.prayer_blocked = prayer_correct;
    r.final_damage = osrs_prayer_reduce_damage(raw_damage, target_prayer, attack_style, is_pvp);

    /* 2. vengeance: 75% of post-prayer damage reflected to attacker.
       ref: osrs_pvp_combat.h:607-618 */
    if (target_veng_active && r.final_damage > 0) {
        r.veng_damage = (int)(r.final_damage * 0.75f);
    }

    /* 3. recoil: floor(damage * 0.1) + 1 reflected to attacker.
       charge tracking is caller's responsibility (ring of recoil has 40 charges,
       ring of suffering (i) has infinite).
       ref: osrs_pvp_combat.h:621-645, encounter_zulrah.h:660-675 */
    if (target_has_recoil && r.final_damage > 0) {
        r.recoil_damage = r.final_damage / 10 + 1;
    }

    /* 4. smite: floor(damage / 4) drained from target prayer.
       ref: osrs_pvp_combat.h:688-691 */
    if (attacker_smite_active && r.final_damage > 0) {
        r.smite_drain = r.final_damage / 4;
    }

    return r;
}

/* ======================================================================== */
/* helpers                                                                   */
/* ======================================================================== */

/* check if player has a recoil-capable ring equipped.
   ring of recoil (finite charges) or ring of suffering (i) (infinite).
   replaces has_recoil_effect() in osrs_pvp_combat.h:22 and
   zul_has_recoil_effect() in encounter_zulrah.h. */
static inline int osrs_has_recoil_ring(const uint8_t* equipped) {
    uint8_t ring = equipped[GEAR_SLOT_RING];
    return ring == ITEM_RING_OF_RECOIL || ring == ITEM_RING_OF_SUFFERING_RI;
}

#endif /* OSRS_DAMAGE_H */
