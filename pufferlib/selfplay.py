"""Selfplay-pool training: a fraction of envs play primary vs a frozen historical
snapshot, the rest are pure selfplay. Used by `_train` in pufferl.py — gated on
`selfplay.enabled` (config section).

Pool grows on two triggers:
  - snapshot_interval: every N global steps, save primary weights as a new
    pool entry regardless of winrate. Provides a steady cadence.
  - winrate-driven swap: when primary beats the current opponent at >=
    swap_winrate over >= min_games, also save primary as a pool entry, then
    swap to a new opponent. Marks progress checkpoints in the curriculum.

Swap (without a snapshot) also fires when opp_timeout_steps have elapsed
since the current opponent was finalized. Timeout prevents stalemates from
pinning the curriculum to a single opponent indefinitely.

Pool storage is disk-only (paths held in memory; weights only on GPU when
loaded as the frozen bank). Stride-eviction preserves temporal coverage when
the pool exceeds its cap.
"""
import os

import numpy as np

try:
    from pufferlib import _C
except ImportError:
    _C = None


OPP_PFSP = 16


def pfsp_weight(winrate, mode, floor):
    if mode == 'uniform':
        return 1.0
    if mode == 'pfsp_hard':
        return max((1.0 - winrate) ** 2, floor)
    if mode == 'pfsp_var':
        return max(winrate * (1.0 - winrate), floor)
    raise RuntimeError(f'unknown PFSP sampling mode: {mode}')


def normalize_to_cumulative_thousand(weights):
    raw = np.asarray(weights, dtype=np.float64)
    if raw.ndim != 1 or raw.size == 0:
        raise RuntimeError('PFSP weights must be a nonempty 1-D array')
    if np.any(raw < 0.0) or not np.all(np.isfinite(raw)):
        raise RuntimeError(f'PFSP weights must be finite and nonnegative: {weights!r}')
    positive = raw > 0.0
    if not np.any(positive):
        raise RuntimeError('PFSP weights must contain at least one positive entry')

    probs = raw / raw.sum()
    counts = np.floor(probs * 1000.0).astype(np.int32)
    counts[(counts == 0) & positive] = 1

    while int(counts.sum()) > 1000:
        idx = int(np.argmax(np.where(counts > 1, counts, -1)))
        if counts[idx] <= 1:
            raise RuntimeError('too many positive PFSP weights to preserve a 1000-point floor')
        counts[idx] -= 1

    while int(counts.sum()) < 1000:
        residual = probs * 1000.0 - counts
        idx = int(np.argmax(residual))
        counts[idx] += 1

    cum_weights = np.cumsum(counts).astype(np.int32)
    cum_weights[-1] = 1000
    return cum_weights


def scripted_pfsp_weights(base_weights, winrates, mode, floor):
    priors = np.asarray(base_weights, dtype=np.float64)
    rates = np.asarray(winrates, dtype=np.float64)
    if priors.shape != rates.shape:
        raise RuntimeError('scripted priors and winrates must have the same shape')
    raw = np.array(
        [prior * pfsp_weight(float(rate), mode, floor)
         for prior, rate in zip(priors, rates)],
        dtype=np.float64,
    )
    return raw / raw.sum(), normalize_to_cumulative_thousand(raw)


def scripted_env_hardness(winrates, base_weights, neutral_winrate, solved_winrate):
    rates = np.asarray(winrates, dtype=np.float64)
    priors = np.asarray(base_weights, dtype=np.float64)
    if rates.shape != priors.shape:
        raise RuntimeError('scripted hardness winrates and priors must have the same shape')
    if rates.ndim != 1 or rates.size == 0:
        raise RuntimeError('scripted hardness requires a nonempty 1-D winrate array')
    if not np.all(np.isfinite(rates)) or np.any(rates < 0.0) or np.any(rates > 1.0):
        raise RuntimeError(f'scripted winrates must be finite probabilities: {winrates!r}')
    if not np.all(np.isfinite(priors)) or np.any(priors < 0.0) or priors.sum() <= 0.0:
        raise RuntimeError(f'scripted priors must be finite nonnegative weights: {base_weights!r}')
    if not (0.0 <= neutral_winrate < solved_winrate <= 1.0):
        raise RuntimeError('scripted_env_neutral_winrate must be below scripted_env_solved_winrate')

    difficulty = (solved_winrate - rates) / (solved_winrate - neutral_winrate)
    difficulty = np.clip(difficulty, 0.0, 1.0)
    return float(np.dot(difficulty, priors / priors.sum()))


