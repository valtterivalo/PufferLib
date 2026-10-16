#!/usr/bin/env python3
"""Empirical pool of real opponent setups for the riskfight sim.

Reads our own fights (tapes, `E` lines: the opponent composition tools/rollout_export.py kept) and the spectated duels (spectator.duels: both
fighters of each duel), one fight per tape and one fight per duel fighter, and writes ocean/osrs/encounters/riskfight/riskfight_pool_data.h:
distinct opponent kits (weapon he fights with, the other weapons he swung or wielded, every worn raw id) with the number of fights each was seen in.

What a source shows of him (see the report): the 12 composition slots head, cape, neck, weapon, body, shield, arms, legs, hair, hands, feet, jaw. Ring
and ammo are not part of the composition the client exposes, so worn[12] is always 0 (unobservable, not "empty"). Tapes written before 2026-10-10 hold
no composition (only animations): they are counted in the report and give no row.

    scripts/mine_riskfight_pool.py [--tapes DIR|FILE ...] [--duels FILE] [--out HEADER] [--report MD]
    scripts/mine_riskfight_pool.py --check

Deterministic; python3 standard library only. Unknown input (a malformed tape or duel line, an item header that does not parse) aborts.
"""
import argparse
import collections
import glob
import json
import os
import re
import subprocess
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
HOME = os.path.expanduser("~")
DEFAULT_TAPES = os.path.join(HOME, ".light2/c-engine/tapes")
DEFAULT_DUELS = os.path.join(HOME, ".light2/c-engine/spectator.duels")
DEFAULT_OUT = os.path.join(ROOT, "ocean/osrs/encounters/riskfight/riskfight_pool_data.h")
DEFAULT_REPORT = "/tmp/threat-pool/pool_report.md"
DEFAULT_ITEMS_GENERATED = os.path.join(ROOT, "ocean/osrs/osrs_items_generated.h")
DEFAULT_ITEMS = os.path.join(ROOT, "ocean/osrs/osrs_items.h")
DEFAULT_JAVA_ITEMS = os.path.join(ROOT, "ocean/osrs/gearswapper/gs_java_items.h")
LIGHT_RESOURCES = os.path.join(HOME, "Projects/storm/light/plugins/gear-swapper/src/test/resources")
DEFAULT_NAMES = [os.path.join(LIGHT_RESOURCES, "fight-items.json"), os.path.join(LIGHT_RESOURCES, "spectator-items.json")]

POOL_WORN, POOL_SWITCHES = 13, 4
KIT = 12                       # composition slots the sources keep: head cape neck weapon body shield arms legs hair hands feet jaw
WEAPON, SHIELD = 3, 5
ARMOUR_SLOTS = [0, 1, 2, 4, 5, 7, 9, 10]           # the slots the sim wears (gs_stat_item_slot) besides the weapon, ring 12 and ammo 13 never observed
KIT_NAMES = ["head", "cape", "neck", "weapon", "body", "shield", "arms", "legs", "hair", "hands", "feet", "jaw", "ring"]
# gs_stat_item_slot: the kit slots the sim can wear
SIM_KIT_SLOTS = {0, 1, 2, 3, 4, 5, 7, 9, 10, 12, 13}

# Equivalent raw ids the tape exporter folds (tools/rollout_export.py CANONICAL): the tape already carries them folded, the duels do not.
EXPORT_CANONICAL = {24592: 13441, 24595: 3144, 24598: 3024, 24601: 3026, 24603: 3028, 24605: 3030,
                    20513: 10551, 28067: 10551, 28070: 10828, 21287: 21295}

# A hit on us that equals his damage taken // 10 + 1 within a tick is the reflect of a ring of recoil (or suffering): the exporter labels it class R. The
# ring id stored for a fight that showed it; every other fight stores 0 = not seen (not "no ring").
RECOIL_RING = 2550

# Animations that are a swing (attack, spec or cast) of the weapon in hand; block, eat, teleport and Vengeance animations are not. Identified from the
# exporter's tables (SWING_SPEED, WEAPON_SPEED_ANIMS, SPEC_COST) and from which weapon was in hand on every other animation seen (see the report).
SWING_ANIMS = {376, 378, 386, 388, 390, 393, 401, 403, 415, 420, 422, 423, 426, 428, 429, 440, 1060, 1062, 1074, 1161, 1167, 1378, 1658, 1659, 1665, 1666,
               1667, 1978, 1979, 2066, 2067, 2080, 2081, 2661, 2845, 3295, 3297, 3300, 3996, 4230, 5865, 5866, 6600, 7045, 7054, 7514, 7515, 7516, 7517,
               7521, 7644, 7645, 7855, 8056, 8145, 8194, 8195, 8292, 8970, 8977, 9166, 9401, 9463, 9465, 10172, 10173, 10427, 11057, 11060, 11124, 11275,
               11465, 11645, 11902, 12297, 435, 1720, 7218, 7219, 7222, 7305}
# Special attack animations (a subset of SWING_ANIMS): the weapon in hand at one of these is a spec weapon.
SPEC_ANIMS = {1060, 1062, 1074, 1378, 1667, 3300, 7514, 7515, 7521, 7644, 7645, 10427, 11275}

