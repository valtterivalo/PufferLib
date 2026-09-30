#ifndef OSRS_CACHE_INTERFACES_H
#define OSRS_CACHE_INTERFACES_H

#include "osrs_cache.h"

enum { CACHE_INDEX_INTERFACES = 3 };
enum { IF_LISTENER_COUNT = 18, IF_TRIGGER_COUNT = 3, IF_ACTION_MAX = 255 };

static const char* const IF3_LISTENER_NAMES[IF_LISTENER_COUNT] = {
    "onLoad", "onMouseOver", "onMouseLeave", "onTargetLeave", "onTargetEnter",
    "onVarTransmit", "onInvTransmit", "onStatTransmit", "onTimer", "onOp",
    "onMouseRepeat", "onClick", "onClickRepeat", "onRelease", "onHold",
    "onDrag", "onDragComplete", "onScrollWheel",
};

static const char* const IF_TRIGGER_NAMES[IF_TRIGGER_COUNT] = {
    "varTransmit", "invTransmit", "statTransmit",
};

typedef struct {
    int is_string;
    int32_t int_value;
    char* str_value;
} IfListenerValue;

typedef struct {
    int present;
    int count;
    IfListenerValue values[255];
} IfListener;

typedef struct {
    int present;
    int count;
    int32_t values[255];
} IfTrigger;

typedef struct {
    uint32_t id;
    int32_t parent_id;
    uint32_t group_id;
    uint32_t file_id;
    int is_if3;
    int type;
    int hidden;
    int sprite_tiling;
    int filled;
    int line_direction;
    int text_shadowed;
    int flipped_vertically;
    int flipped_horizontally;
    int no_click_through;
    int opacity;
    int border_type;
    int line_width;
    int line_height;
    int32_t content_type;
    int32_t x, y, width, height;
    int32_t width_mode, height_mode, x_position_mode, y_position_mode;
    int32_t scroll_width, scroll_height;
    int32_t sprite_id;
    int32_t texture_id;
    int32_t shadow_color;
    int32_t model_id;
    int32_t model_type;
    int32_t font_id;
    int32_t text_color;
    uint32_t click_mask;
    int32_t x_text_alignment, y_text_alignment;
    char* name;
    char* text;
    char* target_verb;
    int action_count;
    char* actions[IF_ACTION_MAX];
    IfListener listeners[IF_LISTENER_COUNT];
    IfTrigger triggers[IF_TRIGGER_COUNT];
    int trailing_bytes;
} IfComponent;

static char* if_read_string(CacheBuf* b) {
    const uint8_t* start = b->p;
    cache_skip_string(b);
    int len = (int)(b->p - start - 1);
    char* out = malloc((size_t)len + 1);
    memcpy(out, start, (size_t)len);
    out[len] = 0;
    return out;
}

static int32_t if_missing_u16(int v) { return v == 0xFFFF ? -1 : v; }

static int32_t if_parent_id(uint32_t component_id, int raw) {
    return raw == 0xFFFF ? -1 : raw + (int32_t)(component_id & ~0xFFFFu);
}

static IfListener if_decode_listener(CacheBuf* b) {
    IfListener out = {0};
    int count = cache_u8(b);
    if (count == 0) return out;
    out.present = 1;
    out.count = count;
    for (int i = 0; i < count; i++) {
        int value_type = cache_u8(b);
        if (value_type == 0) {
            out.values[i].is_string = 0;
            out.values[i].int_value = (int32_t)cache_u32(b);
        } else {
            assert(value_type == 1);
            out.values[i].is_string = 1;
            out.values[i].str_value = if_read_string(b);
        }
    }
    return out;
}

static IfTrigger if_decode_trigger(CacheBuf* b) {
    IfTrigger out = {0};
    int count = cache_u8(b);
    if (count == 0) return out;
    out.present = 1;
    out.count = count;
    for (int i = 0; i < count; i++) out.values[i] = (int32_t)cache_u32(b);
    return out;
}