def adaptive_scripted_env_pct(max_pct, floor_frac, hardness):
    if not (0.0 <= max_pct <= 1.0):
        raise RuntimeError(f'scripted_env_pct must be in [0, 1], got {max_pct}')
    if not (0.0 <= floor_frac <= 1.0):
        raise RuntimeError(f'scripted_env_floor_frac must be in [0, 1], got {floor_frac}')
    if not (0.0 <= hardness <= 1.0):
        raise RuntimeError(f'scripted hardness must be in [0, 1], got {hardness}')
    floor_pct = max_pct * floor_frac
    return floor_pct + (max_pct - floor_pct) * hardness


def scripted_env_target_count(num_eligible_envs, effective_pct):
    if num_eligible_envs < 0:
        raise RuntimeError(f'num_eligible_envs must be nonnegative, got {num_eligible_envs}')
    if not (0.0 <= effective_pct <= 1.0):
        raise RuntimeError(f'effective scripted env pct must be in [0, 1], got {effective_pct}')
    if num_eligible_envs == 0 or effective_pct == 0.0:
        return 0
    target = int(round(num_eligible_envs * effective_pct))
    return min(num_eligible_envs, max(1, target))


def assign_scripted_envs(
        num_envs, eligible_env_indices, target_count, scripted_pfsp_enabled,
        scripted_dispatcher_opp, scripted_opps_list, scripted_base_weights, rng):
    if target_count < 0:
        raise RuntimeError(f'target_count must be nonnegative, got {target_count}')
    eligible = np.asarray(eligible_env_indices, dtype=np.int32)
    if target_count > len(eligible):
        raise RuntimeError(
            f'target_count {target_count} exceeds eligible scripted envs {len(eligible)}')

    scripted_opps = np.full(num_envs, -1, dtype=np.int32)
    if target_count == 0:
        return scripted_opps
    if len(scripted_opps_list) == 0:
        raise RuntimeError('scripted env assignment requires scripted_opp_pool')

    chosen = eligible[:target_count]
    if scripted_pfsp_enabled:
        scripted_opps[chosen] = scripted_dispatcher_opp
        return scripted_opps

    weights = np.asarray(scripted_base_weights, dtype=np.float64)
    if weights.ndim != 1 or weights.size != len(scripted_opps_list):
        raise RuntimeError('scripted base weights must match scripted_opp_pool')
    if not np.all(np.isfinite(weights)) or np.any(weights < 0.0) or weights.sum() <= 0.0:
        raise RuntimeError(f'scripted base weights must be finite nonnegative: {weights!r}')
    probs = weights / weights.sum()
    opp_assignments = rng.choice(len(scripted_opps_list), size=target_count, p=probs)
    for env_idx, opp_idx in zip(chosen, opp_assignments):
        scripted_opps[env_idx] = scripted_opps_list[opp_idx]
    return scripted_opps


def sample_opponent(pool, rng, mode='sqrt'):
    n = len(pool)
    if n == 0:
        return None
    if mode == 'uniform':
        idx = int(rng.integers(n))
    elif mode in ('pfsp_hard', 'pfsp_var'):
        weights = np.array(
            [pfsp_weight(e.get('winrate', 0.5), mode, 0.01) for e in pool],
            dtype=np.float64)
        weights /= weights.sum()
        idx = int(rng.choice(n, p=weights))
    else:
        weights = np.array([(i + 1) ** 0.5 for i in range(n)], dtype=np.float64)
        weights /= weights.sum()
        idx = int(rng.choice(n, p=weights))
    return pool[idx]


def update_elo(primary_elo, opp_elo, score_rate, k):
    expected = 1.0 / (1.0 + 10.0 ** ((opp_elo - primary_elo) / 400.0))
    delta = k * (score_rate - expected)
    return primary_elo + delta, opp_elo - delta


def evict(pool, max_size):
    '''Drop every other entry from the oldest half once the pool exceeds max_size.
    Newest half is preserved intact.'''
    if len(pool) <= max_size:
        return pool
    half = len(pool) // 2
    return pool[:half:2] + pool[half:]


