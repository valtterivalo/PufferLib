/**
 * @fileoverview Loads terrain mesh from .terrain binary into raylib Model.
 *
 * Binary format:
 *   magic: uint32 "TERR" (0x54455252)
 *   vertex_count: uint32
 *   region_count: uint32
 *   min_world_x: int32
 *   min_world_y: int32
 *   vertices: float32[vertex_count * 3]
 *   colors: uint8[vertex_count * 4]
 */

#ifndef OSRS_PVP_TERRAIN_H
#define OSRS_PVP_TERRAIN_H

#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TERR_MAGIC 0x54455252

typedef struct {
    Model model;
    int vertex_count;
    int region_count;
    int min_world_x;
    int min_world_y;
    int loaded;
    /* heightmap for ground-level queries */
    float* heightmap;
    int hm_min_x;
    int hm_min_y;
    int hm_width;
    int hm_height;
} TerrainMesh;

static TerrainMesh* terrain_load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "terrain_load: could not open %s\n", path);
        return NULL;
    }

    uint32_t magic, vert_count, region_count;
    int32_t min_wx, min_wy;
    fread(&magic, 4, 1, f);
    if (magic != TERR_MAGIC) {
        fprintf(stderr, "terrain_load: bad magic %08x\n", magic);
        fclose(f);
        return NULL;
    }
    fread(&vert_count, 4, 1, f);
    fread(&region_count, 4, 1, f);
    fread(&min_wx, 4, 1, f);
    fread(&min_wy, 4, 1, f);

    fprintf(stderr, "terrain_load: %u verts, %u regions, origin (%d, %d)\n",
            vert_count, region_count, min_wx, min_wy);

    /* read vertices */
    float* raw_verts = (float*)malloc(vert_count * 3 * sizeof(float));
    fread(raw_verts, sizeof(float), vert_count * 3, f);

    /* read colors */
    unsigned char* raw_colors = (unsigned char*)malloc(vert_count * 4);
    fread(raw_colors, 1, vert_count * 4, f);

    /* build raylib mesh */
    Mesh mesh = { 0 };
    mesh.vertexCount = (int)vert_count;
    mesh.triangleCount = (int)(vert_count / 3);
    mesh.vertices = raw_verts;
    mesh.colors = raw_colors;

    /* compute normals for proper lighting */
    mesh.normals = (float*)calloc(vert_count * 3, sizeof(float));
    for (int i = 0; i < mesh.triangleCount; i++) {
        int base = i * 9;
        float ax = raw_verts[base + 0], ay = raw_verts[base + 1], az = raw_verts[base + 2];
        float bx = raw_verts[base + 3], by = raw_verts[base + 4], bz = raw_verts[base + 5];
        float cx = raw_verts[base + 6], cy = raw_verts[base + 7], cz = raw_verts[base + 8];

        float e1x = bx - ax, e1y = by - ay, e1z = bz - az;
        float e2x = cx - ax, e2y = cy - ay, e2z = cz - az;
        float nx = e1y * e2z - e1z * e2y;
        float ny = e1z * e2x - e1x * e2z;
        float nz = e1x * e2y - e1y * e2x;
        float len = sqrtf(nx * nx + ny * ny + nz * nz);
        if (len > 0.0001f) { nx /= len; ny /= len; nz /= len; }

        for (int v = 0; v < 3; v++) {
            mesh.normals[i * 9 + v * 3 + 0] = nx;
            mesh.normals[i * 9 + v * 3 + 1] = ny;
            mesh.normals[i * 9 + v * 3 + 2] = nz;
        }
    }

    UploadMesh(&mesh, false);

    TerrainMesh* tm = (TerrainMesh*)calloc(1, sizeof(TerrainMesh));
    tm->model = LoadModelFromMesh(mesh);
    tm->vertex_count = (int)vert_count;
    tm->region_count = (int)region_count;
    tm->min_world_x = min_wx;
    tm->min_world_y = min_wy;
    tm->loaded = 1;

    /* read heightmap (appended after colors in the binary) */
    int32_t hm_min_x, hm_min_y;
    uint32_t hm_w, hm_h;
    if (fread(&hm_min_x, 4, 1, f) == 1 &&
        fread(&hm_min_y, 4, 1, f) == 1 &&
        fread(&hm_w, 4, 1, f) == 1 &&
        fread(&hm_h, 4, 1, f) == 1 &&
        hm_w > 0 && hm_h > 0 && hm_w <= 4096 && hm_h <= 4096) {
        tm->hm_min_x = hm_min_x;
        tm->hm_min_y = hm_min_y;
        tm->hm_width = (int)hm_w;
        tm->hm_height = (int)hm_h;
        tm->heightmap = (float*)malloc(hm_w * hm_h * sizeof(float));
        fread(tm->heightmap, sizeof(float), hm_w * hm_h, f);
        fprintf(stderr, "terrain heightmap: %dx%d, origin (%d, %d)\n",
                tm->hm_width, tm->hm_height, tm->hm_min_x, tm->hm_min_y);
    }

    fclose(f);
    return tm;
}

