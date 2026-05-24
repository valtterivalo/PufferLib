# RLVG Inferno Paper Source Notes

Status: draft notes, not submission prose.

## Submission Target

- Venue: RLVG 2026 short-form paper.
- Length: 4 pages, excluding references and appendices.
- Submission deadline: 2026-05-27 23:59 AoE.
- Review: double-blind.
- Fit: benchmarks and deployment, alignment and evaluation in games, data efficiency, multi-agent methods as future work.
- Source: `RLVG-call-for-papers.md`.

## Current Repo State

- Repo: `/Users/valtterivalo/Projects/pufferlib-metal`.
- Branch: `inferno-upstream-sync-may-14`.
- Head when notes started: `7c917f088`.
- PufferLib package version: `4.0.0`.
- Inventory command: `python3 ~/.codex/skills/pufferlib/scripts/puffer_inventory.py .`.
- Worktree was clean after committing the CFP file.

## Inferno Environment Facts

- `config/ocean/osrs_inferno.ini` states the compact surface directly: 9 action heads, 89 mask logits, 744 observation features, and long episodes in the 300 to 8000+ tick range.
- `ocean/osrs/encounters/inferno/encounter_inferno_model.inc` defines 69 waves, 18,000 max ticks, three pillars, Zuk, the moving shield, Jad, Zuk healers, and Inferno NPC definition ids.
- `ocean/osrs/encounters/inferno/encounter_inferno_player_actions.inc` defines the 9 action heads:
  - movement
  - overhead prayer
  - target
  - gear
  - food
  - potion
  - spell
  - special attack
  - offensive prayer
- `ocean/osrs/encounters/inferno/encounter_inferno_forecast.inc` defines the 744 feature observation layout from player state, phase, pillars, NPC slots, step-out forecasts, pending hits, and pending Zuk healer sparks.
- `ocean/osrs/encounters/inferno/encounter_inferno_obs_mask.inc` fail-loud checks that the obs writer emits exactly `INF_NUM_OBS` features and tracks zero-valid action heads.
- `ocean/osrs/encounters/inferno/encounter_inferno_render_snapshot.inc` logs wins, damage, prayer correctness, idle ticks, brews, blood healing, wave, NPC kills, gear switches, and minimum Zuk HP.
- `ocean/osrs/encounters/inferno/encounter_inferno_render_snapshot.inc` defines the archive `progress_score` for Zuk transitions, with a win returning 2.0 and partial Zuk progress derived from minimum Zuk HP plus late-add milestones.
- `ocean/osrs_inferno/binding.c` logs the table `score` separately. For full-start runs, `my_log` uses `wins + (1 - wins) * wave / 69 * 0.5`. This is the paper result metric, not the archive `progress_score`.

## Training And Checkpoint Facts

- Current default config uses:
  - `total_agents = 4096`
  - `num_buffers = 2`
  - `num_threads = 16`
  - `hidden_size = 512`
  - `num_layers = 2`
  - `total_timesteps = 162337668`
  - `horizon = 16`
  - `replay_ratio = 4.0`
  - `minibatch_size = 4096`
  - `score_metric = score`
  - Protein sweep target `score`
- The default sweep covers train horizon, learning rate, entropy, discounting, advantage, priority replay, vector size, model width, curriculum fractions, and reward weights.
- `checkpoints/osrs_inferno/redemption_j6bgoiu4_compact/README.txt` records:
  - source: `/puffertank/docker/goexplore_redemption_repro_20260521`
  - run: `whl5mxay`
  - group: `full-inferno-redemption-repro-compact`
  - tag: `j6bgoiu4-redemption-compact`
  - patch shape: compact Redemption overhead mapping, 5-action PvE head, old overhead-off action maps to Redemption in Inferno
  - result: wins `0.489635`, score `0.736389`, wave `66.720871`, minimum Zuk HP `240.535599`
  - checkpoint: `latest_eval_0000000255655936.bin`
- Current source now uses `ENCOUNTER_OVERHEAD_DIM_PVE_REDEMPTION`, so the public Inferno mask surface is 89 logits. The stored compact Redemption checkpoint belongs to the preceding local compact action mapping and should be described as development telemetry, not as a frozen-schema benchmark score.
- The two stored compact checkpoint binaries are both `7,997,440` bytes. The one-output-logit delta to the current 89-logit decoder would be `512 * 4 = 2,048` bytes, matching the expected incompatibility when loading the compact 88-logit checkpoint into the current explicit-Redemption decoder.
- Remote read-only check on 2026-05-25: `/puffertank/docker/goexplore_redemption_repro_20260521` still exists and contains logs plus compact checkpoint files, but it is not a usable source checkout for compatibility evaluation. Git resolves the top level as `/puffertank/docker`, the subdir has no tracked source files, and the run-specific directory mainly contains artifacts.
- `checkpoints/osrs_inferno/redemption_j6bgoiu4_compact/run_whl5mxay.json` shows 171,704,320 agent steps and approximately 256k SPS at the final logged point.
- The same run JSON records `eval_episodes = 10000`, `seed = 73`, `train.seed = 42`, and `state_curriculum_mode = 1`.
- The downsampled metric arrays include final `env/n = 6223`, but this should not be treated as a precise eval denominator because `pufferlib/pufferl.py` only exits evaluation after `env/n > eval_episodes`.
- Final logged metrics also include `env/frac_normal = 1.0`, `env/prayer_correct_rate = 0.853395`, `env/frac_healer_spawned_normal = 0.847019`, `env/frac_all_zuk_healers_dead_normal = 0.718624`, and `env/frac_min_hp_le_150_normal = 0.615780`.
- Earlier checkpoint folder `pzargw7t_best_20260517` records a higher logged win rate in the older action surface:
  - wins `0.656983`
  - score `0.819216`
  - 172,752,896 agent steps
  - approximately 274k SPS at the final logged point

