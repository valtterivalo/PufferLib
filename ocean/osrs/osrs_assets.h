#ifndef OSRS_ASSETS_H
#define OSRS_ASSETS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OSRS_ASSET_ROOT_DEFAULT "ocean/osrs/data"
#define OSRS_ASSET(path) osrs_asset_path(path)

typedef struct {
    unsigned char* data;
    size_t size;
} OsrsAssetBytes;

typedef enum {
    OSRS_ASSET_GROUP_CORE = 0,
    OSRS_ASSET_GROUP_INFERNO,
    OSRS_ASSET_GROUP_ZULRAH,
    OSRS_ASSET_GROUP_GUI,
    OSRS_ASSET_GROUP_ITEMS,
    OSRS_ASSET_GROUP_HEADERS,
    OSRS_ASSET_GROUP_COMBAT_VISUALS,
    OSRS_ASSET_GROUP_WILDERNESS,
    OSRS_ASSET_GROUP_PVP,
    OSRS_ASSET_GROUP_COUNT,
} OsrsAssetGroupKind;

typedef struct {
    const char* name;
    const char* const* paths;
    size_t path_count;
} OsrsAssetGroup;

static const char* const OSRS_ASSET_CORE_PATHS[] = {
    "equipment.models",
    "equipment.anims",
    "equipment.atlas",
    "equipment.tanim",
    "projectiles.models",
    "projectiles.atlas",
    "spotanims.bin",
    "fonts/runescape.ttf",
    "fonts/runescape_small.ttf",
    "ui/interfaces.bin",
    "ui/interface_manifest.json",
};

static const char* const OSRS_ASSET_INFERNO_PATHS[] = {
    "inferno.models",
    "inferno.anims",
    "inferno.terrain",
    "inferno.objects",
    "inferno.atlas",
    "inferno_zuk.objects",
    "inferno.cmap",
    "inferno_npcs.models",
    "inferno_npcs.anims",
};

static const char* const OSRS_ASSET_ZULRAH_PATHS[] = {
    "zulrah.models",
    "zulrah.anims",
    "zulrah.terrain",
    "zulrah.objects",
    "zulrah.atlas",
    "zulrah.cmap",
};

static const char* const OSRS_ASSET_GUI_PATHS[] = {
    "sprites/gui/compass.png",
    "sprites/gui/side_panel_bg.png",
    "sprites/gui/orb_frame.png",
    "sprites/gui/headicons_prayer_0.png",
    "sprites/gui/hitmarks_0.png",
    "sprites/gui/cross_yellow_1.png",
    "sprites/gui/side_icon_inventory.png",
    "sprites/gui/tradebacking_dark.png",
    "sprites/gui/osrs_stretch_side_topbottom_0.png",
    "sprites/gui/wornicons_11.png",
    "sprites/gui/skill_icon_23.png",
    "sprites/gui/prayeron_24.png",
    "sprites/gui/magicon_47.png",
    "sprites/gui/standard_spell_on_79.png",
    "sprites/gui/minimap_alpha_mask.png",
};

static const char* const OSRS_ASSET_ITEMS_PATHS[] = {
    "sprites/items/30070.png",
    "sprites/items/28945.png",
    "sprites/items/27641.png",
    "sprites/items/23617.png",
    "sprites/items/26233.png",
    "sprites/items/item_stack_variants.tsv",
};

static const char* const OSRS_ASSET_HEADERS_PATHS[] = {
    "item_models.h",
    "player_models.h",
    "npc_models.h",
    "npc_models_inferno.h",
    "npc_models_zulrah.h",
};

static const char* const OSRS_ASSET_COMBAT_VISUALS_PATHS[] = {
    "projectiles.models",
    "projectiles.atlas",
    "spotanims.bin",
    "equipment.anims",
    "inferno.anims",
    "zulrah.anims",
};