static void if_decode_if3(IfComponent* c, CacheBuf* b) {
    cache_u8(b);
    c->is_if3 = 1;
    c->type = cache_u8(b);
    c->content_type = cache_u16(b);
    c->x = cache_i16(b);
    c->y = cache_i16(b);
    c->width = cache_u16(b);
    c->height = c->type == 9 ? cache_i16(b) : cache_u16(b);
    c->width_mode = cache_i8(b);
    c->height_mode = cache_i8(b);
    c->x_position_mode = cache_i8(b);
    c->y_position_mode = cache_i8(b);
    c->parent_id = if_parent_id(c->id, cache_u16(b));
    c->hidden = cache_u8(b) == 1;

    if (c->type == 0) {
        c->scroll_width = cache_u16(b);
        c->scroll_height = cache_u16(b);
        c->no_click_through = cache_u8(b) == 1;
    }
    if (c->type == 5) {
        c->sprite_id = (int32_t)cache_u32(b);
        c->texture_id = cache_u16(b);
        c->sprite_tiling = cache_u8(b) == 1;
        c->opacity = cache_u8(b);
        c->border_type = cache_u8(b);
        c->shadow_color = (int32_t)cache_u32(b);
        c->flipped_vertically = cache_u8(b) == 1;
        c->flipped_horizontally = cache_u8(b) == 1;
    }
    if (c->type == 6) {
        c->model_type = 1;
        c->model_id = (int32_t)cache_u32(b);
        cache_i16(b);
        cache_i16(b);
        cache_u16(b);
        cache_u16(b);
        cache_u16(b);
        cache_u16(b);
        cache_u16(b);
        cache_u8(b);
        cache_u16(b);
        if (c->width_mode != 0) cache_u16(b);
        if (c->height_mode != 0) cache_u16(b);
    }
    if (c->type == 4) {
        c->font_id = if_missing_u16(cache_u16(b));
        c->text = if_read_string(b);
        c->line_height = cache_u8(b);
        c->x_text_alignment = cache_u8(b);
        c->y_text_alignment = cache_u8(b);
        c->text_shadowed = cache_u8(b) == 1;
        c->text_color = (int32_t)cache_u32(b);
    }
    if (c->type == 3) {
        c->text_color = (int32_t)cache_u32(b);
        c->filled = cache_u8(b) == 1;
        c->opacity = cache_u8(b);
    }
    if (c->type == 9) {
        c->line_width = cache_u8(b);
        c->text_color = (int32_t)cache_u32(b);
        c->line_direction = cache_u8(b) == 1;
    }

    c->click_mask = cache_u24(b);
    c->name = if_read_string(b);
    int action_count = cache_u8(b);
    c->action_count = action_count;
    for (int i = 0; i < action_count; i++) {
        char* action = if_read_string(b);
        if (i < IF_ACTION_MAX) c->actions[i] = action;
        else free(action);
    }
    cache_u8(b);
    cache_u8(b);
    cache_u8(b);
    c->target_verb = if_read_string(b);

    for (int i = 0; i < IF_LISTENER_COUNT; i++) c->listeners[i] = if_decode_listener(b);
    for (int i = 0; i < IF_TRIGGER_COUNT; i++) c->triggers[i] = if_decode_trigger(b);
}

