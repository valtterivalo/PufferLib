#pragma once
/* The data-fitted opponent (tests/riskfight_oppmodel.h, riskfight_opp.h) for the trainer. It lives in a C translation unit of its own because its headers
 * (gearswapper tables) are not C++; the env sees only this interface. `state` is a RiskfightState. */
#ifdef __cplusplus
extern "C" {
#endif
void* oppfit_create(void);
void oppfit_destroy(void* bridge);
/** Starts a fight: fresh history, his Vengeance and meal types drawn from `seed`, hit observer installed on the state. Call after riskfight_reset. */
void oppfit_reset(void* bridge, void* state, uint32_t seed);
/** His head actions for the tick about to be processed. */
void oppfit_actions(void* bridge, const void* state, int* actions);
/** After the tick: advances the history he reads. */
void oppfit_after(void* bridge, void* state);
#ifdef __cplusplus
}
#endif
