You are helping with a technical research investigation.

I am providing:

1. A written context brief in `brief.md`
2. A repository digest in `repo_context.txt`
3. The original and revised Go-Explore papers separately

Task:

- Understand the relevant implementation and experiment history.
- Identify the most plausible explanations for the current `osrs_inferno` bottleneck.
- Rank hypotheses by likelihood and expected value.
- Point to specific files, functions, metrics, or experiment outcomes that matter.
- Propose concrete next experiments or code changes.
- Call out where the provided context is insufficient.

Please keep the analysis evidence-led. We are trying to avoid anchoring on our current favorite explanations.

Structure your answer as:

1. Summary of the problem
2. What the evidence says
3. Likely causes, ranked
4. Code-level evidence
5. Recommended experiments or fixes
6. Metrics and gates
7. Risks and tradeoffs
8. What extra context would most improve confidence

Do not give generic RL advice. Work from the evidence in the brief, digest, and papers.
