// Test harness for the colosseum entity encoder — thin wrapper around ocean.cu's
// real implementation. Built --float (-DPRECISION_FLOAT) so the forward/backward
// numerics compare apples-to-apples against the torch float reference.
// Build: nvcc -shared -o colo_entity_test.so tests/test_colosseum_entity_encoder.cu \
//   -I src -lcublas -lcudnn -lcurand -Xcompiler -fPIC -O2

#define PRECISION_FLOAT
#include <string>
#include "../src/models.cu"
#include "../src/ocean.cu"

extern "C" {

static Encoder g_enc;
static ColosseumEntityEncoderWeights* g_w = nullptr;
static ColosseumEntityEncoderActivations* g_a = nullptr;
static Allocator g_param_alloc = {}, g_act_alloc = {}, g_grad_alloc = {};
static int g_obs = 0, g_hidden = 0;

void colo_entity_test_init(int B, int obs_size, int hidden, int mode) {
    if (g_w) {
        alloc_free(&g_param_alloc);
        alloc_free(&g_act_alloc);
        alloc_free(&g_grad_alloc);
        free(g_w);
        free(g_a);
    }
    g_obs = obs_size;
    g_hidden = hidden;
    g_enc = {};
    g_enc.in_dim = obs_size;
    g_enc.out_dim = hidden;
    create_custom_encoder("osrs_colosseum", &g_enc, mode);

    g_w = (ColosseumEntityEncoderWeights*)g_enc.create_weights(&g_enc);
    g_param_alloc = {};
    g_enc.reg_params(g_w, &g_param_alloc);
    alloc_create(&g_param_alloc);

    g_a = (ColosseumEntityEncoderActivations*)calloc(1, sizeof(ColosseumEntityEncoderActivations));
    g_act_alloc = {};
    g_grad_alloc = {};
    g_enc.reg_train(g_w, g_a, &g_act_alloc, &g_grad_alloc, B);
    alloc_create(&g_act_alloc);
    alloc_create(&g_grad_alloc);
}

// global_w [hidden, obs], l1_w [16, 43], l2_w [hidden, 16]; device float ptrs.
void colo_entity_test_set_weights(void* global_w, void* l1_w, void* l2_w) {
    cudaMemcpy(g_w->global_w.data, global_w, numel(g_w->global_w.shape) * sizeof(float), cudaMemcpyDeviceToDevice);
    cudaMemcpy(g_w->entity_l1_w.data, l1_w, numel(g_w->entity_l1_w.shape) * sizeof(float), cudaMemcpyDeviceToDevice);
    cudaMemcpy(g_w->entity_l2_w.data, l2_w, numel(g_w->entity_l2_w.shape) * sizeof(float), cudaMemcpyDeviceToDevice);
}

void colo_entity_test_forward(void* output, void* obs, int B) {
    PrecisionTensor input = {.data = (precision_t*)obs, .shape = {B, g_obs}};
    PrecisionTensor result = g_enc.forward(g_w, g_a, input, 0);
    cudaMemcpy(output, result.data, (int64_t)B * g_hidden * sizeof(float), cudaMemcpyDeviceToDevice);
    cudaDeviceSynchronize();
}

void colo_entity_test_backward(void* grad, int B) {
    PrecisionTensor g = {.data = (precision_t*)grad, .shape = {B, g_hidden}};
    g_enc.backward(g_w, g_a, g, 0);
    cudaDeviceSynchronize();
}

void colo_entity_test_get_global_wgrad(void* dst) {
    cudaMemcpy(dst, g_a->global_wgrad.data, numel(g_a->global_wgrad.shape) * sizeof(float), cudaMemcpyDeviceToDevice);
}
void colo_entity_test_get_l1_wgrad(void* dst) {
    cudaMemcpy(dst, g_a->entity_l1_wgrad.data, numel(g_a->entity_l1_wgrad.shape) * sizeof(float), cudaMemcpyDeviceToDevice);
}
void colo_entity_test_get_l2_wgrad(void* dst) {
    cudaMemcpy(dst, g_a->entity_l2_wgrad.data, numel(g_a->entity_l2_wgrad.shape) * sizeof(float), cudaMemcpyDeviceToDevice);
}

// argmax buffer [B, hidden] of winning NPC index (-1 if all-inactive).
void colo_entity_test_get_argmax(void* dst, int B) {
    cudaMemcpy(dst, g_a->pool_argmax.data, (int64_t)B * g_hidden * sizeof(int), cudaMemcpyDeviceToDevice);
}

// mode 2 inventory pool: inv_l1_w [16, 28], inv_l2_w [hidden, 16]; device float ptrs.
void colo_entity_test_set_inv_weights(void* inv_l1_w, void* inv_l2_w) {
    cudaMemcpy(g_w->inv_l1_w.data, inv_l1_w, numel(g_w->inv_l1_w.shape) * sizeof(float), cudaMemcpyDeviceToDevice);
    cudaMemcpy(g_w->inv_l2_w.data, inv_l2_w, numel(g_w->inv_l2_w.shape) * sizeof(float), cudaMemcpyDeviceToDevice);
}
void colo_entity_test_get_inv_l1_wgrad(void* dst) {
    cudaMemcpy(dst, g_a->inv_l1_wgrad.data, numel(g_a->inv_l1_wgrad.shape) * sizeof(float), cudaMemcpyDeviceToDevice);
}
void colo_entity_test_get_inv_l2_wgrad(void* dst) {
    cudaMemcpy(dst, g_a->inv_l2_wgrad.data, numel(g_a->inv_l2_wgrad.shape) * sizeof(float), cudaMemcpyDeviceToDevice);
}

}  // extern "C"