## PufferLib 4 Source Facts

- `src/pufferlib.cu` defines static rollout and train buffers with shapes for observations, actions, values, logprobs, rewards, terminals, ratio, importance, and optional action masks.
- `src/pufferlib.cu` transposes rollout data from `(T, B)` to `(B, T)` for training.
- `src/pufferlib.cu` uses CUDA graph capture for the single-step rollout callback.
- `src/pufferlib.cu` copies action masks into rollout buffers when the environment opts in, then samples masked logits.
- `src/pufferlib.cu` has priority replay over current-epoch trajectory segments using absolute advantage.
- `src/pufferlib.cu` has a custom advantage function that combines GAE-like recursion with VTrace-style importance clipping.
- `src/models.cu` says the core of 4.0 is the MinGRU fused scan operation, used to parallelize training across sequence length.
- `src/models.cu` implements a MinGRU gate with highway output mixing.
- `pufferlib/sweep.py` implements Protein with Gaussian process score and cost models, Pareto observations, cost caps, Sobol startup samples, and a cost-weighted acquisition.
- `src/puffernet.h` provides a small C inference path used by browser demos and standalone visual runners.

## Public Sources

- Puffer docs: https://puffer.ai/docs.html
  - PufferLib 4 has a native backend, CUDA C kernels, static memory, CUDA graphs, async environment workers, action masks, PufferNet with MinGRU and highway layers, and Protein sweeps.
- Puffer blog: https://puffer.ai/blog.html
  - PufferLib 3 introduced Protein, Puffer Advantage, and a high-speed Ocean workflow.
  - The blog frames score and wall-clock cost as the sweep target, not a single handpicked run.
- PufferLib arXiv: https://arxiv.org/abs/2406.12905
  - Title: PufferLib: Making Reinforcement Learning Libraries and Environments Play Nice
  - Author: Joseph Suarez
  - PufferLib wraps diverse environments and scales them from classic benchmarks to complex simulators.
- PufferLib GitHub: https://github.com/PufferAI/PufferLib
  - Public branch is currently 4.0 and repo description matches the open-source toolkit.
- OSRS Wiki Inferno page: https://oldschool.runescape.wiki/w/Inferno
  - The Inferno is a solo minigame with 69 waves, a level 1400 TzKal-Zuk final boss, no restocking between waves, and a mandatory shield mechanic in the final fight.
- MinGRU paper: https://arxiv.org/abs/2410.01201
  - Title: Were RNNs All We Needed?
  - Authors: Leo Feng, Frederick Tung, Mohamed Osama Ahmed, Yoshua Bengio, and Hossein Hajimirsadeghi
  - Minimal GRU and LSTM variants are designed for parallel training and fewer parameters.
- Highway Networks: https://arxiv.org/abs/1505.00387
  - Gated highway connections help train deep networks.

## Joseph Suarez Articles Read Through Browser

- `https://x.com/jsuarez/article/2041501879222284776`
  - Title: The Engineering behind PufferLib 4.0's 20,000,000 step/second RL Training.
  - Main points: Torch and LibTorch became bottlenecks for small-model RL, static contiguous memory made CUDA graphing and profiling cleaner, fused kernels and bf16 stability mattered, async buffered vectorization and pinned memory helped.
- `https://x.com/jsuarez/article/2042273938592407674`
  - Title: A High Throughput Recurrent Network for PufferLib 4.0.
  - Main points: PufferNet combines MinGRU with highway connections, keeps single-step rollout efficiency, supports longer trajectory segments, and avoids normalization overhead for small recurrent models.
- `https://x.com/jsuarez/article/2041896392587722885`
  - Title: Visualizing 20k RL Experiments with Constellation.
  - Main points: Puffer 4 analysis uses many sweeps, trillions of interactions, browser-local experiment visualization, and a cost plus score lens instead of single-run worship.
- `https://x.com/jsuarez/article/1950587206290309307`
  - Title: Game Reinforcement Learning isn't Playing Around.
  - Main points: games are interpretable, fast enough to simulate, complex enough to require high-level reasoning, and useful for studying interaction.

## Paper Thesis Candidates

1. The Inferno is a good RLVG benchmark because it is long-horizon, timing-sensitive, mechanically interpretable, and visually inspectable.
2. PufferLib 4 makes this type of benchmark workable because it makes iteration cheap enough to debug the environment and the policy together.
3. The result is more than a checkpoint. The contribution is a development pattern: playable simulator, strict masks, comparable score, behavioral logs, visual replay, and high-throughput sweeps.
4. The paper should not oversell completion. It should report a development checkpoint, the exact logged performance, and the lessons from making failure modes visible.

## Draft Risks

- Double-blind review means the submission should avoid direct personal repo links, names, and local paths. Keep local evidence in notes, not in paper references.
- The public Puffer 4 articles are informal but valuable. Cite them as project notes or web articles, not peer-reviewed work.
- Do not claim solved Inferno unless a current no-render eval proves it. The compact redemption checkpoint logged 0.49 win rate, not near-perfect play.
- Use "score" separately from reward because reward coefficients changed across sweeps.
