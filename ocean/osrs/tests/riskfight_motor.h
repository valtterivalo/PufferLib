#ifndef RISKFIGHT_MOTOR_H
#define RISKFIGHT_MOTOR_H
#include <math.h>

/* Our hand: when a chain of clicks started at a tick's start is pressed, and on which game tick the server processes each click.
 * The engine's hand since light PR #360 (2026-10-19): a fresh run's first input waits a primed human reaction (HumanReaction.PRIMED, lognormal median 180 ms,
 * p1 133, p99 243) after the tick start, then the hop plays and presses on arrival; the run's later clicks follow on at the actuator's batch pace, with no
 * second reaction. light's engineReactionTimingSelfTest (the actuator's own code over the donor library) gives the first press at p10/p50/p90 248/285/325
 * ms after the tick start on 5-click inventory runs (235/272/317 on 28 recorded 3-click batches), each later gap 76/97/115 (min 71), P(third press before
 * the 465 ms cutoff) 0.37-0.41. Before #360 the engine pressed its first click at 111 ms (p50) and all three of a triple eat landed on one tick; the old
 * model here was fitted on the user's own hotkey swaps (calib README Fact 20: 261/437/639 ms p50 for clicks 1-3), slower than the engine on every click.
 *
 * Model: click k comes GAP_k after click k-1 (click 0 after the tick start). GAP_0 is lognormal (median 280, p90 322); GAP_k (k >= 1) is a 71 ms floor (no
 * hand clicks faster) plus a lognormal (median 26, p90 44: gaps p50/p90 97/115), k >= 4 repeating the last row. The server takes a click for the next tick
 * only when it arrives before about 465 ms (n 2,373 live). A leading tab hotkey is not modelled: the sim has no tab switch. The opponent has no motor
 * model on purpose: his reaction latency is inside his fitted hazards, and a second meal on the tick after a first one is 0.3% (2 of 744, Fact 9), so his
 * chains do not slip across ticks. */
enum { MOTOR_CUTOFF_MS = 465, MOTOR_TICK_MS = 600, MOTOR_ROWS = 5 };
static const double MOTOR_FLOOR_MS = 71.0;
static const double MOTOR_MU[MOTOR_ROWS] = {5.6348, 3.2581, 3.2581, 3.2581, 3.2581};
static const double MOTOR_SIGMA[MOTOR_ROWS] = {0.1091, 0.4108, 0.4108, 0.4108, 0.4108};

/** Inverse of the standard normal CDF (Acklam), u in (0, 1). */
static double motor_inverse_normal(double u) {
    static const double a[] = {-3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02, 1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239e+00};
    static const double b[] = {-5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02, 6.680131188771972e+01, -1.328068155288572e+01};
    static const double c[] = {-7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00, -2.549732539343734e+00, 4.374664141464968e+00, 2.938163982698783e+00};
    static const double d[] = {7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00, 3.754408661907416e+00};
    if (u < 1e-9) u = 1e-9;
    if (u > 1 - 1e-9) u = 1 - 1e-9;
    if (u < 0.02425) {
        double q = sqrt(-2 * log(u));
        return (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) / ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1);
    }
    if (u > 1 - 0.02425) {
        double q = sqrt(-2 * log(1 - u));
        return -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) / ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1);
    }
    double q = u - 0.5, r = q * q;
    return (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q / (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1);
}

/** Milliseconds from click `index - 1` (the tick start for index 0) to click `index` of a chain, for a uniform draw u in [0, 1). */
static double motor_gap_ms(double u, int index) {
    int row = index < MOTOR_ROWS ? index : MOTOR_ROWS - 1;
    double gap = exp(MOTOR_MU[row] + MOTOR_SIGMA[row] * motor_inverse_normal(u));
    return index ? MOTOR_FLOOR_MS + gap : gap;
}

/** The game tick that processes a click pressed `ms` after the start of tick `tick` (the decision tick). */
static int motor_land_tick(int tick, double ms) {
    return tick + (ms < MOTOR_CUTOFF_MS ? 1 : 2 + (int)((ms - MOTOR_CUTOFF_MS) / MOTOR_TICK_MS));
}
#endif
