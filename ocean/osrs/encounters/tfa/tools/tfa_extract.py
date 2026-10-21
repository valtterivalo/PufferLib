"""Regenerate every TFA replay fixture (fights, events, rooms, stats) from the gameplay recordings."""


import json
import subprocess
import sys
from pathlib import Path

BOSS_NPC_MIN, BOSS_NPC_MAX = 16640, 16690
BOSS_NPC_EXTRA = frozenset({16761, 16762})
WANTED = {
    "GAME_TICK_OBSERVED",
    "ANIMATION_OBSERVED",
    "PROJECTILE_MOVED_OBSERVED",
    "HITSPLAT_APPLIED_OBSERVED",
    "NPC_SPAWNED_OBSERVED",
    "NPC_DESPAWNED_OBSERVED",
    "NPC_CHANGED_OBSERVED",
    "GRAPHICS_OBJECT_CREATED_OBSERVED",
    "OVERHEAD_TEXT_OBSERVED",
    "CHAT_MESSAGE_OBSERVED",
    "TILE_OBJECT_SPAWNED_OBSERVED",
    "TILE_OBJECT_DESPAWNED_OBSERVED",
    "SCENE_COLLISION_OBSERVED",
    "INTERACTING_OBSERVED",
    "ACTOR_STATE_OBSERVED",
    "ACTOR_SPOTANIMS_CHANGED_OBSERVED",
    "NPC_OVERHEAD_ICON_CHANGED_OBSERVED",
}
NEEDLES = [f'"eventKind":"{k}"'.encode() for k in WANTED]


def is_boss_npc(npc_id):
    return npc_id is not None and (BOSS_NPC_MIN <= npc_id <= BOSS_NPC_MAX or npc_id in BOSS_NPC_EXTRA)


def actor(a):
    if a["kind"] == "NONE":
        return None
    if a["kind"] == "PLAYER":
        return ["P", a["cleanName"], a["worldPoint"]["x"], a["worldPoint"]["y"]]
    return ["N", a["npcId"], a["npcIndex"], a["worldPoint"]["x"], a["worldPoint"]["y"]]


def reduce_record(rec):
    kind = rec["eventKind"]
    p = rec["payload"]
    t = rec["lastObservedGameTick"]
    s = rec["sequence"]
    if kind == "GAME_TICK_OBSERVED":
        if "player" not in p:
            return None
        pl = p["player"]
        so = p["sceneOrigin"]
        npcs = [
            [n["npcIndex"], n["npcId"], n["worldPoint"]["x"], n["worldPoint"]["y"], n["healthRatio"], n["healthScale"], n["dead"], n["animation"], n["sizeTiles"], n["orientation"]]
            for n in p["npcs"]
            if is_boss_npc(n["npcId"])
        ]
        return ["T", t, s, pl["cleanName"], pl["worldPoint"]["x"], pl["worldPoint"]["y"], so["baseX"], so["baseY"], so["instance"], pl["prayers"]["active"], pl["combatResources"]["hitpoints"]["boosted"], pl["combatResources"]["prayer"]["boosted"], npcs]
    if kind == "ACTOR_STATE_OBSERVED":
        a = p["actor"]
        if a["kind"] != "PLAYER" or p["observation"] != "GAME_TICK":
            return None
        wp = p["state"]["worldPoint"]
        return ["Q", t, s, a["cleanName"], wp["x"], wp["y"], p["state"]["overheadIcon"]]
    if kind == "ACTOR_SPOTANIMS_CHANGED_OBSERVED":
        if p["kind"] == "NPC" and not is_boss_npc(p["npcId"]):
            return None
        return ["SP", t, s, actor(p), [[x["id"], x["startCycle"] - rec["lastObservedClientCycle"]] for x in p["spotAnims"]]]
    if kind == "NPC_OVERHEAD_ICON_CHANGED_OBSERVED":
        if not is_boss_npc(p["npcId"]):
            return None
        return ["OH", t, s, actor(p), p["overheadSpriteIds"]]
    if kind == "ANIMATION_OBSERVED":
        a = p["actor"]
        if a["kind"] != "NPC" or not is_boss_npc(a["npcId"]) or p["animation"] == -1:
            return None
        return ["A", t, s, a["npcId"], a["npcIndex"], a["worldPoint"]["x"], a["worldPoint"]["y"], p["animation"]]
    if kind == "PROJECTILE_MOVED_OBSERVED":
        if p["observationKind"] != "FIRST_OBSERVED":
            return None
        return ["J", t, s, p["projectileId"], p["sourceWorldPoint"]["x"], p["sourceWorldPoint"]["y"], actor(p["sourceActor"]), p["targetWorldPoint"]["x"], p["targetWorldPoint"]["y"], actor(p["targetActor"]), p["startCycle"], p["endCycle"]]
    if kind == "HITSPLAT_APPLIED_OBSERVED":
        r = p["recipient"]
        return ["H", t, s, actor(r), p["amount"], p["hitsplatType"]]
    if kind in ("NPC_SPAWNED_OBSERVED", "NPC_DESPAWNED_OBSERVED", "NPC_CHANGED_OBSERVED"):
        if not is_boss_npc(p["npcId"]):
            return None
        tag = {"NPC_SPAWNED_OBSERVED": "S", "NPC_DESPAWNED_OBSERVED": "D", "NPC_CHANGED_OBSERVED": "C"}[kind]
        return [tag, t, s, p["npcId"], p["npcIndex"], p["worldPoint"]["x"], p["worldPoint"]["y"], p["healthRatio"], p["healthScale"], p["dead"]]
    if kind == "GRAPHICS_OBJECT_CREATED_OBSERVED":
        lp = p["localPoint"]
        return ["G", t, s, p["graphicsId"], p["worldPoint"]["x"], p["worldPoint"]["y"], p["startCycle"] - rec["lastObservedClientCycle"], lp["x"] % 128, lp["y"] % 128, p["z"]]
    if kind == "OVERHEAD_TEXT_OBSERVED":
        if p["overheadText"]["text"] is None:
            return None
        return ["O", t, s, actor(p["actor"]), p["overheadText"]["text"]]
    if kind == "CHAT_MESSAGE_OBSERVED":
        return ["M", t, s, p["type"], p["message"]]
    if kind in ("TILE_OBJECT_SPAWNED_OBSERVED", "TILE_OBJECT_DESPAWNED_OBSERVED"):
        w = p["eventTileWorldPoint"]
        tag = "B" if kind == "TILE_OBJECT_SPAWNED_OBSERVED" else "E"
        ok = p["objectKind"]
        if ok == "GAME_OBJECT":
            geo = [p["sizeX"], p["sizeY"], p["orientation"]]
        elif ok == "WALL_OBJECT":
            geo = [1, 1, p["orientationA"]]
        elif ok in ("GROUND_OBJECT", "DECORATIVE_OBJECT"):
            geo = [1, 1, 0]
        else:
            raise ValueError(ok)
        return [tag, t, s, p["objectId"], w["x"], w["y"], *geo, {"GAME_OBJECT": "G", "WALL_OBJECT": "W", "GROUND_OBJECT": "F", "DECORATIVE_OBJECT": "D"}[ok], p["config"]]
    if kind == "SCENE_COLLISION_OBSERVED":
        g = p["grid"]
        assert g["encoding"] == "SIGNED_INT32_BIG_ENDIAN_BASE64_V1" and g["width"] == 104 and g["height"] == 104
        return ["X", t, s, p["baseX"], p["baseY"], p["instance"], g["data"]]
    if kind == "INTERACTING_OBSERVED":
        src = p["source"]
        if src["kind"] != "PLAYER":
            return None
        return ["I", t, s, actor(src), actor(p["target"])]
    raise ValueError(kind)


