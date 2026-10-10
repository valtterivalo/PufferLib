#include <inttypes.h>
#include "../osrs_env.h"

static OsrsEnv initial, env;

static void read_player(FILE* file, const char* name, Player* player) {
    char label[32];
    assert(fscanf(file, "%31s", label) == 1 && strcmp(label, name) == 0);
    int* base[] = {&player->base_attack, &player->base_strength, &player->base_defence,
        &player->base_ranged, &player->base_magic, &player->base_prayer, &player->base_hitpoints};
    int* current[] = {&player->current_attack, &player->current_strength, &player->current_defence,
        &player->current_ranged, &player->current_magic, &player->current_prayer, &player->current_hitpoints};
    for (int i = 0; i < 7; i++) assert(fscanf(file, "%d", base[i]) == 1 && *base[i] > 0);
    for (int i = 0; i < 7; i++) assert(fscanf(file, "%d", current[i]) == 1 && *current[i] > 0);
    int offensive, overhead, stance;
    assert(fscanf(file, "%d%d%d", &offensive, &overhead, &stance) == 3);
    assert(offensive >= OFFENSIVE_PRAYER_NONE && offensive <= OFFENSIVE_PRAYER_AUGURY);
    assert(overhead >= PRAYER_NONE && overhead <= PRAYER_PROTECT_MELEE);
    assert(stance >= FIGHT_STYLE_ACCURATE && stance <= FIGHT_STYLE_DEFENSIVE_AUTOCAST);
    player->offensive_prayer = (OffensivePrayer)offensive;
    player->prayer = (OverheadPrayer)overhead;
    player->fight_style = (FightStyle)stance;
    const EquipmentSlot slots[] = {SLOT_HEAD, SLOT_CAPE, SLOT_NECK, SLOT_AMMO, SLOT_WEAPON,
        SLOT_SHIELD, SLOT_BODY, SLOT_LEGS, SLOT_HANDS, SLOT_FEET, SLOT_RING};
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
        int item;
        assert(fscanf(file, "%d", &item) == 1);
        assert(item == ITEM_NONE || (item >= 0 && item < NUM_ITEMS));
        assert(item == ITEM_NONE || ITEM_DATABASE[item].slot == slots[slot]);
        player->equipped[slot] = (uint8_t)item;
    }
    assert(!item_is_two_handed(player->equipped[GEAR_SLOT_WEAPON]) ||
        player->equipped[GEAR_SLOT_SHIELD] == ITEM_NONE);
    osrs_refresh_player_equipment(player);
    pvp_refresh_visible_gear(player);
}

static void print_player(const Player* player) {
    printf("{\"base_levels\":[%d,%d,%d,%d,%d,%d,%d],\"current_levels\":[%d,%d,%d,%d,%d,%d,%d],"
        "\"offensive_prayer\":%d,\"overhead_prayer\":%d,\"stance\":%d,\"equipment\":[",
        player->base_attack, player->base_strength, player->base_defence, player->base_ranged,
        player->base_magic, player->base_prayer, player->base_hitpoints,
        player->current_attack, player->current_strength, player->current_defence, player->current_ranged,
        player->current_magic, player->current_prayer, player->current_hitpoints,
        player->offensive_prayer, player->prayer, player->fight_style);
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) printf("%s%d", slot ? "," : "", player->equipped[slot]);
    printf("],\"is_lunar_spellbook\":%d}", player->is_lunar_spellbook);
}

static void print_json_string(const char* text) {
    putchar('"');
    for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
        if (*p == '"' || *p == '\\') printf("\\%c", *p);
        else if (*p < 32) printf("\\u%04x", *p);
        else putchar(*p);
    }
    putchar('"');
}

static void print_distribution(const double* pmf, const double* native_pmf, const uint64_t* counts, int size) {
    printf("[");
    for (int i = 0; i < size; i++)
        printf("%s{\"value\":%d,\"reference_probability\":%.17g,\"native_marginal_probability\":%.17g,\"sample_count\":%" PRIu64 "}",
            i ? "," : "", i, pmf[i], native_pmf[i], counts[i]);
    printf("]");
}

