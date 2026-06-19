# OSRS Line of Sight + Pathfinding — canonical wiki spec (ground truth)

User-provided OSRS wiki text. This is the AUTHORITATIVE spec for movement/LoS in all OSRS encounters
(colosseum, inferno, zulrah, pvp). osrs-sdk (refs/osrs-sdk/src/sdk/{Mob,Pathing}.ts) is the reference
implementation of this. Any encounter's NPC/player movement + LoS must match this exactly.

## Line of Sight
- LoS = a direct, unobstructed straight path between two tiles, computed via the pathfinding system.
- Visible if within 15 tiles of the observer's tile.
- Outside LoS if blocked by scenery (wall/tree), ANOTHER CHARACTER, or AROUND A CORNER.
- Click a target out of LoS -> the character RUNS toward the target until it reaches a tile with valid LoS.
  If it cannot move / cannot find a path to a LoS tile -> action cancelled, "I can't reach that!".
- Melee: target must be ADJACENT and reachable. Ranged/Magic: target within LoS AND attack range.
- Safespotting = attack from outside the opponent's LoS or attack range.
- Multi-tile NPC: the CLOSEST tile to the player must be within LoS + range to attack without moving.

## Pathfinding — two modes
### Player pathing = BFS
- Reachability: SW tile of an object destination must be within a 101x101 area around start.
- Requested tiles: a clicked tile -> that tile; a clicked NPC/object/player -> ALL tiles within melee range of it.
- BFS from start. Neighbour check order: **West, East, South, North, SW, SE, NW, NE**.
  - Path distance stored as prev+1; previous tile stored. 128x128 grid (start at NE corner of centre).
  - Obstructions block some tiles as neighbours. Terminates when all reachable tiles seen OR a requested tile found.
- If a requested tile is found -> it's the target. Else a 2nd search over a 21x21 grid centred at the SW tile of
  the clicked entity/tile (western then southern priority); target = first tile with an existing path that has
  path length < 100, shortest path distance, and target closest in Euclidean distance to the nearest requested tile.
  Else no target.
- Post-process: extract first 25 corners ("checkpoint tiles"); last checkpoint = destination (can "stop early"
  if >25 checkpoints needed).
- Priorities: never leave the 128x128 area; fewest total tiles travelled; prioritise cardinal lines + long straight lines.

### NPC pathing (and player between checkpoints) = "follow mode" — NAIVE
- "Naively paths DIAGONALLY to the end tile and then STRAIGHT if there are no diagonals left."
- i.e. step diagonally toward target while both x and y differ AND the diagonal is legal, then step straight on
  the remaining axis. This is the dumb greedy movement (no BFS) — NPCs wedge on obstacles + are safespottable.

## Blockage types (critical for the diagonal corner-cut bug)
- Non-blocking (free) tile: movement allowed from all directions.
- Full-blocking tile: disallows movement from all directions (occupied by an object).
- Walls: occupy a tile BORDER; block movement across that one cardinal edge. You can still stand on the tile.
- **Pillars: occupy a tile CORNER; block movement across that DIAGONAL direction** (like walls do for cardinals).
  Often only on some corners, so SW<->NE walk-through is common. You can stand on a tile with a pillar corner.
- DIAGONAL MOVEMENT RULE (implied): a diagonal step is illegal if it would cut the corner of a blocking tile/pillar
  — i.e. BOTH orthogonal leading tiles must be clear. If the diagonal is blocked, follow-mode falls back to the
  straight (cardinal) step. THIS is the colosseum shaman bug: it took the diagonal past the 3x3 pillar's corner
  instead of falling back to the vertical step.

## Path recalculation
- If no target tile found and target is object/tile/item -> pathing permanently cancelled.
- If target is an NPC/player -> retry pathfinding every tick until a valid target tile is found.
- When only one checkpoint remains and the target moved -> recalc every tick from the current tile.
  (Players traverse 2 checkpoints/tick, so they can run past a target that moved toward them.)