def reduce_file(src, dst):
    proc = subprocess.Popen(["zstd", "-dc", str(src)], stdout=subprocess.PIPE)
    tmp = Path(str(dst) + ".part")
    with open(tmp, "w") as out:
        for line in proc.stdout:
            if not any(n in line[:320] for n in NEEDLES):
                continue
            row = reduce_record(json.loads(line))
            if row is not None:
                out.write(json.dumps(row, separators=(",", ":")) + "\n")
    assert proc.wait() == 0, f"zstd failed on {src}"
    tmp.rename(dst)

import base64
import bisect
import collections
import re
import statistics
import struct

REC_DIR = Path.home() / ".light2/gameplay-recordings/local-debug"
CACHE = Path("/private/tmp/claude-501/-Users-valtterivalo-Projects-storm-light/47dd9744-cdbc-47b5-baaa-2af116ea270f/scratchpad/fixtures/cache")
OUT = Path(__file__).resolve().parent.parent / "fixtures"
TFA_NOTES = Path.home() / ".light2/tfa"
REGION = 64
TFA_SPOTANIM_MIN, TFA_SPOTANIM_MAX = 4030, 4450
SCENE = 104
MOVE_BLOCK = 0x240100
MOVE_MASK = 0x2401FF
UNLOADED = 0xFFFFFF
VOID_CHUNK = 0x40000000
STATIC_BULK_OBJECTS = 200
FIGHT_GAP_TICKS = 20
HIT_WINDOW = 8
GFX_WINDOW = 10
GFX_RADIUS = 1
POST_TICKS = 5
CHAT_PAD = 40

GROUPS = {
    "guardian": frozenset(range(16657, 16665)),
    "bork": frozenset({16640, 16641, 16642}),
    "ezekiel": frozenset(range(16652, 16657)),
    "dreadnoth": frozenset({16665, 16666}),
    "moia": frozenset({16667, 16670}),
    "xarth": frozenset(range(16684, 16689)),
    "ocellus": frozenset(range(16672, 16682)),
}
BOSS_TITLE = {
    "guardian": "Archive Guardian",
    "bork": "Bork",
    "ezekiel": "Ezekiel",
    "dreadnoth": "Dreadnoth the Jellied",
    "moia": "Moia",
    "xarth": "Xarth",
    "ocellus": "Ocellus",
}
START_ANIMS = {
    "guardian": {14750, 14751, 14752, 14753, 14754},
    "bork": {14542, 14550, 14554, 14555},
    "ezekiel": {14685, 14686, 14687},
    "dreadnoth": {14777, 14779, 14780, 14782},
    "moia": {14785, 14788},
    "xarth": {14983, 14984, 14985, 14989, 14992},
    "ocellus": {14806, 14842, 14846, 14848, 14851},
}
DEATH_ANIMS = {"guardian": {14749}, "bork": set(), "ezekiel": {14697}, "dreadnoth": set(), "moia": set(), "xarth": {14995}, "ocellus": set()}
ATTACK_ANIMS = {
    "guardian": {14745, 14748, 14750, 14751, 14752, 14753, 14754},
    "bork": {14542, 14545, 14550, 14551, 14554, 14555},
    "ezekiel": {14685, 14686, 14687, 14688, 14691, 14693, 14694},
    "dreadnoth": {14777, 14779, 14780, 14782},
    "moia": {14785, 14788},
    "xarth": {14983, 14984, 14985, 14989, 14992},
    "ocellus": {14806, 14807, 14809, 14810, 14811, 14812, 14813, 14814, 14837, 14842, 14846, 14848, 14851, 14857, 14858, 14861, 14869, 14871, 14878, 14881},
}
PROT_NAMES = {"PROTECT_FROM_MELEE": "PROT_MELEE", "PROTECT_FROM_MISSILES": "PROT_RANGE", "PROTECT_FROM_MAGIC": "PROT_MAGIC"}
OVERHEAD_PROT = {"MELEE": "PROT_MELEE", "RANGED": "PROT_RANGE", "MAGIC": "PROT_MAGIC", "NONE": "-", "RETRIBUTION": "-", "SMITE": "-", "REDEMPTION": "-"}
EVENT_HEADER = "fight_id\tt\tkind\tactor\tid\tx\ty\ta\tb\tc"


def load_gameval():
    table = collections.defaultdict(dict)
    for line in (TFA_NOTES / "notes/tfa-gameval.tsv").read_text().splitlines()[1:]:
        kind, ident, const, *name = line.split("\t")
        table[kind][int(ident)] = const
    return table


def load_rule_ids():
    ids = collections.defaultdict(set)
    for line in (TFA_NOTES / "rules.txt").read_text().splitlines():
        m = re.match(r"(projectile|graphic|object|anim|npc) (\d+)", line)
        if m:
            ids[m.group(1)].add(int(m.group(2)))
    return ids


def recordings():
    return sorted(p for p in REC_DIR.glob("session_2*T*.jsonl.zst") if p.name[8:16] >= TFA_RELEASE_DAY)


TFA_RELEASE_DAY = "20261020"


def cached_rows(src):
    dst = CACHE / (src.name[: -len(".jsonl.zst")] + ".jsonl")
    if not dst.exists() or dst.stat().st_mtime < src.stat().st_mtime:
        CACHE.mkdir(parents=True, exist_ok=True)
        reduce_file(src, dst)
    return [json.loads(line) for line in open(dst)]


def short_name(src):
    return src.name.split(".jsonl")[0].split("Z_")[0] + "Z"


def prayer_label(active):
    return ",".join(sorted(PROT_NAMES.get(p, p) for p in active)) or "-"


def protection(active):
    prot = sorted(PROT_NAMES[p] for p in active if p in PROT_NAMES)
    return "+".join(prot) or "NONE"


def actor_name(a):
    return a[1].replace("\xa0", " ")


class Fight:
    def __init__(self, boss, rec, first, last):
        self.boss, self.rec, self.first, self.last = boss, rec, first, last
        self.events = []
        self.fight_id = None


def segment_fights(rows):
    ticks = [r for r in rows if r[0] == "T"]
    fights = []
    for boss, ids in GROUPS.items():
        cur = None
        for r in ticks:
            if not any(n[1] in ids for n in r[12]):
                continue
            if cur is None or r[1] - cur[1] > FIGHT_GAP_TICKS:
                cur = [r[1], r[1], r]
                fights.append((boss, cur))
            cur[1] = r[1]
    return [(b, c[0], c[1], c[2]) for b, c in fights]


def origin_of(first_tick_row, ids):
    npc = next(n for n in first_tick_row[12] if n[1] in ids)
    return npc[2] // REGION * REGION, npc[3] // REGION * REGION


