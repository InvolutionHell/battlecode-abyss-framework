#pragma once
// The 9-class policy / value net (rl/train/pretrain.py) evaluated inside a bot, as a prior for the search.
//   conv3x3(17 -> C) relu, conv3x3(C -> C) relu, dense(C*49 + 10 scalars + round -> H) relu,
//   policy head: 9 action classes (0-3 a step forward / right / back / left of the facing, 4-7 the same as the first
//   step of a multi-step move, 8 split) + the split fraction logit; value head: the win logit.
// Weights: rl_weights.hpp (RL_* symbols, rl/train/export_bot.py: int16 values with one float scale per output row),
// dequantised once per process. Another file name: define RL_WEIGHTS_HEADER, e.g. -DRL_WEIGHTS_HEADER='"my.hpp"'.
// With an auxiliary-head net (the header defines RL_HAS_AUX; pretrain.py --aux-weight) run() also fills aux[]: this
// dragon's P(split within 5 turns), P(eat a pearl within 5), P(die by enemy contact within 10), P(an enemy dies on it
// within 10) (rl/data/aux_labels.py).
//
// This is the library copy. bots/abyss/rl_net.hpp is the released bot's copy of the same network code (forwardFast
// gives the same outputs); it keeps the scalar weight copies, which this copy frees unless RL_KEEP_SCALAR is defined.
#include <algorithm>
#include <cmath>
#include <vector>

#include "rl_features.hpp"
#ifndef RL_WEIGHTS_HEADER
#define RL_WEIGHTS_HEADER "rl_weights.hpp"
#endif
#include RL_WEIGHTS_HEADER

namespace bc {

struct RLNet {
    static constexpr int C = RL_NC1, H = RL_NH, NIN = RL_NC1 * 49 + FS + 1, NOUT = NCLASS + 1;
    std::vector<float> w1, w2, wf, wp, wv;
    std::vector<float> w1T, w2T, wfT;  // the same weights, output channel innermost (forwardFast)
    float a1[C * 49], a2[C * 49], h[H];
    float logits[NOUT];
    float prob[NCLASS];  // softmax over the action classes (0-3 step forward / right / back / left, 4-7 sprints, 8 split)
    float value = 0;     // win logit
#ifdef RL_HAS_AUX
    enum { AUX_SPLIT5, AUX_EAT5, AUX_DIE10, AUX_KILL10 };
    std::vector<float> wa;
    float aux[RL_NAUX] = {};  // probabilities, indexed by the enum above
#endif

    RLNet() {
        auto deq = [](int16_t const* q, float const* s, int rows, int per, std::vector<float>& out) {
            out.resize(static_cast<size_t>(rows) * per);
            for (int r = 0; r < rows; r++)
                for (int i = 0; i < per; i++) out[r * per + i] = q[r * per + i] * s[r];
        };
        deq(RL_W_C1_Q, RL_W_C1_S, C, FC * 9, w1);
        deq(RL_W_C2_Q, RL_W_C2_S, C, C * 9, w2);
        deq(RL_W_F1_Q, RL_W_F1_S, H, NIN, wf);
        deq(RL_W_POL_Q, RL_W_POL_S, NOUT, H, wp);
        deq(RL_W_VAL_Q, RL_W_VAL_S, 1, H, wv);
#ifdef RL_HAS_AUX
        deq(RL_W_AUX_Q, RL_W_AUX_S, RL_NAUX, H, wa);
#endif
        auto convT = [](std::vector<float> const& w, int cin, std::vector<float>& out) {
            out.resize(w.size());
            for (int o = 0; o < C; o++)
                for (int k = 0; k < cin * 9; k++) out[static_cast<size_t>(k) * C + o] = w[static_cast<size_t>(o) * cin * 9 + k];
        };
        convT(w1, FC, w1T);
        convT(w2, C, w2T);
        wfT.resize(wf.size());
        for (int j = 0; j < H; j++)
            for (int i = 0; i < NIN; i++) wfT[static_cast<size_t>(i) * H + j] = wf[static_cast<size_t>(j) * NIN + i];
#ifndef RL_KEEP_SCALAR
        // play uses forwardFast only: drop the scalar copies (~1.8 MB a dragon process at C 64 / H 128)
        std::vector<float>().swap(w1);
        std::vector<float>().swap(w2);
        std::vector<float>().swap(wf);
#endif
    }

    // The network with the output channel innermost - independent lanes the compiler vectorises under -msimd128 (a
    // scalar dot product is a float reduction it may not reorder) - and the zero inputs skipped (one-hot features,
    // relu). Measured in the judge's sandbox, the scalar pass cost about 30M CPU points a turn and this one about 9M at
    // C 64 / H 128. Same sums in another order.
    static inline void axpy(float* __restrict a, float const* __restrict w, float x, int n) {
        for (int k = 0; k < n; k++) a[k] += w[k] * x;
    }
    static void convFast(float const* in, int cin, float const* wT, float const* b, float* out) {
        alignas(16) static float acc[49][C];
        for (int p = 0; p < 49; p++)
            for (int o = 0; o < C; o++) acc[p][o] = b[o];
        for (int i = 0; i < cin; i++)
            for (int rr = 0; rr < 7; rr++)
                for (int cc = 0; cc < 7; cc++) {
                    float const x = in[(i * 7 + rr) * 7 + cc];
                    if (x == 0.0f) continue;
                    for (int kr = 0; kr < 3; kr++) {
                        int const r = rr - kr + 1;
                        if (r < 0 || r > 6) continue;
                        for (int kc = 0; kc < 3; kc++) {
                            int const c = cc - kc + 1;
                            if (c < 0 || c > 6) continue;
                            axpy(acc[r * 7 + c], &wT[(static_cast<size_t>(i) * 9 + kr * 3 + kc) * C], x, C);
                        }
                    }
                }
        for (int o = 0; o < C; o++)
            for (int p = 0; p < 49; p++) out[o * 49 + p] = acc[p][o] > 0 ? acc[p][o] : 0.0f;
    }

