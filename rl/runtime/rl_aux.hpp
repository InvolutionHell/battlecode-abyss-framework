#pragma once
// The auxiliary heads of a 9-class net (pretrain.py --aux-weight) as a second network in one bot, next to the move
// prior of rl_net.hpp. The same trunk as rl_net.hpp's forwardFast (output channels innermost, zero inputs skipped) on
// the RLA_* weights of
//   python3 rl/train/export_bot.py <aux weights_rl.hpp> rla_weights.hpp --prefix RLA
// Only the trunk and the aux head run (no policy / value heads).
//   static bc::RLNetAux aux;  aux.run(init, turn);  aux.p[RLNetAux::AUX_EAT5] ...
// Heads (rl/data/aux_labels.py): P(split within its next 5 turns), P(eat a pearl within 5), P(die by enemy contact
// within 10), P(an enemy dies on it within 10). Death and kill rates fall with the dragon's length, so read them
// relative to the base rate at that length (rl/eval/aux_eval.py prints the calibration by length and by round).
#include <algorithm>
#include <cmath>
#include <vector>

#include "rl_features.hpp"
#include "rla_weights.hpp"

namespace bc {

struct RLNetAux {
    static constexpr int C = RLA_NC1, H = RLA_NH, NIN = RLA_NC1 * 49 + FS + 1, NA = RLA_NAUX;
    enum { AUX_SPLIT5, AUX_EAT5, AUX_DIE10, AUX_KILL10 };
    std::vector<float> w1T, w2T, wfT, wa;
    float a1[C * 49], a2[C * 49], h[H];
    float p[NA] = {};  // probabilities, indexed by the enum above

    RLNetAux() {
        auto deq = [](int16_t const* q, float const* s, int rows, int per, std::vector<float>& out) {
            out.resize(static_cast<size_t>(rows) * per);
            for (int r = 0; r < rows; r++)
                for (int i = 0; i < per; i++) out[static_cast<size_t>(r) * per + i] = q[static_cast<size_t>(r) * per + i] * s[r];
        };
        std::vector<float> w1, w2, wf;
        deq(RLA_W_C1_Q, RLA_W_C1_S, C, FC * 9, w1);
        deq(RLA_W_C2_Q, RLA_W_C2_S, C, C * 9, w2);
        deq(RLA_W_F1_Q, RLA_W_F1_S, H, NIN, wf);
        deq(RLA_W_AUX_Q, RLA_W_AUX_S, NA, H, wa);
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
    }

    static inline void axpy(float* __restrict a, float const* __restrict w, float x, int n) {
        for (int k = 0; k < n; k++) a[k] += w[k] * x;
    }
    // 3x3 convolution with zero padding and relu, in / out [channel][7][7], accumulated output-channel innermost
    static void conv(float const* in, int cin, float const* wT, float const* b, float* out) {
        alignas(16) static float acc[49][C];
        for (int q = 0; q < 49; q++)
            for (int o = 0; o < C; o++) acc[q][o] = b[o];
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
            for (int q = 0; q < 49; q++) out[o * 49 + q] = acc[q][o] > 0 ? acc[q][o] : 0.0f;
    }

    // This turn's observation, quantised like the training records (featurize_rl.cpp: x100 as a byte).
    void run(Init const& init, Turn const& t) {
        Features f;
        featurize(init, t, f);
        for (float& v : f.v) v = std::round(std::min(2.55f, std::max(0.0f, v)) * 100.0f) / 100.0f;
        forward(f.v, t.round);
    }

    void forward(float const* x, int round) {
        conv(x, FC, w1T.data(), RLA_B_C1, a1);
        conv(a1, C, w2T.data(), RLA_B_C2, a2);
        float const rnd = static_cast<float>(std::min(round / 2, 255) * 2) / 500.0f;
        float const* sc = &x[FC * 49];
        alignas(16) float hs[H];
        for (int j = 0; j < H; j++) hs[j] = RLA_B_F1[j];
        for (int i = 0; i < C * 49; i++)
            if (a2[i] != 0.0f) axpy(hs, &wfT[static_cast<size_t>(i) * H], a2[i], H);
        for (int i = 0; i < FS; i++)
            if (sc[i] != 0.0f) axpy(hs, &wfT[static_cast<size_t>(C * 49 + i) * H], sc[i], H);
        axpy(hs, &wfT[static_cast<size_t>(C * 49 + FS) * H], rnd, H);
        for (int j = 0; j < H; j++) h[j] = hs[j] > 0 ? hs[j] : 0.0f;
        for (int k = 0; k < NA; k++) {
            float s = RLA_B_AUX[k];
            for (int j = 0; j < H; j++) s += wa[static_cast<size_t>(k) * H + j] * h[j];
            p[k] = 1.0f / (1.0f + std::exp(-s));
        }
    }
};

}  // namespace bc
