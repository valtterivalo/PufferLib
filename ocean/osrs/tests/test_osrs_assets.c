#define _POSIX_C_SOURCE 200112L

#include "../osrs_assets.h"
#include <assert.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void test_asset_paths(void) {
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
}

static void test_manifest_path_validation(void) {
    assert(osrs_asset_manifest_path_is_valid("sprites/gui/compass.png"));
    assert(osrs_asset_manifest_path_is_valid("ui/interfaces.bin"));
    assert(!osrs_asset_manifest_path_is_valid(NULL));
    assert(!osrs_asset_manifest_path_is_valid(""));
    assert(!osrs_asset_manifest_path_is_valid("/tmp/osrs-data/equipment.models"));
    assert(!osrs_asset_manifest_path_is_valid("./equipment.models"));
    assert(!osrs_asset_manifest_path_is_valid("../equipment.models"));
    assert(!osrs_asset_manifest_path_is_valid("sprites//gui/compass.png"));
    assert(!osrs_asset_manifest_path_is_valid("sprites/../gui/compass.png"));
    assert(!osrs_asset_manifest_path_is_valid("sprites/gui/"));
    assert(!osrs_asset_manifest_path_is_valid("sprites\\gui\\compass.png"));
}

static void test_asset_groups(void) {
    assert(osrs_asset_group_count() == OSRS_ASSET_GROUP_COUNT);

    const OsrsAssetGroup* core = osrs_asset_group_at(OSRS_ASSET_GROUP_CORE);
    assert(core);
    assert(strcmp(core->name, "core") == 0);
    assert(osrs_asset_group_contains(core, "equipment.models"));
    assert(osrs_asset_group_contains(core, "ui/interfaces.bin"));

    const OsrsAssetGroup* inferno = osrs_asset_group_by_name("inferno");
    assert(inferno);
    assert(osrs_asset_group_contains(inferno, "inferno.models"));
    assert(osrs_asset_group_contains(inferno, "inferno_zuk.objects"));
    assert(osrs_asset_group_contains(inferno, "inferno.atlas"));

    const OsrsAssetGroup* pvp = osrs_asset_group_by_name("pvp");
    assert(pvp);
    assert(osrs_asset_group_contains(pvp, "equipment.models"));
    assert(osrs_asset_group_contains(pvp, "projectiles.models"));
    assert(osrs_asset_group_contains(pvp, "wilderness.cmap"));
    assert(osrs_asset_group_contains(pvp, "wilderness.terrain"));
    assert(osrs_asset_group_contains(pvp, "wilderness.objects"));
    assert(osrs_asset_group_contains(pvp, "wilderness.atlas"));

    for (size_t group_idx = 0; group_idx < osrs_asset_group_count(); group_idx++) {
        const OsrsAssetGroup* group = osrs_asset_group_at(group_idx);
        assert(group);
        assert(group->name);
        assert(group->path_count > 0);
        for (size_t path_idx = 0; path_idx < group->path_count; path_idx++) {
            assert(osrs_asset_manifest_path_is_valid(group->paths[path_idx]));
        }
    }
}

static void test_group_validation_collects_all_missing_assets(void) {
    char tmp_root[128];
    int n = snprintf(tmp_root, sizeof(tmp_root), "/tmp/osrs-assets-test-%ld",
        (long)getpid());
    assert(n > 0 && (size_t)n < sizeof(tmp_root));
    rmdir(tmp_root);
    assert(mkdir(tmp_root, 0700) == 0);

    setenv("OSRS_ASSET_ROOT", tmp_root, 1);
    const OsrsAssetGroup* inferno = osrs_asset_group_at(OSRS_ASSET_GROUP_INFERNO);
    const char* missing_paths[2] = {0};
    size_t missing_count = osrs_asset_validate_group(
        OSRS_ASSET_GROUP_INFERNO, missing_paths, 2);

    assert(missing_count == inferno->path_count);
    assert(strcmp(missing_paths[0], inferno->paths[0]) == 0);
    assert(strcmp(missing_paths[1], inferno->paths[1]) == 0);

    unsetenv("OSRS_ASSET_ROOT");
    assert(rmdir(tmp_root) == 0);
}

static void test_current_assets_satisfy_required_groups(void) {
    unsetenv("OSRS_ASSET_ROOT");
    for (size_t group_idx = 0; group_idx < osrs_asset_group_count(); group_idx++) {
        assert(osrs_asset_validate_group(
            (OsrsAssetGroupKind)group_idx, NULL, 0) == 0);
    }
}

int main(void) {
    test_asset_paths();
    test_manifest_path_validation();
    test_asset_groups();
    test_group_validation_collects_all_missing_assets();
    test_current_assets_satisfy_required_groups();

    unsetenv("OSRS_ASSET_ROOT");
    assert(osrs_asset_exists("sprites/gui/compass.png"));
    FILE* asset = osrs_asset_fopen("sprites/gui/compass.png", "rb");
    assert(asset);
    fclose(asset);

    FILE* rooted_asset = osrs_asset_fopen("ocean/osrs/data/sprites/gui/compass.png", "rb");
    assert(rooted_asset);
    fclose(rooted_asset);

    OsrsAssetBytes bytes = osrs_asset_read_all("sprites/gui/compass.png");
    assert(bytes.data);
    assert(bytes.size > 0);
    osrs_asset_bytes_free(&bytes);
    assert(!bytes.data);
    assert(bytes.size == 0);
    return 0;
}
