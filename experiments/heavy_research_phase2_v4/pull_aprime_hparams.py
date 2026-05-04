"""Pull exact swept hparams + summary stats for the 6 A' configs.

A1 (v3): kvuqyvh9, fs4s6vst, e3vyhuh9, 4c0z0c4n
A2 (control): 7os16e09, nq35wm0g

Output goes to aprime_hparams.json (machine readable for the runner)
and aprime_hparams.txt (human readable).
"""

import json
import wandb


PROJECT = "valtterivalo-clock-cloud/puffer4"
A1_IDS = ["kvuqyvh9", "fs4s6vst", "e3vyhuh9", "4c0z0c4n"]
A2_IDS = ["7os16e09", "nq35wm0g"]
SWEEP_KEYS = [
    "train.learning_rate",
    "train.ent_coef",
    "train.gamma",
    "train.gae_lambda",
    "train.min_lr_ratio",
    "train.clip_coef",
    "train.vf_coef",
    "train.vf_clip_coef",
    "train.max_grad_norm",
    "train.replay_ratio",
    "train.prio_alpha",
    "train.vtrace_rho_clip",
    "train.vtrace_c_clip",
]
SUMMARY_KEYS = [
    "env/zuk_objective_normal",
    "env/score_normal",
    "env/min_zuk_hp_normal",
    "env/wins_normal",
    "env/phase_reached_normal",
    "env/death_tick_normal",
    "env/episode_return_normal",
]


def safe_get(d, key, default=None):
    parts = key.split(".")
    cur = d
    for p in parts:
        if isinstance(cur, dict):
            cur = cur.get(p)
        else:
            return default
        if cur is None:
            return default
    return cur


def pull(api, run_id, arm_label):
    run = api.run(f"{PROJECT}/{run_id}")
    cfg = dict(run.config)
    summary = dict(run.summary)
    out = {
        "run_id": run_id,
        "arm": arm_label,
        "name": run.name,
        "tags": list(run.tags),
        "state": run.state,
        "hparams": {k: safe_get(cfg, k) for k in SWEEP_KEYS},
        "summary": {k: summary.get(k) for k in SUMMARY_KEYS},
    }
    return out


def main():
    api = wandb.Api(timeout=60)
    rows = []
    for rid in A1_IDS:
        rows.append(pull(api, rid, "A1_v3"))
    for rid in A2_IDS:
        rows.append(pull(api, rid, "A2_ctrl"))

    out_json = "experiments/heavy_research_phase2_v4/aprime_hparams.json"
    with open(out_json, "w") as f:
        json.dump(rows, f, indent=2)

    out_txt = "experiments/heavy_research_phase2_v4/aprime_hparams.txt"
    with open(out_txt, "w") as f:
        for r in rows:
            f.write(f"{'='*78}\n")
            f.write(f"{r['arm']}  {r['run_id']}  ({r['name']})  state={r['state']}\n")
            f.write(f"{'='*78}\n")
            f.write("Summary:\n")
            for k, v in r["summary"].items():
                if v is None:
                    continue
                if isinstance(v, float):
                    f.write(f"  {k:<35} {v:.4f}\n")
                else:
                    f.write(f"  {k:<35} {v}\n")
            f.write("Hparams (use these literally for A'):\n")
            for k, v in r["hparams"].items():
                if v is None:
                    continue
                if isinstance(v, float):
                    f.write(f"  {k:<35} {v:.6g}\n")
                else:
                    f.write(f"  {k:<35} {v}\n")
            f.write("\n")

    print(f"wrote {out_json} and {out_txt}")
    for r in rows:
        obj = r["summary"].get("env/zuk_objective_normal")
        score = r["summary"].get("env/score_normal")
        hp = r["summary"].get("env/min_zuk_hp_normal")
        print(f"  {r['arm']:<8} {r['run_id']}  obj={obj}  score={score}  min_hp={hp}")


if __name__ == "__main__":
    main()
