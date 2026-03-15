# inferno encounter specs

extracted from reference code (osrs-sdk TypeScript, RuneLite inferno plugin Java).
this file is the source of truth for encounter_inferno.h implementation.

## NPC types

| id | name | OSRS name | default attack | ticks after anim | range | size | notes |
|---|---|---|---|---|---|---|---|
| 0 | nibbler | Jal-Nib | melee | 4 | melee | 1 | eats pillars |
| 1 | bat | Jal-MejRah | ranged | 3 | 4 | 1 | short range |
| 2 | blob | Jal-Ak | prayer-reads | 6 | 15 | 3 | switches style based on player prayer, splits on death |
| 3 | meleer | Jal-ImKot | melee | 4 | 1 | 4 | can dig/burrow (12 tick delay), destroys pillars |
| 4 | ranger | Jal-Xil | ranged | 4 | 98 | 3 | 125 HP, 250 range, can melee if close (50% chance) |
| 5 | mager | Jal-Zek | magic | 4 | 98 | 4 | can heal other NPCs, can melee if close |
| 6 | jad | JalTok-Jad | alternates range/mage | 3 | 99 | 5 | 8-tick attack cycle, spawns healers |
| 7 | zuk | TzKal-Zuk | special | 10 (normal), 7 (final) | 99 | huge | shield mechanic, final phase |

## wave compositions

each wave always starts with 3 nibblers (except cleanup waves 3,8,17,34 which have 6 nibblers).
waves 1-66 add progressively harder combinations. wave 67 = 1 jad, 68 = 3 jads, 69 = zuk.

```
wave  monsters (N=nibbler B=bat BL=blob M=meleer R=ranger MA=mager J=jad Z=zuk)
1     NNN B
2     NNN BB
3     NNNNNN
4     NNN BL
5     NNN B BL
6     NNN BB BL
7     NNN BL BL
8     NNNNNN
9     NNN M
10    NNN B M
11    NNN BB M
12    NNN BL M
13    NNN B BL M
14    NNN BB BL M
15    NNN BL BL M
16    NNN MM
17    NNNNNN
18    NNN R
19    NNN B R
20    NNN BB R
21    NNN BL R
22    NNN B BL R
23    NNN BB BL R
24    NNN BL BL R
25    NNN M R
26    NNN B M R
27    NNN BB M R
28    NNN BL M R
29    NNN B BL M R
30    NNN BB BL M R
31    NNN BL BL M R
32    NNN MM R
33    NNN RR
34    NNNNNN
35    NNN MA
36    NNN B MA
37    NNN BB MA
38    NNN BL MA
39    NNN B BL MA
40    NNN BB BL MA
41    NNN BL BL MA
42    NNN M MA
43    NNN B M MA
44    NNN BB M MA
45    NNN BL M MA
46    NNN B BL M MA
47    NNN BB BL M MA
48    NNN BL BL M MA
49    NNN MM MA
50    NNN R MA
51    NNN B R MA
52    NNN BB R MA
53    NNN BL R MA
54    NNN B BL R MA
55    NNN BB BL R MA
56    NNN BL BL R MA
57    NNN M R MA
58    NNN B M R MA
59    NNN BB M R MA
60    NNN BL M R MA
61    NNN B BL M R MA
62    NNN BB BL M R MA
63    NNN BL BL M R MA
64    NNN MM R MA
65    NNN RR MA
66    NNN MA MA
67    J
68    JJJ
69    Z
```

## LOS algorithm (fixed-point ray tracing)

directional bitmasks:
- FULL_MASK  = 0x20000
- EAST_MASK  = 0x01000
- WEST_MASK  = 0x10000
- NORTH_MASK = 0x00400
- SOUTH_MASK = 0x04000

ray tracing uses fixed-point Q16 arithmetic. start at tile center (0x8000).
x-dominant or y-dominant based on |dx| vs |dy|. step along major axis,
check directional masks at each tile crossing. diagonal crossings check both axes.

for NPC LOS: flip perspective — trace from target's closest point back to NPC.
for melee range (r=1): adjacency check only, no ray tracing.

## mob AI

movement: 1 tile per tick toward aggro target. diagonal movement if both axes are clear.
if target is under mob: random cardinal walk. if diagonal would collide with target: strip Y, move X only.

attack gating: must have LOS, not be under target, attack delay <= 0.
melee switchover: 50% chance to switch to melee when close (for mobs with canMeleeIfClose).

blob prayer reading: at ticksTillNextAttack==3, if player has protect missiles -> blob switches to magic.
if player has protect magic -> blob switches to ranged.

meleer burrow: 12-tick delay after burrow animation, prevents movement during that time.
mager respawn: 8-tick delay after respawn animation.

jad timing: 3 ticks after attack animation, then 8-tick continuous cycle.
zuk timing: 10-tick normal phase, 7-tick final phase (after ticksSinceFinalPhase > 3).

## collision

AABB test with bottom-left origin (y is TOP of footprint).
size-N mob at (x,y) occupies tiles (x..x+s-1, y-s+1..y).

pillars are 3x3 entities that block LOS with FULL_MASK but may or may not block movement
depending on collision type.

## arena layout (from joseph's scape prototype)

pillar positions: south (21,37), west (11,23), north (28,21) — all 3x3.
player spawn: (28,17).
test NPCs: (12,19) size 4, (26,42) size 4, (14,25) size 3.

## key reference files

- `.refs/osrs-sdk/src/sdk/LineOfSight.ts` — LOS ray tracing
- `.refs/osrs-sdk/src/sdk/Mob.ts` — NPC movement, aggro, attack logic
- `.refs/osrs-sdk/src/sdk/Collision.ts` — AABB collision, LOS entity queries
- `.refs/osrs-sdk/src/sdk/testing/TestNpc.ts` — Jal-Xil exact stats
- `~/Projects/storm/storm/reference-plugins/inferno/` — wave mappings, NPC types, plugin logic
