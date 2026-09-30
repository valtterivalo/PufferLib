#ifndef OSRS_FONT_H
#define OSRS_FONT_H

#include "osrs_asset_raylib.h"

#include <assert.h>

typedef enum { OSRS_FONT_P11, OSRS_FONT_P12, OSRS_FONT_B12, OSRS_FONT_Q8, OSRS_FONT_COUNT } OsrsFontId;
enum { OSRS_FONT_CACHE_ID_BASE = 494, OSRS_FONT_MAX_LINES = 32 };
typedef enum { OSRS_TEXT_PLAIN, OSRS_TEXT_SHADOWED } OsrsTextShadow;
typedef enum { OSRS_TEXT_ALIGN_START, OSRS_TEXT_ALIGN_CENTER, OSRS_TEXT_ALIGN_END, OSRS_TEXT_ALIGN_SPREAD } OsrsTextAlign;

static const char* const OSRS_FONT_FILES[OSRS_FONT_COUNT] = {
    "fonts/p11_full.font", "fonts/p12_full.font", "fonts/b12_full.font", "fonts/q8_full.font",
};

typedef struct {
    Texture2D atlas;
    int cell_w, cell_h, ascent, max_ascent, max_descent;
    uint8_t advances[256];
} OsrsFont;

typedef struct {
    const char* text;
    int len;
} OsrsTextSpan;

static inline OsrsFontId osrs_font_from_cache_id(int cache_id) {
    assert(cache_id >= OSRS_FONT_CACHE_ID_BASE && cache_id < OSRS_FONT_CACHE_ID_BASE + OSRS_FONT_COUNT);
    return (OsrsFontId)(cache_id - OSRS_FONT_CACHE_ID_BASE);
}

static OsrsFont osrs_font_load(const char* path) {
    OsrsAssetBytes bytes = osrs_asset_read_all(path);
    assert(bytes.size > 265);
    uint32_t magic;
    memcpy(&magic, bytes.data, 4);
    assert(magic == OSRS_FONT_MAGIC);
    const uint8_t* h = bytes.data + 4;
    OsrsFont f = {.cell_w = h[0], .cell_h = h[1], .ascent = h[2], .max_ascent = h[3], .max_descent = h[4]};
    memcpy(f.advances, h + 5, 256);
    const uint8_t* mask = h + 261;
    assert(bytes.size == (size_t)(265 + 256 * f.cell_w * f.cell_h));
    int aw = 16 * f.cell_w, ah = 16 * f.cell_h;
    Color* px = calloc((size_t)(aw * ah), sizeof(Color));
    for (int c = 0; c < 256; c++)
        for (int y = 0; y < f.cell_h; y++)
            for (int x = 0; x < f.cell_w; x++)
                if (mask[(c * f.cell_h + y) * f.cell_w + x])
                    px[((c / 16) * f.cell_h + y) * aw + (c % 16) * f.cell_w + x] = WHITE;
    Image img = {.data = px, .width = aw, .height = ah, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    f.atlas = LoadTextureFromImage(img);
    SetTextureFilter(f.atlas, TEXTURE_FILTER_POINT);
    free(px);
    osrs_asset_bytes_free(&bytes);
    return f;
}

static inline int osrs_text_tag_end(const char* text, int i, int len) {
    int j = i + 1;
    while (j < len && text[j] != '>') j++;
    assert(j < len);
    return j;
}

static inline int osrs_text_tag_is(const char* text, int i, int j, const char* tag) {
    int n = (int)strlen(tag);
    return j - i - 1 == n && memcmp(text + i + 1, tag, (size_t)n) == 0;
}

static int osrs_font_span_width(const OsrsFont* f, const char* text, int len) {
    int width = 0;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '<') {
            int j = osrs_text_tag_end(text, i, len);
            c = osrs_text_tag_is(text, i, j, "lt") ? '<' : osrs_text_tag_is(text, i, j, "gt") ? '>' : 0;
            i = j;
            if (!c) continue;
        }
        width += f->advances[c == 160 ? ' ' : c];
    }
    return width;
}

static inline int osrs_font_width(const OsrsFont* f, const char* text) {
    return osrs_font_span_width(f, text, (int)strlen(text));
}

static int osrs_font_break_lines(const OsrsFont* f, const char* text, int wrap_width, OsrsTextSpan* lines) {
    int count = 0, len = (int)strlen(text);
    int start = 0, width = 0, brk = -1, brk_width = 0, brk_skip = 0;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '<') {
            int j = osrs_text_tag_end(text, i, len);
            if (osrs_text_tag_is(text, i, j, "br")) {
                assert(count < OSRS_FONT_MAX_LINES);
                lines[count++] = (OsrsTextSpan){text + start, i - start};
                start = j + 1, width = 0, brk = -1;
            }
            c = osrs_text_tag_is(text, i, j, "lt") ? '<' : osrs_text_tag_is(text, i, j, "gt") ? '>' : 0;
            i = j;
        }
        if (c) width += f->advances[c];
        if (c == ' ') brk = i + 1, brk_width = width, brk_skip = 1;
        if (wrap_width > 0 && width > wrap_width && brk >= 0) {
            assert(count < OSRS_FONT_MAX_LINES);
            lines[count++] = (OsrsTextSpan){text + start, brk - brk_skip - start};
            start = brk, brk = -1, width -= brk_width;
        }
        if (c == '-') brk = i + 1, brk_width = width, brk_skip = 0;
    }
    if (len > start) {
        assert(count < OSRS_FONT_MAX_LINES);
        lines[count++] = (OsrsTextSpan){text + start, len - start};
    }
    return count;
}

