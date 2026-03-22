/**
 * @file ocean_binding.c
 * @brief PufferLib ocean-compatible C binding for OSRS PvP environment
 *
 * Replaces the legacy binding.c with PufferLib's native env_binding.h
 * template. All N environments are managed in C with a single vec_step
 * call per batch, eliminating per-env Python overhead.
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "osrs_pvp.h"
#include "osrs_encounter.h"
#include "encounters/encounter_nh_pvp.h"
#include "encounters/encounter_zulrah.h"

#define Env OsrsPvp

/* shared collision map: loaded once, read-only, shared across all envs */
static CollisionMap* g_collision_map = NULL;

static void dict_set_int(PyObject* dict, const char* key, int value) {
    PyObject* obj = PyLong_FromLong(value);
    PyDict_SetItemString(dict, key, obj);
    Py_DECREF(obj);
}

static void dict_set_bool(PyObject* dict, const char* key, int value) {
    PyObject* obj = PyBool_FromLong(value ? 1 : 0);
    PyDict_SetItemString(dict, key, obj);
    Py_DECREF(obj);
}

static PyObject* build_inventory_list(Player* p, int slot) {
    int count = p->num_items_in_slot[slot];
    if (count < 0) count = 0;
    if (count > MAX_ITEMS_PER_SLOT) count = MAX_ITEMS_PER_SLOT;
    PyObject* list = PyList_New(count);
    for (int i = 0; i < count; i++) {
        PyList_SetItem(list, i, PyLong_FromLong(p->inventory[slot][i]));
    }
    return list;
}

static PyObject* build_obs_with_mask(Env* env) {
    ensure_obs_norm_initialized();
    PyObject* obs_list = PyList_New(NUM_AGENTS);

    for (int agent = 0; agent < NUM_AGENTS; agent++) {
        PyObject* agent_obs = PyList_New(OCEAN_OBS_SIZE);
        float* obs = env->observations + agent * SLOT_NUM_OBSERVATIONS;
        unsigned char* mask = env->action_masks + agent * ACTION_MASK_SIZE;

        for (int i = 0; i < SLOT_NUM_OBSERVATIONS; i++) {
            PyList_SetItem(agent_obs, i, PyFloat_FromDouble((double)(obs[i] / OBS_NORM_DIVISORS[i])));
        }
        for (int i = 0; i < ACTION_MASK_SIZE; i++) {
            PyList_SetItem(agent_obs, SLOT_NUM_OBSERVATIONS + i, PyFloat_FromDouble((double)mask[i]));
        }
        PyList_SetItem(obs_list, agent, agent_obs);
    }

    return obs_list;
}

static PyObject* build_pending_actions(Env* env) {
    PyObject* actions = PyList_New(NUM_AGENTS);
    for (int agent = 0; agent < NUM_AGENTS; agent++) {
        PyObject* agent_actions = PyList_New(NUM_ACTION_HEADS);
        int* src = env->pending_actions + agent * NUM_ACTION_HEADS;
        for (int i = 0; i < NUM_ACTION_HEADS; i++) {
            PyList_SetItem(agent_actions, i, PyLong_FromLong(src[i]));
        }
        PyList_SetItem(actions, agent, agent_actions);
    }
    return actions;
}

static PyObject* build_executed_actions(Env* env) {
    PyObject* actions = PyList_New(NUM_AGENTS);
    for (int agent = 0; agent < NUM_AGENTS; agent++) {
        PyObject* agent_actions = PyList_New(NUM_ACTION_HEADS);
        int* src = env->last_executed_actions + agent * NUM_ACTION_HEADS;
        for (int i = 0; i < NUM_ACTION_HEADS; i++) {
            PyList_SetItem(agent_actions, i, PyLong_FromLong(src[i]));
        }
        PyList_SetItem(actions, agent, agent_actions);
    }
    return actions;
}

