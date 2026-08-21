#define PRECISION_FLOAT
#define ENV_HEADER "../ocean/minimal/minimal.h"
#define PUFFER_ENV_NAME "minimal"
#define NUM_GEAR_SLOTS 11
#define PUFFER_OSRS_INFERNO
#include "../src/pufferl.cu"

extern "C" {

static Decoder test_decoder;
static OsrsEntityDecoderWeights* test_weights;
static OsrsEntityDecoderActivations* test_activations;
static OsrsEntityEncoderActivations* test_encoder_activations;
static Allocator test_params;
static Allocator test_acts;
static Allocator test_grads;
static bool test_cublas_initialized;

static void test_free_allocator(Allocator* allocator) {
    if (allocator->mem) cudaFree(allocator->mem);
    free(allocator->regs);
    *allocator = {};
}

static void test_reset() {
    test_free_allocator(&test_params);
    test_free_allocator(&test_acts);
    test_free_allocator(&test_grads);
    free(test_weights);
    free(test_activations);
    free(test_encoder_activations);
    test_weights = nullptr;
    test_activations = nullptr;
    test_encoder_activations = nullptr;
    osrs_entity_encoder_last = nullptr;
    osrs_entity_decoder_keygrad = nullptr;
}

int osrs_entity_decoder_test_compiled_mode() {
    return OSRS_INFERNO_COMPILED_DECODER_MODE;
}

int osrs_entity_decoder_test_uses_default_decoder() {
    Decoder decoder = {
        .hidden_dim = 40,
        .output_dim = 436,
    };
    create_custom_decoder("osrs_inferno", &decoder);
    return decoder.forward == nullptr;
}

int osrs_entity_decoder_test_has_encoder_keygrad() {
    return osrs_entity_decoder_keygrad != nullptr;
}

void osrs_entity_decoder_test_init(int batch, int hidden) {
    test_reset();
    if (!test_cublas_initialized) {
        cublas_init_handle();
        test_cublas_initialized = true;
    }
    test_decoder = {};
    test_decoder.hidden_dim = hidden;
    test_decoder.output_dim = 436;
    create_osrs_entity_decoder<OSRS_INFERNO_COMPILED_DECODER_MODE>(&test_decoder);
    test_weights = (OsrsEntityDecoderWeights*)
        test_decoder.create_weights(&test_decoder);
    test_decoder.reg_params(test_weights, &test_params);
    alloc_create(&test_params);

    size_t encoder_activation_size = sizeof(OsrsEntityEncoderActivations) +
        2 * sizeof(OsrsEntityBranchActivations);
    test_encoder_activations = (OsrsEntityEncoderActivations*)calloc(
        1, encoder_activation_size);
    OsrsEntityBranchActivations* branches =
        osrs_entity_branch_activations(test_encoder_activations);
    branches[OSRS_ENTITY_TARGET_BRANCH].h1 = {
        .shape = {
            batch * OSRS_ENTITY_TARGET_SLOTS,
            OSRS_ENTITY_TARGET_KEY_DIM,
        },
    };
    osrs_entity_encoder_last = test_encoder_activations;

    test_activations = (OsrsEntityDecoderActivations*)calloc(
        1, test_decoder.activation_size);
    test_decoder.reg_train(
        test_weights, test_activations, &test_acts, &test_grads, batch);
    alloc_create(&test_acts);
    alloc_create(&test_grads);
}

void osrs_entity_decoder_test_set_weights(
    void* linear_w,
    void* query_w,
    void* key_w,
    void* log_temperature
) {
    Prec* destinations[] = {
        &test_weights->linear_w,
        &test_weights->query_w,
        &test_weights->key_w,
        &test_weights->log_temperature,
    };
    void* sources[] = {linear_w, query_w, key_w, log_temperature};
    for (int index = 0; index < 4; index++) {
        cudaMemcpy(
            destinations[index]->data,
            sources[index],
            numel(destinations[index]->shape) * sizeof(float),
            cudaMemcpyDeviceToDevice);
    }
}

void osrs_entity_decoder_test_forward(
    void* hidden,
    void* keys,
    void* output
) {
    int batch = test_activations->out.shape[0];
    int hidden_size = test_weights->hidden_dim;
    OsrsEntityBranchActivations* branches =
        osrs_entity_branch_activations(test_encoder_activations);
    branches[OSRS_ENTITY_TARGET_BRANCH].h1.data = (precision_t*)keys;
    Prec input = {
        .data = (precision_t*)hidden,
        .shape = {batch, hidden_size},
    };
    Prec result = test_decoder.forward(
        test_weights, test_activations, input, 0);
    cudaMemcpy(
        output,
        result.data,
        numel(result.shape) * sizeof(float),
        cudaMemcpyDeviceToDevice);
}

void osrs_entity_decoder_test_backward(
    void* grad_logits,
    void* grad_value,
    void* hidden_grad
) {
    int batch = test_activations->out.shape[0];
    Float logits = {
        .data = (float*)grad_logits,
        .shape = {batch, test_weights->output_dim},
    };
    Float value = {
        .data = (float*)grad_value,
        .shape = {batch},
    };
    Float logstd = {};
    Prec result = test_decoder.backward(
        test_weights, test_activations, logits, logstd, value, 0);
    cudaMemcpy(
        hidden_grad,
        result.data,
        numel(result.shape) * sizeof(float),
        cudaMemcpyDeviceToDevice);
}

void osrs_entity_decoder_test_get_grad(int index, void* output) {
    Prec* gradients[] = {
        &test_activations->linear_wgrad,
        &test_activations->query_wgrad,
        &test_activations->key_wgrad,
        &test_activations->temperature_grad,
        &test_activations->key_grad,
    };
    Prec* gradient = gradients[index];
    cudaMemcpy(
        output,
        gradient->data,
        numel(gradient->shape) * sizeof(float),
        cudaMemcpyDeviceToDevice);
}

}