static const char* const OSRS_ASSET_WILDERNESS_PATHS[] = {
    "wilderness.cmap",
    "wilderness.terrain",
    "wilderness.objects",
    "wilderness.atlas",
};

static const char* const OSRS_ASSET_PVP_PATHS[] = {
    "equipment.models",
    "equipment.anims",
    "equipment.atlas",
    "equipment.tanim",
    "projectiles.models",
    "projectiles.atlas",
    "spotanims.bin",
    "ui/interfaces.bin",
    "ui/interface_manifest.json",
    "wilderness.cmap",
    "wilderness.terrain",
    "wilderness.objects",
    "wilderness.atlas",
};

static const OsrsAssetGroup OSRS_ASSET_GROUPS[OSRS_ASSET_GROUP_COUNT] = {
    [OSRS_ASSET_GROUP_CORE] = {
        .name = "core",
        .paths = OSRS_ASSET_CORE_PATHS,
        .path_count = sizeof(OSRS_ASSET_CORE_PATHS) / sizeof(OSRS_ASSET_CORE_PATHS[0]),
    },
    [OSRS_ASSET_GROUP_INFERNO] = {
        .name = "inferno",
        .paths = OSRS_ASSET_INFERNO_PATHS,
        .path_count = sizeof(OSRS_ASSET_INFERNO_PATHS) / sizeof(OSRS_ASSET_INFERNO_PATHS[0]),
    },
    [OSRS_ASSET_GROUP_ZULRAH] = {
        .name = "zulrah",
        .paths = OSRS_ASSET_ZULRAH_PATHS,
        .path_count = sizeof(OSRS_ASSET_ZULRAH_PATHS) / sizeof(OSRS_ASSET_ZULRAH_PATHS[0]),
    },
    [OSRS_ASSET_GROUP_GUI] = {
        .name = "gui",
        .paths = OSRS_ASSET_GUI_PATHS,
        .path_count = sizeof(OSRS_ASSET_GUI_PATHS) / sizeof(OSRS_ASSET_GUI_PATHS[0]),
    },
    [OSRS_ASSET_GROUP_ITEMS] = {
        .name = "items",
        .paths = OSRS_ASSET_ITEMS_PATHS,
        .path_count = sizeof(OSRS_ASSET_ITEMS_PATHS) / sizeof(OSRS_ASSET_ITEMS_PATHS[0]),
    },
    [OSRS_ASSET_GROUP_HEADERS] = {
        .name = "headers",
        .paths = OSRS_ASSET_HEADERS_PATHS,
        .path_count = sizeof(OSRS_ASSET_HEADERS_PATHS) / sizeof(OSRS_ASSET_HEADERS_PATHS[0]),
    },
    [OSRS_ASSET_GROUP_COMBAT_VISUALS] = {
        .name = "combat_visuals",
        .paths = OSRS_ASSET_COMBAT_VISUALS_PATHS,
        .path_count = sizeof(OSRS_ASSET_COMBAT_VISUALS_PATHS) /
            sizeof(OSRS_ASSET_COMBAT_VISUALS_PATHS[0]),
    },
    [OSRS_ASSET_GROUP_WILDERNESS] = {
        .name = "wilderness",
        .paths = OSRS_ASSET_WILDERNESS_PATHS,
        .path_count = sizeof(OSRS_ASSET_WILDERNESS_PATHS) / sizeof(OSRS_ASSET_WILDERNESS_PATHS[0]),
    },
    [OSRS_ASSET_GROUP_PVP] = {
        .name = "pvp",
        .paths = OSRS_ASSET_PVP_PATHS,
        .path_count = sizeof(OSRS_ASSET_PVP_PATHS) / sizeof(OSRS_ASSET_PVP_PATHS[0]),
    },
};

static inline const char* osrs_asset_root(void) {
    const char* root = getenv("OSRS_ASSET_ROOT");
    return root && root[0] ? root : OSRS_ASSET_ROOT_DEFAULT;
}

