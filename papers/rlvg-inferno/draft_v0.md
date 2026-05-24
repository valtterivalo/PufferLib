# Training Through the Inferno: A Systems Account of a Modern Game RL Benchmark

## Abstract

Modern video games can make useful reinforcement learning benchmarks, but only if the benchmark is fast enough to train, faithful enough to debug, and clear enough for a human to inspect. We describe an Old School RuneScape Inferno environment built in PufferLib 4. The Inferno is a 69-wave solo challenge with prayer switching, line-of-sight positioning, resource management, a moving-shield final boss, and long episodes that can run for thousands of game ticks. These mechanics stress the parts of RL that small toy games often avoid: partial observability, delayed consequences, action validity, sparse terminal success, and behavior that must make sense to a player.

The main contribution is a benchmark design pattern instead of a single algorithmic novelty. The environment exposes a compact symbolic observation, a structured multi-head action space, fail-loud action masks, a real-time renderer, replay tools, and behavior-level logs. PufferLib 4 supplies the training substrate: native CUDA kernels, static memory, CUDA graph capture, recurrent PufferNet policies, prioritized current-epoch replay, Protein sweeps, and browser-side inference. A compact Redemption checkpoint trained for 171.7M environment steps reached 0.49 logged evaluation win rate and 0.736 score, with average terminal progress near the Zuk healer threshold. The result is not a final solved benchmark. It is evidence that high-throughput systems work and game-faithful environment engineering have to co-evolve.

## 1. Why The Inferno?

The Old School RuneScape Inferno is a solo player-versus-monster challenge released in 2017. The player must survive 69 waves and defeat TzKal-Zuk to earn the infernal cape. The OSRS Wiki describes the activity as a no-restock encounter, with three pillars that block projectiles during the waves and a mandatory moving shield during the final boss fight [1]. That makes it a useful game RL target for a simple reason: a competent player can explain almost every failure.

If the agent dies to a Jal-Zek, it probably missed a magic prayer. If a pillar falls, it ignored nibblers. If it loses Zuk shield health, it failed to tag spawns. If it dies after healers spawn, it mishandled priority, movement, prayer, or supplies. These are not opaque score changes. They are inspectable mistakes in a known game.

This matters for RLVG benchmarks. Many game benchmarks offer dense pixels and an easy score, but leave researchers guessing why a policy failed. The Inferno goes the other way. It has a structured domain model, a small action interface, explicit legal-action masks, and a renderer that lets a human watch the same mistakes the policy experienced. The hard part is not wrapping a game binary. The hard part is turning expert game knowledge into a simulator that trains fast while still failing in recognizable ways.

## 2. Environment Surface

The current Inferno environment uses 744 observation features, 9 action heads, and 89 action mask logits. Episodes are long by arcade RL standards, usually hundreds to thousands of 0.6 second game ticks. The environment runs many agents in parallel and writes fixed-size observations and masks directly into native PufferLib buffers.

The observation vector is symbolic, not pixel-based. It contains player health, prayer, resources, wave phase, gear state, combat stats, target state, weapon range, NPC slots, pillar state, pending hits, pending Zuk healer area attacks, and a short step-out forecast. Positions are normalized and mostly egocentric or arena-relative. Categories use one-hot structure or slot membership. This follows a simple benchmark rule: expose the state a skilled player would use, then log whether the agent uses it.

The action space is a structured interface over OSRS intent:

- movement
- overhead prayer
- target selection
- gear switching
- food
- potion
- spell
- special attack
- offensive prayer

The target head points into observation slots rather than raw entity ids. The mask writer validates every head and records zero-valid-head failures. This choice matters because invalid actions are not an exploration challenge here. They are interface noise. The policy should decide whether to run west, pray magic, tag a healer, cast barrage, or drink a restore. It should not spend capacity discovering that an absent NPC cannot be attacked.

Reward and score are separate. The reward has shaped terms for damage, healer tagging, shield preservation, late-wave pressure, and final-phase priorities. The score is a comparable progress metric used for sweeps and evaluation. This split was necessary because the reward changed many times during development, while the project still needed a stable way to compare policies across runs.

