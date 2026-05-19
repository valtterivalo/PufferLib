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