# Archetype of a worn set from the name of body, legs and head; first match wins. (keyword in lower-cased name, archetype)
ARCHETYPES = [
    ("dharok", "Dharok's"), ("torag", "Torag's"), ("verac", "Verac's"), ("guthan", "Guthan's"), ("ahrim", "Ahrim's"), ("karil", "Karil's"),
    ("bandos", "Bandos"), ("armadyl", "Armadyl"), ("inquisitor", "Inquisitor's"), ("justiciar", "Justiciar"), ("ancestral", "Ancestral"),
    ("void", "Void"), ("fighter torso", "Fighter torso"), ("obsidian", "Obsidian"), ("torso of", "Torso"), ("neitiznot", "Neitiznot"),
    ("black d'hide", "Black d'hide"), ("blue d'hide", "d'hide"), ("green d'hide", "d'hide"), ("red d'hide", "d'hide"), ("d'hide", "d'hide"),
    ("mystic", "Mystic"), ("wizard", "Wizard"), ("dragon", "Dragon"), ("rune", "Rune"), ("adamant", "Adamant"), ("mithril", "Mithril"),
    ("black ", "Black"), ("iron", "Iron"), ("steel", "Steel"), ("bronze", "Bronze"), ("granite", "Granite"), ("proselyte", "Proselyte"),
    ("bloodbark", "Bloodbark"), ("splitbark", "Splitbark"), ("zamorak", "Zamorak"), ("saradomin", "Saradomin"), ("guthix", "Guthix"),
    ("blessed", "Blessed"), ("elite", "Elite"), ("graceful", "Graceful"), ("shayzien", "Shayzien"), ("oathplate", "Oathplate"),
    ("masori", "Masori"), ("torva", "Torva"), ("eclipse moon", "Eclipse moon"), ("crystal", "Crystal"), ("elder chaos", "Elder chaos"),
    ("elderchaos", "Elder chaos"), ("vesta", "Vesta's"), ("statius", "Statius's"), ("morrigan", "Morrigan's"), ("zuriel", "Zuriel's"),
    ("mixed hide", "Mixed hide"), ("leather", "Leather"), ("woven", "Woven"), ("ghostly", "Ghostly"), ("skirt", "Skirt"), ("virtus", "Virtus"), ("pegasian", "Pegasian"), ("primordial", "Primordial"), ("eternal", "Eternal"),
]


def die(message):
    print(f"mine_riskfight_pool: {message}", file=sys.stderr)
    sys.exit(2)


def display(path):
    return "~" + path[len(HOME):] if path.startswith(HOME + "/") else path


# ---------------------------------------------------------------- item tables

class Items:
    """What the sim can do with a raw id: a fixed ITEM_DATABASE row (with an attack style for weapons), a stat-priced row from the Java table, or neither."""

    def __init__(self, generated_path, items_path, java_path, name_paths):
        text = read(generated_path)
        self.fixed = {}     # raw id -> (ITEM_X, name, slot)
        by_symbol = {}
        for m in re.finditer(r'\[(ITEM_[A-Z0-9_]+)\]\s*=\s*\{\s*\.item_id\s*=\s*(\d+),\s*\.name\s*=\s*"((?:[^"\\]|\\.)*)",\s*\.slot\s*=\s*(SLOT_[A-Z]+)', text):
            symbol, raw, name, slot = m.group(1), int(m.group(2)), m.group(3), m.group(4)
            by_symbol[symbol] = (raw, name, slot)
            self.fixed[raw] = (symbol, name, slot)
        count = re.search(r"NUM_ITEMS\s*=\s*(\d+)", text)
        if not count or int(count.group(1)) != len(by_symbol):
            die(f"{generated_path}: parsed {len(by_symbol)} item rows, NUM_ITEMS says {count.group(1) if count else '?'}")
        items = read(items_path)
        block = re.search(r"#define OSRS_ITEM_RAW_GEAR_ALIAS_ROWS\(X\)((?:[^\n]*\\\n)*[^\n]*)", items)
        if not block:
            die(f"{items_path}: OSRS_ITEM_RAW_GEAR_ALIAS_ROWS not found")
        self.alias = {}     # raw id -> canonical raw id of its ITEM_DATABASE row
        for raw, symbol in re.findall(r"X\((\d+),\s*(ITEM_[A-Z0-9_]+)\)", block.group(1)):
            if symbol not in by_symbol:
                die(f"{items_path}: alias to unknown {symbol}")
            self.alias[int(raw)] = by_symbol[symbol][0]
        body = re.search(r"static inline int get_item_attack_style\(uint8_t item_index\) \{\s*switch \(item_index\) \{(.*?)default:", items, re.S)
        if not body:
            die(f"{items_path}: get_item_attack_style not found")
        self.styled = set()  # ITEM_X symbols with an attack style
        for labels, style in re.findall(r"((?:\s*case ITEM_[A-Z0-9_]+:)+)\s*return (\d+);", body.group(1)):
            if int(style) > 0:
                self.styled.update(re.findall(r"case (ITEM_[A-Z0-9_]+):", labels))
        self.java = {}      # raw id -> dict
        jtext = read(java_path)
        rows = 0
        for m in re.finditer(r'^\s*\{(\d+), "((?:[^"\\]|\\.)*)", ([^}\n]*)\},?\s*$', jtext, re.M):
            fields = [f.strip() for f in m.group(3).split(",")]
            if len(fields) != 21:
                die(f"{java_path}: row {m.group(1)} has {len(fields)} fields, expected 21 after the name")
            rows += 1
            self.java[int(m.group(1))] = dict(name=m.group(2), aspeed=int(fields[14]), slot=int(fields[15]), has_stats=int(fields[16]),
                                              base_speed=int(fields[19]))
        if rows < 100:
            die(f"{java_path}: only {rows} rows parsed")
        self.names = {}
        for path in name_paths:
            data = json.loads(read(path))
            data = data.get("items", data)
            for raw, entry in data.items():
                self.names.setdefault(int(raw), entry["name"])
        # Barrows pieces: the "100" id, its four degraded ids (+1..+4, six ids per piece from 4856) and the undamaged item all fold onto the "100" id
        self.fold = dict(EXPORT_CANONICAL)
        for raw in range(4856, 5000):
            offset = (raw - 4856) % 6
            base = raw - offset
            if 0 < offset <= 4:
                own = self.java[raw]["name"] if raw in self.java else self.names.get(raw)
                if own is not None and not own.endswith(f" {100 - 25 * offset}"):
                    die(f"barrows id {raw} is named {own!r}, expected degradation {100 - 25 * offset}")
                self.fold[raw] = base
        for raw, row in self.java.items():
            m = re.fullmatch(r"(.*) 100", row["name"])
            if m and 4856 <= raw < 5000:
                for other, other_row in self.java.items():
                    if other_row["name"] == m.group(1) and other < 4856:
                        self.fold[other] = raw

    def canon(self, raw):
        """The raw id the sim treats this one as: exporter folds, barrows degradation, the sim's own gear alias rows."""
        raw = self.fold.get(raw, raw)
        return self.alias.get(raw, raw)

    def name(self, raw):
        if raw in self.fixed:
            return self.fixed[raw][1]
        if raw in self.java:
            return self.java[raw]["name"]
        return self.names.get(raw, f"id {raw}")

    def resolve(self, raw, slot):
        """'fixed' (hand-written row, usable), 'stat' (priced from the Java table: no special attack, no effect), 'unknown' (the live server falls back to a prior)."""
        canon = self.canon(raw)
        if canon in self.fixed:
            symbol, _, db_slot = self.fixed[canon]
            if slot == WEAPON and db_slot == "SLOT_WEAPON" and symbol not in self.styled:
                return "unknown"
            return "fixed"
        row = self.java.get(raw)
        if row and row["has_stats"] and row["slot"] in SIM_KIT_SLOTS:
            if row["slot"] == WEAPON and not (row["aspeed"] > 0 or row["base_speed"] > 0):
                return "unknown"
            return "stat"
        return "unknown"