def decode_collision(row):
    return struct.unpack(">%di" % (SCENE * SCENE), base64.b64decode(row[6]))


def region_collision(rows, origin, a, b):
    ox, oy = origin
    known = {}
    for r in rows:
        if r[0] != "X" or not a <= r[1] <= b:
            continue
        vals = decode_collision(r)
        bx, by = r[3], r[4]
        for i in range(REGION):
            sx = ox + i - bx
            if not 0 <= sx < SCENE:
                continue
            for j in range(REGION):
                sy = oy + j - by
                if not 0 <= sy < SCENE or (i, j) in known:
                    continue
                v = vals[sx * SCENE + sy]
                if (v & UNLOADED) != UNLOADED:
                    known[(i, j)] = v
    return known


def build_fight(rows, local, boss, first, last, first_row, gv, rule_ids):
    ids = GROUPS[boss]
    ox, oy = origin_of(first_row, ids)
    f = Fight(boss, None, first, last)
    f.origin = (ox, oy)
    entry = first
    for r in reversed([r for r in rows if r[0] == "T" and r[1] < first]):
        if not (0 <= r[4] - ox < REGION and 0 <= r[5] - oy < REGION) or r[1] < entry - 1:
            break
        entry = r[1]
    a_tick, b_tick = entry, last + POST_TICKS
    window = [r for r in rows if a_tick - CHAT_PAD <= r[1] <= b_tick + CHAT_PAD]
    title = BOSS_TITLE[boss]
    chat = [(r[1], r[4]) for r in window if r[0] == "M" and r[3] == "GAMEMESSAGE"]
    f.challenge_tick = next((t for t, m in chat if m == f"Challenge started: {title}." and first - CHAT_PAD <= t <= last), None)
    complete = next(((t, m) for t, m in chat if m.startswith(f"Challenge complete: {title}.") and first <= t <= last + CHAT_PAD), None)
    fail = next((t for t, m in chat if m == "You failed to survive the Fractured Archive." and first <= t <= last + CHAT_PAD), None)
    f.p0_deaths = [t for t, m in chat if m.startswith("You have died.") and first <= t <= last + CHAT_PAD]
    f.other_deaths = [t for t, m in chat if re.match(r".+ has died\. Total deaths", m) and not m.startswith("You have") and first <= t <= last + CHAT_PAD]
    f.complete_text = complete[1] if complete else None
    start_anim = next((r for r in window if r[0] == "A" and r[3] in ids and r[7] in START_ANIMS[boss] and first <= r[1] <= last), None)
    if start_anim:
        f.start, f.start_kind = start_anim[1], "attack_anim"
    elif f.challenge_tick is not None:
        f.start, f.start_kind = f.challenge_tick, "challenge_chat"
    else:
        f.start, f.start_kind = first, "first_seen"
    death = next((r for r in window if r[0] == "A" and r[3] in ids and r[7] in DEATH_ANIMS[boss] and first <= r[1] <= last), None)
    dead_id = next((r for r in window if r[0] == "T" and first <= r[1] <= last and any(n[1] == 16658 for n in r[12])), None) if boss == "guardian" else None
    if complete:
        f.end, f.outcome, f.end_kind = complete[0], "kill", "challenge_complete_chat"
    elif fail is not None:
        f.end, f.outcome, f.end_kind = fail, "wipe", "failed_to_survive_chat"
    elif death:
        f.end, f.outcome, f.end_kind = death[1], "kill", "boss_death_anim"
    elif dead_id:
        f.end, f.outcome, f.end_kind = dead_id[1], "kill", "dead_npc_id_16658"
    elif f.start_kind == "first_seen":
        f.end, f.outcome, f.end_kind = last, "no_combat", "last_boss_tick"
    else:
        f.end, f.outcome, f.end_kind = last, "unknown", "last_boss_tick"
    vis_a, vis_b = entry, max(last, f.end) + POST_TICKS
    f.entry = entry
    f.window = (vis_a, vis_b)

    def rel(x, y):
        return x - ox, y - oy

    room_ticks = {r[1] for r in rows if r[0] == "T" and (first <= r[1] <= last or (vis_a <= r[1] <= vis_b and 0 <= r[4] - ox < REGION and 0 <= r[5] - oy < REGION))}
    wrows = [r for r in rows if vis_a <= r[1] <= vis_b and (r[0] == "M" or r[1] in room_ticks)]
    players = collections.OrderedDict()
    players[local.replace("\xa0", " ")] = "P0"

    def player_label(name):
        if name not in players:
            players[name] = f"P{len(players)}"
        return players[name]

    def note_actor(a):
        if a and a[0] == "P":
            player_label(actor_name(a))

    for r in wrows:
        k = r[0]
        if k == "T":
            assert r[3] == local
        elif k == "H":
            note_actor(r[3])
        elif k == "J":
            note_actor(r[6])
            note_actor(r[9])
        elif k == "I":
            note_actor(r[3])
            note_actor(r[4])
        elif k == "Q" and r[3] != local:
            player_label(r[3].replace("\xa0", " "))
    f.players = players
    ords = collections.defaultdict(dict)

    def npc_label(nid, idx):
        d = ords[nid]
        if idx not in d:
            d[idx] = len(d) + 1
        return f"{nid}#{d[idx]}"

    def label(a):
        if a is None:
            return "-"
        if a[0] == "P":
            return player_label(actor_name(a))
        return npc_label(a[1], a[2])

    ev = f.events
    cur_id = {}
    last_pray = None
    last_mate_pray = {}
    last_hp = None
    last_npc = {}
    f.p0_pos = {}
    f.p0_pray = {}
    f.p0_hp = {}
    f.p0_out_of_region = 0
    f.npc_out_of_region = 0
    seen_obj_gv = set(gv["ObjectID1"])
    seen_gfx = set(gv["SpotanimID"]) | rule_ids["graphic"] | rule_ids["projectile"] | set(range(TFA_SPOTANIM_MIN, TFA_SPOTANIM_MAX + 1))
    f.dropped_gfx = collections.Counter()
    f.dropped_oob = collections.Counter()
    bulk = collections.Counter(r[1] for r in wrows if r[0] == "B")
    static_objects = []
    for r in wrows:
        k, t, s = r[0], r[1], r[2]

        def add(kind, actor, ident, x, y, a="-", b="-", c="-", idx=None, nid=None):
            if kind != "POS" and isinstance(x, int) and not (0 <= x < REGION and 0 <= y < REGION):
                f.dropped_oob[kind] += 1
                return
            ev.append({"tick": t, "seq": s, "kind": kind, "actor": actor, "id": ident, "x": x, "y": y, "a": a, "b": b, "c": c, "idx": idx, "nid": nid})

        if k == "T":
            px, py = rel(r[4], r[5])
            f.p0_pray[t] = frozenset(r[9])
            f.p0_hp[t] = (r[10], r[11])
            if 0 <= px < REGION and 0 <= py < REGION:
                f.p0_pos[t] = (px, py)
                add("POS", "P0", 0, px, py)
            else:
                f.p0_out_of_region += 1
                add("POS", "P0", 0, "-", "-", a="out_of_region")
            pr = tuple(sorted(r[9]))
            if pr != last_pray:
                add("PRAY", "P0", 0, "-", "-", a=prayer_label(r[9]))
                last_pray = pr
            if r[10] != last_hp:
                add("HP", "P0", r[10], "-", "-", a=r[11])
                last_hp = r[10]
            for n in r[12]:
                idx, nid, x, y, hr, hs = n[0], n[1], n[2], n[3], n[4], n[5]
                cur_id[idx] = nid
                lab = npc_label(nid, idx)
                x, y = rel(x, y)
                if not (0 <= x < REGION and 0 <= y < REGION):
                    f.npc_out_of_region += 1
                if last_npc.get(idx, (None,))[0] != (x, y, nid):
                    add("NPOS", lab, nid, x, y, idx=idx, nid=nid)
                if last_npc.get(idx, (None, None))[1] != (hr, hs) and not (hr == -1 and idx not in last_npc):
                    add("NHP", lab, hr, "-", "-", a=hs, idx=idx, nid=nid)
                if last_npc.get(idx, (None, None, None))[2] != n[9]:
                    add("NORI", lab, nid, x, y, a=n[9], idx=idx, nid=nid)
                last_npc[idx] = ((x, y, nid), (hr, hs), n[9])
        elif k == "Q":
            if r[3] == local:
                continue
            lab = player_label(r[3].replace("\xa0", " "))
            x, y = rel(r[4], r[5])
            if 0 <= x < REGION and 0 <= y < REGION:
                add("POS", lab, 0, x, y)
            pr = OVERHEAD_PROT[r[6]]
            if last_mate_pray.get(lab) != pr:
                add("PRAY", lab, 0, "-", "-", a=pr)
                last_mate_pray[lab] = pr
        elif k == "SP":
            add("SPOT", label(r[3]), "+".join(str(x[0]) for x in r[4]) or "-", "-", "-", a="+".join(str(x[1]) for x in r[4]) or "-")
        elif k == "OH":
            add("OVH", label(r[3]), "+".join(str(x) for x in r[4]) or "-", "-", "-")
        elif k == "A":
            x, y = rel(r[5], r[6])
            add("ANIM", npc_label(r[3], r[4]), r[7], x, y, idx=r[4], nid=r[3])
        elif k == "J":
            src = r[6]
            tgt = r[9]
            if r[3] not in seen_gfx and not (src and src[0] == "N"):
                f.dropped_gfx[("PROJ", r[3])] += 1
                continue
            flight = round((r[11] - r[10]) / 30 * 2) / 2
            sx, sy = rel(r[4], r[5])
            tx, ty = rel(r[7], r[8])
            if not (0 <= sx < REGION and 0 <= sy < REGION) and not (0 <= tx < REGION and 0 <= ty < REGION):
                f.dropped_oob["PROJ"] += 1
                continue
            add("PROJ", label(src), r[3], sx, sy, a=label(tgt), b=f"{flight:g}", c=f"{tx},{ty}", idx=src[2] if src and src[0] == "N" else None, nid=src[1] if src and src[0] == "N" else None)
        elif k == "H":
            rcp = r[3]
            if rcp[0] == "N" and not (16640 <= rcp[1] <= 16690):
                continue
            x, y = (rel(rcp[2], rcp[3]) if rcp[0] == "P" else rel(rcp[3], rcp[4]))
            add("HIT", label(rcp), r[4], x, y, a=r[5], idx=rcp[2] if rcp[0] == "N" else None, nid=rcp[1] if rcp[0] == "N" else None)
        elif k in ("S", "D"):
            x, y = rel(r[5], r[6])
            add("NPC+" if k == "S" else "NPC-", npc_label(r[3], r[4]), r[3], x, y, a=r[7], b=r[8], idx=r[4], nid=r[3])
            if k == "S":
                cur_id[r[4]] = r[3]
        elif k == "C":
            x, y = rel(r[5], r[6])
            add("NPCID", npc_label(r[3], r[4]), r[3], x, y, a=cur_id.get(r[4], "-"), idx=r[4], nid=r[3])
            cur_id[r[4]] = r[3]
        elif k == "G":
            if r[3] not in seen_gfx:
                f.dropped_gfx[("GFX", r[3])] += 1
                continue
            x, y = rel(r[4], r[5])
            add("GFX", "-", r[3], x, y, a=r[6], b=f"{r[7]},{r[8]}", c=r[9])
        elif k == "O":
            if r[3][0] == "N" and 16640 <= r[3][1] <= 16690:
                x, y = rel(r[3][3], r[3][4])
                add("OV", npc_label(r[3][1], r[3][2]), 0, x, y, a=r[4], idx=r[3][2], nid=r[3][1])
        elif k == "M":
            if r[3] == "GAMEMESSAGE":
                add("CHAT", "-", 0, "-", "-", a=r[4])
        elif k in ("B", "E"):
            if k == "B" and bulk[t] >= STATIC_BULK_OBJECTS:
                if r[3] in seen_obj_gv:
                    if 0 <= r[4] - ox < REGION and 0 <= r[5] - oy < REGION:
                        static_objects.append((r[3], r[4] - ox, r[5] - oy, r[6], r[7], r[8], r[9]))
                continue
            if bulk[t] >= STATIC_BULK_OBJECTS:
                continue
            if r[3] in seen_obj_gv:
                x, y = rel(r[4], r[5])
                add("OBJ+" if k == "B" else "OBJ-", "-", r[3], x, y, a=f"{r[6]}x{r[7]}", b=r[8], c=f"{r[9]}{r[10] & 31}r{r[10] >> 6 & 3}")
    f.pray_ticks = sorted(f.p0_pray)
    f.static_objects = collections.Counter(static_objects)
    f.static_npcs = sorted({(n[1], n[2] - ox, n[3] - oy) for n in first_row[12]})
    f.collision = region_collision(rows, (ox, oy), vis_a, vis_b)
    f.party_size = len(players)
    ev.sort(key=lambda e: (e["tick"], e["seq"]))
    return f


