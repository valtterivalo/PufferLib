"""Confirm the flag-off policy has the same flat param count as the Linear baseline,
and the flag-on policy adds exactly the entity-encoder weights."""

import sys
sys.argv = ["puffer"]

import pufferlib.pufferl as pufferl
from pufferlib import _C


def make_args(entity_encoder):
    args = pufferl.load_config("osrs_colosseum")
    # Shrink so create_pufferl is cheap.
    args["vec"]["total_agents"] = 256
    args["vec"]["num_buffers"] = 1
    args["train"]["minibatch_size"] = 256
    args["train"]["horizon"] = 16
    args["train"]["total_timesteps"] = 1_000_000
    args["policy"]["entity_encoder"] = entity_encoder
    args["rank"] = 0
    args["world_size"] = 1
    return args


def count(entity_encoder):
    args = make_args(entity_encoder)
    p = _C.create_pufferl(args)
    n = p.num_params()
    _C.close(p)
    return n


off = count(0)
on = count(1)

hidden = 1024
expected_extra = 16 * 43 + hidden * 16  # entity_l1 + entity_l2
print(f"flag-off params: {off}")
print(f"flag-on  params: {on}")
print(f"delta:           {on - off}  (expected {expected_extra})")
assert on - off == expected_extra, "entity encoder param delta mismatch"
print("PASS: flag-off == baseline, flag-on adds exactly entity_l1 + entity_l2")
