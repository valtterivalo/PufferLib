/**
 * @fileoverview Loads placed map objects from .objects binary into a single raylib Model.
 *
 * Supports two binary formats:
 *   v1 (OBJS): vertices + colors only (flat vertex coloring)
 *   v2 (OBJ2): vertices + colors + texcoords (texture atlas support)
 *
 * When v2 format is detected, also loads the companion .atlas file (raw RGBA)
 * and assigns it as the model's diffuse texture. Vertex colors are multiplied
 * by the texture sample: textured faces use white vertex color + real texture,
 * non-textured faces use HSL vertex color + white atlas pixel.
 */

#ifndef OSRS_PVP_OBJECTS_H
#define OSRS_PVP_OBJECTS_H

#include "raylib.h"
#include "rlgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define OBJS_MAGIC 0x4F424A53  /* "OBJS" v1 */
#define OBJ2_MAGIC 0x4F424A32  /* "OBJ2" v2 with texcoords */
#define ATLS_MAGIC 0x41544C53  /* "ATLS" texture atlas */

typedef struct {
    Model model;
    Texture2D atlas_texture;  /* loaded from .atlas file (0 if none) */
    int placement_count;
    int total_vertex_count;
    int min_world_x;
    int min_world_y;
    int has_textures;
    int loaded;
} ObjectMesh;

/**
 * Load texture atlas from .atlas binary file.
 * Format: uint32 magic, uint32 width, uint32 height, uint8 pixels[w*h*4] (RGBA).
 */
static Texture2D objects_load_atlas(const char* atlas_path) {
    Texture2D tex = { 0 };
    FILE* f = fopen(atlas_path, "rb");
    if (!f) {
        fprintf(stderr, "objects_load_atlas: could not open %s\n", atlas_path);
        return tex;
    }

    uint32_t magic, width, height;
    fread(&magic, 4, 1, f);
    if (magic != ATLS_MAGIC) {
        fprintf(stderr, "objects_load_atlas: bad magic %08x (expected ATLS)\n", magic);
        fclose(f);
        return tex;
    }
    fread(&width, 4, 1, f);
    fread(&height, 4, 1, f);

    size_t pixel_size = (size_t)width * height * 4;
    unsigned char* pixels = (unsigned char*)malloc(pixel_size);
    size_t read = fread(pixels, 1, pixel_size, f);
    fclose(f);

    if (read != pixel_size) {
        fprintf(stderr, "objects_load_atlas: incomplete read (%zu/%zu)\n", read, pixel_size);
        free(pixels);
        return tex;
    }

    /* create raylib Image from raw RGBA, then upload as texture */
    Image img = {
        .data = pixels,
        .width = (int)width,
        .height = (int)height,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
    };
    tex = LoadTextureFromImage(img);
    /* set texture filtering for better quality at angles */
    SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
    free(pixels);

    fprintf(stderr, "objects_load_atlas: loaded %ux%u atlas texture\n", width, height);
    return tex;
}

static ObjectMesh* objects_load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "objects_load: could not open %s\n", path);
        return NULL;
    }

    uint32_t magic, placement_count, total_verts;
    int32_t min_wx, min_wy;
    fread(&magic, 4, 1, f);

    int has_textures = 0;
    if (magic == OBJ2_MAGIC) {
        has_textures = 1;
    } else if (magic != OBJS_MAGIC) {
        fprintf(stderr, "objects_load: bad magic %08x\n", magic);
        fclose(f);
        return NULL;
    }

    fread(&placement_count, 4, 1, f);
    fread(&min_wx, 4, 1, f);
    fread(&min_wy, 4, 1, f);
    fread(&total_verts, 4, 1, f);

    fprintf(stderr, "objects_load: %u placements, %u verts, format=%s\n",
            placement_count, total_verts, has_textures ? "OBJ2" : "OBJS");

    /* read vertices */
    float* raw_verts = (float*)malloc(total_verts * 3 * sizeof(float));
    fread(raw_verts, sizeof(float), total_verts * 3, f);

    /* read colors */
    unsigned char* raw_colors = (unsigned char*)malloc(total_verts * 4);
    fread(raw_colors, 1, total_verts * 4, f);

    /* read texture coordinates (v2 only) */
    float* raw_texcoords = NULL;
    if (has_textures) {
        raw_texcoords = (float*)malloc(total_verts * 2 * sizeof(float));
        fread(raw_texcoords, sizeof(float), total_verts * 2, f);
    }
    fclose(f);

    /* build raylib mesh */
    Mesh mesh = { 0 };
    mesh.vertexCount = (int)total_verts;
    mesh.triangleCount = (int)(total_verts / 3);
    mesh.vertices = raw_verts;
    mesh.colors = raw_colors;
    mesh.texcoords = raw_texcoords;

    /* compute normals */
    mesh.normals = (float*)calloc(total_verts * 3, sizeof(float));
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

    ObjectMesh* om = (ObjectMesh*)calloc(1, sizeof(ObjectMesh));
    om->model = LoadModelFromMesh(mesh);
    om->placement_count = (int)placement_count;
    om->total_vertex_count = (int)total_verts;
    om->min_world_x = min_wx;
    om->min_world_y = min_wy;
    om->has_textures = has_textures;
    om->loaded = 1;

    /* load atlas texture if v2 format */
    if (has_textures) {
        /* derive atlas path from objects path: replace .objects with .atlas */
        char atlas_path[1024];
        strncpy(atlas_path, path, sizeof(atlas_path) - 1);
        atlas_path[sizeof(atlas_path) - 1] = '\0';
        char* dot = strrchr(atlas_path, '.');
        if (dot) {
            strcpy(dot, ".atlas");
        } else {
            strncat(atlas_path, ".atlas", sizeof(atlas_path) - strlen(atlas_path) - 1);
        }

        om->atlas_texture = objects_load_atlas(atlas_path);
        if (om->atlas_texture.id > 0) {
            /* assign atlas as diffuse map for the model's material */
            om->model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = om->atlas_texture;
        }
    }

    return om;
}

/* shift object vertices so world coordinates (wx, wy) become local (0, 0).
   must match terrain_offset() values for alignment. */
static void objects_offset(ObjectMesh* om, int wx, int wy) {
    if (!om || !om->loaded) return;
    float dx = (float)wx;
    float dz = (float)wy;
    float* verts = om->model.meshes[0].vertices;
    for (int i = 0; i < om->total_vertex_count; i++) {
        verts[i * 3 + 0] -= dx;        /* X */
        verts[i * 3 + 2] += dz;        /* Z (negated world Y) */
    }
    UpdateMeshBuffer(om->model.meshes[0], 0, verts,
                     om->total_vertex_count * 3 * sizeof(float), 0);
    om->min_world_x -= wx;
    om->min_world_y -= wy;
    fprintf(stderr, "objects_offset: shifted by (%d, %d)\n", wx, wy);
}

static void objects_free(ObjectMesh* om) {
    if (!om) return;
    if (om->loaded) {
        if (om->atlas_texture.id > 0) {
            UnloadTexture(om->atlas_texture);
        }
        UnloadModel(om->model);
    }
    free(om);
}

#endif /* OSRS_PVP_OBJECTS_H */