## 3. Training Substrate

PufferLib 4 is a good match for this environment because the bottleneck includes the full edit-train-watch loop. The public Puffer docs describe a native backend built from a small Python driver and CUDA C, with static contiguous allocations, CUDA graphs, async environment workers, pinned buffers, fused kernels, and PufferNet as the default recurrent model [2]. The Puffer 4 engineering article gives the motivation: small-model RL can be dominated by framework overhead unless memory layout, kernel launch overhead, and elementwise fusion are designed together [3].

PufferNet combines MinGRU with highway-style output mixing. MinGRU is attractive because it can train across the sequence dimension with a parallel scan, while still giving cheap recurrent single-step inference during rollouts [4, 5]. Highway connections add a gated output path without breaking the recurrent state scan [4, 6]. In the current codebase, the MinGRU fused scan is the load-bearing recurrent kernel. The same policy family is used for the Inferno checkpoint described here.

The trainer also matters at the algorithmic level. PufferLib uses a PPO-like native trainer with a custom advantage function that combines GAE-style recursion with VTrace-style importance clipping, plus prioritized replay over current-epoch trajectory segments. The default Inferno config uses 4096 agents, two rollout buffers, 16 worker threads, a two-layer 512 hidden PufferNet, a 16 tick horizon, priority replay, and a replay ratio of 4.

Hyperparameter search is not an afterthought. Protein treats a sweep as the basic unit of RL work. It models score and wall-clock cost, samples around Pareto points, applies cost caps, and uses downsampled learning curves as observations. The Puffer blog frames this as a practical replacement for single-run tuning, especially when reward scales and useful run lengths vary by environment [7]. Inferno used this style directly: sweeps covered learning rate, horizon, discounting, advantage, replay, vector size, model size, curriculum fractions, and reward weights.

## 4. Curriculum And Debugging

The hardest engineering lesson was that the simulator and the learner could not be debugged separately. Training exposed missing observations, stale masks, reward traps, and render bugs. Rendering exposed wrong projectiles, stuck pathing, broken projectile anchoring, missing animations, and cases where a policy looked good in scalar logs but bad to a player.

The environment therefore grew a few non-negotiable debugging surfaces.

First, it is playable. Human controls use the same action heads as the policy. If a human cannot click an NPC, path around a pillar, or flick a prayer, the policy surface is suspect.

Second, it has a 3D renderer and replay path. The renderer is not a cosmetic demo. It is the fastest way to catch semantic bugs. The mager projectile, blob projectile anchor, Zuk pathing, healer tags, and visual action timing all became easier to reason about once the policy could be watched in the same lab view used for the website.

Third, it logs failure-mode metrics. The Inferno log includes win rate, wave, episode length, damage, minimum Zuk HP, prayer correctness, idle ticks, gear switches, supplies, Zuk healer damage, shield damage, and late-phase progress. These metrics are chosen because they change decisions. A single episode return cannot tell whether the agent died because it failed prayer, wasted brews, ignored healers, or tried to preserve shaped reward instead of finishing Zuk.

Fourth, it supports curriculum. The default config starts most agents from wave 1, but mixes in later waves including wave 56, 67, 69, 70, and 71. This gave the policy repeated contact with Jad, triple Jad, Zuk, and healer states without removing the full-run objective. Later work added state-buffer curriculum and replay ladders for final-phase states. These techniques were useful only when the loaded state preserved the same invariants as normal reset, so snapshot and restore code became part of the benchmark contract.

## 5. Checkpoint Result

The best current public-demo candidate in this branch is the compact Redemption checkpoint from run `whl5mxay`. It used the compact Redemption overhead mapping, 4096 agents, a two-layer 512 hidden PufferNet, a 16 tick horizon, state curriculum, priority replay, and 171.7M environment steps. The stored run summary reports:

- win rate: 0.489635
- score: 0.736389
- average final wave: 66.720871
- average minimum Zuk HP on failed runs: 240.535599
- final logged throughput: about 256k steps per second