static PyObject* build_player_state(Player* p) {
    PyObject* dict = PyDict_New();

    dict_set_int(dict, "x", p->x);
    dict_set_int(dict, "y", p->y);
    dict_set_int(dict, "hp", p->current_hitpoints);
    dict_set_int(dict, "max_hp", p->base_hitpoints);
    dict_set_int(dict, "prayer", p->current_prayer);
    dict_set_int(dict, "max_prayer", p->base_prayer);
    dict_set_int(dict, "spec_energy", p->special_energy);
    dict_set_int(dict, "frozen_ticks", p->frozen_ticks);
    dict_set_int(dict, "freeze_immunity_ticks", p->freeze_immunity_ticks);
    dict_set_int(dict, "gear", p->visible_gear);
    dict_set_int(dict, "overhead_prayer", p->prayer);
    dict_set_int(dict, "offensive_prayer", p->offensive_prayer);
    dict_set_int(dict, "attack_timer", p->attack_timer);
    dict_set_int(dict, "food_timer", p->food_timer);
    dict_set_int(dict, "potion_timer", p->potion_timer);
    dict_set_int(dict, "karambwan_timer", p->karambwan_timer);
    dict_set_int(dict, "veng_active", p->veng_active);
    dict_set_int(dict, "veng_cooldown", p->veng_cooldown);
    dict_set_int(dict, "food_count", p->food_count);
    dict_set_int(dict, "karambwan_count", p->karambwan_count);
    dict_set_int(dict, "brew_doses", p->brew_doses);
    dict_set_int(dict, "restore_doses", p->restore_doses);
    dict_set_int(dict, "ranged_potion_doses", p->ranged_potion_doses);
    dict_set_int(dict, "combat_potion_doses", p->combat_potion_doses);
    dict_set_int(dict, "damage_this_tick", p->damage_applied_this_tick);
    dict_set_int(dict, "last_attack_style", p->last_attack_style);
    dict_set_int(dict, "just_attacked", p->just_attacked);
    dict_set_int(dict, "attack_was_on_prayer", p->attack_was_on_prayer);
    dict_set_int(dict, "last_queued_hit_damage", p->last_queued_hit_damage);

    dict_set_int(dict, "hit_landed_this_tick", p->hit_landed_this_tick);
    dict_set_int(dict, "hit_attacker_idx", p->hit_attacker_idx);
    dict_set_int(dict, "hit_style", p->hit_style);
    dict_set_int(dict, "hit_damage", p->hit_damage);
    dict_set_int(dict, "hit_was_successful", p->hit_was_successful);
    dict_set_int(dict, "hit_was_on_prayer", p->hit_was_on_prayer);
    dict_set_int(dict, "freeze_applied_this_tick", p->freeze_applied_this_tick);

    dict_set_int(dict, "weapon", p->equipped[GEAR_SLOT_WEAPON]);
    dict_set_int(dict, "helm", p->equipped[GEAR_SLOT_HEAD]);
    dict_set_int(dict, "body", p->equipped[GEAR_SLOT_BODY]);
    dict_set_int(dict, "legs", p->equipped[GEAR_SLOT_LEGS]);
    dict_set_int(dict, "shield", p->equipped[GEAR_SLOT_SHIELD]);
    dict_set_int(dict, "cape", p->equipped[GEAR_SLOT_CAPE]);
    dict_set_int(dict, "neck", p->equipped[GEAR_SLOT_NECK]);
    dict_set_int(dict, "ring", p->equipped[GEAR_SLOT_RING]);
    dict_set_int(dict, "feet", p->equipped[GEAR_SLOT_FEET]);

    // Export inventory for all dynamic gear slots
    static const int EXPORT_SLOTS[] = {
        GEAR_SLOT_WEAPON, GEAR_SLOT_SHIELD, GEAR_SLOT_BODY, GEAR_SLOT_LEGS,
        GEAR_SLOT_HEAD, GEAR_SLOT_CAPE, GEAR_SLOT_NECK, GEAR_SLOT_RING
    };
    static const char* EXPORT_NAMES[] = {
        "weapon_inventory", "shield_inventory", "body_inventory", "legs_inventory",
        "head_inventory", "cape_inventory", "neck_inventory", "ring_inventory"
    };
    for (int i = 0; i < 8; i++) {
        PyObject* inv = build_inventory_list(p, EXPORT_SLOTS[i]);
        PyDict_SetItemString(dict, EXPORT_NAMES[i], inv);
        Py_DECREF(inv);
    }

    dict_set_int(dict, "base_attack", p->base_attack);
    dict_set_int(dict, "base_strength", p->base_strength);
    dict_set_int(dict, "base_defence", p->base_defence);
    dict_set_int(dict, "base_ranged", p->base_ranged);
    dict_set_int(dict, "base_magic", p->base_magic);
    dict_set_int(dict, "current_attack", p->current_attack);
    dict_set_int(dict, "current_strength", p->current_strength);
    dict_set_int(dict, "current_defence", p->current_defence);
    dict_set_int(dict, "current_ranged", p->current_ranged);
    dict_set_int(dict, "current_magic", p->current_magic);

    return dict;
}

