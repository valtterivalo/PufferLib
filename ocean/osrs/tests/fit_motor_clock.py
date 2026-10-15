#!/usr/bin/env python3
"""Fit of riskfight_motor.h to the execution budget of calib README Fact 20 (11,202 queued gear-swapper clicks, 167 recordings).

Click k of a chain comes GAP_k after click k-1 (click 0 after the tick start): GAP_0 lognormal(mu0, s0), GAP_k = floor + lognormal(mu_k, s_k) for k >= 1.
Targets: the k-th click's p50/p90 ms (261/343, 437/620, 639/898, 841/1141, 1161/1417), P(first < 405 ms) = .96, P(second < 405 ms) = .37,
P(third < 400 ms) = 0 (0 of 757 live) and P(fifth < 858 ms) = 0 (0 of 114). Nelder-Mead on squared relative quantile errors (numpy only).

    python3 fit_motor_clock.py   # prints MOTOR_MU, MOTOR_SIGMA, MOTOR_FLOOR_MS and the fitted quantiles
"""
import numpy as np

P50 = np.array([261, 437, 639, 841, 1161.0])
P90 = np.array([343, 620, 898, 1141, 1417.0])
Z = np.random.default_rng(1).standard_normal((60000, 5))


def simulate(x):
    gaps = np.exp(x[:5] + np.abs(x[5:10]) * Z)
    gaps[:, 1:] += np.exp(x[10])
    return np.cumsum(gaps, axis=1)


def loss(x):
    t = simulate(x)
    r = list((np.percentile(t, 50, axis=0) - P50) / P50) + list((np.percentile(t, 90, axis=0) - P90) / P90)
    r += [(np.mean(t[:, 0] < 405) - 0.96) * 3, (np.mean(t[:, 1] < 405) - 0.37) * 3, np.mean(t[:, 2] < 400) * 20, np.mean(t[:, 4] < 858) * 20]
    return float((np.array(r) ** 2).sum())


def nelder_mead(f, x0, iterations):
    n = len(x0)
    simplex = [x0] + [x0 + np.eye(n)[i] * 0.1 for i in range(n)]
    values = [f(s) for s in simplex]
    for _ in range(iterations):
        order = np.argsort(values)
        simplex = [simplex[i] for i in order]
        values = [values[i] for i in order]
        centre = np.mean(simplex[:-1], axis=0)
        reflected = centre + (centre - simplex[-1])
        fr = f(reflected)
        if fr < values[0]:
            expanded = centre + 2 * (centre - simplex[-1])
            fe = f(expanded)
            simplex[-1], values[-1] = (expanded, fe) if fe < fr else (reflected, fr)
        elif fr < values[-2]:
            simplex[-1], values[-1] = reflected, fr
        else:
            contracted = centre + 0.5 * (simplex[-1] - centre)
            fc = f(contracted)
            if fc < values[-1]:
                simplex[-1], values[-1] = contracted, fc
            else:
                simplex = [simplex[0]] + [simplex[0] + 0.5 * (s - simplex[0]) for s in simplex[1:]]
                values = [values[0]] + [f(s) for s in simplex[1:]]
    best = int(np.argmin(values))
    return simplex[best], values[best]


if __name__ == "__main__":
    start = np.array([5.5655, 4.7, 4.7, 4.7, 5.0, 0.2, 0.6, 0.6, 0.4, 0.2, np.log(60)])
    x, value = nelder_mead(loss, start, 500)
    print("MOTOR_MU", np.round(x[:5], 4).tolist())
    print("MOTOR_SIGMA", np.round(np.abs(x[5:10]), 4).tolist())
    print("MOTOR_FLOOR_MS", round(float(np.exp(x[10])), 2), "loss", value)
    t = simulate(x)
    print("p50", np.percentile(t, 50, axis=0).round(), "p90", np.percentile(t, 90, axis=0).round())
    for cutoff in (405, 465):
        print("P(click k < %d)" % cutoff, [round(float((t[:, k] < cutoff).mean()), 3) for k in range(5)])
    print("P(third < 400)", float((t[:, 2] < 400).mean()), "P(fifth < 858)", float((t[:, 4] < 858).mean()))
