import numpy as np

import torch
import torch.nn as nn
import torch.nn.functional as F

class Policy(nn.Module):
    def __init__(self, encoder, decoder, network):
        super().__init__()
        self.encoder = encoder
        self.decoder = decoder
        self.network = network

    def initial_state(self, batch_size, device):
        return self.network.initial_state(batch_size, device)

    def forward_eval(self, x, state):
        h = self.encoder(x)
        h, state = self.network.forward_eval(h, state)
        logits, values = self.decoder(h)
        return logits, values, state

    def forward(self, x):
        B, TT = x.shape[:2]
        h = self.encoder(x.reshape(B*TT, *x.shape[2:]))
        h = self.network.forward_train(h.reshape(B, TT, -1))
        logits, values = self.decoder(h.reshape(B*TT, -1))
        return logits, values.reshape(B, TT)

class DefaultEncoder(nn.Module):
    def __init__(self, obs_size, hidden_size=128):
        super().__init__()
        self.encoder = nn.Linear(obs_size, hidden_size)

    def forward(self, observations):
        return self.encoder(observations.view(observations.shape[0], -1).float())

class MinimalEntityEncoder(nn.Module):
    def __init__(self, obs_size, hidden_size=128):
        super().__init__()
        self.self_obs_size = 2
        self.point_obs_size = 4
        self.num_points = 16
        self.hidden_size = hidden_size

        self.encoder = nn.Sequential(
            nn.Linear(self.self_obs_size + self.point_obs_size, 16),
            nn.ReLU(),
            nn.Linear(16, hidden_size),
        )

    def forward(self, observations):
        self_obs = observations[:, :self.self_obs_size].unsqueeze(1).expand(
            observations.shape[0], self.num_points, self.self_obs_size)
        point_obs = observations[:, self.self_obs_size:].reshape(
            observations.shape[0], self.num_points, self.point_obs_size)
        cat = torch.cat([self_obs, point_obs], dim=-1)
        return self.encoder(cat).max(dim=1)[0]