static PyObject* my_get(PyObject* dict, Env* env) {
    dict_set_int(dict, "tick", env->tick);
    dict_set_bool(dict, "episode_over", env->episode_over);
    dict_set_int(dict, "winner", env->winner);
    dict_set_int(dict, "pid_holder", env->pid_holder);
    dict_set_bool(dict, "opponent_read_this_tick", env->opponent.has_read_this_tick);
    dict_set_int(dict, "opponent_read_style", env->opponent.read_agent_style);

    PyObject* p0 = build_player_state(&env->players[0]);
    PyObject* p1 = build_player_state(&env->players[1]);
    PyDict_SetItemString(dict, "player0", p0);
    PyDict_SetItemString(dict, "player1", p1);
    Py_DECREF(p0);
    Py_DECREF(p1);

    PyObject* obs_with_mask = build_obs_with_mask(env);
    PyDict_SetItemString(dict, "obs_with_mask", obs_with_mask);
    Py_DECREF(obs_with_mask);

    PyObject* pending_actions = build_pending_actions(env);
    PyDict_SetItemString(dict, "pending_actions", pending_actions);
    Py_DECREF(pending_actions);

    PyObject* executed_actions = build_executed_actions(env);
    PyDict_SetItemString(dict, "executed_actions", executed_actions);
    Py_DECREF(executed_actions);

    return dict;
}

/* ── reward shaping annealing ─────────────────────────────────────────────
 * MY_PUT: enables env_put(handle, shaping_scale=X) for single-env updates
 * MY_METHODS: registers vec_set_shaping_scale for batch updates from Python
 *
 * vec_set_shaping_scale is defined before env_binding.h because MY_METHODS
 * is expanded inside the method table (which lives inside the include).
 * we inline a local VecEnv typedef since the real one isn't available yet.
 */

#define MY_PUT
#define MY_GET

static int my_put(Env* env, PyObject* args, PyObject* kwargs) {
    (void)args;
    PyObject* val = PyDict_GetItemString(kwargs, "shaping_scale");
    if (val) {
        env->shaping.shaping_scale = (float)PyFloat_AsDouble(val);
    }
    PyObject* use_c_opponent = PyDict_GetItemString(kwargs, "use_c_opponent");
    if (use_c_opponent) {
        env->use_c_opponent = PyObject_IsTrue(use_c_opponent) ? 1 : 0;
    }
    PyObject* use_c_opponent_p0 = PyDict_GetItemString(kwargs, "use_c_opponent_p0");
    if (use_c_opponent_p0) {
        env->use_c_opponent_p0 = PyObject_IsTrue(use_c_opponent_p0) ? 1 : 0;
    }
    PyObject* use_external = PyDict_GetItemString(kwargs, "use_external_opponent_actions");
    if (use_external) {
        env->use_external_opponent_actions = PyObject_IsTrue(use_external) ? 1 : 0;
    }
    PyObject* opp0_type = PyDict_GetItemString(kwargs, "opponent0_type");
    if (opp0_type) {
        env->opponent_p0.type = (OpponentType)PyLong_AsLong(opp0_type);
        opponent_reset(env, &env->opponent_p0);
    }
    // click_budget removed (loadout system doesn't need click budgets)
    PyObject* auto_reset = PyDict_GetItemString(kwargs, "auto_reset");
    if (auto_reset) {
        env->auto_reset = PyObject_IsTrue(auto_reset) ? 1 : 0;
    }
    PyObject* seed_obj = PyDict_GetItemString(kwargs, "seed");
    if (seed_obj) {
        uint32_t seed = (uint32_t)PyLong_AsUnsignedLong(seed_obj);
        if (!PyErr_Occurred()) {
            pvp_seed(env, seed);
        }
    }
    PyObject* tier_weights = PyDict_GetItemString(kwargs, "gear_tier_weights");
    if (tier_weights) {
        PyObject* seq = PySequence_Fast(tier_weights, "gear_tier_weights must be a sequence");
        if (!seq) return -1;
        Py_ssize_t n = PySequence_Fast_GET_SIZE(seq);
        if (n >= 4) {
            PyObject** items = PySequence_Fast_ITEMS(seq);
            for (int i = 0; i < 4; i++) {
                env->gear_tier_weights[i] = (float)PyFloat_AsDouble(items[i]);
            }
        }
        Py_DECREF(seq);
    }
    PyObject* opp_actions = PyDict_GetItemString(kwargs, "opponent_actions");
    if (opp_actions) {
        PyObject* seq = PySequence_Fast(opp_actions, "opponent_actions must be a sequence");
        if (!seq) return -1;
        Py_ssize_t n = PySequence_Fast_GET_SIZE(seq);
        PyObject** items = PySequence_Fast_ITEMS(seq);
        for (int i = 0; i < NUM_ACTION_HEADS; i++) {
            if (i < n) {
                long val_int = PyLong_AsLong(items[i]);
                if (PyErr_Occurred()) {
                    Py_DECREF(seq);
                    return -1;
                }
                env->external_opponent_actions[i] = (int)val_int;
            } else {
                env->external_opponent_actions[i] = 0;
            }
        }
        env->use_external_opponent_actions = 1;
        Py_DECREF(seq);
    }
    return 0;
}