def read(path):
    try:
        with open(path) as fh:
            return fh.read()
    except OSError as e:
        die(f"cannot read {path}: {e}")


# ---------------------------------------------------------------- fights

class Fight:
    """One opponent in one fight: what he wore (ticks per id and slot), which weapons he held and swung."""

    def __init__(self, source, label, items):
        self.source, self.label, self.items = source, label, items
        self.ticks = 0
        self.slot_ticks = [collections.Counter() for _ in range(KIT)]
        self.wield = collections.Counter()
        self.swings = collections.Counter()
        self.specs = collections.Counter()
        self.first_weapon = None
        self.hits_on_him = 0    # our landed hits on him with damage (tape only)
        self.reflected = 0      # hits on us labelled as the reflect of such a hit

    def read(self, kit, ticks=1):
        """He showed `kit` (12 raw ids, -1 empty) for `ticks` ticks."""
        self.ticks += ticks
        for slot, raw in enumerate(kit):
            if raw >= 0:
                self.slot_ticks[slot][self.items.canon(raw)] += ticks
        weapon = self.items.canon(kit[WEAPON]) if kit[WEAPON] >= 0 else -1
        if weapon >= 0:
            self.wield[weapon] += ticks
            if self.first_weapon is None:
                self.first_weapon = weapon

    def swing(self, weapon, anim):
        if weapon < 0 or anim not in SWING_ANIMS:
            return
        weapon = self.items.canon(weapon)
        self.swings[weapon] += 1
        if anim in SPEC_ANIMS:
            self.specs[weapon] += 1

    def usable(self):
        return self.ticks > 0 and bool(self.wield)

    def main_weapon(self):
        pool = self.swings if self.swings else self.wield
        return min(pool, key=lambda w: (-pool[w], -self.wield[w], w))

    def other_weapons(self):
        main = self.main_weapon()
        others = [w for w in self.wield if w != main] + [w for w in self.swings if w != main and w not in self.wield]
        return sorted(set(others), key=lambda w: (-self.swings[w], -self.wield[w], w))

    def worn(self):
        """Per kit slot the id he wore longest (empty slots do not count against a worn one: a shield under a two-handed switch is still his shield)."""
        worn = [0] * POOL_WORN
        worn[12] = RECOIL_RING if self.ring_seen() else 0
        for slot in range(KIT):
            if slot == WEAPON:
                continue
            if self.slot_ticks[slot]:
                counts = self.slot_ticks[slot]
                worn[slot] = min(counts, key=lambda raw: (-counts[raw], raw))
        return worn

    def ring_seen(self):
        """His ring of recoil showed: the reflect hit us twice, or once and for at least half of our landed hits."""
        return self.reflected >= 2 or (self.reflected >= 1 and 2 * self.reflected >= self.hits_on_him)

    def armour_changes(self):
        """Slots other than weapon and shield where he wore more than one id."""
        return [slot for slot in range(KIT) if slot not in (WEAPON, SHIELD) and len(self.slot_ticks[slot]) > 1]