static inline int osrs_asset_has_prefix(const char* s, const char* prefix) {
    size_t n = strlen(prefix);
    return strncmp(s, prefix, n) == 0;
}

static inline int osrs_asset_is_absolute_path(const char* path) {
    return path && path[0] == '/';
}

static inline int osrs_asset_manifest_path_is_valid(const char* path) {
    if (!path || !path[0]) return 0;
    if (osrs_asset_is_absolute_path(path)) return 0;
    if (strchr(path, '\\')) return 0;

    const char* part = path;
    while (*part) {
        const char* slash = strchr(part, '/');
        size_t len = slash ? (size_t)(slash - part) : strlen(part);
        if (len == 0) return 0;
        if (len == 1 && part[0] == '.') return 0;
        if (len == 2 && part[0] == '.' && part[1] == '.') return 0;
        if (!slash) return 1;
        part = slash + 1;
    }
    return 0;
}

static inline size_t osrs_asset_group_count(void) {
    return OSRS_ASSET_GROUP_COUNT;
}

static inline const OsrsAssetGroup* osrs_asset_group_at(size_t idx) {
    if (idx >= OSRS_ASSET_GROUP_COUNT) return NULL;
    return &OSRS_ASSET_GROUPS[idx];
}

static inline const OsrsAssetGroup* osrs_asset_group_by_name(const char* name) {
    if (!name) return NULL;
    for (size_t i = 0; i < OSRS_ASSET_GROUP_COUNT; i++) {
        if (strcmp(OSRS_ASSET_GROUPS[i].name, name) == 0) {
            return &OSRS_ASSET_GROUPS[i];
        }
    }
    return NULL;
}

static inline int osrs_asset_group_contains(
    const OsrsAssetGroup* group,
    const char* path
) {
    if (!group || !path) return 0;
    for (size_t i = 0; i < group->path_count; i++) {
        if (strcmp(group->paths[i], path) == 0) return 1;
    }
    return 0;
}

static inline const char* osrs_asset_logical_path(const char* path) {
    if (!path) return "";
    while (osrs_asset_has_prefix(path, "./")) path += 2;
    const char* root = osrs_asset_root();
    size_t root_len = strlen(root);
    if (root_len > 0 && strncmp(path, root, root_len) == 0 &&
            (path[root_len] == '/' || path[root_len] == '\0')) {
        path += root_len;
        if (path[0] == '/') path++;
    }
    if (osrs_asset_has_prefix(path, "data/")) path += 5;
    const char* data_part = strstr(path, "/data/");
    if (data_part) path = data_part + 6;
    return path;
}

static inline const char* osrs_asset_path(const char* path) {
    enum { OSRS_ASSET_PATH_RING = 64, OSRS_ASSET_PATH_MAX = 2048 };
    static char paths[OSRS_ASSET_PATH_RING][OSRS_ASSET_PATH_MAX];
    static int path_idx = 0;

    if (!path) {
        fprintf(stderr, "osrs_asset_path: path is null\n");
        abort();
    }

    int idx = path_idx++ % OSRS_ASSET_PATH_RING;
    if (osrs_asset_is_absolute_path(path)) {
        int n = snprintf(paths[idx], sizeof(paths[idx]), "%s", path);
        if (n < 0 || (size_t)n >= sizeof(paths[idx])) {
            fprintf(stderr, "osrs_asset_path: absolute path too long: %s\n", path);
            abort();
        }
        return paths[idx];
    }

    const char* logical = osrs_asset_logical_path(path);
    int n = snprintf(paths[idx], sizeof(paths[idx]), "%s/%s",
        osrs_asset_root(), logical);
    if (n < 0 || (size_t)n >= sizeof(paths[idx])) {
        fprintf(stderr, "osrs_asset_path: path too long under %s: %s\n",
            osrs_asset_root(), logical);
        abort();
    }
    return paths[idx];
}