static PyObject* vec_set_shaping_scale(PyObject* self, PyObject* args) {
    (void)self;
    PyObject* handle_obj = PyTuple_GetItem(args, 0);
    if (!handle_obj) return NULL;

    /* VecEnv layout: {Env** envs; int num_envs;} — matches env_binding.h */
    typedef struct { Env** envs; int num_envs; } VecEnvLocal;
    VecEnvLocal* vec = (VecEnvLocal*)PyLong_AsVoidPtr(handle_obj);
    if (!vec) {
        PyErr_SetString(PyExc_ValueError, "invalid vec env handle");
        return NULL;
    }

    PyObject* scale_obj = PyTuple_GetItem(args, 1);
    if (!scale_obj) return NULL;
    float s = (float)PyFloat_AsDouble(scale_obj);
    if (PyErr_Occurred()) return NULL;

    for (int i = 0; i < vec->num_envs; i++) {
        vec->envs[i]->shaping.shaping_scale = s;
    }
    Py_RETURN_NONE;
}

/* ── PFSP opponent scheduling ────────────────────────────────────────────
 * vec_set_pfsp_weights: push new pool + cumulative weights to all envs
 * vec_get_pfsp_stats: aggregate win/episode counters across all envs, then reset
 */

static PyObject* vec_set_pfsp_weights(PyObject* self, PyObject* args) {
    (void)self;
    PyObject* handle_obj = PyTuple_GetItem(args, 0);
    if (!handle_obj) return NULL;

    typedef struct { Env** envs; int num_envs; } VecEnvLocal;
    VecEnvLocal* vec = (VecEnvLocal*)PyLong_AsVoidPtr(handle_obj);
    if (!vec) {
        PyErr_SetString(PyExc_ValueError, "invalid vec env handle");
        return NULL;
    }

    PyObject* pool_obj = PyTuple_GetItem(args, 1);       /* list of int (OpponentType) */
    PyObject* weights_obj = PyTuple_GetItem(args, 2);     /* list of int (cum weights * 1000) */
    if (!pool_obj || !weights_obj) return NULL;

    int pool_size = (int)PyList_Size(pool_obj);
    if (pool_size > MAX_OPPONENT_POOL) pool_size = MAX_OPPONENT_POOL;

    OpponentType pool[MAX_OPPONENT_POOL];
    int cum_weights[MAX_OPPONENT_POOL];
    for (int i = 0; i < pool_size; i++) {
        pool[i] = (OpponentType)PyLong_AsLong(PyList_GetItem(pool_obj, i));
        cum_weights[i] = (int)PyLong_AsLong(PyList_GetItem(weights_obj, i));
    }
    if (PyErr_Occurred()) return NULL;

    for (int e = 0; e < vec->num_envs; e++) {
        Env* env = vec->envs[e];
        int was_unconfigured = (env->pfsp.pool_size == 0);
        env->pfsp.pool_size = pool_size;
        for (int i = 0; i < pool_size; i++) {
            env->pfsp.pool[i] = pool[i];
            env->pfsp.cum_weights[i] = cum_weights[i];
        }
        // Only reset on first configuration — restarts the episode that was started
        // during env creation before the pool was set (would have used fallback opponent).
        // Periodic weight updates must NOT reset mid-episode.
        if (was_unconfigured) {
            pvp_reset(env);
            ocean_write_obs(env);
        }
    }
    Py_RETURN_NONE;
}

