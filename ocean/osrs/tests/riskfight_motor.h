#ifndef RISKFIGHT_MOTOR_H
#define RISKFIGHT_MOTOR_H
#include <math.h>

/* Our hand: when a chain of clicks started at a tick's start is pressed, and on which game tick the server processes each click.
 * Measured on 11,202 queued gear-swapper clicks of 167 own recordings (calib README Fact 20): the k-th click of a chain commits, in ms after the
 * tick start, at p50/p90 261/343, 437/620, 639/898, 841/1141, 1161/1417; one click is processed on the first tick after the decision (96% of first
 * clicks, 37% of second clicks), three clicks in one tick 0 of 757 below 400 ms, five 0 of 114 below 858 ms; the server takes a click for the next
 * tick only when it arrives before about 465 ms (n 2,373; the plugin's own 405 is safe, 0 of 1,710 late).
 *
 * Model: click k comes GAP_k after click k-1 (click 0 after the tick start). GAP_0 is lognormal; GAP_k (k >= 1) is a 76 ms floor (no hand clicks
 * faster) plus a lognormal, k >= 4 repeating the last row. Parameters from fit_motor_clock.py (Nelder-Mead on the ten quantiles above plus
 * P(first < 405) = .96, P(second < 405) = .37, P(third < 400) = 0 and P(fifth < 858) = 0): model p50/p90 263/352, 436/623, 656/897,
 * 861/1108, 1178/1425; P(third < 400) .002, P(fifth < 858) .0004. The opponent has no motor model on purpose: his reaction latency is inside
 * his fitted hazards, and a second meal on the tick after a first one is 0.3% (2 of 744, Fact 9), so his chains do not slip across ticks. */
enum { MOTOR_CUTOFF_MS = 465, MOTOR_TICK_MS = 600, MOTOR_ROWS = 5 };
static const double MOTOR_FLOOR_MS = 76.35;
static const double MOTOR_MU[MOTOR_ROWS] = {5.5774, 4.3777, 4.8085, 4.8084, 5.4684};
static const double MOTOR_SIGMA[MOTOR_ROWS] = {0.2262, 0.9077, 0.6059, 0.2840, 0.1231};

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