    // 3x3 convolution with zero padding and relu; in / out are [channel][7][7] (the scalar reference pass).
    static void conv(float const* in, int cin, float const* w, float const* b, float* out) {
        static float pad[C][9][9];
        for (int i = 0; i < cin; i++)
            for (int r = 0; r < 9; r++)
                for (int c = 0; c < 9; c++)
                    pad[i][r][c] = (r >= 1 && r <= 7 && c >= 1 && c <= 7) ? in[(i * 7 + r - 1) * 7 + c - 1] : 0.0f;
        for (int o = 0; o < C; o++) {
            float acc[7][7];
            for (int r = 0; r < 7; r++)
                for (int c = 0; c < 7; c++) acc[r][c] = b[o];
            for (int i = 0; i < cin; i++)
                for (int kr = 0; kr < 3; kr++)
                    for (int kc = 0; kc < 3; kc++) {
                        float const wk = w[((o * cin + i) * 3 + kr) * 3 + kc];
                        for (int r = 0; r < 7; r++)
                            for (int c = 0; c < 7; c++) acc[r][c] += wk * pad[i][r + kr][c + kc];
                    }
            for (int r = 0; r < 7; r++)
                for (int c = 0; c < 7; c++) out[(o * 7 + r) * 7 + c] = acc[r][c] > 0 ? acc[r][c] : 0.0f;
        }
    }

    // This turn's observation through the network. Features are quantised like the training records
    // (featurize_rl.cpp: x100 as a byte, so 0..2.55 in steps of 0.01), the round like the record's round / 2.
    void run(Init const& init, Turn const& t) {
        Features f;
        featurize(init, t, f);
        for (float& v : f.v) v = std::round(std::min(2.55f, std::max(0.0f, v)) * 100.0f) / 100.0f;
        forwardFast(f.v, t.round);
    }

    void forwardFast(float const* x, int round) {
        convFast(x, FC, w1T.data(), RL_B_C1, a1);
        convFast(a1, C, w2T.data(), RL_B_C2, a2);
        float const rnd = static_cast<float>(std::min(round / 2, 255) * 2) / 500.0f;
        float const* sc = &x[FC * 49];
        alignas(16) float hs[H];
        for (int j = 0; j < H; j++) hs[j] = RL_B_F1[j];
        for (int i = 0; i < C * 49; i++)
            if (a2[i] != 0.0f) axpy(hs, &wfT[static_cast<size_t>(i) * H], a2[i], H);
        for (int i = 0; i < FS; i++)
            if (sc[i] != 0.0f) axpy(hs, &wfT[static_cast<size_t>(C * 49 + i) * H], sc[i], H);
        axpy(hs, &wfT[static_cast<size_t>(C * 49 + FS) * H], rnd, H);
        for (int j = 0; j < H; j++) h[j] = hs[j] > 0 ? hs[j] : 0.0f;
        heads();
    }

    // the scalar pass (tests: define RL_KEEP_SCALAR); play uses forwardFast
    void forward(float const* x, int round) {
#ifndef RL_KEEP_SCALAR
        forwardFast(x, round);
        return;
#endif
        conv(x, FC, w1.data(), RL_B_C1, a1);
        conv(a1, C, w2.data(), RL_B_C2, a2);
        float const rnd = static_cast<float>(std::min(round / 2, 255) * 2) / 500.0f;
        float const* sc = &x[FC * 49];
        for (int j = 0; j < H; j++) {
            float const* wr = &wf[static_cast<size_t>(j) * NIN];
            float s = RL_B_F1[j];
            for (int i = 0; i < C * 49; i++) s += wr[i] * a2[i];
            for (int i = 0; i < FS; i++) s += wr[C * 49 + i] * sc[i];
            s += wr[C * 49 + FS] * rnd;
            h[j] = s > 0 ? s : 0.0f;
        }
        heads();
    }
    void heads() {
        for (int k = 0; k < NOUT; k++) {
            float s = RL_B_POL[k];
            for (int j = 0; j < H; j++) s += wp[k * H + j] * h[j];
            logits[k] = s;
        }
        float v = RL_B_VAL[0];
        for (int j = 0; j < H; j++) v += wv[j] * h[j];
        value = v;
#ifdef RL_HAS_AUX
        for (int k = 0; k < RL_NAUX; k++) {
            float s = RL_B_AUX[k];
            for (int j = 0; j < H; j++) s += wa[k * H + j] * h[j];
            aux[k] = 1.0f / (1.0f + std::exp(-s));
        }
#endif
        float mx = logits[0];
        for (int k = 1; k < NCLASS; k++) mx = std::max(mx, logits[k]);
        float z = 0;
        for (int k = 0; k < NCLASS; k++) z += prob[k] = std::exp(logits[k] - mx);
        for (int k = 0; k < NCLASS; k++) prob[k] /= z;
    }
};

}  // namespace bc