static PyObject* vec_get_pfsp_stats(PyObject* self, PyObject* args) {
    (void)self;
    PyObject* handle_obj = PyTuple_GetItem(args, 0);
    if (!handle_obj) return NULL;

    typedef struct { Env** envs; int num_envs; } VecEnvLocal;
    VecEnvLocal* vec = (VecEnvLocal*)PyLong_AsVoidPtr(handle_obj);
    if (!vec) {
        PyErr_SetString(PyExc_ValueError, "invalid vec env handle");
        return NULL;
    }

    /* Aggregate across all envs */
    float total_wins[MAX_OPPONENT_POOL] = {0};
    float total_episodes[MAX_OPPONENT_POOL] = {0};
    int pool_size = 0;

    for (int e = 0; e < vec->num_envs; e++) {
        Env* env = vec->envs[e];
        if (env->pfsp.pool_size > pool_size) pool_size = env->pfsp.pool_size;
        for (int i = 0; i < env->pfsp.pool_size; i++) {
            total_wins[i] += env->pfsp.wins[i];
            total_episodes[i] += env->pfsp.episodes[i];
        }
        /* Reset per-env counters (read-and-reset pattern) */
        memset(env->pfsp.wins, 0, sizeof(env->pfsp.wins));
        memset(env->pfsp.episodes, 0, sizeof(env->pfsp.episodes));
    }

    /* Build return tuple: (wins_list, episodes_list) */
    PyObject* wins_list = PyList_New(pool_size);
    PyObject* eps_list = PyList_New(pool_size);
    for (int i = 0; i < pool_size; i++) {
        PyList_SetItem(wins_list, i, PyFloat_FromDouble((double)total_wins[i]));
        PyList_SetItem(eps_list, i, PyFloat_FromDouble((double)total_episodes[i]));
    }
    return PyTuple_Pack(2, wins_list, eps_list);
}

/* ── Self-play support ───────────────────────────────────────────────────
 * vec_enable_selfplay: set p1 obs buffer pointer for all envs
 * vec_set_opponent_actions: push opponent actions to all envs
 */

static PyObject* vec_enable_selfplay(PyObject* self, PyObject* args) {
    (void)self;
    PyObject* handle_obj = PyTuple_GetItem(args, 0);
    PyObject* buf_obj = PyTuple_GetItem(args, 1);
    PyObject* mask_obj = PyTuple_GetItem(args, 2);
    if (!handle_obj || !buf_obj || !mask_obj) return NULL;

    typedef struct { Env** envs; int num_envs; } VecEnvLocal;
    VecEnvLocal* vec = (VecEnvLocal*)PyLong_AsVoidPtr(handle_obj);
    if (!vec) {
        PyErr_SetString(PyExc_ValueError, "invalid vec env handle");
        return NULL;
    }

    Py_buffer obs_view;
    if (PyObject_GetBuffer(buf_obj, &obs_view, PyBUF_C_CONTIGUOUS | PyBUF_WRITABLE) < 0) {
        return NULL;
    }
    Py_buffer mask_view;
    if (PyObject_GetBuffer(mask_obj, &mask_view, PyBUF_C_CONTIGUOUS | PyBUF_WRITABLE) < 0) {
        PyBuffer_Release(&obs_view);
        return NULL;
    }
    float* buf = (float*)obs_view.buf;
    unsigned char* mask = (unsigned char*)mask_view.buf;

    for (int e = 0; e < vec->num_envs; e++) {
        vec->envs[e]->ocean_obs_p1 = buf + e * OCEAN_OBS_SIZE;
        vec->envs[e]->ocean_selfplay_mask = &mask[e];
    }

    PyBuffer_Release(&obs_view);
    PyBuffer_Release(&mask_view);
    Py_RETURN_NONE;
}