class ColosseumEntityEncoder(nn.Module):
    """Flat baseline + NPC entity-pool augmentation for the OSRS Colosseum obs.

    Torch REFERENCE implementation: no policy instantiates this (the torch eval
    path uses the default Linear encoder); it exists as the finite-diff oracle
    for the native CUDA encoder.

    Torch reference for the native CUDA colosseum entity encoder (finite-diff checked).
    The global path is a bias-free Linear over the full flat obs (matching the native
    backend's bias-free Linear encoder), so this module is a clean A/B:
    output = global(flat) + entity_pool(NPCs), isolating the entity-pool contribution.
    All Linears are bias=False and GELU uses the tanh approximation to stay
    bit-comparable with the native CUDA + puffernet inference paths. Downstream mixing
    happens in the MinGRU net, so the sum keeps hidden_size and leaves the recurrent
    input dim unchanged.

    Obs layout verified from ocean/osrs/encounters/colosseum/encounter_colosseum_obs_mask.inc
    and encounter_colosseum_model.inc. The NPC block is COLO_OBS_NPCS=24 contiguous
    records of COLO_FEATURES_PER_NPC=37 floats, written by col_write_npc_slot in the
    col_write_obs_ctx loop. NPC_START = COLO_OBS_AFTER_EQUIPPED_SELF:
        player(36) + pillars(4*3=12) + inventory(28*28=784) + equipped(11*18=198) = 1030
    and NPC_START + 24*37 = 1918 = COLO_OBS_AFTER_NPCS. Each record begins with a
    COLO_NUM_NPC_TYPES=12-wide type one-hot (col_write_npc_slot), so an active
    slot has type_onehot.sum() > 0 while an inactive slot is fully zero-filled (the writer
    does `i += COLO_FEATURES_PER_NPC` for inactive slots after a full memset).

    mode selects the encoder:
        1 = global(flat) + NPC pool                       (the SOTA encoder, byte-identical to before)
        2 = global(flat) + NPC pool + INVENTORY-CELL pool  (B3)
    The inventory pool is a second shared per-entity MLP masked-maxpooled over the 28
    inventory cells (offsets COLO_OBS_AFTER_PILLARS=48 .. COLO_OBS_AFTER_INVENTORY=832,
    28 floats/cell), with a cell counted active iff its present flag (cell-local offset 0,
    osrs_write_inventory_cell_affordance_features out[0]) > 0. The biggest flat obs section
    (784 floats) is collision-heavy under a raw matmul; a shared per-cell MLP + pool is the
    parameter-efficient inductive bias that lets a smaller hidden recover the score (the env
    is rollout-bound at hs<=1024, so this extra encoder capacity is ~free on throughput).
    Parameter order (.bin checkpoint contract): global_w, npc_l1, npc_l2, inv_l1, inv_l2 --
    submodule definition order below MUST yield exactly that for the native mirrors to load.
    """

    NPC_START = 1030
    NUM_NPCS = 24
    FEATURES_PER_NPC = 37
    TYPE_ONEHOT_START = 0
    TYPE_ONEHOT_LEN = 12
    BOTTLENECK = 16

    INV_START = 48
    NUM_INV_CELLS = 28
    FEATURES_PER_CELL = 28
    INV_PRESENT_OFFSET = 0
    INV_BOTTLENECK = 16

    def __init__(self, obs_size, hidden_size=128, mode=1):
        super().__init__()
        self.hidden_size = hidden_size
        self.mode = int(mode)
        self.npc_block_size = self.NUM_NPCS * self.FEATURES_PER_NPC
        npc_end = self.NPC_START + self.npc_block_size
        assert obs_size >= npc_end, (
            f'obs_size {obs_size} too small for colosseum NPC block ending at {npc_end}'
        )

        self.global_encoder = nn.Linear(obs_size, hidden_size, bias=False)
        self.entity_encoder = nn.Sequential(
            nn.Linear(self.FEATURES_PER_NPC, self.BOTTLENECK, bias=False),
            nn.GELU(approximate="tanh"),
            nn.Linear(self.BOTTLENECK, hidden_size, bias=False),
        )
        self.inv_block_size = self.NUM_INV_CELLS * self.FEATURES_PER_CELL
        if self.mode >= 2:
            inv_end = self.INV_START + self.inv_block_size
            assert obs_size >= inv_end, (
                f'obs_size {obs_size} too small for colosseum inventory block ending at {inv_end}'
            )
            self.inv_encoder = nn.Sequential(
                nn.Linear(self.FEATURES_PER_CELL, self.INV_BOTTLENECK, bias=False),
                nn.GELU(approximate="tanh"),
                nn.Linear(self.INV_BOTTLENECK, hidden_size, bias=False),
            )

    @staticmethod
    def _masked_maxpool(embeddings, active):
        """Maxpool embeddings [B, N, H] over active entities; an all-inactive row -> 0."""
        masked = embeddings.masked_fill(~active.unsqueeze(-1), float('-inf'))
        pooled = masked.max(dim=1)[0]
        return torch.where(active.any(dim=1, keepdim=True), pooled, torch.zeros_like(pooled))

    def forward(self, observations):
        x = observations.view(observations.shape[0], -1).float()
        out = self.global_encoder(x)

        npcs = x[:, self.NPC_START:self.NPC_START + self.npc_block_size].reshape(
            x.shape[0], self.NUM_NPCS, self.FEATURES_PER_NPC)
        npc_type = npcs[:, :, self.TYPE_ONEHOT_START:self.TYPE_ONEHOT_START + self.TYPE_ONEHOT_LEN]
        npc_active = npc_type.sum(dim=-1) > 0
        out = out + self._masked_maxpool(self.entity_encoder(npcs), npc_active)

        if self.mode >= 2:
            cells = x[:, self.INV_START:self.INV_START + self.inv_block_size].reshape(
                x.shape[0], self.NUM_INV_CELLS, self.FEATURES_PER_CELL)
            cell_active = cells[:, :, self.INV_PRESENT_OFFSET] > 0
            out = out + self._masked_maxpool(self.inv_encoder(cells), cell_active)

        return out

class DefaultDecoder(nn.Module):
    def __init__(self, nvec, hidden_size=128):
        super().__init__()
        self.nvec = tuple(nvec)
        self.is_continuous = sum(nvec) == len(nvec)

        if self.is_continuous:
            num_atns = len(nvec)
            self.decoder_mean = nn.Linear(hidden_size, num_atns)
            self.decoder_logstd = nn.Parameter(torch.zeros(1, num_atns))
        else:
            self.decoder = nn.Linear(hidden_size, int(np.sum(nvec)))

        self.value_function = nn.Linear(hidden_size, 1)

    def forward(self, hidden):
        if self.is_continuous:
            mean = self.decoder_mean(hidden)
            logstd = self.decoder_logstd.expand_as(mean)
            logits = torch.distributions.Normal(mean, torch.exp(logstd))
        else:
            logits = self.decoder(hidden)
            if len(self.nvec) > 1:
                logits = logits.split(self.nvec, dim=1)

        values = self.value_function(hidden)
        return logits, values