int main(int argc, char** argv) {
    assert(argc == 4 || argc == 5);
    char* end;
    uint64_t samples = strtoull(argv[2], &end, 10);
    assert(end != argv[2] && *end == '\0' && samples > 0);
    uint64_t seed = strtoull(argv[3], &end, 10);
    assert(end != argv[3] && *end == '\0');
    FILE* file = fopen(argv[1], "r");
    assert(file);
    int style_value, magic_type, special, distance, reference_melee_type;
    assert(fscanf(file, "attack %d%d%d%d%d", &style_value, &magic_type, &special, &distance, &reference_melee_type) == 5);
    assert(style_value >= ATTACK_STYLE_MELEE && style_value <= ATTACK_STYLE_MAGIC);
    assert(magic_type >= 0 && magic_type <= OSRS_MAGIC_ATTACK_ANCIENT_BLOOD);
    assert(special == 0 || special == 1);
    assert(distance > 0);
    assert(reference_melee_type >= MELEE_BONUS_STAB && reference_melee_type <= MELEE_BONUS_CRUSH);
    AttackStyle style = (AttackStyle)style_value;
    for (int actor = 0; actor < NUM_AGENTS; actor++) init_player(&initial.players[actor]);
    read_player(file, "attacker", &initial.players[0]);
    read_player(file, "defender", &initial.players[1]);
    char trailing[2];
    assert(fscanf(file, "%1s", trailing) == EOF && !ferror(file));
    assert(fclose(file) == 0);
    Player* attacker = &initial.players[0];
    Player* defender = &initial.players[1];
    attacker->x = FIGHT_AREA_BASE_X + 10;
    defender->x = attacker->x + distance;
    attacker->y = defender->y = FIGHT_AREA_BASE_Y + 10;
    attacker->current_gear = special ? GEAR_SPEC : style == ATTACK_STYLE_MELEE
        ? GEAR_MELEE : style == ATTACK_STYLE_RANGED ? GEAR_RANGED : GEAR_MAGE;
    int weapon = attacker->equipped[GEAR_SLOT_WEAPON];
    assert(!special || (style == ATTACK_STYLE_MELEE &&
        (weapon == ITEM_VOIDWAKER || weapon == ITEM_GRANITE_MAUL || weapon == ITEM_GRANITE_MAUL_ORNATE)));
    assert(style != ATTACK_STYLE_MAGIC || (magic_type != 0 && !special));
    assert(style == ATTACK_STYLE_MAGIC || magic_type == 0);
    assert(style != ATTACK_STYLE_MELEE || distance <= get_attack_range(attacker, style));
    assert(style != ATTACK_STYLE_MAGIC || (magic_type == OSRS_MAGIC_ATTACK_ANCIENT_ICE
        ? can_cast_ice_spell(attacker) : can_cast_blood_spell(attacker)));
    assert(style != ATTACK_STYLE_RANGED || get_item_attack_style(weapon) == ATTACK_STYLE_RANGED);
    assert(style != ATTACK_STYLE_RANGED || attacker->equipped[GEAR_SLOT_AMMO] == ITEM_NONE);
    assert(attacker->equipment_effect_profile.dharok_piece_count < 4);
    assert(weapon != ITEM_ZURIELS_STAFF);
    uint32_t unsupported = OSRS_ITEM_EFFECT_ELYSIAN | OSRS_ITEM_EFFECT_RECOIL_RING |
        OSRS_ITEM_EFFECT_ECHO_BOOTS | OSRS_ITEM_EFFECT_BLOOD_FURY | OSRS_ITEM_EFFECT_SANG_HEAL;
    assert(!(attacker->equipment_effect_profile.effect_mask & (unsupported |
        OSRS_ITEM_EFFECT_TWISTED_BOW | OSRS_ITEM_EFFECT_CRYSTAL_ARMOUR | OSRS_ITEM_EFFECT_DHAROK_PIECE |
        OSRS_ITEM_EFFECT_VIRTUS_PIECE | OSRS_ITEM_EFFECT_FANG | OSRS_ITEM_EFFECT_TUMEKENS_SHADOW |
        OSRS_ITEM_EFFECT_DRAGON_HUNTER_WAND | OSRS_ITEM_EFFECT_VENATOR_BOUNCE | OSRS_ITEM_EFFECT_CONFLICTION)));
    assert(!(defender->equipment_effect_profile.effect_mask & unsupported));

    int attack_bonus[NUM_AGENTS][3] = {{0}}, defence_bonus[NUM_AGENTS][3] = {{0}};
    int ranged_attack[NUM_AGENTS] = {0}, magic_attack[NUM_AGENTS] = {0};
    int ranged_defence[NUM_AGENTS] = {0}, magic_defence[NUM_AGENTS] = {0};
    int strength[NUM_AGENTS][3] = {{0}};
    for (int actor = 0; actor < NUM_AGENTS; actor++) {
        for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
            int item = initial.players[actor].equipped[slot];
            if (item == ITEM_NONE) continue;
            const Item* gear = &ITEM_DATABASE[item];
            attack_bonus[actor][0] += gear->attack_stab;
            attack_bonus[actor][1] += gear->attack_slash;
            attack_bonus[actor][2] += gear->attack_crush;
            defence_bonus[actor][0] += gear->defence_stab;
            defence_bonus[actor][1] += gear->defence_slash;
            defence_bonus[actor][2] += gear->defence_crush;
            ranged_attack[actor] += gear->attack_ranged;
            magic_attack[actor] += gear->attack_magic;
            ranged_defence[actor] += gear->defence_ranged;
            magic_defence[actor] += gear->defence_magic;
            strength[actor][0] += gear->melee_strength;
            strength[actor][1] += gear->ranged_strength;
            strength[actor][2] += gear->magic_damage;
        }
    }
    int melee_type = special ? (weapon == ITEM_VOIDWAKER ? MELEE_BONUS_SLASH : MELEE_BONUS_CRUSH) : MELEE_BONUS_STAB;
    if (!special) for (int i = 1; i < 3; i++) if (attack_bonus[0][i] > attack_bonus[0][melee_type]) melee_type = i;
    int attack_level = style == ATTACK_STYLE_MELEE ? attacker->current_attack
        : style == ATTACK_STYLE_RANGED ? attacker->current_ranged : attacker->current_magic;
    int strength_level = style == ATTACK_STYLE_MELEE ? attacker->current_strength : attacker->current_ranged;
    float attack_multiplier = 1.0f, strength_multiplier = 1.0f;
    if ((style == ATTACK_STYLE_MELEE && attacker->offensive_prayer == OFFENSIVE_PRAYER_PIETY) ||
        (style == ATTACK_STYLE_RANGED && attacker->offensive_prayer == OFFENSIVE_PRAYER_RIGOUR)) {
        attack_multiplier = 1.20f;
        strength_multiplier = 1.23f;
    } else if (style == ATTACK_STYLE_MAGIC && attacker->offensive_prayer == OFFENSIVE_PRAYER_AUGURY)
        attack_multiplier = 1.25f;
    else if ((style == ATTACK_STYLE_MELEE && attacker->offensive_prayer == OFFENSIVE_PRAYER_MELEE_LOW) ||
        (style == ATTACK_STYLE_RANGED && attacker->offensive_prayer == OFFENSIVE_PRAYER_RANGED_LOW) ||
        (style == ATTACK_STYLE_MAGIC && attacker->offensive_prayer == OFFENSIVE_PRAYER_MAGIC_LOW)) {
        attack_multiplier = 1.15f;
        if (style != ATTACK_STYLE_MAGIC) strength_multiplier = 1.15f;
    }
    int attack_stance = attacker->fight_style == FIGHT_STYLE_ACCURATE ? (style == ATTACK_STYLE_MAGIC ? 2 : 3)
        : attacker->fight_style == FIGHT_STYLE_CONTROLLED && style == ATTACK_STYLE_MELEE ? 1 : 0;
    int strength_stance = style == ATTACK_STYLE_MELEE ? (attacker->fight_style == FIGHT_STYLE_AGGRESSIVE ? 3
        : attacker->fight_style == FIGHT_STYLE_CONTROLLED ? 1 : 0) : 0;
    int defence_stance = defender->fight_style == FIGHT_STYLE_DEFENSIVE || defender->fight_style == FIGHT_STYLE_LONGRANGE ? 3
        : defender->fight_style == FIGHT_STYLE_CONTROLLED ? 1 : 0;
    float defence_multiplier = defender->offensive_prayer >= OFFENSIVE_PRAYER_PIETY ? 1.25f
        : defender->offensive_prayer != OFFENSIVE_PRAYER_NONE ? 1.15f : 1.0f;
    int effective_attack = (int)(attack_level * attack_multiplier) + attack_stance + (style == ATTACK_STYLE_MAGIC ? 9 : 8);
    int effective_defence = (int)(defender->current_defence * defence_multiplier) + defence_stance + 8;
    if (style == ATTACK_STYLE_MAGIC) {
        float magic_multiplier = defender->offensive_prayer == OFFENSIVE_PRAYER_AUGURY ? 1.25f
            : defender->offensive_prayer == OFFENSIVE_PRAYER_MAGIC_LOW ? 1.15f : 1.0f;
        effective_defence = (int)((int)(defender->current_magic * magic_multiplier) * 0.7f +
            (int)(defender->current_defence * defence_multiplier) * 0.3f) + defence_stance + 8;
    }
    int att_bonus = style == ATTACK_STYLE_MELEE ? attack_bonus[0][melee_type]
        : style == ATTACK_STYLE_RANGED ? ranged_attack[0] : magic_attack[0];
    int def_bonus = style == ATTACK_STYLE_MELEE ? defence_bonus[1][melee_type]
        : style == ATTACK_STYLE_RANGED ? ranged_defence[1] : magic_defence[1];
    assert(att_bonus >= -64 && def_bonus >= -64);
    int attack_roll = effective_attack * (att_bonus + 64);
    int reference_attack_roll = (effective_attack - (style == ATTACK_STYLE_MAGIC ? attack_stance : 0)) *
        ((style == ATTACK_STYLE_MELEE ? attack_bonus[0][reference_melee_type] : att_bonus) + 64);
    int defence_roll = effective_defence * (def_bonus + 64);
    int reference_defence_roll = effective_defence *
        ((style == ATTACK_STYLE_MELEE ? defence_bonus[1][reference_melee_type] : def_bonus) + 64);
    int max_hit = ((int)(strength_level * strength_multiplier) + strength_stance + 8) *
        (strength[0][style == ATTACK_STYLE_RANGED ? 1 : 0] + 64);
    max_hit = (max_hit + 320) / 640;
    int reference_max_hit = max_hit;
    if (style == ATTACK_STYLE_RANGED) {
        int ranged_stance = attacker->fight_style == FIGHT_STYLE_ACCURATE ? 3 : 0;
        reference_max_hit = (((int)(strength_level * strength_multiplier) + ranged_stance + 8) *
            (strength[0][1] + 64) + 320) / 640;
    }
    if (style == ATTACK_STYLE_MAGIC) {
        int level = attacker->current_magic;
        int base_hit = magic_type == OSRS_MAGIC_ATTACK_ANCIENT_ICE
            ? (level >= ICE_BARRAGE_LEVEL ? ICE_BARRAGE_MAX_HIT : level >= ICE_BLITZ_LEVEL ? ICE_BLITZ_MAX_HIT
                : level >= ICE_BURST_LEVEL ? ICE_BURST_MAX_HIT : ICE_RUSH_MAX_HIT)
            : (level >= BLOOD_BARRAGE_LEVEL ? BLOOD_BARRAGE_MAX_HIT : level >= BLOOD_BLITZ_LEVEL ? BLOOD_BLITZ_MAX_HIT
                : level >= BLOOD_BURST_LEVEL ? BLOOD_BURST_MAX_HIT : BLOOD_RUSH_MAX_HIT);
        reference_max_hit = base_hit * (100 + strength[0][2] +
            (attacker->offensive_prayer == OFFENSIVE_PRAYER_AUGURY ? 4
                : attacker->offensive_prayer == OFFENSIVE_PRAYER_MAGIC_LOW ? 2 : 0)) / 100;
        max_hit = (int)(base_hit * (1.0f + strength[0][2] / 100.0f) *
            (attacker->offensive_prayer == OFFENSIVE_PRAYER_AUGURY ? 1.04f : 1.0f));
    }
    assert(max_hit > 0 && reference_max_hit > 0);
    float continuous_accuracy = attack_roll > defence_roll
        ? 1.0f - (float)(defence_roll + 2) / (2.0f * (attack_roll + 1))
        : (float)attack_roll / (2.0f * (defence_roll + 1));
    uint64_t numerator = attack_roll > defence_roll ? 2ull * attack_roll - defence_roll : attack_roll;
    uint64_t denominator = attack_roll > defence_roll ? 2ull * (attack_roll + 1) : 2ull * (defence_roll + 1);
    int successes = 0;
    for (uint64_t roll = 0; roll < 65536; roll++) successes += special
        ? roll * denominator < numerator * 65536 : (float)roll / 65536.0f < continuous_accuracy;
    double quantized_accuracy = special && weapon == ITEM_VOIDWAKER ? 1 : successes / 65536.0;
    double native_accuracy_marginal = special && weapon == ITEM_VOIDWAKER ? 1
        : ((uint64_t)successes * 65536 - (numerator > 0)) / (double)UINT32_MAX;
    double accuracy = special && weapon == ITEM_VOIDWAKER ? 1 : reference_attack_roll > reference_defence_roll
        ? 1 - (double)(reference_defence_roll + 2) / (2.0 * (reference_attack_roll + 1))
        : (double)reference_attack_roll / (2.0 * (reference_defence_roll + 1));
    int minimum = special && weapon == ITEM_VOIDWAKER ? max_hit / 2 : 0;
    int maximum = special && weapon == ITEM_VOIDWAKER ? max_hit * 3 / 2 : max_hit;
    int reference_minimum = special && weapon == ITEM_VOIDWAKER ? reference_max_hit / 2 : 0;
    int reference_maximum = special && weapon == ITEM_VOIDWAKER ? reference_max_hit * 3 / 2 : reference_max_hit;
    int size = (maximum > reference_maximum ? maximum : reference_maximum) + 1;
    double* raw_pmf = calloc((size_t)size, sizeof(double));
    double* native_raw_pmf = calloc((size_t)size, sizeof(double));
    double* native_damage_pmf = calloc((size_t)size, sizeof(double));
    double* native_heal_pmf = calloc((size_t)size, sizeof(double));
    double* damage_pmf = calloc((size_t)size, sizeof(double));
    double* heal_pmf = calloc((size_t)size, sizeof(double));
    uint64_t* raw_counts = calloc((size_t)size, sizeof(uint64_t));
    uint64_t* damage_counts = calloc((size_t)size, sizeof(uint64_t));
    uint64_t* heal_counts = calloc((size_t)size, sizeof(uint64_t));
    assert(raw_pmf && damage_pmf && heal_pmf && native_raw_pmf && native_damage_pmf && native_heal_pmf && raw_counts && damage_counts && heal_counts);
    raw_pmf[0] = 1 - accuracy;
    native_raw_pmf[0] = 1 - native_accuracy_marginal;
    for (int raw = reference_minimum; raw <= reference_maximum; raw++)
        raw_pmf[raw == 0 && !(special && weapon == ITEM_VOIDWAKER) ? 1 : raw] +=
            accuracy / (reference_maximum - reference_minimum + 1);
    uint64_t draw_size = (uint64_t)(maximum - minimum + 1);
    for (int raw = minimum; raw <= maximum; raw++) {
        int damage = !special && raw == 0 ? 1 : raw;
        uint64_t residue = (uint64_t)(raw - minimum);
        uint64_t multiplicity = UINT32_MAX / draw_size + (residue > 0 && residue <= UINT32_MAX % draw_size);
        native_raw_pmf[damage] += native_accuracy_marginal * multiplicity / UINT32_MAX;
    }
    AttackStyle damage_style = special && weapon == ITEM_VOIDWAKER ? ATTACK_STYLE_MAGIC : style;
    int protected = (damage_style == ATTACK_STYLE_MELEE && defender->prayer == PRAYER_PROTECT_MELEE) ||
        (damage_style == ATTACK_STYLE_RANGED && defender->prayer == PRAYER_PROTECT_RANGED) ||
        (damage_style == ATTACK_STYLE_MAGIC && defender->prayer == PRAYER_PROTECT_MAGIC);
    int missing_hp = attacker->base_hitpoints > attacker->current_hitpoints
        ? attacker->base_hitpoints - attacker->current_hitpoints : 0;
    for (int raw = 0; raw < size; raw++) {
        int native_damage = protected ? (int)(raw * 0.6f) : raw;
        int damage = protected ? raw * 3 / 5 : raw;
        int heal = magic_type == OSRS_MAGIC_ATTACK_ANCIENT_BLOOD ? damage / 4 : 0;
        if (heal > missing_hp) heal = missing_hp;
        damage_pmf[damage] += raw_pmf[raw];
        heal_pmf[heal] += raw_pmf[raw];
        int native_heal = magic_type == OSRS_MAGIC_ATTACK_ANCIENT_BLOOD ? native_damage / 4 : 0;
        if (native_heal > missing_hp) native_heal = missing_hp;
        native_damage_pmf[native_damage] += native_raw_pmf[raw];
        native_heal_pmf[native_heal] += native_raw_pmf[raw];
    }
    uint64_t stream = seed, successful = 0, accurate_zero_maul = 0;
    uint64_t accurate_zero_landed_zero = 0, accurate_zero_flag_false = 0;
    uint32_t first_accurate_zero_seed = 0;
    for (uint64_t sample = 0; sample < samples; sample++) {
        env = initial;
        stream += UINT64_C(0x9e3779b97f4a7c15);
        uint64_t mixed = stream;
        mixed = (mixed ^ (mixed >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        mixed = (mixed ^ (mixed >> 27)) * UINT64_C(0x94d049bb133111eb);
        mixed ^= mixed >> 31;
        env.rng_state = 1 + mixed % UINT32_MAX;
        uint32_t initial_rng = env.rng_state;
        perform_attack(&env, 0, 1, style, special, magic_type, distance);
        assert(env.players[0].just_attacked && env.players[0].num_pending_hits == 1);
        PendingHit hit = env.players[0].pending_hits[0];
        int accurate_zero_draw = 0;
        if (special && weapon != ITEM_VOIDWAKER) {
            uint32_t roll = initial_rng;
            roll ^= roll << 13; roll ^= roll >> 17; roll ^= roll << 5;
            int accurate = (uint64_t)(roll & 65535) * denominator < numerator * 65536;
            roll ^= roll << 13; roll ^= roll >> 17; roll ^= roll << 5;
            if (accurate && roll % (max_hit + 1) == 0) {
                accurate_zero_draw = 1;
                if (accurate_zero_maul++ == 0) first_accurate_zero_seed = initial_rng;
            }
        }
        assert(hit.damage >= 0 && hit.damage < size && native_raw_pmf[hit.damage] > 0);
        successful += hit.hit_success;
        raw_counts[hit.damage]++;
        for (int tick = 0; tick <= hit.ticks_until_hit; tick++) pvp_process_incoming_hits(&env, 1);
        int damage = env.players[1].hit_damage;
        int heal = env.players[0].current_hitpoints - attacker->current_hitpoints;
        accurate_zero_landed_zero += accurate_zero_draw && damage == 0;
        accurate_zero_flag_false += accurate_zero_draw && hit.hit_success == 0;
        int expected_damage = protected ? (int)(hit.damage * 0.6f) : hit.damage;
        int expected_heal = magic_type == OSRS_MAGIC_ATTACK_ANCIENT_BLOOD ? damage / 4 : 0;
        if (expected_heal > missing_hp) expected_heal = missing_hp;
        assert(damage == expected_damage && heal == expected_heal);
        damage_counts[damage]++;
        heal_counts[heal]++;
    }
    printf("{\"scenario_path\":");
    print_json_string(argv[1]);
    printf(",\"schema\":2,\"samples\":%" PRIu64 ",\"seed\":%" PRIu64 ",\"scenario\":{\"style\":%d,"
        "\"magic_type\":%d,\"special\":%d,\"distance\":%d,\"melee_bonus_type\":%d,\"attacker\":", samples, seed, style, magic_type, special, distance, reference_melee_type);
    print_player(attacker);
    printf(",\"defender\":");
    print_player(defender);
    printf("},\"reference\":{\"attack_roll\":%d,\"defence_roll\":%d,\"max_hit\":%d,\"melee_bonus_type\":%d,"
        "\"accuracy\":%.17g,\"native_quantized_accuracy\":%.17g,\"native_accuracy_marginal\":%.17g,\"native_hit_success_flags\":%" PRIu64 ",\"protected\":%d,\"raw_damage\":",
        reference_attack_roll, reference_defence_roll, reference_max_hit, reference_melee_type, accuracy, quantized_accuracy, native_accuracy_marginal, successful, protected);
    print_distribution(raw_pmf, native_raw_pmf, raw_counts, size);
    printf(",\"landed_damage\":");
    print_distribution(damage_pmf, native_damage_pmf, damage_counts, size);
    printf(",\"blood_healing\":");
    print_distribution(heal_pmf, native_heal_pmf, heal_counts, size);
    printf("},\"native_consistency\":{\"attack_roll\":%d,\"defence_roll\":%d,\"max_hit\":%d,\"melee_bonus_type\":%d,"
        "\"accurate_raw_zero_maul_draws\":%" PRIu64 ",\"accurate_raw_zero_landed_zero\":%" PRIu64 ","
        "\"accurate_raw_zero_flag_false\":%" PRIu64 ",\"first_accurate_zero_maul_initial_rng\":%" PRIu32 "},",
        attack_roll, defence_roll, max_hit, melee_type, accurate_zero_maul, accurate_zero_landed_zero,
        accurate_zero_flag_false, first_accurate_zero_seed);
    printf("\"reference_assumptions\":{\"reference_probability\":\"externally_specified_ideal_independent_draws\",\"native_marginal_probability\":\"exact_nonzero32bit_accuracy_and_modulo_marginals_product_using_native_mechanics\",\"damage_draw\":\"ideal_uniform_integer\","
        "\"native_seed_draw\":\"splitmix64_to_nonzero_xorshift32_per_attack\",\"live_inputs\":\"declared_not_inferred\","
        "\"native_hit_success_flags\":\"engine_flag;_maul_special_zero_damage_flag_is_false_not_independent_accuracy_measure\","
        "\"manual_magic_stance\":\"manual_cast_reference_has_no_accurate_powered_staff_plus2;_live_manual_mapping_requires_confirmation\","
        "\"item_stats\":\"declared_canonical_simulator_table_not_independently_recovered_live_equipment\","
        "\"defence_rounding\":\"declared_PvP_weighted_magic_defence_model_not_exact_server_integer_order_proof\","
        "\"rng_limit\":\"sequential_accuracy_damage_dependence_not_exhaustively_enumerated;_native_marginals_are_not_exact_joint_PMF\"},"
        "\"reference_sources\":[\"https://secure.runescape.com/m=news/project-rebalance-combat-changes?oldschool=1\","
        "\"https://secure.runescape.com/m=news/wilderness-boss-rework?oldschool=1\","
        "\"https://raw.githubusercontent.com/weirdgloop/osrs-dps-calc/89c3e25b344aea90d0189746e4b5f73dde0f0383/src/lib/PlayerVsNPCCalc.ts\","
        "\"https://oldschool.runescape.wiki/w/Blood_Barrage\",\"https://oldschool.runescape.wiki/w/Protect_from_Magic\","
        "\"https://oldschool.runescape.wiki/w/Damage_per_second/Magic\",\"https://oldschool.runescape.wiki/w/Damage_per_second/Melee\"]");
    if (argc == 5) {
        file = fopen(argv[4], "r");
        assert(file);
        uint64_t count = 0, impossible = 0;
        double log_likelihood = 0;
        printf(",\"observations\":{\"source_path\":");
        print_json_string(argv[4]);
        printf(",\"values\":[");
        int damage, result;
        while ((result = fscanf(file, "%d", &damage)) == 1) {
            assert(damage >= 0);
            double probability = damage < size ? damage_pmf[damage] : 0;
            printf("%s{\"damage\":%d,\"reference_probability\":%.17g}", count ? "," : "", damage, probability);
            count++;
            impossible += probability == 0;
            if (probability > 0) log_likelihood += log(probability);
        }
        assert(result == EOF && !ferror(file) && fclose(file) == 0);
        printf("],\"count\":%" PRIu64 ",\"outside_support\":%" PRIu64 ",\"log_likelihood\":", count, impossible);
        if (impossible) printf("null"); else printf("%.17g", log_likelihood);
        printf("}");
    }
    printf("}\n");
    free(raw_pmf); free(damage_pmf); free(heal_pmf);
    free(native_raw_pmf); free(native_damage_pmf); free(native_heal_pmf);
    free(raw_counts); free(damage_counts); free(heal_counts);
    return 0;
}
