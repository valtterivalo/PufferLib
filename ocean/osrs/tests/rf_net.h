#ifndef RF_NET_H
#define RF_NET_H
/* CPU inference of a trained Puffer 5.0 policy (Linear encoder, MinGRU stack with highway, Linear decoder with fused value) on the
 * riskfight observation. Weight file: flat fp32 tensors, each 8-float aligned, order encoder, decoder, one 3H x H projection per layer.
 * Mirrors src/puffercpu.c; one agent, one step at a time, recurrent state kept between calls. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RF_NET_MAX_LAYERS = 8, RF_NET_MAX_HIDDEN = 1024 };
typedef struct {
    int input, hidden, layers, atn;
    float* weights;
    const float *encoder, *decoder, *gru[RF_NET_MAX_LAYERS];
    float state[RF_NET_MAX_LAYERS][RF_NET_MAX_HIDDEN];
    /* Every head's logits of the last act, unmasked, concatenated like the mask: what rl_explain prints. */
    float* logits;
} RfNet;

static int rf_net_align8(int n) { return (n + 7) & ~7; }

static RfNet* rf_net_load(const char* path, int input, int hidden, int layers, const int dims[], int heads) {
    if (hidden > RF_NET_MAX_HIDDEN || layers > RF_NET_MAX_LAYERS) { fprintf(stderr, "rf_net: size\n"); exit(2); }
    RfNet* net = calloc(1, sizeof(*net));
    net->input = input; net->hidden = hidden; net->layers = layers;
    for (int i = 0; i < heads; i++) net->atn += dims[i];
    net->logits = calloc(net->atn, sizeof(float));
    FILE* f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    long bytes = ftell(f);
    rewind(f);
    int expected = rf_net_align8(hidden * input);
    expected = rf_net_align8(expected + (net->atn + 1) * hidden);
    for (int l = 0; l < layers; l++) expected = rf_net_align8(expected + 3 * hidden * hidden);
    if (bytes != (long)expected * 4) { fprintf(stderr, "rf_net: %s has %ld bytes, architecture needs %ld\n", path, bytes, (long)expected * 4); exit(2); }
    net->weights = malloc(bytes);
    if (fread(net->weights, 1, bytes, f) != (size_t)bytes) { perror("rf_net read"); exit(2); }
    fclose(f);
    int at = 0;
    net->encoder = net->weights + at; at = rf_net_align8(at + hidden * input);
    net->decoder = net->weights + at; at = rf_net_align8(at + (net->atn + 1) * hidden);
    for (int l = 0; l < layers; l++) { net->gru[l] = net->weights + at; at = rf_net_align8(at + 3 * hidden * hidden); }
    return net;
}

static void rf_net_reset(RfNet* net) { memset(net->state, 0, sizeof(net->state)); }
static float rf_sigmoid(float x) { return 1.0f / (1.0f + expf(-x)); }

static uint32_t rf_net_rng(uint32_t* s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* Samples (hard=0) or takes the argmax (hard=1) of every head under the mask; the mask is the concatenation of per-head masks. */
static void rf_net_act(RfNet* net, const float* obs, const unsigned char* mask, uint32_t* rng, int hard, const int dims[], int heads, int* actions) {
    int H = net->hidden;
    float x[RF_NET_MAX_HIDDEN], y[RF_NET_MAX_HIDDEN], combined[3 * RF_NET_MAX_HIDDEN];
    for (int o = 0; o < H; o++) {
        float sum = 0;
        const float* w = net->encoder + (size_t)o * net->input;
        for (int i = 0; i < net->input; i++) sum += obs[i] * w[i];
        x[o] = sum;
    }
    for (int l = 0; l < net->layers; l++) {
        for (int o = 0; o < 3 * H; o++) {
            float sum = 0;
            const float* w = net->gru[l] + (size_t)o * H;
            for (int i = 0; i < H; i++) sum += x[i] * w[i];
            combined[o] = sum;
        }
        for (int h = 0; h < H; h++) {
            float hidden = combined[h], gate = rf_sigmoid(combined[H + h]), hw = rf_sigmoid(combined[2 * H + h]);
            float h_tilde = hidden >= 0 ? hidden + 0.5f : rf_sigmoid(hidden);
            float s = net->state[l][h];
            float out = s + gate * (h_tilde - s);
            y[h] = hw * out + (1 - hw) * x[h];
            net->state[l][h] = out;
        }
        memcpy(x, y, H * sizeof(float));
    }
    int at = 0;
    float logits[512];
    for (int a = 0; a < heads; a++) {
        int n = dims[a];
        for (int i = 0; i < n; i++) {
            float sum = 0;
            const float* w = net->decoder + (size_t)(at + i) * H;
            for (int k = 0; k < H; k++) sum += x[k] * w[k];
            logits[i] = sum;
            net->logits[at + i] = sum;
        }
        const unsigned char* m = mask + at;
        int first = -1;
        for (int i = 0; i < n && first < 0; i++) if (m[i]) first = i;
        if (first < 0) { actions[a] = 0; at += n; continue; }
        float top = logits[first];
        for (int i = first + 1; i < n; i++) if (m[i] && logits[i] > top) top = logits[i];
        if (hard) {
            actions[a] = first;
            float best = logits[first];
            for (int i = first + 1; i < n; i++) if (m[i] && logits[i] > best) { best = logits[i]; actions[a] = i; }
        } else {
            float total = 0;
            for (int i = 0; i < n; i++) if (m[i]) total += expf(logits[i] - top);
            float u = (rf_net_rng(rng) >> 8) / 16777216.0f * total, run = 0;
            actions[a] = first;
            for (int i = first; i < n; i++) {
                if (!m[i]) continue;
                run += expf(logits[i] - top);
                actions[a] = i;
                if (u < run) break;
            }
        }
        at += n;
    }
}
#endif