static inline FILE* osrs_asset_fopen(const char* path, const char* mode) {
    return fopen(osrs_asset_path(path), mode);
}

static inline int osrs_asset_exists(const char* path) {
    FILE* f = osrs_asset_fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static inline size_t osrs_asset_validate_group(
    OsrsAssetGroupKind kind,
    const char** missing_paths,
    size_t missing_path_capacity
) {
    const OsrsAssetGroup* group = osrs_asset_group_at((size_t)kind);
    if (!group) {
        fprintf(stderr, "osrs_asset_validate_group: bad group: %d\n", (int)kind);
        abort();
    }

    size_t missing_count = 0;
    for (size_t i = 0; i < group->path_count; i++) {
        const char* path = group->paths[i];
        if (!osrs_asset_manifest_path_is_valid(path) || !osrs_asset_exists(path)) {
            if (missing_paths && missing_count < missing_path_capacity) {
                missing_paths[missing_count] = path;
            }
            missing_count++;
        }
    }
    return missing_count;
}

static inline void osrs_asset_require_group(OsrsAssetGroupKind kind) {
    const OsrsAssetGroup* group = osrs_asset_group_at((size_t)kind);
    if (!group) {
        fprintf(stderr, "osrs_asset_require_group: bad group: %d\n", (int)kind);
        abort();
    }

    size_t invalid_count = 0;
    size_t missing_count = 0;
    for (size_t i = 0; i < group->path_count; i++) {
        const char* path = group->paths[i];
        if (!osrs_asset_manifest_path_is_valid(path)) {
            if (invalid_count == 0) {
                fprintf(stderr, "OSRS asset group '%s' has invalid manifest paths:\n",
                    group->name);
            }
            fprintf(stderr, "  %s\n", path ? path : "(null)");
            invalid_count++;
            continue;
        }
        if (!osrs_asset_exists(path)) {
            if (missing_count == 0) {
                fprintf(stderr, "OSRS asset group '%s' is missing files under %s:\n",
                    group->name, osrs_asset_root());
            }
            fprintf(stderr, "  %s\n", osrs_asset_path(path));
            missing_count++;
        }
    }

    if (invalid_count || missing_count) {
        fprintf(stderr,
            "OSRS asset group '%s' failed validation: %zu invalid, %zu missing\n",
            group->name, invalid_count, missing_count);
        abort();
    }
}

static inline void osrs_asset_require_all_groups(void) {
    for (size_t i = 0; i < OSRS_ASSET_GROUP_COUNT; i++) {
        osrs_asset_require_group((OsrsAssetGroupKind)i);
    }
}

static inline OsrsAssetBytes osrs_asset_read_all(const char* path) {
    OsrsAssetBytes out = {0};
    const char* full_path = osrs_asset_path(path);
    FILE* f = osrs_asset_fopen(path, "rb");
    if (!f) return out;
    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "%s: fseek end failed\n", full_path);
        abort();
    }
    long size = ftell(f);
    if (size < 0) {
        fprintf(stderr, "%s: ftell failed\n", full_path);
        abort();
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "%s: fseek start failed\n", full_path);
        abort();
    }
    if (size == 0) {
        fclose(f);
        return out;
    }

    out.data = (unsigned char*)malloc((size_t)size);
    if (!out.data) {
        fprintf(stderr, "%s: malloc failed for %ld bytes\n", full_path, size);
        abort();
    }
    out.size = (size_t)size;
    size_t got = fread(out.data, 1, out.size, f);
    if (got != out.size) {
        fprintf(stderr, "%s: short read (%zu/%zu)\n", full_path, got, out.size);
        abort();
    }
    fclose(f);
    return out;
}

static inline void osrs_asset_bytes_free(OsrsAssetBytes* bytes) {
    if (!bytes) return;
    free(bytes->data);
    bytes->data = NULL;
    bytes->size = 0;
}

#endif