class MLP(nn.Module):
    def __init__(self, hidden_size, num_layers=1, **kwargs):
        super().__init__()
        layers = []
        for _ in range(num_layers):
            layers += [nn.Linear(hidden_size, hidden_size), nn.GELU()]
        self.net = nn.Sequential(*layers)

    def initial_state(self, batch_size, device):
        return ()

    def forward_eval(self, h, state):
        return self.net(h), state

    def forward_train(self, h):
        return self.net(h)

class MinGRU(nn.Module):
    # https://arxiv.org/abs/2410.01201v1
    def __init__(self, hidden_size, num_layers=1, **kwargs):
        super().__init__()
        self.hidden_size = hidden_size
        self.num_layers = num_layers
        self.layers = nn.ModuleList([
            nn.Linear(hidden_size, 3 * hidden_size, bias=False) for _ in range(num_layers)
        ])

    def _g(self, x):
        return torch.where(x >= 0, x + 0.5, x.sigmoid())

    def _log_g(self, x):
        return torch.where(x >= 0, (F.relu(x) + 0.5).log(), -F.softplus(-x))

    def _highway(self, x, out, proj):
        g = proj.sigmoid()
        return g * out + (1.0 - g) * x

    def _heinsen_scan(self, log_coeffs, log_values):
        a_star = log_coeffs.cumsum(dim=1)
        return (a_star + (log_values - a_star).logcumsumexp(dim=1)).exp()

    def initial_state(self, batch_size, device):
        return (torch.zeros(self.num_layers, batch_size, self.hidden_size, device=device),)

    def forward_eval(self, h, state):
        state = state[0]
        assert state.shape[1] == h.shape[0]
        h = h.unsqueeze(1)
        state_out = []
        for i in range(self.num_layers):
            hidden, gate, proj = self.layers[i](h).chunk(3, dim=-1)
            out = torch.lerp(state[i:i+1].transpose(0, 1), self._g(hidden), gate.sigmoid())
            h = self._highway(h, out, proj)
            state_out.append(out[:, -1:])
        return h.squeeze(1), (torch.stack(state_out, 0).squeeze(2),)

    def forward_train(self, h):
        T = h.shape[1]
        for i in range(self.num_layers):
            hidden, gate, proj = self.layers[i](h).chunk(3, dim=-1)
            log_coeffs = -F.softplus(gate)
            log_values = -F.softplus(-gate) + self._log_g(hidden)
            out = self._heinsen_scan(log_coeffs, log_values)[:, -T:]
            h = self._highway(h, out, proj)
        return h

class LSTM(nn.Module):
    def __init__(self, hidden_size, num_layers=1, **kwargs):
        super().__init__()
        self.hidden_size = hidden_size
        self.num_layers = num_layers

        self.lstm = nn.LSTM(hidden_size, hidden_size, num_layers=num_layers)
        self.cell = nn.ModuleList([torch.nn.LSTMCell(hidden_size, hidden_size) for _ in range(num_layers)])

        for i in range(num_layers):
            cell = self.cell[i]
            w_ih = getattr(self.lstm, f'weight_ih_l{i}')
            w_hh = getattr(self.lstm, f'weight_hh_l{i}')
            b_ih = getattr(self.lstm, f'bias_ih_l{i}')
            b_hh = getattr(self.lstm, f'bias_hh_l{i}')
            nn.init.orthogonal_(w_ih, 1.0)
            nn.init.orthogonal_(w_hh, 1.0)
            b_ih.data.zero_()
            b_hh.data.zero_()
            cell.weight_ih = w_ih
            cell.weight_hh = w_hh
            cell.bias_ih = b_ih
            cell.bias_hh = b_hh

    def initial_state(self, batch_size, device):
        h = torch.zeros(self.num_layers, batch_size, self.hidden_size, device=device)
        c = torch.zeros(self.num_layers, batch_size, self.hidden_size, device=device)
        return h, c

    def forward_eval(self, h, state):
        assert state[0].shape[1] == state[1].shape[1] == h.shape[0]
        lstm_h, lstm_c = state
        for i in range(self.num_layers):
            h, c = self.cell[i](h, (lstm_h[i], lstm_c[i]))
            lstm_h[i] = h
            lstm_c[i] = c
        return h, (lstm_h, lstm_c)

    def forward_train(self, h):
        # h: [B, T, H]
        h = h.transpose(0, 1)
        h, _ = self.lstm(h)
        return h.transpose(0, 1)