An older best checkpoint in the previous action surface reached 0.656983 logged win rate and 0.819216 score at similar scale. We do not present these as final benchmark numbers. They are development checkpoints from a changing environment. The useful result is that the system reached full clears and late-Zuk consistency while still leaving clear behavioral failures to fix.

That distinction is important. In a game benchmark with visible behavior, "solved" cannot mean only a high scalar score. A website checkpoint that clears but wastes supplies, mistimes healers, or relies on a simulator bug is not solved. The benchmark should keep enough observability that these failures remain embarrassing.

## 6. Lessons For RLVG Benchmarks

The first lesson is that game benchmarks need a player-facing contract. If the behavior looks wrong, the benchmark is probably wrong. A renderer, replay files, and human controls are not luxuries. They are test fixtures.

The second lesson is that action masks should be strict. OSRS has many impossible actions: attacking missing targets, drinking empty potions, casting unavailable spells, switching to already equipped gear, or stepping into blocked tiles. Masking these actions does not make the problem easy. It removes client ceremony so the model can spend capacity on game decisions.

The third lesson is that reward is not score. Reward can be shaped, swept, and thrown away. Score should survive those edits. In the Inferno, score tracks win and progress through the late fight. Reward pays for credit assignment.

The fourth lesson is that systems speed changes the research method. PufferLib 4 makes more mistakes affordable. The team can change a prayer observation, run a sweep, inspect a replay, and fix a renderer bug without waiting days. Joseph Suarez's Puffer 4 articles make the same point from the systems side: static memory, fused kernels, CUDA graphs, and MinGRU are not aesthetic choices. They make RL development cheap enough to be empirical [3, 4, 8].

The last lesson is that modern game RL does not need to choose between toy speed and game complexity. The Inferno is narrow compared with a full MMO, but it has real MMO mechanics: resources, timing, target priority, geometry, long episodes, and a final encounter that changes rules mid-fight. It is small enough to simulate and rich enough to punish lazy abstractions.

## 7. Limitations

This is an early-stage benchmark paper. The simulator is not a full OSRS client. It approximates combat and encounter mechanics that matter for training, then uses cache-derived render assets for inspection. The checkpoint results are from an active development branch. The environment changed during visual, action-space, and reward work, so old checkpoints cannot be compared without their exact configs.

The benchmark also depends on expert knowledge. That is both a feature and a risk. Expert mechanics make failures interpretable, but they can also hide assumptions in the simulator. The safest path is to keep tests, render checks, replay checks, and player controls close to the training surface.

## 8. Conclusion

The OSRS Inferno environment shows what a modern game RL benchmark can look like when systems work and environment design move together. The agent sees a compact game-state tensor, acts through a player-like interface, trains through a native recurrent RL stack, and can be watched in a browser. The benchmark asks whether the policy wins and whether the training run, logs, replay, and renderer agree about why.

## References

[1] OSRS Wiki. Inferno. https://oldschool.runescape.wiki/w/Inferno

[2] PufferAI. PufferLib docs. https://puffer.ai/docs.html

[3] Joseph Suarez. The Engineering behind PufferLib 4.0's 20,000,000 step/second RL Training. https://x.com/jsuarez/article/2041501879222284776

[4] Joseph Suarez. A High Throughput Recurrent Network for PufferLib 4.0. https://x.com/jsuarez/article/2042273938592407674

[5] Feng et al. Were RNNs All We Needed? arXiv:2410.01201. https://arxiv.org/abs/2410.01201

[6] Srivastava, Greff, and Schmidhuber. Highway Networks. arXiv:1505.00387. https://arxiv.org/abs/1505.00387

[7] PufferAI. PufferLib blog. https://puffer.ai/blog.html

[8] Joseph Suarez. Visualizing 20k RL Experiments with Constellation. https://x.com/jsuarez/article/2041896392587722885

[9] Joseph Suarez. Game Reinforcement Learning isn't Playing Around. https://x.com/jsuarez/article/1950587206290309307

[10] Suarez. PufferLib: Making Reinforcement Learning Libraries and Environments Play Nice. arXiv:2406.12905. https://arxiv.org/abs/2406.12905