def parse_tape(path, items):
    stamp = os.path.basename(path)[:15]
    name = None
    anims, kits, hits = [], {}, []
    for line in read(path).splitlines():
        if line.startswith("FIGHT "):
            f = line.split()
            if len(f) != 6:
                die(f"{path}: bad FIGHT line {line!r}")
            name = f[3]
        elif line.startswith("A "):
            f = line.split()
            if f[2] == "O":
                anims.append((int(f[1]), int(f[3])))
        elif line.startswith("H "):
            f = line.split()
            hits.append((f[2], int(f[3]), f[5]))
        elif line.startswith("E "):
            f = line.split()
            if f[2] == "1":
                if int(f[3]) != KIT or len(f) != 4 + KIT:
                    die(f"{path}: E line with {f[3]} slots, expected {KIT}: {line!r}")
                kits[int(f[1])] = [int(v) for v in f[4:]]
    if name is None:
        die(f"{path}: no FIGHT line")
    positions = sorted((int(f[8]), int(f[9])) for f in (l.split() for l in read(path).splitlines() if l.startswith("T ")))
    if not positions:
        die(f"{path}: no T lines")
    fight = Fight("tape", f"{stamp} {name}", items)
    for tick in sorted(kits):
        fight.read(kits[tick])
    for tick, anim in anims:
        if tick in kits:
            fight.swing(kits[tick][WEAPON], anim)
    fight.hits_on_him = sum(1 for side, amount, klass in hits if side == "O" and klass == "A" and amount > 0)
    fight.reflected = sum(1 for side, amount, klass in hits if side == "U" and klass == "R")
    fight.position = (positions[len(positions) // 2][0], sorted(y for _, y in positions)[len(positions) // 2])
    fight.file = os.path.basename(path)
    fight.kit_anims = {}
    return fight


def parse_duels(path, items):
    """Both fighters of every duel: DUEL id rec start end 0 0 dh dh / E tick side kit / R tick third (13 fields per side)."""
    fights = []
    duels = []
    duel = None
    for number, line in enumerate(read(path).splitlines(), 1):
        f = line.split()
        if not f:
            continue
        if f[0] == "DUEL":
            if len(f) != 9:
                die(f"{path}:{number}: bad DUEL line")
            duel = dict(id=f[1], recording=f[2], start=int(f[3]), end=int(f[4]), dharok=int(f[7]), eq=[[], []], rows=[])
            duels.append(duel)
        elif f[0] == "E":
            if duel is None or len(f) != 15 or f[2] not in ("a", "b"):
                die(f"{path}:{number}: bad E line")
            duel["eq"][f[2] == "b"].append((int(f[1]), [int(v) for v in f[3:]]))
        elif f[0] == "R":
            if duel is None or len(f) != 3 + 26:
                die(f"{path}:{number}: bad R line")
            duel["rows"].append((int(f[1]), f[3:16], f[16:29]))
        else:
            die(f"{path}:{number}: unknown line kind {f[0]!r}")
    for duel in duels:
        for side in (0, 1):
            fight = Fight("duel", f"{duel['id']} side {side}", items)
            fight.duel = duel
            fight.side = side
            eq = duel["eq"][side]
            ticks = [row[0] for row in duel["rows"]]
            for i, (tick, kit) in enumerate(eq):
                end = eq[i + 1][0] if i + 1 < len(eq) else duel["end"] + 1
                if end > tick:
                    fight.read(kit, end - tick)
            kit_at = {}
            current, i = None, 0
            for tick in ticks:
                while i < len(eq) and eq[i][0] <= tick:
                    current = eq[i][1]
                    i += 1
                kit_at[tick] = current
            for tick, a, b in duel["rows"]:
                row = (a, b)[side]
                weapon, anims = int(row[1]), row[12]
                if anims != "-" and kit_at[tick] is not None:
                    for anim in anims.split("|"):
                        fight.swing(kit_at[tick][WEAPON], int(anim))
            fights.append(fight)
    return fights, duels


def load_tapes(paths, items):
    files = []
    for path in paths:
        if os.path.isdir(path):
            files += sorted(glob.glob(os.path.join(path, "*.tape")))
        else:
            files.append(path)
    if not files:
        die("no tapes")
    return [parse_tape(p, items) for p in sorted(files)]


def spectator_locations(directory):
    """Recording id (rNN, the position in the sorted recording list as spectator_export.py numbers them) -> (world, median x, median y) of its fighters."""
    paths = sorted(glob.glob(os.path.join(directory, "*.jsonl.zst")))
    if not paths:
        die(f"no spectator recordings in {directory}")
    out = {}
    for index, path in enumerate(paths):
        xs, ys, worlds = [], [], collections.Counter()
        proc = subprocess.Popen(["zstd", "-dc", path], stdout=subprocess.PIPE)
        for raw in proc.stdout:
            if b'"ACTOR_STATE_OBSERVED"' not in raw[:400]:
                continue
            actor = json.loads(raw)["payload"].get("actor") or {}
            point = actor.get("worldPoint")
            if point and actor.get("kind") == "PLAYER" and actor.get("scopeRole") in ("SELECTED", "PLAYER_FIGHTER"):
                xs.append(point["x"])
                ys.append(point["y"])
                worlds[actor["world"]] += 1
        if proc.wait() != 0:
            die(f"zstd failed on {path}")
        if xs:
            xs.sort()
            ys.sort()
            out[f"r{index:02d}"] = (worlds.most_common(1)[0][0], xs[len(xs) // 2], ys[len(ys) // 2])
    return out


# ---------------------------------------------------------------- rows

def row_of(fight, items):
    """The setup row of one fight: (weapon, switches, worn), and the number of its raw ids the sim cannot resolve."""
    main = fight.main_weapon()
    others = fight.other_weapons()
    worn = fight.worn()
    return main, tuple(others), tuple(worn)


def unknown_count(items, weapon, switches, worn):
    ids = {(weapon, WEAPON)} | {(w, WEAPON) for w in switches} | {(raw, slot) for slot, raw in enumerate(worn) if raw and slot in SIM_KIT_SLOTS}
    return sum(1 for raw, slot in sorted(ids) if items.resolve(raw, slot) == "unknown")


def archetype(items, worn):
    """Armour archetype from body, legs and head names; 'bare' for none."""
    for slot in (4, 7, 0):
        if worn[slot]:
            name = items.name(worn[slot]).lower()
            for key, label in ARCHETYPES:
                if key in name:
                    return label
    return "bare" if not any(worn[s] for s in (0, 4, 7)) else "other"


def build(tapes, duel_fights, items):
    rows = collections.OrderedDict()
    usable = [f for f in tapes + duel_fights if f.usable()]
    for fight in usable:
        weapon, switches, worn = row_of(fight, items)
        kept = switches[:POOL_SWITCHES]
        key = (weapon, kept, worn)
        row = rows.setdefault(key, dict(weapon=weapon, switches=kept, worn=worn, sources=collections.defaultdict(list), overflow=0,
                                        unknown=unknown_count(items, weapon, kept, worn)))
        row["sources"][fight.source].append(fight)
        row["overflow"] += len(switches) - len(kept)
    ordered = sorted(rows.values(), key=lambda r: (-sum(len(v) for v in r["sources"].values()), r["weapon"], r["switches"], r["worn"]))
    return ordered, usable


def source_string(row):
    total = sum(len(v) for v in row["sources"].values())
    if total == 1:
        kind, [fight] = next(iter(row["sources"].items()))
        return f"{kind} {fight.label}"
    return ", ".join(f"{kind} x{len(row['sources'][kind])}" for kind in sorted(row["sources"]))


def c_string(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def render_header(rows, tapes_label, duels_label, n_tape, n_duel):
    out = [f"/* Generated by scripts/mine_riskfight_pool.py from {tapes_label}, {duels_label}. Do not edit. */",
           f"/* {sum(sum(len(v) for v in r['sources'].values()) for r in rows)} fights in {len(rows)} distinct setups: {n_tape} own tape fights, {n_duel} spectated duel fighters. */",
           "/* worn[] is indexed by the client worn slot 0..12 (head, cape, neck, weapon, body, shield, arms, legs, hair, hands, feet, jaw, ring); weapon (3) is 0 here, the weapon is `weapon`. */",
           "/* Ring (12) is 2550 (a ring of recoil or suffering) where the tape showed his reflect hitting us, 0 = not seen (duels never show it); ammo has no column. */",
           f"enum {{ RISKFIGHT_POOL_WORN = {POOL_WORN}, RISKFIGHT_POOL_SWITCHES = {POOL_SWITCHES} }};",
           "typedef struct {",
           '    const char* source;                        /* "tape 20261005T193654 KeengCr1mson" or "duel <id> side <n>" ; for merged identical setups: "tape x12, duel x3" */',
           "    uint16_t fights;                           /* weight: number of fights with this setup */",
           "    uint16_t weapon;                           /* raw id of the weapon he fights with most */",
           "    uint16_t switches[RISKFIGHT_POOL_SWITCHES];/* raw ids of other weapons he swung/wielded, 0 = none */",
           "    uint16_t worn[RISKFIGHT_POOL_WORN];        /* raw ids by the Light client worn slot index 0..12 (head, cape, neck, weapon(0 here), body, shield, ?, legs, ?, hands, feet, ?, ring); -1 stored as 0 */",
           "    uint8_t unknown;                           /* count of raw ids in this row the sim item table cannot resolve */",
           "} RiskfightPoolRow;",
           "static const RiskfightPoolRow RISKFIGHT_POOL_ROWS[] = {"]
    for row in rows:
        switches = list(row["switches"]) + [0] * (POOL_SWITCHES - len(row["switches"]))
        fights = sum(len(v) for v in row["sources"].values())
        if fights > 65535 or row["unknown"] > 255:
            die("row weight or unknown count does not fit its field")
        worn = ", ".join(map(str, row["worn"]))
        out.append(f"    {{ {c_string(source_string(row))}, {fights}, {row['weapon']}, {{{', '.join(map(str, switches))}}}, {{{worn}}}, {row['unknown']} }},")
    out.append("};")
    return "\n".join(out) + "\n"


def parse_header(text):
    """Rows of a generated header: (source, fights, weapon, switches, worn, unknown)."""
    if f"RISKFIGHT_POOL_WORN = {POOL_WORN}, RISKFIGHT_POOL_SWITCHES = {POOL_SWITCHES}" not in text:
        die("header constants differ from the generator")
    body = re.search(r"RISKFIGHT_POOL_ROWS\[\] = \{\n(.*?)\n\};", text, re.S)
    if not body:
        die("header has no row table")
    rows = []
    for line in body.group(1).splitlines():
        m = re.fullmatch(r'\s*\{ "((?:[^"\\]|\\.)*)", (\d+), (\d+), \{([\d, ]*)\}, \{([\d, ]*)\}, (\d+) \},', line)
        if not m:
            die(f"header row does not parse: {line!r}")
        switches = [int(v) for v in m.group(4).split(",")]
        worn = [int(v) for v in m.group(5).split(",")]
        if len(switches) != POOL_SWITCHES or len(worn) != POOL_WORN or worn[3] != 0 or worn[12] not in (0, RECOIL_RING):
            die(f"header row has wrong arity or a weapon/ring in worn: {line!r}")
        rows.append((m.group(1), int(m.group(2)), int(m.group(3)), switches, worn, int(m.group(6))))
    return rows


# ---------------------------------------------------------------- report

def top(counter, n=None):
    return counter.most_common(n)


def table(header, rows):
    out = ["| " + " | ".join(header) + " |", "|" + "|".join("---" for _ in header) + "|"]
    out += ["| " + " | ".join(str(c) for c in row) + " |" for row in rows]
    return "\n".join(out)


def report(rows, usable, tapes, duel_fights, duels, items, args, locations):
    n_fight = len(usable)
    by_row = {}
    for row in rows:
        for fights in row["sources"].values():
            for fight in fights:
                by_row[id(fight)] = row
    name = items.name
    L = []
    w = L.append
    ntape_all = len(tapes)
    tape_use = [f for f in tapes if f.usable()]
    duel_use = [f for f in duel_fights if f.usable()]
    w("# Opponent setup pool (mined from tapes and spectated duels)\n")
    w("Generated by `scripts/mine_riskfight_pool.py`; numbers are for the sources named below.\n")
    w("## Sources and counts\n")
    w(f"- tapes `{display(args.tapes[0]) if len(args.tapes) == 1 else ', '.join(display(t) for t in args.tapes)}`: {ntape_all} tapes (all our own fights), "
      f"{len(tape_use)} show his composition (`E ... 1 12 ...` lines, written by `tools/rollout_export.py` since the recordings of 2026-10-10), "
      f"{ntape_all - len(tape_use)} do not (recorded before the composition capture: only his animations are known, no ids) and give no row.")
    names_tapes = collections.Counter(f.label.split(" ", 1)[1] for f in tape_use)
    w(f"- usable tape fights: {len(tape_use)} against {len(names_tapes)} distinct opponent ids (name = the FIGHT header's opponent); "
      f"{sum(1 for n in names_tapes.values() if n > 1)} opponents were fought more than once (max {max(names_tapes.values())}).")
    w(f"- duels `{display(args.duels)}`: {len(duels)} duels, {len(duel_fights)} fighters, {len(duel_use)} with a composition; each fighter is one fight. "
      f"Total weighted fights: {n_fight}; distinct setups: {len(rows)}.")
    # duel mode
    same_arch = 0
    same_weapon = 0
    dharok = 0
    pair = collections.defaultdict(dict)
    for f in duel_use:
        pair[f.duel["id"]][f.side] = f
    both = [(p[0], p[1]) for p in pair.values() if len(p) == 2]
    for a, b in both:
        wa, wb = a.worn(), b.worn()
        same_arch += archetype(items, wa) == archetype(items, wb)
        same_weapon += a.main_weapon() == b.main_weapon()
        dharok += a.duel["dharok"]
    w(f"- duel pairs with both fighters composed: {len(both)}; same armour archetype on both sides {same_arch} ({100*same_arch/max(1,len(both)):.0f}%), same main weapon {same_weapon} "
      f"({100*same_weapon/max(1,len(both)):.0f}%), Dharok flag (either fighter wears/wields Dharok's) {dharok} ({100*dharok/max(1,len(both)):.0f}%).\n")
    w("Own fights by place (median of our position over the fight): " + "; ".join(
        f"{place} {n}" for place, n in sorted(collections.Counter(
            "Edgeville (y < 3523)" if f.position[1] < 3523 else f"x~{f.position[0] // 50 * 50}, y~{f.position[1] // 50 * 50}" for f in tape_use).items())) + ".\n")
    w("### What each source records of his gear\n")
    w("- tape: per tick `E tick 1 12 id*12` = the client's player-composition equipment of him (slots head, cape, neck, weapon, body, shield, arms, legs, hair, hands, feet, jaw; "
      "`rawEquipmentIds - itemOffset`, -1 empty), folded through the exporter's CANONICAL variants. His animations are `A tick O anim`, `KIT` his animation counts. "
      "`T` lines carry OUR worn/bag, not his.")
    w("- duel: `E tick side id*12` on every change of the same 12-slot composition, `R` lines per tick per fighter with `weapon` and his animation list.")
    seen = [f for f in tape_use if f.ring_seen()]
    none = [f for f in tape_use if not f.ring_seen() and f.reflected == 0 and f.hits_on_him >= 3]
    vague = [f for f in tape_use if not f.ring_seen() and f not in none]
    w("- ammo (kit slot 13): **not observable**, the composition has no ammo slot (12 entries, slots 6, 8, 11 are always empty); there is no pool column for it.")
    w("- ring (slot 12): the composition has no ring slot either, but **a ring of recoil / suffering shows itself through its reflect**: a hit on us equal to his damage taken // 10 + 1 "
      "on the same or next tick (the exporter's class `R` rows, `T` field `recoil_evidence`; our own ring's reflect lands on him as `H O ... R` and is not counted). "
      f"In the {len(tape_use)} own fights with a composition: ring seen in {len(seen)} (reflected twice, or once and at least half of our landed hits), "
      f"no reflect despite at least 3 landed hits in {len(none)} (no recoil ring, or its charges gone), inconclusive {len(vague)}. "
      f"`worn[12]` = {RECOIL_RING} (recoil-class ring) for the first group, 0 = not seen for the rest. Duels give no such signal: both fighters' reflects and real hits "
      "land on the same ticks in the per-tick `dmg` fields and the match rate is not bimodal (tried: tick and next tick, single hits of 10 or more), so a duel fighter's ring is unobservable and stored 0. "
      "Other rings (berserker, ring of the gods, ...) are unobservable in every source.\n")
    # location
    w("### Mode of the spectated duels\n")
    w(f"Mirrored gear: both fighters wear the same armour archetype in {100*same_arch/max(1,len(both)):.0f}% of the duel pairs and hold the same main weapon in "
      f"{100*same_weapon/max(1,len(both)):.0f}%; Dharok's greataxe or set on either side in {100*dharok/max(1,len(both)):.0f}%. They are fights between people at a fighting spot, not a "
      "rule-bound match with fixed gear; they are an opponent setup source like the tapes (our own fights, which all lie at the Edgeville spot).\n")
    if locations is None:
        w("Location of the duels: not computed (rerun with `--spectator-dir ~/.light2/gameplay-recordings/spectator-combat`).\n")
    else:
        place = {}
        for rid, (world, mx, my) in locations.items():
            place[rid] = f"world {world}, " + ("Edgeville (y < 3523, outside the ditch)" if my < 3523 else "y >= 3523 (wilderness coordinates)")
        by_place = collections.defaultdict(list)
        for duel in duels:
            by_place[place.get(duel["recording"], "no position")].append(duel["id"])
        fighters_by_place = collections.defaultdict(list)
        for f in duel_use:
            fighters_by_place[place.get(f.duel["recording"], "no position")].append(f)
        w("Location of the duels (median fighter position per recording, from the recordings, not the duel file):\n")
        w(table(["place", "duels", "fighters with composition", "Dharok's archetype", "top main weapons", "fights with a spec seen"], [
            (k, len(by_place[k]), len(fighters_by_place[k]), sum(1 for f in fighters_by_place[k] if archetype(items, f.worn()) == "Dharok's"),
             ", ".join(f"{name(m)} {c}" for m, c in top(collections.Counter(f.main_weapon() for f in fighters_by_place[k]), 4)),
             sum(1 for f in fighters_by_place[k] if f.specs)) for k in sorted(by_place)]))
        w("\nMedian position per recording: " + "; ".join(f"{rid} world {wd} ({mx}, {my})" for rid, (wd, mx, my) in sorted(locations.items()) if any(d['recording'] == rid for d in duels)) + "\n")
    # weapon histogram
    main_hist = collections.Counter()
    main_hist_src = collections.defaultdict(collections.Counter)
    swung = collections.Counter()
    wielded = collections.Counter()
    specs = collections.Counter()
    spec_src = collections.defaultdict(collections.Counter)
    for f in usable:
        m = f.main_weapon()
        main_hist[m] += 1
        main_hist_src[m][f.source] += 1
        for wid in f.other_weapons():
            wielded[wid] += 1
        for wid in f.swings:
            swung[wid] += 1
        for wid in f.specs:
            specs[wid] += 1
            spec_src[wid][f.source] += 1
    def res(raw, slot=WEAPON):
        return items.resolve(raw, slot)
    w("## Main weapon histogram (fights)\n")
    w(table(["weapon", "raw id", "fights", "tape", "duel", "sim"], [
        (name(wid), wid, n, main_hist_src[wid]["tape"], main_hist_src[wid]["duel"], res(wid)) for wid, n in top(main_hist)]))
    w("\n## Other weapons (switches): fights in which the weapon was held or swung, but is not the main one\n")
    w(table(["weapon", "raw id", "fights", "sim"], [(name(wid), wid, n, res(wid)) for wid, n in top(wielded)]))
    w("\n## Spec weapons: fights in which he used a special attack animation with the weapon\n")
    w(table(["weapon", "raw id", "fights", "tape", "duel", "sim"], [
        (name(wid), wid, n, spec_src[wid]["tape"], spec_src[wid]["duel"], res(wid)) for wid, n in top(specs)]))
    spec_fights = sum(1 for f in usable if f.specs)
    w(f"\nFights with a spec animation seen: {spec_fights} of {n_fight}. Fights with at least one switch weapon: {sum(1 for f in usable if f.other_weapons())}; "
      f"with more than {POOL_SWITCHES}: {sum(1 for f in usable if len(f.other_weapons()) > POOL_SWITCHES)}.")
    # co-occurrence of spec weapons with main weapons
    co = collections.defaultdict(collections.Counter)
    for f in usable:
        m = f.main_weapon()
        for wid in f.specs:
            co[m][wid] += 1
    w("\n### Spec weapon by main weapon (fights)\n")
    w(table(["main weapon", "fights", "spec weapons seen (fights)"], [
        (f"{name(m)} ({m})", main_hist[m], ", ".join(f"{name(s)} {c}" for s, c in top(co[m])) or "none seen") for m, _ in top(main_hist)]))
    # armour archetypes
    arch = collections.Counter()
    arch_fights = collections.defaultdict(list)
    for f in usable:
        a = archetype(items, f.worn())
        arch[a] += 1
        arch_fights[a].append(f)
    w("\n## Armour archetypes (body, then legs, then head name)\n")
    w(table(["archetype", "fights", "main weapons (fights)", "fights with a spec weapon seen"], [
        (a, n, ", ".join(f"{name(m)} {c}" for m, c in top(collections.Counter(f.main_weapon() for f in arch_fights[a]), 3)),
         sum(1 for f in arch_fights[a] if f.specs)) for a, n in top(arch)]))
    other = collections.Counter((name(f.worn()[4]), name(f.worn()[7])) for f in usable if archetype(items, f.worn()) == "other")
    if other:
        w("\nBody / legs of the `other` archetype: " + "; ".join(f"{b} + {l} x{n}" for (b, l), n in top(other)) + "\n")
    # coverage
    unknown_rows = [r for r in rows if r["unknown"]]
    unknown_fights = sum(sum(len(v) for v in r["sources"].values()) for r in unknown_rows)
    stat_fights = 0
    weapon_unknown_fights = 0
    weapon_stat_fights = 0
    nospec_fights = 0
    unknown_ids = collections.Counter()
    stat_ids = collections.Counter()
    for f in usable:
        weapon, switches, worn = row_of(f, items)
        ids = [(weapon, WEAPON)] + [(s, WEAPON) for s in switches[:POOL_SWITCHES]] + [(raw, slot) for slot, raw in enumerate(worn) if raw and slot in SIM_KIT_SLOTS]
        tiers = {(raw, slot): items.resolve(raw, slot) for raw, slot in ids}
        for (raw, slot), tier in tiers.items():
            if tier == "unknown":
                unknown_ids[(raw, slot)] += 1
            elif tier == "stat":
                stat_ids[(raw, slot)] += 1
        if any(t == "stat" for t in tiers.values()):
            stat_fights += 1
        if any(t == "unknown" for (r, s), t in tiers.items() if s == WEAPON):
            weapon_unknown_fights += 1
        if any(t == "stat" for (r, s), t in tiers.items() if s == WEAPON):
            weapon_stat_fights += 1
        if any(items.resolve(s, WEAPON) != "fixed" and f.specs.get(s) for s in f.specs):
            nospec_fights += 1
    w("\n## What the sim item table cannot represent\n")
    w("Tiers per raw id: `fixed` = hand-written ITEM_DATABASE row (or gear alias) and, for a weapon, an attack style; `stat` = priced from `gs_java_items.h` at run time (no special attack, no effect); "
      "`unknown` = neither (the live server falls back to a prior: nearest same-style special-free weapon / bare slot), also a fixed weapon row with no attack style. "
      "The `unknown` field of a pool row counts the distinct unknown ids of the row.\n")
    w(f"- fights with at least one unknown id (any slot): {unknown_fights} of {n_fight} ({100*unknown_fights/max(1,n_fight):.1f}%) in {len(unknown_rows)} of {len(rows)} setups")
    w(f"- fights with an unknown WEAPON (main or switch): {weapon_unknown_fights}; with a stat-priced weapon (no special attack): {weapon_stat_fights}; "
      f"fights with any stat-priced id: {stat_fights}; fights in which a spec was swung with a weapon that is not `fixed`: {nospec_fights}\n")
    if unknown_ids:
        w(table(["raw id", "name", "slot", "fights"], [(raw, name(raw), KIT_NAMES[slot], n) for (raw, slot), n in sorted(unknown_ids.items(), key=lambda kv: (-kv[1], kv[0]))]))
    if stat_ids:
        w("\nStat-priced only (no hand-written row):\n")
        w(table(["raw id", "name", "slot", "fights"], [(raw, name(raw), KIT_NAMES[slot], n) for (raw, slot), n in sorted(stat_ids.items(), key=lambda kv: (-kv[1], kv[0]))]))
    # armour change inside a fight
    changed = sum(1 for f in usable if f.armour_changes())
    w(f"\nFights in which he wore more than one id in a slot other than weapon and shield (armour switch or kit read change): {changed} of {n_fight}. "
      "The row keeps the id worn longest.\n")
    # visible weapon x archetype -> specs
    w("## Given visible weapon and armour archetype: which spec weapons does he carry?\n")
    w("Group = (main weapon, armour archetype). Counts are fights; `spec` = a spec animation with that weapon was seen, `switch` = the weapon was held or swung besides the main one.\n")
    groups = collections.defaultdict(list)
    for f in usable:
        groups[(f.main_weapon(), archetype(items, f.worn()))].append(f)
    order = sorted(groups, key=lambda k: (-len(groups[k]), k[0], k[1]))[:20]
    rows_g = []
    for key in order:
        fs = groups[key]
        sp = collections.Counter(wid for f in fs for wid in f.specs)
        sw = collections.Counter(wid for f in fs for wid in f.other_weapons())
        rows_g.append((f"{name(key[0])} ({key[0]})", key[1], len(fs), sum(1 for f in fs if f.specs),
                       ", ".join(f"{name(s)} {c}" for s, c in top(sp)) or "-", ", ".join(f"{name(s)} {c}" for s, c in top(sw, 5)) or "-"))
    w(table(["main weapon", "armour", "fights", "fights with spec", "spec weapons (fights)", "switch weapons (fights)"], rows_g))
    covered = sum(len(groups[k]) for k in order)
    w(f"\nThese 20 groups cover {covered} of {n_fight} fights; {len(groups)} groups in all.\n")
    # own fights only
    own = collections.defaultdict(list)
    for f in tape_use:
        own[(f.main_weapon(), archetype(items, f.worn()))].append(f)
    w("Own tape fights only (the opponents we actually met, {} fights):\n".format(len(tape_use)))
    rows_o = []
    for key in sorted(own, key=lambda k: (-len(own[k]), k[0], k[1]))[:15]:
        fs = own[key]
        sp = collections.Counter(wid for f in fs for wid in f.specs)
        rows_o.append((f"{name(key[0])} ({key[0]})", key[1], len(fs), sum(1 for f in fs if f.specs), ", ".join(f"{name(s)} {c}" for s, c in top(sp)) or "-"))
    w(table(["main weapon", "armour", "fights", "fights with spec", "spec weapons (fights)"], rows_o))
    w("")
    # first-seen
    first = collections.defaultdict(list)
    for f in usable:
        first[(f.first_weapon, archetype(items, f.worn()))].append(f)
    w("Same table keyed by the weapon he was first seen holding (what a live prior sees at the start):\n")
    rows_f = []
    for key in sorted(first, key=lambda k: (-len(first[k]), k[0], k[1]))[:20]:
        fs = first[key]
        sp = collections.Counter(wid for f in fs for wid in f.specs)
        rows_f.append((f"{name(key[0])} ({key[0]})", key[1], len(fs), sum(1 for f in fs if f.specs), ", ".join(f"{name(s)} {c}" for s, c in top(sp)) or "-"))
    w(table(["first weapon", "armour", "fights", "fights with spec", "spec weapons (fights)"], rows_f))
    # top 30 rows
    w("\n## Top 30 setups\n")
    rows30 = []
    for row in rows[:30]:
        worn = row["worn"]
        rows30.append((sum(len(v) for v in row["sources"].values()), source_string(row), f"{name(row['weapon'])} ({row['weapon']})",
                       ", ".join(f"{name(s)} ({s})" for s in row["switches"]) or "-",
                       " / ".join(f"{KIT_NAMES[s]} {name(worn[s])}" for s in (0, 1, 2, 4, 5, 7, 9, 10) if worn[s]), row["unknown"]))
    w(table(["fights", "source", "weapon", "switches", "worn", "unknown"], rows30))
    w(f"\nSetups with a switch overflow (more than {POOL_SWITCHES} other weapons, the rest dropped): {sum(1 for r in rows if r['overflow'])}; "
      f"setups seen once: {sum(1 for r in rows if sum(len(v) for v in r['sources'].values()) == 1)} of {len(rows)}.\n")
    # per-fight list
    w("## Per fight (tapes)\n")
    w(table(["tape", "opponent", "main weapon", "spec weapons", "archetype"], [
        (f.file, f.label.split(' ', 1)[1], name(f.main_weapon()), ", ".join(name(s) for s in f.specs) or "-", archetype(items, f.worn())) for f in tape_use]))
    return "\n".join(L) + "\n"


# ---------------------------------------------------------------- main

def run(args):
    items = Items(args.items_generated, args.items, args.java_items, args.names)
    tapes = load_tapes(args.tapes, items)
    duel_fights, duels = parse_duels(args.duels, items)
    rows, usable = build(tapes, duel_fights, items)
    return items, tapes, duel_fights, duels, rows, usable


def labels(args):
    tape_label = "tapes " + " ".join(display(t) for t in args.tapes)
    return tape_label, "duels " + display(args.duels)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tapes", nargs="+", default=[DEFAULT_TAPES], help="tape directories or files (default: the C tape store)")
    ap.add_argument("--duels", default=DEFAULT_DUELS, help="spectator.duels")
    ap.add_argument("--out", default=DEFAULT_OUT, help="generated header")
    ap.add_argument("--report", default=DEFAULT_REPORT, help="markdown report (not written with --check)")
    ap.add_argument("--items-generated", default=DEFAULT_ITEMS_GENERATED)
    ap.add_argument("--items", default=DEFAULT_ITEMS)
    ap.add_argument("--java-items", default=DEFAULT_JAVA_ITEMS)
    ap.add_argument("--names", nargs="+", default=DEFAULT_NAMES, help="JSON id -> {name} files for names the Java table lacks")
    ap.add_argument("--spectator-dir", help="spectator-combat recordings: adds the per-location split of the duels to the report (reads every recording, about 30 s)")
    ap.add_argument("--check", action="store_true", help="reparse the header and assert it matches the sources")
    args = ap.parse_args()
    items, tapes, duel_fights, duels, rows, usable = run(args)
    n_tape = sum(1 for f in usable if f.source == "tape")
    n_duel = sum(1 for f in usable if f.source == "duel")
    tape_label, duel_label = labels(args)
    header = render_header(rows, f"{tape_label} ({n_tape} of {len(tapes)} tapes)", f"{duel_label} ({n_duel} fighters)", n_tape, n_duel)
    if args.check:
        parsed = parse_header(read(args.out))
        expected = parse_header(header)
        if len(parsed) != len(expected):
            die(f"{args.out}: {len(parsed)} rows, sources give {len(expected)}")
        if sum(r[1] for r in parsed) != len(usable):
            die(f"{args.out}: weights sum to {sum(r[1] for r in parsed)}, sources give {len(usable)} fights")
        tape_weight = sum(len(r["sources"].get("tape", [])) for r in rows)
        duel_weight = sum(len(r["sources"].get("duel", [])) for r in rows)
        if tape_weight != n_tape or duel_weight != n_duel:
            die("internal source weights differ from the fights counted")
        if parsed != expected:
            for a, b in zip(parsed, expected):
                if a != b:
                    die(f"{args.out}: row differs from the sources:\n  header  {a}\n  sources {b}")
        if read(args.out) != header:
            die(f"{args.out}: text differs from the generator output (rerun without --check)")
        print(f"check ok: {len(parsed)} rows, {sum(r[1] for r in parsed)} fights ({n_tape} tape of {len(tapes)} tapes, {n_duel} duel fighters of {len(duel_fights)}), "
              f"unknown rows {sum(1 for r in parsed if r[5])}")
        return
    with open(args.out, "w") as fh:
        fh.write(header)
    os.makedirs(os.path.dirname(args.report), exist_ok=True)
    with open(args.report, "w") as fh:
        fh.write(report(rows, usable, tapes, duel_fights, duels, items, args,
                        spectator_locations(args.spectator_dir) if args.spectator_dir else None))
    print(f"wrote {args.out}: {len(rows)} rows, {len(usable)} fights ({n_tape} tape of {len(tapes)} tapes, {n_duel} duel fighters of {len(duel_fights)}); report {args.report}")


if __name__ == "__main__":
    main()