def anonymize_text(text, name_map, all_names):
    text = text.replace("\xa0", " ")
    for name in sorted(all_names | set(name_map), key=len, reverse=True):
        text = text.replace(name.replace("\xa0", " "), name_map.get(name.replace("\xa0", " "), "PX"))
    m = re.match(r"^(.+?)( has (?:died\. Total deaths|proceeded to|returned to|entered)\b.*)$", text)
    if m and not re.fullmatch(r"P\d+|PX", m.group(1)):
        text = "PX" + m.group(2)
    return text


def write_events(boss, fights, all_names, gv):
    path = OUT / f"tfa_{boss}_events.tsv"
    with open(path, "w") as out:
        out.write(EVENT_HEADER + "\n")
        for f in fights:
            name_map = {n: p for n, p in f.players.items()}
            for e in f.events:
                a = e["a"]
                if e["kind"] in ("CHAT", "OV"):
                    a = anonymize_text(a, name_map, all_names).replace("\t", " ").replace("\n", " ")
                out.write(f"{f.fight_id}\t{e['tick'] - f.start}\t{e['kind']}\t{e['actor']}\t{e['id']}\t{e['x']}\t{e['y']}\t{a}\t{e['b']}\t{e['c']}\n")
    return path


def hist(xs):
    c = collections.Counter(xs)
    return " ".join(f"{k}:{v}" for k, v in sorted(c.items(), key=lambda kv: (isinstance(kv[0], str), kv[0]))) or "-"