/* shift terrain so world coordinates (wx, wy) become local (0, 0).
   offsets all mesh vertices and heightmap origin. must call before rendering. */
static void terrain_offset(TerrainMesh* tm, int wx, int wy) {
    if (!tm || !tm->loaded) return;
    float dx = (float)wx;
    float dz = (float)wy;  /* Z = -world_y in our coord system */
    float* verts = tm->model.meshes[0].vertices;
    for (int i = 0; i < tm->vertex_count; i++) {
        verts[i * 3 + 0] -= dx;        /* X */
        verts[i * 3 + 2] += dz;        /* Z (negated world Y) */
    }
    UpdateMeshBuffer(tm->model.meshes[0], 0, verts,
                     tm->vertex_count * 3 * sizeof(float), 0);
    tm->min_world_x -= wx;
    tm->min_world_y -= wy;
    if (tm->heightmap) {
        tm->hm_min_x -= wx;
        tm->hm_min_y -= wy;
    }
    fprintf(stderr, "terrain_offset: shifted by (%d, %d), new origin (%d, %d)\n",
            wx, wy, tm->min_world_x, tm->min_world_y);
}

/* mirror terrain mesh along Z axis (north-south flip).
   center_y is in OSRS tile coords; Z = -Y in our system.
   flips Z around -(center_y), swaps triangle winding to fix normals. */
static void terrain_mirror_z(TerrainMesh* tm, float center_y) {
    if (!tm || !tm->loaded) return;
    float center_z = -center_y;
    float* verts = tm->model.meshes[0].vertices;
    for (int i = 0; i < tm->vertex_count; i++) {
        verts[i * 3 + 2] = 2.0f * center_z - verts[i * 3 + 2];
    }
    /* flipping one axis reverses triangle winding — swap first and third vertex */
    for (int t = 0; t < tm->vertex_count / 3; t++) {
        int a = t * 3 * 3, c = (t * 3 + 2) * 3;
        for (int j = 0; j < 3; j++) {
            float tmp = verts[a + j]; verts[a + j] = verts[c + j]; verts[c + j] = tmp;
        }
        if (tm->model.meshes[0].colors) {
            unsigned char* cols = tm->model.meshes[0].colors;
            int ca = t * 3 * 4, cc = (t * 3 + 2) * 4;
            for (int j = 0; j < 4; j++) {
                unsigned char tmp = cols[ca + j]; cols[ca + j] = cols[cc + j]; cols[cc + j] = tmp;
            }
        }
    }
    UpdateMeshBuffer(tm->model.meshes[0], 0, verts,
                     tm->vertex_count * 3 * sizeof(float), 0);
    if (tm->model.meshes[0].colors) {
        UpdateMeshBuffer(tm->model.meshes[0], 3, tm->model.meshes[0].colors,
                         tm->vertex_count * 4, 0);
    }
    fprintf(stderr, "terrain_mirror_z: mirrored around y=%.1f (z=%.1f)\n", center_y, center_z);
}

/* query terrain height at a world tile position (tile corner) */
static float terrain_height_at(TerrainMesh* tm, int world_x, int world_y) {
    if (!tm || !tm->heightmap) return -2.0f;
    int lx = world_x - tm->hm_min_x;
    int ly = world_y - tm->hm_min_y;
    if (lx < 0 || lx >= tm->hm_width || ly < 0 || ly >= tm->hm_height)
        return -2.0f;
    return tm->heightmap[lx + ly * tm->hm_width];
}

/**
 * Average height of a tile's 4 corners. matches how OSRS places players
 * on sloped terrain (average of SW, SE, NW, NE corner heights).
 */
static float terrain_height_avg(TerrainMesh* tm, int world_x, int world_y) {
    float h00 = terrain_height_at(tm, world_x, world_y);
    float h10 = terrain_height_at(tm, world_x + 1, world_y);
    float h01 = terrain_height_at(tm, world_x, world_y + 1);
    float h11 = terrain_height_at(tm, world_x + 1, world_y + 1);
    return (h00 + h10 + h01 + h11) * 0.25f;
}

static void terrain_free(TerrainMesh* tm) {
    if (!tm) return;
    if (tm->loaded) {
        UnloadModel(tm->model);
    }
    free(tm->heightmap);
    free(tm);
}

#endif /* OSRS_PVP_TERRAIN_H */
