#ifndef GS_STAKES_H
#define GS_STAKES_H
/* Stakes.java: what each side loses on death, and the ratio of his loss over ours. */
#include "gs_core.h"
#include "gs_prices.h"

typedef struct { long unit, quantity; } GsHolding;
typedef struct { long us, him; } GsStakes;

/* Untradeable.java: the coins a PvP death moves for an item without a market price, the dearer of that and the price. */
static const GsPriceRow GS_UNTRADEABLE[] = {
    {6570, 150000L},
    {21295, 225000L},
    {10551, 150000L},
    {8850, 35000L},
    {12954, 240000L},
    {22322, 600000L},
    {11663, 160000L},
    {11664, 160000L},
    {11665, 160000L},
    {8839, 180000L},
    {8840, 180000L},
    {8842, 120000L},
    {13072, 250000L},
    {13073, 250000L},
    {7462, 78000L},
    {24224, 500000L},
    {27831, 5000000L},
    {27832, 5000000L},
    {27900, 5000000L},
    {27908, 10000000L},
    {27912, 5000000L},
    {27916, 10000000L},
    {27920, 5000000L}};

static long gs_price(int item) {
    long market = 0, coins = 0;
    for (size_t i = 0; i < sizeof(GS_PRICES) / sizeof(*GS_PRICES); i++) if (GS_PRICES[i].item == item) market = GS_PRICES[i].price;
    for (size_t i = 0; i < sizeof(GS_UNTRADEABLE) / sizeof(*GS_UNTRADEABLE); i++) if (GS_UNTRADEABLE[i].item == item) coins = GS_UNTRADEABLE[i].price;
    return market > coins ? market : coins;
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