def median(xs):
    return f"{statistics.median(xs):g}" if xs else "-"


def cheb(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def pray_at(f, tick):
    ticks = f.pray_ticks
    return f.p0_pray[ticks[bisect.bisect_right(ticks, tick) - 1]]


def hit_after(p0_hits, tick):
    for ht, amt in p0_hits:
        if tick <= ht <= tick + HIT_WINDOW:
            return ht, amt
        if ht > tick + HIT_WINDOW:
            return None
    return None


def intervals(fights, boss, split_by_npc_id=False):
    gaps = collections.defaultdict(list)
    for f in fights:
        by_idx = collections.defaultdict(list)
        for e in f.events:
            if e["kind"] == "ANIM" and e["id"] in ATTACK_ANIMS[boss]:
                by_idx[(e["idx"], e["nid"]) if split_by_npc_id else e["idx"]].append(e)
        for lst in by_idx.values():
            for a, b in zip(lst, lst[1:]):
                gaps[a["id"]].append(b["tick"] - a["tick"])
    return gaps


def stats_md(boss, fights, gv):
    names = gv["AnimationID"]
    lines = [f"# TFA {boss} stats", "", f"Generated by tools/tfa_extract.py from {len(fights)} fights. Tick = recorder lastObservedGameTick. Hit association windows: {HIT_WINDOW} ticks after the anim or landing, first P0 hit wins (same method as light/tools/tfa_digest.py).", ""]
    gaps = intervals(fights, boss)
    lines += ["## Attack anim to next attack-class anim by the same NPC index (ticks)", "", "| anim | name | n | histogram |", "|---|---|---|---|"]
    for a in sorted(gaps):
        lines.append(f"| {a} | {names.get(a, '')} | {len(gaps[a])} | {hist(gaps[a])} |")
    p0_hits_by_fight = {}
    for f in fights:
        p0_hits_by_fight[f.fight_id] = [(e["tick"], e["id"]) for e in f.events if e["kind"] == "HIT" and e["actor"] == "P0"]
    lines += ["", "## Attack anim to first P0 hit within the window", "", "Delay is hit tick minus anim tick. Amounts are split by P0 protection prayer at the hit tick. 'n_no_hit' counts anims with no P0 hit in the window.", "", "| anim | name | n_anims | n_no_hit | first-hit delay hist | all P0 hits by offset 0..8 | prayer | n | zero | min | med | max |", "|---|---|---|---|---|---|---|---|---|---|---|---|"]
    delay = collections.defaultdict(list)
    all_off = collections.defaultdict(list)
    per_prayer = collections.defaultdict(lambda: collections.defaultdict(list))
    nohit = collections.Counter()
    count = collections.Counter()
    for f in fights:
        hits = p0_hits_by_fight[f.fight_id]
        for e in f.events:
            if e["kind"] == "ANIM" and e["id"] in ATTACK_ANIMS[boss] and e["nid"] in GROUPS[boss]:
                count[e["id"]] += 1
                all_off[e["id"]] += [ht - e["tick"] for ht, _ in hits if 0 <= ht - e["tick"] <= HIT_WINDOW]
                h = hit_after(hits, e["tick"])
                if h is None:
                    nohit[e["id"]] += 1
                    continue
                delay[e["id"]].append(h[0] - e["tick"])
                pr = protection(pray_at(f, h[0]))
                per_prayer[e["id"]][pr].append(h[1])
    for a in sorted(count):
        if not delay[a]:
            lines.append(f"| {a} | {names.get(a, '')} | {count[a]} | {nohit[a]} | - | {hist(all_off[a])} | - | 0 | - | - | - | - |")
            continue
        first = True
        for pr in sorted(per_prayer[a]):
            amts = per_prayer[a][pr]
            head = f"| {a} | {names.get(a, '')} | {count[a]} | {nohit[a]} | {hist(delay[a])} | {hist(all_off[a])} |" if first else "| | | | | | |"
            first = False
            lines.append(f"{head} {pr} | {len(amts)} | {sum(1 for x in amts if x == 0)} | {min(amts)} | {median(amts)} | {max(amts)} |")
    shots = collections.defaultdict(list)
    for f in fights:
        for e in f.events:
            if e["kind"] == "PROJ":
                shots[e["id"]].append((f, e))
    lines += ["", "## Projectiles", "", "Flight = (endCycle - startCycle)/30 rounded to 0.5. Landing = first-observed projectile tick + floor(flight) (flight 2.5 lands +2, 3.5 lands +3, 4.5 lands +4). Hit delay = first P0 hit in [landing, landing+window] minus landing, only for projectiles whose target is P0.", "", "| proj | gameval | n | sources | flight hist | n_to_P0 | hit-delay from landing | prayer | n | zero | min | med | max |", "|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    spot = gv["SpotanimID"]
    for pid in sorted(shots):
        group = shots[pid]
        srcs = collections.Counter(e["actor"].split("#")[0] for _, e in group)
        flights = [float(e["b"]) for _, e in group]
        delays = []
        per = collections.defaultdict(list)
        to_p0 = 0
        for f, e in group:
            if e["a"] != "P0":
                continue
            to_p0 += 1
            land = e["tick"] + int(float(e["b"]))
            h = hit_after(p0_hits_by_fight[f.fight_id], land)
            if h:
                delays.append(h[0] - land)
                pr = protection(pray_at(f, h[0]))
                per[pr].append(h[1])
        base = f"| {pid} | {spot.get(pid, '')} | {len(group)} | {dict(srcs)} | {hist(flights)} | {to_p0} | {hist(delays)} |"
        if not per:
            lines.append(f"{base} - | 0 | - | - | - | - |")
        first = True
        for pr in sorted(per):
            amts = per[pr]
            head = base if first else "| | | | | | | |"
            first = False
            lines.append(f"{head} {pr} | {len(amts)} | {sum(1 for x in amts if x == 0)} | {min(amts)} | {median(amts)} | {max(amts)} |")
    lines += ["", "## Telegraph (GFX or OBJ+) to P0 damaging hit", "", f"Offset = hit tick minus telegraph tick for P0 hits with amount > 0 inside {GFX_WINDOW} ticks while P0 is within Chebyshev {GFX_RADIUS} of the telegraph tile at the hit tick. 'p0_near_at_create' counts telegraphs created within radius of P0.", "", "| kind | id | gameval | n | p0_near_at_create | n_hits_matched | offset hist | amount min/med/max |", "|---|---|---|---|---|---|---|---|"]
    tele = collections.defaultdict(list)
    for f in fights:
        for e in f.events:
            if e["kind"] in ("GFX", "OBJ+"):
                tele[(e["kind"], e["id"])].append((f, e))
    objn = gv["ObjectID1"]
    silent = []
    for key in sorted(tele):
        offs, amts, near = [], [], 0
        for f, e in tele[key]:
            pos0 = f.p0_pos.get(e["tick"])
            if pos0 and cheb(pos0, (e["x"], e["y"])) <= GFX_RADIUS:
                near += 1
            for ht, amt in p0_hits_by_fight[f.fight_id]:
                if amt > 0 and 0 <= ht - e["tick"] <= GFX_WINDOW and ht in f.p0_pos and cheb(f.p0_pos[ht], (e["x"], e["y"])) <= GFX_RADIUS:
                    offs.append(ht - e["tick"])
                    amts.append(amt)
        gname = (spot if key[0] == "GFX" else objn).get(key[1], "")
        if not offs:
            silent.append(f"{key[0]} {key[1]} {gname} n={len(tele[key])}")
            continue
        lines.append(f"| {key[0]} | {key[1]} | {gname} | {len(tele[key])} | {near} | {len(offs)} | {hist(offs)} | {(min(amts), median(amts), max(amts)) if amts else '-'} |")
    lines += ["", "Telegraphs with no matched P0 hit: " + "; ".join(silent), "", "## All P0 hits by protection prayer (any source)", "", "| prayer | n | zero | min | med | max |", "|---|---|---|---|---|---|"]
    allp = collections.defaultdict(list)
    for f in fights:
        for ht, amt in p0_hits_by_fight[f.fight_id]:
            allp[protection(pray_at(f, ht))].append(amt)
    for pr in sorted(allp):
        a = allp[pr]
        lines.append(f"| {pr} | {len(a)} | {sum(1 for x in a if x == 0)} | {min(a)} | {median(a)} | {max(a)} |")
    return lines


def room_txt(boss, fights, gv):
    lines = [
        f"# TFA {boss} room. Anchor = SW tile of the 64x64 instance region containing the boss spawn tile (x,y here are world tile minus region origin).",
        "# MAP: 64x64 region tiles, north at top, one row per y (label at left), x=0 at left. '#' blocked (0x100|0x40000|0x200000), '.' walkable, 'w' walkable with directional wall bits (see WALLS), '?' never observed, ' ' void chunk (0x40000000).",
        "# WALLS: 'x y hexmask' for 'w' tiles, bits NW=01 N=02 NE=04 E=08 SE=10 S=20 SW=40 W=80 block movement toward that neighbour. Other 0x?????? bits (line of sight etc) are dropped.",
        "",
        "## ANCHOR",
    ]
    spawn_rel = collections.Counter()
    for f in fights:
        for n in f.static_npcs:
            if n[0] in GROUPS[boss]:
                spawn_rel[(n[0], n[1], n[2])] += 1
    lines.append(f"fights: {len(fights)}. Region origin per fight (world tiles): " + "; ".join(f"{f.fight_id}={f.origin[0]},{f.origin[1]}" for f in fights))
    lines.append("anchor rule: origin = (floor(x/64)*64, floor(y/64)*64) of any boss-group NPC at the first boss-present tick. Instances are placed on 64-aligned regions, so the boss spawn tile has the same anchor coordinates in every fight (table below).")
    oob = collections.Counter()
    for f in fights:
        oob.update(f.dropped_oob)
    lines.append(f"P0 ticks with P0 outside the region (POS rows carry '-'): {sum(f.p0_out_of_region for f in fights)}. Boss-id NPC-ticks outside the region: {sum(f.npc_out_of_region for f in fights)}. Events dropped because their tile lies outside the region (other instance visible in the 104x104 scene): {dict(oob)}")
    lines.append("")
    lines.append("## BOSS SPAWN / FIRST-TICK NPCS (id x y : fights seen)")
    for (nid, x, y), c in sorted(spawn_rel.items()):
        lines.append(f"{nid} {x} {y} : {c}/{len(fights)}")
    modal = {}
    for (nid, x, y), c in spawn_rel.items():
        if nid not in modal or c > modal[nid][0]:
            modal[nid] = (c, x, y)
    lines.append("modal tile per id (the NPC may already have moved when first listed): " + "; ".join(f"{nid} at {x},{y} ({c}/{len(fights)})" for nid, (c, x, y) in sorted(modal.items())))
    all_static = collections.Counter()
    for f in fights:
        for n in f.static_npcs:
            all_static[n] += 1
    lines.append("")
    lines.append("## OTHER NPCS AT FIRST BOSS TICK (id x y : fights seen) (non-group NPCs and moving ones will vary)")
    for n, c in sorted(all_static.items()):
        if n[0] not in GROUPS[boss]:
            lines.append(f"{n[0]} {n[1]} {n[2]} : {c}/{len(fights)}")
    lines.append("")
    obj_runs = collections.defaultdict(int)
    obj_info = {}
    for f in fights:
        for key in f.static_objects:
            obj_runs[key] += 1
    seen_fights = sum(1 for f in fights if f.static_objects)
    lines.append(f"## STATIC OBJECTS at scene load inside the region (gameval ObjectID ids, seen in a strict majority of the {seen_fights} fights whose scene load was recorded): id x y sizeX sizeY orient kind(G=game W=wall D=decor/ground-decor) : fights seen. Ids with more than 40 tiles are summarised.")
    by_id = collections.defaultdict(list)
    occasional = 0
    for key, c in obj_runs.items():
        if c * 2 > seen_fights:
            by_id[key[0]].append((key, c))
        else:
            occasional += 1
    objn = gv["ObjectID1"]
    for oid in sorted(by_id):
        items = sorted(by_id[oid])
        full = [(k, c) for k, c in items if c == len(fights)]
        tag = objn.get(oid, "")
        if len(items) > 40:
            xs = [k[1] for k, _ in items]
            ys = [k[2] for k, _ in items]
            lines.append(f"{oid} {tag} count={len(items)} (in all fights: {len(full)}) bbox x {min(xs)}..{max(xs)} y {min(ys)}..{max(ys)}")
        else:
            for k, c in items:
                lines.append(f"{oid} {k[1]} {k[2]} {k[3]} {k[4]} {k[5]} {k[6]} : {c}/{len(fights)} {tag}")
    lines.append(f"objects seen in at most half of the loads (dynamic or partial loads), omitted: {occasional}")
    votes = collections.defaultdict(collections.Counter)
    for f in fights:
        for t, v in f.collision.items():
            votes[t][v & MOVE_MASK if not v & VOID_CHUNK else VOID_CHUNK] += 1
    canon = {t: c.most_common(1)[0][0] for t, c in votes.items()}
    lines.append("")
    lines.append("## COLLISION CROSS-RUN CHECK")
    total_mismatch = 0
    for f in fights:
        mism = [t for t, v in f.collision.items() if (v & MOVE_MASK if not v & VOID_CHUNK else VOID_CHUNK) != canon[t]]
        total_mismatch += len(mism)
        lines.append(f"{f.fight_id}: tiles known {len(f.collision)}/4096, differing from majority {len(mism)}" + (f" e.g. {sorted(mism)[:8]}" if mism else ""))
    unanimous = sum(1 for t, c in votes.items() if len(c) == 1)
    lines.append(f"tiles observed in any fight {len(votes)}/4096, unanimous {unanimous}, contested {len(votes) - unanimous}, total fight-tile mismatches {total_mismatch}")
    contested = sorted(t for t, c in votes.items() if len(c) > 1)
    if contested:
        lines.append("contested tiles (x,y: {flags:count}) " + "; ".join(f"{t[0]},{t[1]}:" + str({hex(k): v for k, v in votes[t].items()}) for t in contested[:30]) + (" ..." if len(contested) > 30 else ""))
    lines.append("")
    lines.append("## MAP (majority flags, movement bits only)")
    walls = []
    for j in range(REGION - 1, -1, -1):
        row = []
        for i in range(REGION):
            v = canon.get((i, j))
            if v is None:
                row.append("?")
            elif v == VOID_CHUNK:
                row.append(" ")
            elif v & MOVE_BLOCK:
                row.append("#")
            elif v & 0xFF:
                row.append("w")
                walls.append((i, j, v & 0xFF))
            else:
                row.append(".")
        lines.append(f"{j:2d} " + "".join(row))
    lines.append("   " + "".join(str(i // 10 % 10) if i % 10 == 0 else " " for i in range(REGION)))
    lines.append("   " + "".join(str(i % 10) for i in range(REGION)))
    lines.append("")
    lines.append("## WALLS (x y hexmask)")
    for i, j, m in sorted(walls):
        lines.append(f"{i} {j} {m:02x}")
    free = {t for t, v in canon.items() if v != VOID_CHUNK and not v & MOVE_BLOCK}
    spawn = next(iter(sorted(spawn_rel)), None)
    if spawn:
        start = (spawn[1], spawn[2])
        seen = {start}
        stack = [start]
        while stack:
            cx, cy = stack.pop()
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    n = (cx + dx, cy + dy)
                    if n in free and n not in seen:
                        seen.add(n)
                        stack.append(n)
        xs = [t[0] for t in seen]
        ys = [t[1] for t in seen]
        lines.append("")
        lines.append(f"## WALKABLE BOUNDS (8-neighbour flood from boss spawn {start}, walls ignored): x {min(xs)}..{max(xs)} y {min(ys)}..{max(ys)} tiles {len(seen)}")
    p0 = collections.Counter()
    for f in fights:
        for t, pos in f.p0_pos.items():
            p0[pos] += 1
    if p0:
        xs = [p[0] for p in p0]
        ys = [p[1] for p in p0]
        lines.append(f"## P0 OBSERVED BOUNDS during fights: x {min(xs)}..{max(xs)} y {min(ys)}..{max(ys)} distinct tiles {len(p0)}")
    lines.append("")
    lines.append("## PLAYER POSITIONS AT FIGHT START (P0 tile at start tick, then at start-10)")
    for f in fights:
        a = f.p0_pos.get(f.start)
        b = f.p0_pos.get(f.start - 10)
        lines.append(f"{f.fight_id} start_kind={f.start_kind} P0@start={a} P0@start-10={b}")
    return lines


def checks(all_fights):
    lines = [
        "# Claims in rules.txt and tfa-cycles-*.md against the recomputed fixtures",
        "",
        "claimed = the note's count of attack-to-attack gaps equal to the stated value. computed = same count over every recording in this fixture set, which is larger than the notes' sample (later recordings add Ocellus, Dreadnoth and Ezekiel fights, and ezekiel_03 is a no-combat segment). 'by id' splits the NPC by (index, npc id) like the notes did, 'merged' follows one NPC index across id swaps.",
        "",
        "| claim | source | claimed gap count (n) | computed merged (n) | computed by id (n) | verdict |",
        "|---|---|---|---|---|---|",
    ]
    merged = {b: intervals(fs, b) for b, fs in all_fights.items()}
    by_id = {b: intervals(fs, b, True) for b, fs in all_fights.items()}

    def row(desc, src, boss, anim, gap, claimed, claimed_n):
        def cell(g):
            xs = g[boss][anim]
            return sum(1 for x in xs if x == gap), len(xs)

        m, i = cell(merged), cell(by_id)
        verdict = "match" if claimed in (m[0], i[0]) else "DIFFERS"
        lines.append(f"| {desc} | {src} | {claimed} ({claimed_n}) | {m[0]} ({m[1]}) | {i[0]} ({i[1]}) | {verdict} |")

    row("Guardian 14751 next 5", "cycles-b", "guardian", 14751, 5, 780, 854)
    row("Guardian 14753 next 5", "cycles-b", "guardian", 14753, 5, 220, 241)
    row("Guardian 14754 next 15", "cycles-b", "guardian", 14754, 15, 72, 73)
    row("Guardian 14748 next 5", "cycles-b", "guardian", 14748, 5, 64, 65)
    row("Guardian 14745 next 4", "cycles-b", "guardian", 14745, 4, 18, 18)
    row("Xarth 14985 next 4", "cycles-b", "xarth", 14985, 4, 302, 302)
    row("Xarth 14983 next 4", "cycles-b", "xarth", 14983, 4, 4, 4)
    row("Xarth 14992 next 3", "cycles-b", "xarth", 14992, 3, 12, 12)
    row("Dreadnoth 14780 next 8", "cycles-b", "dreadnoth", 14780, 8, 44, 45)
    row("Bork 14542 next 7", "cycles-b", "bork", 14542, 7, 16, 18)
    row("Moia 14785 next 5", "cycles-b", "moia", 14785, 5, 6, 9)
    row("Ezekiel 14685 next 5", "cycles-a", "ezekiel", 14685, 5, 462, 481)
    row("Ezekiel 14686 next 5", "cycles-a", "ezekiel", 14686, 5, 18, 18)
    row("Ezekiel 14687 next 7", "cycles-a", "ezekiel", 14687, 7, 139, 160)
    row("Ezekiel 14688 next 5", "cycles-a", "ezekiel", 14688, 5, 30, 33)
    row("Ezekiel 14693 next 4", "cycles-a", "ezekiel", 14693, 4, 100, 103)
    row("Ocellus P3 14842 next 3", "cycles-a", "ocellus", 14842, 3, 23, 23)
    row("Ocellus P3 14846 next 4", "cycles-a", "ocellus", 14846, 4, 11, 12)
    row("Ocellus P3 14851 next 6", "cycles-a", "ocellus", 14851, 6, 3, 3)
    row("Ocellus P1 14811 next 3", "cycles-a", "ocellus", 14811, 3, 22, 55)
    row("Ocellus P1 14810 next 7", "cycles-a", "ocellus", 14810, 7, 6, 6)
    lines += ["", "## rules.txt statements recomputed", "", "| claim | computed | verdict |", "|---|---|---|"]

    def events(boss, kind, ident):
        return [(f, e) for f in all_fights[boss] for e in f.events if e["kind"] == kind and e["id"] == ident]

    def p0_hits(f):
        return [(e["tick"], e["id"]) for e in f.events if e["kind"] == "HIT" and e["actor"] == "P0"]

    def delay_hist(boss, anim):
        out = []
        for f, e in events(boss, "ANIM", anim):
            h = hit_after(p0_hits(f), e["tick"])
            if h:
                out.append(h[0] - e["tick"])
        return out

    def flights(boss, pid):
        return [float(e["b"]) for _, e in events(boss, "PROJ", pid)]

    def check(claim, computed, ok):
        lines.append(f"| {claim} | {computed} | {'match' if ok else 'DIFFERS'} |")

    fl = flights("guardian", 4201)
    check("projectile 4201 flight ~2t", hist(fl), set(fl) <= {2.0, 2.5})
    d = delay_hist("guardian", 14751)
    check("anim 14751 hit +4 (388 of 856 anims)", f"delay hist {hist(d)}; anims {len(events('guardian', 'ANIM', 14751))}", sum(1 for x in d if x == 4) == 388)
    for pid in (4056, 4057):
        fl = flights("bork", pid)
        check(f"projectile {pid} 5t flight", hist(fl), set(fl) == {5.0})
    fl = flights("bork", 4045)
    check("projectile 4045 4t flight", hist(fl), set(fl) == {4.0})
    fl = flights("bork", 4440)
    check("projectile 4440 4.5t flight", hist(fl), set(fl) == {4.5})
    d = delay_hist("ezekiel", 14686)
    check("anim 14686 magic lands 2t later", hist(d), set(d) == {2})
    allo = collections.Counter()
    for f, e in events("dreadnoth", "ANIM", 14777):
        allo.update(ht - e["tick"] for ht, _ in p0_hits(f) if 0 <= ht - e["tick"] <= HIT_WINDOW)
    check("anim 14777 slam hits melee 1t after the anim", f"all P0 hits by offset from anim: {hist(allo.elements())}", max(range(0, 4), key=lambda k: allo[k]) == 1)
    check("anim 14777 then 3 ranged shots +4/+5/+6 (anim line)", f"offsets 4:{allo[4]} 5:{allo[5]} 6:{allo[6]}", min(allo[4], allo[5], allo[6]) > 2 * max(allo[2], allo[3]))
    shots = collections.Counter()
    for f, e in events("dreadnoth", "PROJ", 4211):
        prev = [a["tick"] for a in f.events if a["kind"] == "ANIM" and a["id"] == 14777 and a["tick"] <= e["tick"]]
        if e["tick"] - prev[-1] <= HIT_WINDOW:
            shots[(e["tick"] - prev[-1], e["b"])] += 1
    check("projectile 4211 volley: 3 shots landing +3/+4/+5t (projectile line, measured from anim)", f"first-observed minus anim, by flight: {dict(sorted(shots.items()))}; landing = observed + floor(flight) = anim +4/+5/+6", False)
    fl = flights("dreadnoth", 4212)
    check("projectile 4212 glob lands ~6t later", hist(fl), set(fl) == {5.5} or set(fl) == {6.0})
    d = delay_hist("dreadnoth", 14779)
    check("anim 14779 basic attack every 4t", hist([g for g in intervals(all_fights["dreadnoth"], "dreadnoth")[14779]]), sum(1 for g in intervals(all_fights["dreadnoth"], "dreadnoth")[14779] if g == 4) >= 24)
    fl = flights("moia", 4213)
    check("projectile 4213 magic dagger 5.5t flight", hist(fl), set(fl) == {5.5})
    d = delay_hist("moia", 14785)
    check("anim 14785 dagger hits all players ~1t later", f"first P0 hit delay {hist(d)}", max(set(d), key=d.count) == 1)
    fl = flights("xarth", 4374)
    check("projectile 4374 ranged flight 2t", hist(fl), set(fl) == {2.0})
    return lines


def main():
    gv = load_gameval()
    rule_ids = load_rule_ids()
    OUT.mkdir(parents=True, exist_ok=True)
    by_boss = collections.defaultdict(list)
    recs = []
    all_names = set()
    for src in recordings():
        rows = cached_rows(src)
        ticks = [r for r in rows if r[0] == "T"]
        if not ticks:
            continue
        local = ticks[0][3]
        names = {local}
        for r in rows:
            if r[0] == "H" and r[3][0] == "P":
                names.add(r[3][1])
            elif r[0] == "I":
                for a in (r[3], r[4]):
                    if a and a[0] == "P":
                        names.add(a[1])
            elif r[0] == "J":
                for a in (r[6], r[9]):
                    if a and a[0] == "P":
                        names.add(a[1])
        all_names |= {n.replace("\xa0", " ") for n in names}
        all_names |= {n for n in names}
        for boss, first, last, first_row in segment_fights(rows):
            f = build_fight(rows, local, boss, first, last, first_row, gv, rule_ids)
            f.rec = short_name(src)
            f.rec_first = ticks[0][1]
            f.rec_last = ticks[-1][1]
            f.joined_mid = f.first <= f.rec_first + 1 or not any(r[0] == "S" and r[3] in GROUPS[boss] and f.first - 1 <= r[1] <= f.first + 1 for r in rows)
            f.ended_mid = f.last >= f.rec_last - 1
            by_boss[boss].append(f)
            recs.append(f)
    counters = collections.Counter()
    for f in sorted(recs, key=lambda f: (f.rec, f.first)):
        counters[f.boss] += 1
        f.fight_id = f"{f.boss}_{counters[f.boss]:02d}"
    fights_tsv = ["fight_id\tboss\trecording\tfirst_tick\tstart_tick\tstart_kind\tchallenge_tick\tend_tick\tend_kind\toutcome\tparty_size\tp0_deaths\tother_deaths\tlast_boss_tick\tnotes"]
    for f in sorted(recs, key=lambda f: (f.boss, f.fight_id)):
        notes = []
        if f.joined_mid:
            notes.append("recording_joined_mid_fight")
        if f.ended_mid:
            notes.append("recording_ended_mid_fight")
        if f.complete_text:
            notes.append(f.complete_text)
        if f.challenge_tick is not None and f.outcome == "kill":
            notes.append(f"ticks_start_to_complete={f.end - f.challenge_tick}")
        fights_tsv.append("\t".join(str(x) for x in (f.fight_id, f.boss, f.rec, f.first, f.start, f.start_kind, "-" if f.challenge_tick is None else f.challenge_tick, f.end, f.end_kind, f.outcome, f.party_size, len(f.p0_deaths), len(f.other_deaths), f.last, ";".join(notes) or "-")))
    (OUT / "tfa_fights.tsv").write_text("\n".join(fights_tsv) + "\n")
    all_fights = {}
    for boss in GROUPS:
        fs = sorted(by_boss[boss], key=lambda f: f.fight_id)
        all_fights[boss] = fs
        if not fs:
            continue
        write_events(boss, fs, all_names, gv)
        (OUT / f"tfa_{boss}_room.txt").write_text("\n".join(room_txt(boss, fs, gv)) + "\n")
        (OUT / f"tfa_{boss}_stats.md").write_text("\n".join(stats_md(boss, fs, gv)) + "\n")
    (OUT / "tfa_claim_checks.md").write_text("\n".join(checks({b: fs for b, fs in all_fights.items() if fs})) + "\n")
    dropped = collections.defaultdict(collections.Counter)
    for f in recs:
        for key, n in f.dropped_gfx.items():
            dropped[key][f.boss] += n
    (OUT / "tfa_dropped_ids.tsv").write_text("kind\tid\ttotal\tper_boss\n" + "".join(f"{k}\t{i}\t{sum(c.values())}\t{dict(c)}\n" for (k, i), c in sorted(dropped.items())))
    audit(all_names)


def audit(all_names):
    for path in OUT.glob("*"):
        text = path.read_text()
        for name in all_names:
            assert name not in text and name.replace("\xa0", " ") not in text, f"real name {name!r} leaked into {path.name}"


if __name__ == "__main__":
    main()