class GRU(nn.Module):
    def __init__(self, hidden_size, num_layers=1, **kwargs):
        super().__init__()
        self.hidden_size = hidden_size
        self.num_layers = num_layers

        self.gru = nn.GRU(hidden_size, hidden_size, num_layers=num_layers)
        self.cell = nn.ModuleList([torch.nn.GRUCell(hidden_size, hidden_size) for _ in range(num_layers)])
        self.norm = torch.nn.RMSNorm(hidden_size)

        for i in range(num_layers):
            cell = self.cell[i]
            w_ih = getattr(self.gru, f'weight_ih_l{i}')
            w_hh = getattr(self.gru, f'weight_hh_l{i}')
            b_ih = getattr(self.gru, f'bias_ih_l{i}')
            b_hh = getattr(self.gru, f'bias_hh_l{i}')
            nn.init.orthogonal_(w_ih, 1.0)
            nn.init.orthogonal_(w_hh, 1.0)
            b_ih.data.zero_()
            b_hh.data.zero_()
            cell.weight_ih = w_ih
            cell.weight_hh = w_hh
            cell.bias_ih = b_ih
            cell.bias_hh = b_hh

    def initial_state(self, batch_size, device):
        h = torch.zeros(self.num_layers, batch_size, self.hidden_size, device=device)
        return (h,)

    def forward_eval(self, h, state):
        assert state[0].shape[1] == h.shape[0]
        state = state[0]
        for i in range(self.num_layers):
            h_in = h
            h = self.cell[i](h, state[i])
            state[i] = h
            h = h + h_in
            h = self.norm(h)
        return h, (state,)

    def forward_train(self, h):
        # h: [B, T, H]
        h = h.transpose(0, 1)
        h_in = h
        h, _ = self.gru(h)
        h = h + h_in
        h = self.norm(h)
        return h.transpose(0, 1)

class NatureEncoder(nn.Module):
    '''NatureCNN encoder (Mnih et al. 2015). Returns [batch, hidden_size].'''
    def __init__(self, env, hidden_size=512, framestack=1, flat_size=64*7*7,
            channels_last=False, downsample=1, **kwargs):
        super().__init__()
        self.channels_last = channels_last
        self.downsample = downsample
        self.network = nn.Sequential(
            nn.Conv2d(framestack, 32, 8, stride=4),
            nn.ReLU(),
            nn.Conv2d(32, 64, 4, stride=2),
            nn.ReLU(),
            nn.Conv2d(64, 64, 3, stride=1),
            nn.ReLU(),
            nn.Flatten(),
            nn.Linear(flat_size, hidden_size),
            nn.ReLU(),
        )

    def forward(self, observations):
        if self.channels_last:
            observations = observations.permute(0, 3, 1, 2)
        if self.downsample > 1:
            observations = observations[:, :, ::self.downsample, ::self.downsample]
        return self.network(observations.float() / 255.0)

class ResidualBlock(nn.Module):
    def __init__(self, channels):
        super().__init__()
        self.conv0 = nn.Conv2d(channels, channels, 3, padding=1)
        self.conv1 = nn.Conv2d(channels, channels, 3, padding=1)

    def forward(self, x):
        inputs = x
        x = F.relu(x)
        x = self.conv0(x)
        x = F.relu(x)
        x = self.conv1(x)
        return x + inputs

class ConvSequence(nn.Module):
    def __init__(self, input_shape, out_channels):
        super().__init__()
        self._input_shape = input_shape
        self._out_channels = out_channels
        self.conv = nn.Conv2d(input_shape[0], out_channels, 3, padding=1)
        self.res_block0 = ResidualBlock(out_channels)
        self.res_block1 = ResidualBlock(out_channels)

    def forward(self, x):
        x = self.conv(x)
        x = F.max_pool2d(x, kernel_size=3, stride=2, padding=1)
        x = self.res_block0(x)
        x = self.res_block1(x)
        return x

    def get_output_shape(self):
        _c, h, w = self._input_shape
        return (self._out_channels, (h + 1) // 2, (w + 1) // 2)

class ImpalaEncoder(nn.Module):
    '''IMPALA ResNet encoder (Espeholt et al. 2018). Returns [batch, hidden_size].'''
    def __init__(self, env, hidden_size=256, cnn_width=16, **kwargs):
        super().__init__()
        h, w, c = env.single_observation_space.shape
        shape = (c, h, w)
        conv_seqs = []
        for out_channels in [cnn_width, 2*cnn_width, 2*cnn_width]:
            conv_seq = ConvSequence(shape, out_channels)
            shape = conv_seq.get_output_shape()
            conv_seqs.append(conv_seq)
        conv_seqs += [
            nn.Flatten(),
            nn.ReLU(),
            nn.Linear(shape[0] * shape[1] * shape[2], hidden_size),
            nn.ReLU(),
        ]
        self.network = nn.Sequential(*conv_seqs)

    def forward(self, observations):
        return self.network(observations.permute(0, 3, 1, 2).float() / 255.0)