def parse_config_sequence(value, cast, field_name):
    if value is None:
        return []
    if isinstance(value, str):
        text = value.strip()
        if not text:
            return []
        raw_values = [part.strip() for part in text.split(',') if part.strip()]
    elif isinstance(value, np.ndarray):
        raw_values = value.tolist()
    elif isinstance(value, (list, tuple)):
        raw_values = list(value)
    else:
        raw_values = [value]

    try:
        return [cast(raw) for raw in raw_values]
    except (TypeError, ValueError) as err:
        raise RuntimeError(
            f'selfplay.{field_name} not parseable as {cast.__name__} list: {value!r}'
        ) from err


def build_perm_tags(num_buffers, agents_per_buffer, agents_per_env, frozen_sizes, num_envs):
    '''Build env-slot -> rollout-row routing and per-env bank tag.

    Multi-bank generalization. `frozen_sizes` is a list of per-bank agent counts
    (per buffer). With one bank this reduces to the legacy single-bank layout.

    Per-buffer physical-row layout (apb = agents_per_buffer, F = sum(frozen_sizes)):
        [0,           apb - 2F)                       primary — selfplay envs (all slots)
        [apb - 2F,    apb - F)                        primary — historical envs' team A
        [apb - F,     apb - F + frozen_sizes[0])      bank 0  — historical envs' team B
        [apb - F + frozen_sizes[0], ... + ...[1])     bank 1  — ... etc.

    Env order within a buffer: selfplay envs first (tag=0), then historical
    envs assigned to banks in block order — the first `frozen_sizes[0]/team_size`
    historical envs play bank 0 (tag=1), next block plays bank 1 (tag=2), etc.

    The C-side bank_layout (pufferlib.cu:1798-1806) lays banks out sequentially
    after primary, so our routing matches: bank b's slice is
    [apb - F + sum(frozen_sizes[:b]),  apb - F + sum(frozen_sizes[:b+1])).

    Returns (perm, tags, num_hist_envs_per_bank) — last is a list of per-bank
    historical-env counts across all buffers, used by selfplay.step to know how
    many env alignments to wait for per bank during swaps.'''
    team_size = agents_per_env // 2
    envs_per_buffer = agents_per_buffer // agents_per_env
    num_banks = len(frozen_sizes)
    total_frozen = sum(frozen_sizes)
    hist_envs_per_bank_per_buffer = [fs // team_size for fs in frozen_sizes]
    total_hist_envs_per_buffer = sum(hist_envs_per_bank_per_buffer)
    selfplay_envs = envs_per_buffer - total_hist_envs_per_buffer
    perm = np.empty(num_buffers * agents_per_buffer, dtype=np.int32)
    tags = np.zeros(num_envs, dtype=np.int32)
    env_idx = 0
    for b_buf in range(num_buffers):
        buf_start          = b_buf * agents_per_buffer
        hist_primary_start = buf_start + agents_per_buffer - 2 * total_frozen
        bank_starts = []
        offset = buf_start + agents_per_buffer - total_frozen
        for bank in range(num_banks):
            bank_starts.append(offset)
            offset += frozen_sizes[bank]
        h_within_buffer = 0
        for e in range(envs_per_buffer):
            slot_base = buf_start + e * agents_per_env
            if e < selfplay_envs:
                for s in range(agents_per_env):
                    perm[slot_base + s] = slot_base + s
                tags[env_idx] = 0
            else:
                # Block assignment: walk cumulative bank capacity to find which
                # bank this historical env belongs to.
                bank_idx = 0
                cum = hist_envs_per_bank_per_buffer[0]
                while h_within_buffer >= cum and bank_idx < num_banks - 1:
                    bank_idx += 1
                    cum += hist_envs_per_bank_per_buffer[bank_idx]
                h_in_bank = h_within_buffer - (cum - hist_envs_per_bank_per_buffer[bank_idx])
                team_a_offset = hist_primary_start + h_within_buffer * team_size
                team_b_offset = bank_starts[bank_idx] + h_in_bank * team_size
                for s in range(team_size):
                    perm[slot_base + s] = team_a_offset + s
                    perm[slot_base + team_size + s] = team_b_offset + s
                tags[env_idx] = bank_idx + 1
                h_within_buffer += 1
            env_idx += 1
    num_hist_envs_per_bank = [n * num_buffers for n in hist_envs_per_bank_per_buffer]
    return perm, tags, num_hist_envs_per_bank


def setup(pufferl, backend, args, run_id):
    '''Wire up agent_perm/tags and bootstrap the frozen bank with the current
    weights so historical envs have an opponent from rollout 1. Returns a
    pool_state dict (or None if disabled).'''
    sp = args.get('selfplay', {})
    if not sp.get('enabled', 0):
        return None
    if backend is not _C:
        raise RuntimeError('selfplay_pool requires the native CUDA backend')

    total_agents = int(args['vec']['total_agents'])
    num_buffers = int(args['vec']['num_buffers'])
    if total_agents % num_buffers != 0:
        raise RuntimeError(f'total_agents ({total_agents}) must be divisible by '
                           f'num_buffers ({num_buffers})')
    agents_per_buffer = total_agents // num_buffers

    num_envs = backend.num_envs(pufferl)
    agents_per_env = total_agents // num_envs
    if agents_per_env % 2 != 0:
        raise RuntimeError(f'agents_per_env ({agents_per_env}) must be even (two equal teams)')
    if agents_per_buffer % agents_per_env != 0:
        raise RuntimeError(f'agents_per_buffer ({agents_per_buffer}) must be divisible by '
                           f'agents_per_env ({agents_per_env})')
    team_size = agents_per_env // 2

    num_banks = int(args['vec'].get('num_frozen_banks', 1))
    if num_banks <= 0:
        raise RuntimeError('selfplay.enabled requires num_frozen_banks >= 1')
    if num_banks > 8:
        raise RuntimeError(f'num_frozen_banks {num_banks} exceeds chess.h CHESS_MAX_BANKS=8')

    # frozen_bank_pct is per-bank (matches C-side: pufferlib.cu:2069). Each bank
    # gets floor(apb * pct) agents, total historical = num_banks * frozen_size.
    frozen_size = int(agents_per_buffer * float(args['vec']['frozen_bank_pct']))
    frozen_size -= frozen_size % team_size
    if frozen_size <= 0:
        raise RuntimeError('selfplay.enabled but frozen_bank_pct rounds to 0 slots '
                           f'after team-size ({team_size}) alignment')
    total_frozen = frozen_size * num_banks
    if total_frozen >= agents_per_buffer // 2:
        raise RuntimeError(f'total_frozen {total_frozen} (= num_banks {num_banks} '
                           f'* per_bank {frozen_size}) >= apb/2 {agents_per_buffer//2}')

    frozen_sizes = [frozen_size] * num_banks
    perm, tags, num_hist_envs_per_bank = build_perm_tags(
        num_buffers, agents_per_buffer, agents_per_env, frozen_sizes, num_envs)
    backend.set_agent_perm(pufferl, perm)
    backend.set_env_tags(pufferl, tags)

    pool_dir = os.path.join(args['checkpoint_dir'], args['env_name'], run_id, 'pool')
    os.makedirs(pool_dir, exist_ok=True)
    bootstrap_path = os.path.join(pool_dir, f'{pufferl.global_step:016d}.bin')
    backend.save_weights(pufferl, bootstrap_path)
    # Load bootstrap into every bank — they'll diverge as each bank's swap fires.
    for b in range(num_banks):
        backend.load_frozen_bank(pufferl, b, bootstrap_path)

    elo_init = float(sp.get('elo_init', 0.0))
    elo_k    = float(sp.get('elo_k',    16.0))
    rng = np.random.default_rng(int(sp.get('seed', 0)))

    banks_state = []
    for b in range(num_banks):
        banks_state.append({
            'cur_opp_path': bootstrap_path,
            'cur_opp_elo': elo_init,
            'hist_score': 0.0,
            'hist_n': 0.0,
            'pending_opp_path': None,
            'pending_opp_elo': None,
            'epoch_armed': 0,
            'opp_started_step': int(pufferl.global_step),
            'num_hist_envs': num_hist_envs_per_bank[b],
            'last_winrate_at_swap': 0.0,
            'last_epochs_to_align': 0,
        })

    scripted_opps_list = parse_config_sequence(
        sp.get('scripted_opp_pool', ''), int, 'scripted_opp_pool')
    weights_list = parse_config_sequence(
        sp.get('scripted_opp_weights', ''), float, 'scripted_opp_weights')
    if weights_list and len(weights_list) != len(scripted_opps_list):
        raise RuntimeError('selfplay.scripted_opp_weights length must match scripted_opp_pool')
    if weights_list and not scripted_opps_list:
        raise RuntimeError('selfplay.scripted_opp_weights requires scripted_opp_pool')
    if not weights_list:
        weights_list = [1.0] * len(scripted_opps_list)

    scripted_env_pct = float(sp.get('scripted_env_pct', 0.0))
    scripted_env_schedule = str(sp.get('scripted_env_schedule', 'fixed')).strip()
    scripted_env_floor_frac = float(sp.get('scripted_env_floor_frac', 0.05))
    scripted_env_neutral_winrate = float(sp.get('scripted_env_neutral_winrate', 0.5))
    scripted_env_solved_winrate = float(sp.get('scripted_env_solved_winrate', 0.9))
    scripted_sampling = str(sp.get('scripted_sampling', 'fixed')).strip()
    scripted_floor_weight = float(sp.get('scripted_floor_weight', 0.01))
    scripted_winrate_alpha = float(sp.get('scripted_winrate_alpha', 0.3))
    scripted_update_interval = int(sp.get('scripted_update_interval', 1_000_000))
    scripted_dispatcher_opp = int(sp.get('scripted_dispatcher_opp', OPP_PFSP))
    if scripted_env_schedule not in ('fixed', 'adaptive'):
        raise RuntimeError(f'unknown selfplay.scripted_env_schedule: {scripted_env_schedule}')
    if scripted_env_pct > 0.0 and not scripted_opps_list:
        raise RuntimeError('selfplay.scripted_env_pct requires scripted_opp_pool')
    if scripted_env_schedule == 'adaptive' and scripted_sampling == 'fixed':
        raise RuntimeError('adaptive scripted env schedule requires adaptive scripted_sampling')
    if scripted_env_schedule == 'adaptive' and not scripted_opps_list:
        raise RuntimeError('adaptive scripted env schedule requires scripted_opp_pool')
    if scripted_env_pct > 0.0 and scripted_opps_list and not hasattr(backend, 'set_env_scripted_opps'):
        raise RuntimeError('scripted-opponent pool requires set_env_scripted_opps')

    scripted_env_indices = np.where(tags == 0)[0].astype(np.int32)
    scripted_pfsp_enabled = (
        scripted_sampling != 'fixed'
        and len(scripted_opps_list) > 0
        and scripted_env_pct > 0.0
    )
    scripted_winrates = np.full(len(scripted_opps_list), 0.5, dtype=np.float64)
    scripted_base_weights = np.asarray(weights_list, dtype=np.float64)
    scripted_weights = np.zeros(len(scripted_opps_list), dtype=np.float64)
    scripted_cum_weights = np.zeros(len(scripted_opps_list), dtype=np.int32)

    if scripted_pfsp_enabled:
        if scripted_dispatcher_opp != OPP_PFSP:
            raise RuntimeError('scripted_dispatcher_opp must be OPP_PFSP for adaptive scripted PFSP')
        if not hasattr(backend, 'set_pfsp_weights') or not hasattr(backend, 'get_pfsp_stats'):
            raise RuntimeError('adaptive scripted PFSP requires set_pfsp_weights and get_pfsp_stats')
        scripted_weights, scripted_cum_weights = scripted_pfsp_weights(
            scripted_base_weights,
            scripted_winrates,
            scripted_sampling,
            scripted_floor_weight,
        )
        backend.set_pfsp_weights(
            pufferl,
            np.asarray(scripted_opps_list, dtype=np.int32),
            scripted_cum_weights,
        )

    pool_state = {
        'pool_dir': pool_dir,
        'pool': [{'path': bootstrap_path, 'elo': elo_init, 'winrate': 0.5, 'n_games': 0}],
        'rng': rng,
        'max_size': int(sp['max_size']),
        'min_games': int(sp['min_games']),
        'swap_winrate': float(sp['swap_winrate']),
        'snapshot_interval': int(sp.get('snapshot_interval', 1_000_000_000)),
        'opp_timeout_steps': int(sp.get('opp_timeout_steps', 500_000_000)),
        'sampling': str(sp.get('sampling', 'sqrt')),
        'num_banks': num_banks,
        'banks': banks_state,
        'primary_elo': elo_init,
        'elo_k': elo_k,
        'last_snapshot_step': int(pufferl.global_step),
        'scripted_opps_list': scripted_opps_list,
        'scripted_env_indices': scripted_env_indices,
        'scripted_envs': np.full(num_envs, -1, dtype=np.int32),
        'scripted_env_schedule': scripted_env_schedule,
        'scripted_env_cap_pct': scripted_env_pct,
        'scripted_env_floor_frac': scripted_env_floor_frac,
        'scripted_env_neutral_winrate': scripted_env_neutral_winrate,
        'scripted_env_solved_winrate': scripted_env_solved_winrate,
        'scripted_env_effective_pct': 0.0,
        'scripted_env_hardness': 0.0,
        'scripted_target_envs': 0,
        'scripted_pfsp_enabled': scripted_pfsp_enabled,
        'scripted_sampling': scripted_sampling,
        'scripted_dispatcher_opp': scripted_dispatcher_opp,
        'scripted_floor_weight': scripted_floor_weight,
        'scripted_winrate_alpha': scripted_winrate_alpha,
        'scripted_update_interval': scripted_update_interval,
        'scripted_base_weights': scripted_base_weights,
        'scripted_winrates': scripted_winrates,
        'scripted_games': np.zeros(len(scripted_opps_list), dtype=np.float64),
        'scripted_weights': scripted_weights,
        'scripted_cum_weights': scripted_cum_weights,
        'scripted_last_update_step': int(pufferl.global_step),
        'scripted_updates': 0,
    }
    refresh_scripted_env_mix(pufferl, backend, pool_state, force=True)
    return pool_state


def scripted_env_mix_target(pool_state):
    cap_pct = pool_state['scripted_env_cap_pct']
    if pool_state['scripted_env_schedule'] == 'fixed':
        hardness = 1.0
        effective_pct = cap_pct
    elif pool_state['scripted_env_schedule'] == 'adaptive':
        hardness = scripted_env_hardness(
            pool_state['scripted_winrates'],
            pool_state['scripted_base_weights'],
            pool_state['scripted_env_neutral_winrate'],
            pool_state['scripted_env_solved_winrate'],
        )
        effective_pct = adaptive_scripted_env_pct(
            cap_pct,
            pool_state['scripted_env_floor_frac'],
            hardness,
        )
    else:
        raise RuntimeError(
            f'unknown scripted env schedule: {pool_state["scripted_env_schedule"]}')

    target = scripted_env_target_count(
        len(pool_state['scripted_env_indices']),
        effective_pct,
    )
    return target, effective_pct, hardness


def refresh_scripted_env_mix(pufferl, backend, pool_state, force=False):
    if not pool_state['scripted_opps_list']:
        return

    target, effective_pct, hardness = scripted_env_mix_target(pool_state)
    changed = target != pool_state['scripted_target_envs']
    pool_state['scripted_env_effective_pct'] = effective_pct
    pool_state['scripted_env_hardness'] = hardness
    pool_state['scripted_target_envs'] = target
    if not force and not changed:
        return
    if target == 0 and not np.any(pool_state['scripted_envs'] >= 0):
        return
    if not hasattr(backend, 'set_env_scripted_opps'):
        raise RuntimeError('scripted-opponent pool requires set_env_scripted_opps')

    scripted_envs = assign_scripted_envs(
        backend.num_envs(pufferl),
        pool_state['scripted_env_indices'],
        target,
        pool_state['scripted_pfsp_enabled'],
        pool_state['scripted_dispatcher_opp'],
        pool_state['scripted_opps_list'],
        pool_state['scripted_base_weights'],
        pool_state['rng'],
    )
    pool_state['scripted_envs'] = scripted_envs
    backend.set_env_scripted_opps(pufferl, scripted_envs)


def update_scripted_pfsp(pufferl, backend, pool_state):
    if not pool_state['scripted_pfsp_enabled']:
        return

    interval = pool_state['scripted_update_interval']
    if interval > 0 and pufferl.global_step - pool_state['scripted_last_update_step'] < interval:
        return

    stats = backend.get_pfsp_stats(pufferl)
    pool_size = int(stats['pool_size'])
    if pool_size != len(pool_state['scripted_opps_list']):
        raise RuntimeError(
            f'scripted PFSP pool size mismatch: C={pool_size}, '
            f'Python={len(pool_state["scripted_opps_list"])}')

    wins = np.asarray(stats['wins'], dtype=np.float64)
    episodes = np.asarray(stats['episodes'], dtype=np.float64)
    for i, n in enumerate(episodes):
        if n <= 0.0:
            continue
        observed = wins[i] / n
        prev_games = pool_state['scripted_games'][i]
        alpha = 1.0 if prev_games == 0.0 else pool_state['scripted_winrate_alpha']
        pool_state['scripted_winrates'][i] = (
            (1.0 - alpha) * pool_state['scripted_winrates'][i]
            + alpha * observed
        )
        pool_state['scripted_games'][i] += n

    weights, cum_weights = scripted_pfsp_weights(
        pool_state['scripted_base_weights'],
        pool_state['scripted_winrates'],
        pool_state['scripted_sampling'],
        pool_state['scripted_floor_weight'],
    )
    pool_state['scripted_weights'] = weights
    pool_state['scripted_cum_weights'] = cum_weights
    pool_state['scripted_last_update_step'] = int(pufferl.global_step)
    pool_state['scripted_updates'] += 1
    backend.set_pfsp_weights(
        pufferl,
        np.asarray(pool_state['scripted_opps_list'], dtype=np.int32),
        cum_weights,
    )
    refresh_scripted_env_mix(pufferl, backend, pool_state)


def log_scripted_pfsp(pool_state, flat_logs):
    flat_logs['pool/scripted_envs'] = int(np.count_nonzero(pool_state['scripted_envs'] >= 0))
    flat_logs['pool/scripted_target_envs'] = int(pool_state['scripted_target_envs'])
    flat_logs['pool/scripted_env_cap_pct'] = float(pool_state['scripted_env_cap_pct'])
    flat_logs['pool/scripted_env_effective_pct'] = float(pool_state['scripted_env_effective_pct'])
    flat_logs['pool/scripted_env_floor_frac'] = float(pool_state['scripted_env_floor_frac'])
    flat_logs['pool/scripted_env_hardness'] = float(pool_state['scripted_env_hardness'])
    flat_logs['pool/scripted_env_adaptive'] = float(
        pool_state['scripted_env_schedule'] == 'adaptive')
    flat_logs['pool/scripted_opp_types'] = len(pool_state['scripted_opps_list'])
    flat_logs['pool/scripted_pfsp_enabled'] = float(pool_state['scripted_pfsp_enabled'])
    if not pool_state['scripted_pfsp_enabled']:
        return

    winrates = pool_state['scripted_winrates']
    weights = pool_state['scripted_weights']
    games = pool_state['scripted_games']
    flat_logs['pool/scripted_pfsp_updates'] = pool_state['scripted_updates']
    flat_logs['pool/scripted_games'] = float(games.sum())
    flat_logs['pool/scripted_min_winrate'] = float(winrates.min())
    flat_logs['pool/scripted_mean_winrate'] = float(winrates.mean())
    flat_logs['pool/scripted_max_winrate'] = float(winrates.max())
    flat_logs['pool/scripted_max_weight'] = float(weights.max())
    flat_logs['pool/scripted_min_weight'] = float(weights.min())
    for opp, winrate, weight, n_games in zip(
            pool_state['scripted_opps_list'], winrates, weights, games):
        flat_logs[f'pool/scripted_opp_{opp}_winrate'] = float(winrate)
        flat_logs[f'pool/scripted_opp_{opp}_weight'] = float(weight)
        flat_logs[f'pool/scripted_opp_{opp}_games'] = float(n_games)


def step(pufferl, backend, pool_state, flat_logs, epoch):
    if pool_state is None:
        return

    update_scripted_pfsp(pufferl, backend, pool_state)

    n_window = float(flat_logs.get('env/n', 0.0))
    num_banks = pool_state['num_banks']

    # 1. Per-bank Elo update from the most recent rollout window.
    for b in range(num_banks):
        bank = pool_state['banks'][b]
        hist_score_w = float(flat_logs.get(f'env/hist_score_bank_{b}', 0.0)) * n_window
        hist_n_w     = float(flat_logs.get(f'env/hist_n_bank_{b}',     0.0)) * n_window
        if hist_n_w > 0.0:
            bank['hist_score'] += hist_score_w
            bank['hist_n']     += hist_n_w
            score_rate = hist_score_w / hist_n_w
            new_p, new_o = update_elo(pool_state['primary_elo'],
                bank['cur_opp_elo'], score_rate, pool_state['elo_k'])
            # All banks update the shared primary Elo. Multiple banks updating
            # primary in one step is fine — Elo is symmetric, just a few more
            # tiny adjustments per rollout window.
            pool_state['primary_elo'] = new_p
            bank['cur_opp_elo'] = new_o
            for entry in pool_state['pool']:
                if entry['path'] == bank['cur_opp_path']:
                    entry['elo'] = new_o
                    break

    # 2. Global snapshot cadence (shared across banks).
    if (pool_state['snapshot_interval'] > 0
            and pufferl.global_step - pool_state['last_snapshot_step']
                >= pool_state['snapshot_interval']):
        snap_path = os.path.join(pool_state['pool_dir'],
            f'{pufferl.global_step:016d}.bin')
        backend.save_weights(pufferl, snap_path)
        pool_state['pool'].append({
            'path': snap_path,
            'elo': pool_state['primary_elo'],
            'winrate': 0.5,
            'n_games': 0,
        })
        pool_state['pool'] = evict(pool_state['pool'], pool_state['max_size'])
        pool_state['last_snapshot_step'] = int(pufferl.global_step)

    # 3. Per-bank swap logic. Each bank decides independently based on its own
    # winrate. Tags 1..num_banks correspond to bank 0..num_banks-1.
    for b in range(num_banks):
        bank = pool_state['banks'][b]
        winrate = (bank['hist_score'] / bank['hist_n']
                       if bank['hist_n'] > 0 else None)
        winrate_met = (winrate is not None
            and bank['hist_n'] >= pool_state['min_games']
            and winrate >= pool_state['swap_winrate'])
        timed_out = (pool_state['opp_timeout_steps'] > 0
            and pufferl.global_step - bank['opp_started_step']
                >= pool_state['opp_timeout_steps'])
        tag_value = b + 1

        if bank['pending_opp_path'] is not None:
            if backend.count_aligned(pufferl, tag_value, 0) >= bank['num_hist_envs']:
                backend.load_frozen_bank(pufferl, b, bank['pending_opp_path'])
                backend.count_aligned(pufferl, tag_value, 1)
                bank['cur_opp_path'] = bank['pending_opp_path']
                bank['cur_opp_elo'] = bank['pending_opp_elo']
                bank['pending_opp_path'] = None
                bank['pending_opp_elo'] = None
                bank['hist_score'] = 0.0
                bank['hist_n'] = 0.0
                bank['opp_started_step'] = int(pufferl.global_step)
                bank['last_epochs_to_align'] = epoch - bank['epoch_armed']
        elif winrate_met or timed_out:
            if winrate is not None and bank['hist_n'] > 0:
                for entry in pool_state['pool']:
                    if entry['path'] == bank['cur_opp_path']:
                        prev_n = entry.get('n_games', 0)
                        alpha = 1.0 if prev_n == 0 else 0.3
                        entry['winrate'] = (
                            (1 - alpha) * entry.get('winrate', 0.5)
                            + alpha * winrate)
                        entry['n_games'] = prev_n + int(bank['hist_n'])
                        break
            if winrate_met and len(pool_state['pool']) < 10:
                snap_path = os.path.join(pool_state['pool_dir'],
                    f'{pufferl.global_step:016d}.bin')
                backend.save_weights(pufferl, snap_path)
                pool_state['pool'].append({
                    'path': snap_path,
                    'elo': pool_state['primary_elo'],
                    'winrate': 0.5,
                    'n_games': 0,
                })
                pool_state['pool'] = evict(pool_state['pool'], pool_state['max_size'])
                pool_state['last_snapshot_step'] = int(pufferl.global_step)
            opp_entry = sample_opponent(pool_state['pool'], pool_state['rng'],
                mode=pool_state.get('sampling', 'sqrt'))
            bank['pending_opp_path'] = opp_entry['path']
            bank['pending_opp_elo'] = opp_entry['elo']
            bank['epoch_armed'] = epoch
            bank['last_winrate_at_swap'] = winrate if winrate is not None else 0.0

    # 4. Emit logs — per-bank and aggregate.
    flat_logs['pool/size']     = len(pool_state['pool'])
    flat_logs['env/elo']       = pool_state['primary_elo']
    flat_logs['pool/num_banks'] = num_banks
    log_scripted_pfsp(pool_state, flat_logs)
    total_score = 0.0
    total_n     = 0.0
    for b in range(num_banks):
        bank = pool_state['banks'][b]
        wr = (bank['hist_score'] / bank['hist_n']
              if bank['hist_n'] > 0 else None)
        flat_logs[f'pool/winrate_at_swap_bank_{b}'] = bank['last_winrate_at_swap']
        flat_logs[f'pool/epochs_to_align_bank_{b}'] = bank['last_epochs_to_align']
        if wr is not None:
            flat_logs[f'pool/winrate_bank_{b}']           = wr
            flat_logs[f'env/historical_winrate_bank_{b}'] = wr
        total_score += bank['hist_score']
        total_n     += bank['hist_n']
    # Aggregate winrate across all banks (legacy compat with old dashboards).
    if total_n > 0:
        agg = total_score / total_n
        flat_logs['pool/winrate']           = agg
        flat_logs['env/historical_winrate'] = agg