static PyObject* vec_set_opponent_actions(PyObject* self, PyObject* args) {
    (void)self;
    PyObject* handle_obj = PyTuple_GetItem(args, 0);
    PyObject* acts_obj = PyTuple_GetItem(args, 1);
    if (!handle_obj || !acts_obj) return NULL;

    typedef struct { Env** envs; int num_envs; } VecEnvLocal;
    VecEnvLocal* vec = (VecEnvLocal*)PyLong_AsVoidPtr(handle_obj);
    if (!vec) {
        PyErr_SetString(PyExc_ValueError, "invalid vec env handle");
        return NULL;
    }

    Py_buffer view;
    if (PyObject_GetBuffer(acts_obj, &view, PyBUF_C_CONTIGUOUS) < 0) {
        return NULL;
    }
    int* acts = (int*)view.buf;

    for (int e = 0; e < vec->num_envs; e++) {
        memcpy(
            vec->envs[e]->external_opponent_actions,
            acts + e * NUM_ACTION_HEADS,
            NUM_ACTION_HEADS * sizeof(int)
        );
    }

    PyBuffer_Release(&view);
    Py_RETURN_NONE;
}

static PyObject* get_encounter_info(PyObject* self, PyObject* args) {
    (void)self;
    const char* name;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
    const EncounterDef* def = encounter_find(name);
    if (!def) {
        PyErr_Format(PyExc_ValueError, "unknown encounter: '%s'", name);
        return NULL;
    }
    PyObject* dims = PyList_New(def->num_action_heads);
    for (int i = 0; i < def->num_action_heads; i++) {
        PyList_SetItem(dims, i, PyLong_FromLong(def->action_head_dims[i]));
    }
    PyObject* result = Py_BuildValue("{s:i,s:i,s:i,s:N}",
        "obs_size", def->obs_size,
        "mask_size", def->mask_size,
        "num_action_heads", def->num_action_heads,
        "action_head_dims", dims);
    return result;
}

#define MY_METHODS \
    {"vec_set_shaping_scale", vec_set_shaping_scale, METH_VARARGS, \
     "Set shaping_scale for all envs in the vec"}, \
    {"vec_set_pfsp_weights", vec_set_pfsp_weights, METH_VARARGS, \
     "Set PFSP pool and cumulative weights for all envs"}, \
    {"vec_get_pfsp_stats", vec_get_pfsp_stats, METH_VARARGS, \
     "Get aggregated PFSP win/episode stats and reset counters"}, \
    {"vec_enable_selfplay", vec_enable_selfplay, METH_VARARGS, \
     "Set p1 obs buffer for self-play in all envs"}, \
    {"vec_set_opponent_actions", vec_set_opponent_actions, METH_VARARGS, \
     "Push opponent actions to all envs for self-play"}, \
    {"get_encounter_info", get_encounter_info, METH_VARARGS, \
     "Get obs/action space dimensions for a named encounter"}

// Wrappers for 3.0 ocean template (expects c_step/c_reset/c_close/c_render).
// When encounter_def is set, dispatch through the encounter interface.
// Otherwise fall through to legacy pvp_step (backwards compat).
static void c_step(Env* env) {
    if (env->encounter_def) {
        const EncounterDef* def = (const EncounterDef*)env->encounter_def;
        def->step(env->encounter_state, env->ocean_acts);
        /* write terminal obs + mask before auto-reset */
        def->write_obs(env->encounter_state, env->ocean_obs);
        float mask_buf[256];
        def->write_mask(env->encounter_state, mask_buf);
        for (int i = 0; i < def->mask_size; i++) {
            env->ocean_obs[def->obs_size + i] = mask_buf[i];
        }
        env->ocean_rew[0] = def->get_reward(env->encounter_state);
        int is_term = def->is_terminal(env->encounter_state);
        env->ocean_term[0] = is_term ? 1 : 0;
        /* auto-reset: write terminal obs (above), then reset for next episode */
        if (is_term) {
            if (def->get_log) def->get_log(env->encounter_state);
            def->reset(env->encounter_state, 0);
        }
    } else {
        pvp_step(env);
        // After auto-reset, pvp_step wrote terminal obs then reset game state.
        // Overwrite with initial-state obs so policy sees correct first observation
        // on the next step. Matches 4.0 training binding behavior.
        if (env->ocean_term[0] && env->auto_reset) {
            ocean_write_obs(env);
        }
    }
}
static void c_reset(Env* env) {
    if (env->encounter_def) {
        const EncounterDef* def = (const EncounterDef*)env->encounter_def;
        def->reset(env->encounter_state, env->rng_seed);
    } else {
        pvp_reset(env);
    }
}
static void c_close(Env* env) {
    if (env->encounter_def && env->encounter_state) {
        const EncounterDef* def = (const EncounterDef*)env->encounter_def;
        def->destroy(env->encounter_state);
        env->encounter_state = NULL;
    }
    pvp_close(env);
}
static void c_render(Env* env) { pvp_render(env); }

