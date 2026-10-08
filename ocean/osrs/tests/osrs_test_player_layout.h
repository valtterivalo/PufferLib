#ifndef OSRS_TEST_PLAYER_LAYOUT_H
#define OSRS_TEST_PLAYER_LAYOUT_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline void osrs_test_require_pve_status_zero(const Player* player) {
    const OsrsStatusState zero = {0};
    assert(memcmp(&player->status, &zero, sizeof(zero)) == 0);
    for (int i = 0; i < MAX_PENDING_HITS; i++)
        assert(player->pending_hits[i].affliction == OSRS_AFFLICTION_NONE);
}

static inline size_t osrs_test_legacy_player_bytes(const Player* player, uint8_t* out) {
    osrs_test_require_pve_status_zero(player);
    const uint8_t* bytes = (const uint8_t*)player;
    size_t size = offsetof(Player, status);
    memcpy(out, bytes, size);
    size_t start = offsetof(Player, status) + sizeof(player->status);
    size_t length = offsetof(Player, pending_hits) - start;
    memcpy(out + size, bytes + start, length);
    size += length;
    for (int i = 0; i < MAX_PENDING_HITS; i++) {
        const uint8_t* hit = (const uint8_t*)&player->pending_hits[i];
        length = offsetof(PendingHit, affliction);
        memcpy(out + size, hit, length);
        size += length;
        start = offsetof(PendingHit, affliction) + sizeof(player->pending_hits[i].affliction);
        length = sizeof(PendingHit) - start;
        memcpy(out + size, hit + start, length);
        size += length;
    }
    start = offsetof(Player, pending_hits) + sizeof(player->pending_hits);
    length = sizeof(Player) - start;
    memcpy(out + size, bytes + start, length);
    return size + length;
}

#endif
