# RLVG Inferno OpenReview Form Fields v0

Status: copy text for OpenReview fields, derived from the current `paper_v0.tex`.

## Upload

Upload only `papers/rlvg-inferno/paper_v0.pdf`.

## Title

Training Through the Inferno: An Inspectable OSRS Boss-Fight Benchmark for RL

## Abstract

Modern game RL benchmarks need fast training, inspectable failures, and enough simulator fidelity that policy mistakes mean something. We present an Old School RuneScape Inferno benchmark built in PufferLib 4. The Inferno is a 69-wave solo boss encounter with prayer switching, line-of-sight positioning, supplies, target priority, and a moving-shield final phase. These mechanics create long episodes, partial observability, sparse success, and failure modes that skilled players can read.

The contribution is a benchmark design pattern: player-readable state, structured actions, strict masks, replayable failures, behavior-level logs, and high-throughput recurrent training. The current environment exposes 744 symbolic observation features and a 9-head action space with 89 total discrete choices. The rollout row appends one mask entry per choice, for 833 floats total. We use PufferLib 4's native recurrent RL stack to make large-agent, long-episode iteration practical. A compact recurrent checkpoint from the preceding compact Redemption action surface trained for 171.7M environment steps and reached 0.49 logged development win rate and 0.736 internal training score in the stored run summary. This run is development telemetry from an earlier compact action mapping, not a frozen-schema evaluation of the current 744-feature, 9-head, 89-choice surface. We use it only to show that this benchmark line can produce logged full clears and late-fight diagnostics.

## Submission Type

Short-form.

## Author Fields

Use actual human authors in OpenReview if the site asks for author metadata. Keep the uploaded PDF anonymous.