#include "../PufferLib/pufferlib/ocean/env_binding.h"

static int my_init(Env* env, PyObject* args, PyObject* kwargs) {
    (void)args;

    // Save PufferLib shared buffer pointers (set by env_binding.h template)
    // before pvp_init redirects them to internal 2-agent buffers
    float* shared_obs = (float*)env->observations;
    int* shared_acts = (int*)env->actions;
    float* shared_rew = (float*)env->rewards;
    unsigned char* shared_term = (unsigned char*)env->terminals;

    // Encounter dispatch: check early before PvP-specific kwargs.
    // Non-PvP encounters (e.g. zulrah) don't need opponent_type, is_lms, etc.
    PyObject* encounter_name = PyDict_GetItemString(kwargs, "encounter");
    if (encounter_name && encounter_name != Py_None) {
        const char* name = PyUnicode_AsUTF8(encounter_name);
        if (!name) return -1;
        const EncounterDef* def = encounter_find(name);
        if (!def) {
            PyErr_Format(PyExc_ValueError, "unknown encounter: '%s'", name);
            return -1;
        }
        EncounterState* es = def->create();
        if (!es) {
            PyErr_SetString(PyExc_RuntimeError, "encounter create() failed");
            return -1;
        }

        // Optional seed
        PyObject* seed_obj = PyDict_GetItemString(kwargs, "seed");
        uint32_t seed = 0;
        if (seed_obj && seed_obj != Py_None) {
            seed = (uint32_t)PyLong_AsLong(seed_obj);
            if (PyErr_Occurred()) { def->destroy(es); return -1; }
            def->put_int(es, "seed", (int)seed);
        }

        def->reset(es, seed);

        // Wire encounter to env
        env->encounter_def = def;
        env->encounter_state = es;
        env->ocean_obs = shared_obs;
        env->ocean_acts = shared_acts;
        env->ocean_rew = shared_rew;
        env->ocean_term = shared_term;

        // Write initial obs through encounter interface
        def->write_obs(es, env->ocean_obs);
        float enc_mask_buf[256]; /* enough for any encounter mask */
        def->write_mask(es, enc_mask_buf);
        for (int i = 0; i < def->mask_size; i++) {
            env->ocean_obs[def->obs_size + i] = enc_mask_buf[i];
        }
        env->ocean_rew[0] = 0.0f;
        env->ocean_term[0] = 0;
        return 0;
    }

    // Legacy PvP path: initialize internal buffers and parse PvP-specific kwargs
    pvp_init(env);

    // Set ocean pointers to PufferLib shared buffers
    env->ocean_obs = shared_obs;
    env->ocean_acts = shared_acts;
    env->ocean_rew = shared_rew;
    env->ocean_term = shared_term;

    // Parse kwargs (required for PvP mode)
    int seed = (int)unpack(kwargs, "seed");
    if (PyErr_Occurred()) return -1;

    int opponent_type = (int)unpack(kwargs, "opponent_type");
    if (PyErr_Occurred()) return -1;

    int is_lms = (int)unpack(kwargs, "is_lms");
    if (PyErr_Occurred()) return -1;

    double shaping_scale = unpack(kwargs, "shaping_scale");
    if (PyErr_Occurred()) return -1;

    int shaping_enabled = (int)unpack(kwargs, "shaping_enabled");
    if (PyErr_Occurred()) return -1;

    // Configure environment
    env->rng_seed = (uint32_t)(seed > 0 ? seed : 0);
    env->has_rng_seed = (seed > 0) ? 1 : 0;
    env->is_lms = is_lms;

    // Configure opponent
    env->use_c_opponent = 1;
    env->opponent.type = (OpponentType)opponent_type;

    // Configure reward shaping
    env->shaping.shaping_scale = (float)shaping_scale;
    env->shaping.enabled = shaping_enabled;
    env->shaping.damage_dealt_coef = 0.005f;
    env->shaping.damage_received_coef = -0.005f;
    env->shaping.correct_prayer_bonus = 0.03f;
    env->shaping.wrong_prayer_penalty = -0.02f;
    env->shaping.prayer_switch_no_attack_penalty = -0.01f;
    env->shaping.off_prayer_hit_bonus = 0.03f;
    env->shaping.melee_frozen_penalty = -0.05f;
    env->shaping.wasted_eat_penalty = -0.001f;
    env->shaping.premature_eat_penalty = -0.02f;
    env->shaping.magic_no_staff_penalty = -0.05f;
    env->shaping.gear_mismatch_penalty = -0.05f;
    env->shaping.spec_off_prayer_bonus = 0.02f;
    env->shaping.spec_low_defence_bonus = 0.01f;
    env->shaping.spec_low_hp_bonus = 0.02f;
    env->shaping.smart_triple_eat_bonus = 0.05f;
    env->shaping.wasted_triple_eat_penalty = -0.0005f;
    env->shaping.damage_burst_bonus = 0.002f;
    env->shaping.damage_burst_threshold = 30;
    env->shaping.premature_eat_threshold = 0.7071f;
    env->shaping.ko_bonus = 0.15f;
    env->shaping.wasted_resources_penalty = -0.07f;
    // Always-on behavioral penalties
    env->shaping.prayer_penalty_enabled = 1;
    env->shaping.click_penalty_enabled = 0;
    env->shaping.click_penalty_threshold = 5;
    env->shaping.click_penalty_coef = -0.003f;

    // Override behavioral penalty config from kwargs
    PyObject* prayer_pen = PyDict_GetItemString(kwargs, "prayer_penalty_enabled");
    if (prayer_pen) env->shaping.prayer_penalty_enabled = PyObject_IsTrue(prayer_pen);
    PyObject* click_pen = PyDict_GetItemString(kwargs, "click_penalty_enabled");
    if (click_pen) env->shaping.click_penalty_enabled = PyObject_IsTrue(click_pen);
    PyObject* click_thresh = PyDict_GetItemString(kwargs, "click_penalty_threshold");
    if (click_thresh) env->shaping.click_penalty_threshold = (int)PyLong_AsLong(click_thresh);
    PyObject* click_coef = PyDict_GetItemString(kwargs, "click_penalty_coef");
    if (click_coef) env->shaping.click_penalty_coef = (float)PyFloat_AsDouble(click_coef);

    // Collision map: load once from path kwarg, share across all envs
    PyObject* cmap_path = PyDict_GetItemString(kwargs, "collision_map_path");
    if (cmap_path && cmap_path != Py_None && g_collision_map == NULL) {
        const char* path = PyUnicode_AsUTF8(cmap_path);
        if (path) {
            g_collision_map = collision_map_load(path);
            if (g_collision_map) {
                fprintf(stderr, "collision map loaded: %d regions from %s\n",
                        g_collision_map->count, path);
            }
        }
    }
    env->collision_map = g_collision_map;

    // Gear tier weights: default 100% basic (tier 0)
    env->gear_tier_weights[0] = 1.0f;
    env->gear_tier_weights[1] = 0.0f;
    env->gear_tier_weights[2] = 0.0f;
    env->gear_tier_weights[3] = 0.0f;

    // Override gear tier weights if provided in kwargs
    PyObject* tier_w = PyDict_GetItemString(kwargs, "gear_tier_weights");
    if (tier_w) {
        PyObject* seq = PySequence_Fast(tier_w, "gear_tier_weights must be a sequence");
        if (seq) {
            Py_ssize_t n = PySequence_Fast_GET_SIZE(seq);
            if (n >= 4) {
                PyObject** items = PySequence_Fast_ITEMS(seq);
                for (int i = 0; i < 4; i++) {
                    env->gear_tier_weights[i] = (float)PyFloat_AsDouble(items[i]);
                }
            }
            Py_DECREF(seq);
        }
    }

    // Legacy path: no encounter, direct pvp_step
    pvp_reset(env);

    // Write initial observations to shared buffer
    ocean_write_obs(env);
    env->ocean_rew[0] = 0.0f;
    env->ocean_term[0] = 0;

    return 0;
}

static int my_log(PyObject* dict, Log* log) {
    assign_to_dict(dict, "episode_return", log->episode_return);
    assign_to_dict(dict, "episode_length", log->episode_length);
    assign_to_dict(dict, "wins", log->wins);
    assign_to_dict(dict, "damage_dealt", log->damage_dealt);
    assign_to_dict(dict, "damage_received", log->damage_received);
    return 0;
}
