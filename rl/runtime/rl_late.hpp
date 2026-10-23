#pragma once
// A second 9-class prior net in one bot: rl_net.hpp's net (RL_* weights) before a switch round, a mid / late-game net
// after it, trained like the first on later rounds (rl/data/build_rl.py --min-round). It reads RLL_* weights:
//   python3 rl/train/export_bot.py <late weights_rl.hpp> rll_weights.hpp --prefix RLL
// The same trunk as rl_net.hpp's forwardFast (output channels innermost, zero inputs skipped); only the transposed
// weights are kept and only the policy head runs.
//   bc::g_rll.from = switchRound;  g_rl.priorUntil = switchRound;   // rl_prior.hpp's prior hands over at switchRound
//   bc::g_rll.run(init, turn);                                       // once per turn (runs only from `from`)
//   if (g_rll.on) cand.total += g_rll.w * g_rll.logDir(cand.dir, turn.dir);
#include <algorithm>
#include <cmath>
#include <vector>

#include "rl_features.hpp"
#include "rll_weights.hpp"

namespace bc {

struct RLNetLate {
    static constexpr int C = RLL_NC1, H = RLL_NH, NIN = RLL_NC1 * 49 + FS + 1, NOUT = NCLASS + 1;
    std::vector<float> w1T, w2T, wfT, wp;
    float a1[C * 49], a2[C * 49], h[H];
    float logits[NOUT];
    float prob[NCLASS] = {};  // softmax over the action classes (0-3 step forward / right / back / left, 4-7 sprints, 8 split)

    RLNetLate() {
        auto deq = [](int16_t const* q, float const* s, int rows, int per, std::vector<float>& out) {
            out.resize(static_cast<size_t>(rows) * per);
            for (int r = 0; r < rows; r++)
                for (int i = 0; i < per; i++) out[static_cast<size_t>(r) * per + i] = q[static_cast<size_t>(r) * per + i] * s[r];
        };
        std::vector<float> w1, w2, wf;  // scalar copies only while transposing
        deq(RLL_W_C1_Q, RLL_W_C1_S, C, FC * 9, w1);
        deq(RLL_W_C2_Q, RLL_W_C2_S, C, C * 9, w2);
        deq(RLL_W_F1_Q, RLL_W_F1_S, H, NIN, wf);
        deq(RLL_W_POL_Q, RLL_W_POL_S, NOUT, H, wp);
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

    void run(Init const& init, Turn const& t) {
        Features f;
        featurize(init, t, f);
        for (float& v : f.v) v = std::round(std::min(2.55f, std::max(0.0f, v)) * 100.0f) / 100.0f;
        forward(f.v, t.round);
    }

    void forward(float const* x, int round) {
        conv(x, FC, w1T.data(), RLL_B_C1, a1);
        conv(a1, C, w2T.data(), RLL_B_C2, a2);
        float const rnd = static_cast<float>(std::min(round / 2, 255) * 2) / 500.0f;
        float const* sc = &x[FC * 49];
        alignas(16) float hs[H];
        for (int j = 0; j < H; j++) hs[j] = RLL_B_F1[j];
        for (int i = 0; i < C * 49; i++)
            if (a2[i] != 0.0f) axpy(hs, &wfT[static_cast<size_t>(i) * H], a2[i], H);
        for (int i = 0; i < FS; i++)
            if (sc[i] != 0.0f) axpy(hs, &wfT[static_cast<size_t>(C * 49 + i) * H], sc[i], H);
        axpy(hs, &wfT[static_cast<size_t>(C * 49 + FS) * H], rnd, H);
        for (int j = 0; j < H; j++) h[j] = hs[j] > 0 ? hs[j] : 0.0f;
        for (int k = 0; k < NOUT; k++) {
            float s = RLL_B_POL[k];
            for (int j = 0; j < H; j++) s += wp[static_cast<size_t>(k) * H + j] * h[j];
            logits[k] = s;
        }
        float mx = logits[0];
        for (int k = 1; k < NCLASS; k++) mx = std::max(mx, logits[k]);
        float z = 0;
        for (int k = 0; k < NCLASS; k++) z += prob[k] = std::exp(logits[k] - mx);
        for (int k = 0; k < NCLASS; k++) prob[k] /= z;
    }
};

struct RLLatePrior {
    float w = 0.05f;   // weight of log p on the search value (see rl_prior.hpp for the scale)
    int from = 150;    // the switch round: the late net from here
    int until = 500;   // ... to here
    bool on = false;   // the net ran this turn
    float prob[NCLASS] = {};

    void run(Init const& init, Turn const& t) {
        on = t.round >= from && t.round < until;
        if (!on) return;
        static RLNetLate net;  // weights dequantised once per process
        net.run(init, t);
        std::copy(net.prob, net.prob + NCLASS, prob);
    }
    float logDir(int d, int facing) const {
        int const rel = (d - facing) & 3;
        return std::log(prob[rel] + prob[4 + rel] + 1e-3f);
    }
    float pSplit() const { return prob[NCLASS - 1]; }
};

inline RLLatePrior g_rll;

}  // namespace bc
