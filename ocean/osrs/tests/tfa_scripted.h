#ifndef TFA_SCRIPTED_H
#define TFA_SCRIPTED_H
#include "../encounters/encounter_tfa.h"

#define tfa_scripted_actions tfa_heuristic_actions

/** Party size of a recorded fight: party_size of tfa_fights.tsv beside the events fixture at events_path. */
static int tfa_fixture_party(const char* events_path, const char* fight_id) {
    char path[1024];
    snprintf(path, sizeof(path), "%.*s/tfa_fights.tsv", (int)(strrchr(events_path, '/') - events_path), events_path);
    FILE* fp = fopen(path, "r");
    assert(fp);
    char line[2048];
    int party = 0;
    while (!party && fgets(line, sizeof(line), fp)) {
        char* col[11] = {0};
        char* save = NULL;
        int n = 0;
        for (char* tok = strtok_r(line, "\t\n", &save); tok && n < 11; tok = strtok_r(NULL, "\t\n", &save)) col[n++] = tok;
        if (strcmp(col[0], fight_id) == 0) party = atoi(col[10]);
    }
    fclose(fp);
    assert(party > 0);
    return party;
}

#endif
