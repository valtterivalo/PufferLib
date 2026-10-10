#define ENV_HEADER "../ocean/osrs_pvp/osrs_pvp.h"
#define PUFFER_ENV_NAME "osrs_pvp"
#define PUFFER_OSRS_PVP
#include "../src/pufferl.cu"
#include <vector>

int main(void) {
    assert(NH_PVP_NUM_OBS == 1003 && NH_PVP_NUM_ACTION_HEADS == 19 && NH_PVP_ACTION_MASK_SIZE == 241);
    static NhPvpState state;
    static NhPvpContext context;
    ENCOUNTER_NH_PVP.init_state((EncounterState*)&state, (EncounterContext*)&context);
    ENCOUNTER_NH_PVP.init_context((EncounterContext*)&context);
    ENCOUNTER_NH_PVP.finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    ENCOUNTER_NH_PVP.reset((EncounterState*)&state, (EncounterContext*)&context, 73);
    Player* player = &state.env.players[0];
    player->inventory_cells[0] = osrs_inventory_cell_from_item(ITEM_ANCESTRAL_TOP);
    player->inventory_cells[1] = osrs_inventory_cell_from_item(ITEM_BLACK_DHIDE_BODY);
    slot_equip_item(player, GEAR_SLOT_BODY, ITEM_ANCESTRAL_TOP);
    const int batch = 2, hidden = 16;
    std::vector<float> observations(batch * NH_PVP_NUM_OBS);
    pvp_write_observations(observations.data(), &state.env, 0);
    OsrsInventoryCell swap = player->inventory_cells[0];
    player->inventory_cells[0] = player->inventory_cells[1];
    player->inventory_cells[1] = swap;
    slot_equip_item(player, GEAR_SLOT_BODY, ITEM_BLACK_DHIDE_BODY);
    pvp_write_observations(observations.data() + NH_PVP_NUM_OBS, &state.env, 0);
    std::vector<precision_t> inputs(observations.size());
    for (int index = 0; index < (int)inputs.size(); index++) inputs[index] = from_float(observations[index]);
    std::vector<precision_t> weights(hidden * NH_PVP_NUM_OBS, from_float(0));
    const int feature = NH_PVP_INVENTORY_START + 15 + 3;
    weights[feature] = from_float(2);
    weights[feature + NH_PVP_INVENTORY_FEATURES] = from_float(-1);
    weights[NH_PVP_NUM_OBS + NH_PVP_TARGET_EQUIPPED_START + 3] = from_float(0.5f);
    weights[2 * NH_PVP_NUM_OBS + NH_PVP_SELF_BONUS_START + 3] = from_float(1);
    cublas_init_handle();
    Arch arch = build_arch(NH_PVP_NUM_OBS, hidden, 1, NH_PVP_ACTION_MASK_SIZE + 1, false, batch);
    Encoder* encoder = &arch.encoder;
    assert(encoder->forward == encoder_forward && encoder->backward == encoder_backward);
    EncoderWeights* ew = (EncoderWeights*)encoder->create_weights(encoder);
    EncoderActivations activations = {};
    Allocator params = {}, acts = {}, grads = {};
    encoder->reg_params(ew, &params);
    alloc_create(&params);
    encoder->reg_train(ew, &activations, &acts, &grads, batch);
    alloc_create(&acts);
    alloc_create(&grads);
    assert(cudaMemcpy(ew->weight.data, weights.data(), weights.size() * sizeof(precision_t), cudaMemcpyHostToDevice) == cudaSuccess);
    Prec input = {.shape = {batch, NH_PVP_NUM_OBS}};
    assert(cudaMalloc((void**)&input.data, inputs.size() * sizeof(precision_t)) == cudaSuccess);
    assert(cudaMemcpy(input.data, inputs.data(), inputs.size() * sizeof(precision_t), cudaMemcpyHostToDevice) == cudaSuccess);
    Prec out = encoder->forward(ew, &activations, input, 0);
    std::vector<precision_t> output(batch * hidden);
    assert(cudaMemcpy(output.data(), out.data, output.size() * sizeof(precision_t), cudaMemcpyDeviceToHost) == cudaSuccess);
    for (int row = 0; row < batch; row++) {
        for (int channel = 0; channel < hidden; channel++) {
            float reference = 0;
            for (int column = 0; column < NH_PVP_NUM_OBS; column++)
                reference += to_float(inputs[row * NH_PVP_NUM_OBS + column]) * to_float(weights[channel * NH_PVP_NUM_OBS + column]);
            assert(to_float(output[row * hidden + channel]) == to_float(from_float(reference)));
        }
    }
    assert(to_float(output[0]) != to_float(output[hidden]));
    assert(to_float(output[2]) != to_float(output[hidden + 2]));
    std::vector<precision_t> output_grad(batch * hidden, from_float(0));
    output_grad[0] = from_float(1);
    output_grad[hidden] = from_float(-1);
    output_grad[2] = from_float(1);
    output_grad[hidden + 2] = from_float(-1);
    Prec grad = {.shape = {batch, hidden}};
    assert(cudaMalloc((void**)&grad.data, output_grad.size() * sizeof(precision_t)) == cudaSuccess);
    assert(cudaMemcpy(grad.data, output_grad.data(), output_grad.size() * sizeof(precision_t), cudaMemcpyHostToDevice) == cudaSuccess);
    encoder->backward(ew, &activations, grad, 0);
    std::vector<precision_t> weight_grad(weights.size());
    assert(cudaMemcpy(weight_grad.data(), activations.wgrad_scratch.data, weight_grad.size() * sizeof(precision_t), cudaMemcpyDeviceToHost) == cudaSuccess);
    for (int channel = 0; channel < hidden; channel++) {
        for (int column = 0; column < NH_PVP_NUM_OBS; column++) {
            float reference = channel == 0 || channel == 2 ? to_float(inputs[column]) - to_float(inputs[NH_PVP_NUM_OBS + column]) : 0;
            assert(to_float(weight_grad[channel * NH_PVP_NUM_OBS + column]) == to_float(from_float(reference)));
        }
    }
    assert(cudaFree(input.data) == cudaSuccess);
    assert(cudaFree(grad.data) == cudaSuccess);
    for (Allocator* alloc : {&params, &acts, &grads}) {
        assert(cudaFree(alloc->mem) == cudaSuccess);
        free(alloc->regs);
    }
    free(ew);
    printf("{\"observations\":%d,\"heads\":%d,\"logits\":%d,\"precision\":\"%s\",\"forward\":\"pass\",\"backward\":\"pass\",\"cell_association\":\"pass\"}\n",
        NH_PVP_NUM_OBS, NH_PVP_NUM_ACTION_HEADS, NH_PVP_ACTION_MASK_SIZE, USE_BF16 ? "bf16" : "float");
    return 0;
}