static inline Color osrs_text_hex_color(const char* hex, int n, unsigned char alpha) {
    unsigned int rgb = 0;
    for (int k = 0; k < n; k++) {
        char d = hex[k];
        rgb = rgb << 4 | (unsigned int)(d <= '9' ? d - '0' : (d | 0x20) - 'a' + 10);
    }
    return (Color){(unsigned char)(rgb >> 16), (unsigned char)(rgb >> 8), (unsigned char)rgb, alpha};
}

static void osrs_font_draw_span(
    const OsrsFont* f, OsrsTextSpan span, int x, int baseline, Color base, Color* color, OsrsTextShadow shadow
) {
    int y = baseline - f->ascent;
    Color shade = {0, 0, 0, base.a};
    for (int i = 0; i < span.len; i++) {
        unsigned char c = (unsigned char)span.text[i];
        if (c == '<') {
            int j = osrs_text_tag_end(span.text, i, span.len);
            int tag = i;
            i = j;
            if (osrs_text_tag_is(span.text, tag, j, "lt")) c = '<';
            else if (osrs_text_tag_is(span.text, tag, j, "gt")) c = '>';
            else if (osrs_text_tag_is(span.text, tag, j, "/col")) { *color = base; continue; }
            else {
                assert(j - tag - 1 > 4 && memcmp(span.text + tag + 1, "col=", 4) == 0);
                *color = osrs_text_hex_color(span.text + tag + 5, j - tag - 5, base.a);
                continue;
            }
        }
        if (c == 160) c = ' ';
        if (c != ' ') {
            Rectangle src = {(float)((c % 16) * f->cell_w), (float)((c / 16) * f->cell_h), (float)f->cell_w, (float)f->cell_h};
            if (shadow == OSRS_TEXT_SHADOWED) DrawTextureRec(f->atlas, src, (Vector2){(float)(x + 1), (float)(y + 1)}, shade);
            DrawTextureRec(f->atlas, src, (Vector2){(float)x, (float)y}, *color);
        }
        x += f->advances[c];
    }
}

static inline void osrs_font_draw(const OsrsFont* f, const char* text, int x, int baseline, Color color, OsrsTextShadow shadow) {
    Color current = color;
    osrs_font_draw_span(f, (OsrsTextSpan){text, (int)strlen(text)}, x, baseline, color, &current, shadow);
}

static inline void osrs_font_draw_centered(const OsrsFont* f, const char* text, int cx, int baseline, Color color, OsrsTextShadow shadow) {
    osrs_font_draw(f, text, cx - osrs_font_width(f, text) / 2, baseline, color, shadow);
}

static int osrs_font_draw_lines(
    const OsrsFont* f, const char* text, Rectangle box, Color color, OsrsTextShadow shadow,
    OsrsTextAlign x_align, OsrsTextAlign y_align, int line_height
) {
    int x = (int)box.x, y = (int)box.y, w = (int)box.width, h = (int)box.height;
    if (line_height == 0) line_height = f->ascent;
    int wrap = h >= line_height + f->max_ascent + f->max_descent || h >= line_height + line_height;
    OsrsTextSpan lines[OSRS_FONT_MAX_LINES];
    int n = osrs_font_break_lines(f, text, wrap ? w : 0, lines);
    assert(x_align != OSRS_TEXT_ALIGN_SPREAD || n == 1);
    if (y_align == OSRS_TEXT_ALIGN_SPREAD && n == 1) y_align = OSRS_TEXT_ALIGN_CENTER;
    int free_h = h - f->max_ascent - f->max_descent - line_height * (n - 1);
    int baseline;
    if (y_align == OSRS_TEXT_ALIGN_START) baseline = y + f->max_ascent;
    else if (y_align == OSRS_TEXT_ALIGN_CENTER) baseline = y + f->max_ascent + free_h / 2;
    else if (y_align == OSRS_TEXT_ALIGN_END) baseline = y + h - f->max_descent - line_height * (n - 1);
    else {
        int gap = free_h / (n + 1) < 0 ? 0 : free_h / (n + 1);
        baseline = y + gap + f->max_ascent;
        line_height += gap;
    }
    Color current = color;
    for (int i = 0; i < n; i++) {
        int lx = x;
        if (x_align == OSRS_TEXT_ALIGN_CENTER) lx = x + (w - osrs_font_span_width(f, lines[i].text, lines[i].len)) / 2;
        else if (x_align == OSRS_TEXT_ALIGN_END) lx = x + w - osrs_font_span_width(f, lines[i].text, lines[i].len);
        osrs_font_draw_span(f, lines[i], lx, baseline, color, &current, shadow);
        baseline += line_height;
    }
    return n;
}

#endif
