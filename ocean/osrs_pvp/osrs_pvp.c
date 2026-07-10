/* No OSRS_VISUAL_DEFAULT_ENCOUNTER: a NULL encounter routes the native
   pvp_init path in osrs_visual.c, which is the PvP env. --encounter still
   overrides to view another encounter from this binary. */
#define OSRS_VISUAL
#include "../osrs/osrs_visual.c"
