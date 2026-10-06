#ifndef GS_STAKES_H
#define GS_STAKES_H
/* Stakes.java: what each side loses on death, and the ratio of his loss over ours. */
#include "gs_core.h"
#include "gs_prices.h"

typedef struct { long unit, quantity; } GsHolding;
typedef struct { long us, him; } GsStakes;

static long gs_price(int item) {
    for (size_t i = 0; i < sizeof(GS_PRICES) / sizeof(*GS_PRICES); i++) if (GS_PRICES[i].item == item) return GS_PRICES[i].price;
    return 0;
}

static GsStakes gs_stakes_of(long us, long him) { return us <= 0 || him <= 0 ? (GsStakes){1, 1} : (GsStakes){us, him}; }
static double gs_stakes_ratio(GsStakes s) { return (double)s.him / (double)s.us; }
static int gs_stakes_kept(int skulled, int protect) { return (skulled ? 0 : 3) + (protect ? 1 : 0); }

static int gs_holding_order(const void* a, const void* b) {
    long x = ((const GsHolding*)a)->unit, y = ((const GsHolding*)b)->unit;
    return x < y ? 1 : x > y ? -1 : 0;
}

static long gs_stakes_lost(GsHolding* holdings, int count, int kept) {
    qsort(holdings, (size_t)count, sizeof(GsHolding), gs_holding_order);
    long left = kept, lost = 0;
    for (int i = 0; i < count; i++) {
        long keeps = left < holdings[i].quantity ? left : holdings[i].quantity;
        left -= keeps;
        lost += holdings[i].unit * (holdings[i].quantity - keeps);
    }
    return lost;
}
#endif