static void if_decode_if1(IfComponent* c, CacheBuf* b) {
    c->is_if3 = 0;
    c->type = cache_u8(b);
    int menu_type = cache_u8(b);
    c->content_type = cache_u16(b);
    c->x = cache_i16(b);
    c->y = cache_i16(b);
    c->width = cache_u16(b);
    c->height = cache_u16(b);
    c->opacity = cache_u8(b);
    c->parent_id = if_parent_id(c->id, cache_u16(b));
    if_missing_u16(cache_u16(b));

    int alt_count = cache_u8(b);
    for (int i = 0; i < alt_count; i++) {
        cache_u8(b);
        cache_u16(b);
    }
    int script_count = cache_u8(b);
    for (int i = 0; i < script_count; i++) {
        int n = cache_u16(b);
        cache_skip(b, 2 * n);
    }

    if (c->type == 0) {
        c->scroll_height = cache_u16(b);
        c->hidden = cache_u8(b) == 1;
    }
    if (c->type == 1) {
        cache_u16(b);
        cache_u8(b);
    }
    if (c->type == 2) {
        cache_u8(b);
        cache_u8(b);
        cache_u8(b);
        cache_u8(b);
        cache_u8(b);
        cache_u8(b);
        for (int i = 0; i < 20; i++) {
            if (cache_u8(b) == 1) {
                cache_i16(b);
                cache_i16(b);
                cache_u32(b);
            }
        }
        for (int i = 0; i < 5; i++) free(if_read_string(b));
    }
    if (c->type == 3) c->filled = cache_u8(b) == 1;
    if (c->type == 4 || c->type == 1) {
        c->x_text_alignment = cache_u8(b);
        c->y_text_alignment = cache_u8(b);
        c->line_height = cache_u8(b);
        c->font_id = if_missing_u16(cache_u16(b));
        c->text_shadowed = cache_u8(b) == 1;
    }
    if (c->type == 4) {
        c->text = if_read_string(b);
        free(if_read_string(b));
    }
    if (c->type == 1 || c->type == 3 || c->type == 4) c->text_color = (int32_t)cache_u32(b);
    if (c->type == 3 || c->type == 4) {
        cache_u32(b);
        cache_u32(b);
        cache_u32(b);
    }
    if (c->type == 5) {
        c->sprite_id = (int32_t)cache_u32(b);
        cache_u32(b);
    }
    if (c->type == 6) {
        c->model_type = 1;
        c->model_id = (int32_t)cache_u32(b);
        cache_u32(b);
        if_missing_u16(cache_u16(b));
        if_missing_u16(cache_u16(b));
        cache_u16(b);
        cache_u16(b);
        cache_u16(b);
    }
    if (c->type == 7) {
        c->x_text_alignment = cache_u8(b);
        c->font_id = if_missing_u16(cache_u16(b));
        c->text_shadowed = cache_u8(b) == 1;
        c->text_color = (int32_t)cache_u32(b);
        cache_i16(b);
        cache_i16(b);
        cache_u8(b);
        for (int i = 0; i < 5; i++) free(if_read_string(b));
    }
    if (c->type == 8) c->text = if_read_string(b);
    if (menu_type == 2 || c->type == 2) {
        c->target_verb = if_read_string(b);
        free(if_read_string(b));
        cache_u16(b);
    }
    if (menu_type == 1 || menu_type == 4 || menu_type == 5 || menu_type == 6) free(if_read_string(b));
}

static IfComponent if_component_decode(uint32_t component_id, const CacheFile* file) {
    IfComponent c = {.id = component_id, .parent_id = -1, .model_type = 1, .line_width = 1,
                      .sprite_id = -1, .model_id = -1, .font_id = -1,
                      .group_id = component_id >> 16, .file_id = component_id & 0xFFFF};
    CacheBuf b = {file->data, file->data + file->size};
    if (file->data[0] == 0xFF) if_decode_if3(&c, &b);
    else if_decode_if1(&c, &b);
    c.trailing_bytes = (int)(b.end - b.p);
    return c;
}

static void if_component_free(IfComponent* c) {
    free(c->name);
    free(c->text);
    free(c->target_verb);
    for (int i = 0; i < c->action_count && i < IF_ACTION_MAX; i++) free(c->actions[i]);
    for (int i = 0; i < IF_LISTENER_COUNT; i++)
        for (int j = 0; j < c->listeners[i].count; j++)
            if (c->listeners[i].values[j].is_string) free(c->listeners[i].values[j].str_value);
}

#endif
