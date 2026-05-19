#define _POSIX_C_SOURCE 200112L

#include "../osrs_assets.h"
#include <assert.h>
#include <string.h>

int main(void) {
    unsetenv("OSRS_ASSET_ROOT");
    const char* default_path = OSRS_ASSET("sprites/gui/compass.png");
    assert(strcmp(default_path, "ocean/osrs/data/sprites/gui/compass.png") == 0);

    setenv("OSRS_ASSET_ROOT", "/tmp/osrs-data", 1);
    const char* override_path = OSRS_ASSET("sprites/gui/compass.png");
    assert(strcmp(override_path, "/tmp/osrs-data/sprites/gui/compass.png") == 0);

    const char* rooted_path = OSRS_ASSET("/var/tmp/custom.bin");
    assert(strcmp(rooted_path, "/var/tmp/custom.bin") == 0);

    const char* normalized_path = OSRS_ASSET("ocean/osrs/data/equipment.models");
    assert(strcmp(normalized_path, "/tmp/osrs-data/equipment.models") == 0);

    const char* a = OSRS_ASSET("a.bin");
    const char* b = OSRS_ASSET("b.bin");
    assert(strcmp(a, "/tmp/osrs-data/a.bin") == 0);
    assert(strcmp(b, "/tmp/osrs-data/b.bin") == 0);

    unsetenv("OSRS_ASSET_ROOT");
    assert(osrs_asset_exists("sprites/gui/compass.png"));
    OsrsAssetBytes bytes = osrs_asset_read_all("sprites/gui/compass.png");
    assert(bytes.data);
    assert(bytes.size > 0);
    osrs_asset_bytes_free(&bytes);
    assert(!bytes.data);
    assert(bytes.size == 0);
    return 0;
}
