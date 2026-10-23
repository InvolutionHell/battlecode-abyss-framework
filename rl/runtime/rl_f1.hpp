#pragma once
// The full-action-head ("F1") net (rl/train/train_f1.py) evaluated inside a bot. Same input as rl_net.hpp
// (rl_features.hpp: 17 x 7 x 7 grid + 10 scalars, quantised like the records, + the round) and the same trunk
// (conv3x3 17 -> C, conv3x3 C -> C, dense -> H, relu); weights f1_weights.hpp (F1_* symbols, int16 a row). Another file
// name: define F1_WEIGHTS_HEADER (the released bot keeps this code as bots/abyss/r4net.hpp reading r4_weights.hpp).
// Outputs after run():
//   prob[16]  actions: MOVE with 1 / 2 / 3+ steps (0-3 / 4-7 / 8-11, first step relative to the facing: 0 forward,
//             1 right, 2 back, 3 left), SPLIT n = 2 / 3-4 / 5+ (12 / 13 / 14), no action (15, the default suicide)
//   splitFrac the split's n / length (for 13 / 14); step2[4] the second step relative to the first (same order)
//   win (probability), kingGap (final longest-dragon gap, own - enemy, in segments), totalGap (final total-length gap)
// Optional input sets (F1_NPRIV > 0): set priv[] before run().
//   train_f1.py --priv (8 inputs): own / enemy longest / 32, own / enemy dragon count / 32, own / enemy total length
//     / 128, am-longest (0 / 1), known (1; all zero when the bot has no estimate).
//   F2 weights (F1_NPRIV = 24 or 44): priv[] = F2Memory::features (f2_memory.hpp); f2_run.hpp does both calls.
// Optional action-value heads, learned only on the class that was played (trust them among the policy's top few
// classes only):
//   F1_HAS_Q   qEat[16] = P(this dragon eats within 5 turns | class), qDie[16] = P(it dies by enemy contact within 10)
//   F1_Q_RET   qEat[16] = the discounted pearls it eats (gamma 0.9 over 20 rounds), qDie as above, qKing[16] = the team's
//              longest dragon's discounted growth in segments (gamma 0.95 over 50 rounds)
//   F1_Q_TEAM  also qKG[16] = the team's longest length 50 rounds on minus now, qKD[16] = P(the team's longest dragon of
//              this round dies within 50), qTot[16] = the change of our total-length lead over 50 rounds
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#ifndef F1_WEIGHTS_HEADER
#define F1_WEIGHTS_HEADER "f1_weights.hpp"
#endif
#include F1_WEIGHTS_HEADER
#include "rl_features.hpp"

namespace bc {

struct F1Net {
    static constexpr int C = F1_NC1, H = F1_NH, NA = F1_NA, NIN = F1_NC1 * 49 + FS + 1 + F1_NPRIV;
    enum { MOVE1 = 0, MOVE2 = 4, MOVE3 = 8, SPLIT2 = 12, SPLIT34 = 13, SPLIT5 = 14, NONE = 15 };
    std::vector<float> w1T, w2T, wfT, wp, wa, wv;
    float a1[C * 49], a2[C * 49], h[H];
    float logits[NA], prob[NA];
    float splitFrac = 0, step2[4] = {}, win = 0.5f, kingGap = 0, totalGap = 0;
    float priv[F1_NPRIV > 0 ? F1_NPRIV : 1] = {};
#ifdef F1_HAS_Q
    std::vector<float> wq;
    float qEat[NA] = {}, qDie[NA] = {};
#if defined(F1_Q_TEAM)
    static constexpr int NQ = 6;
    float qKing[NA] = {}, qKG[NA] = {}, qKD[NA] = {}, qTot[NA] = {};
#elif defined(F1_Q_RET)
    static constexpr int NQ = 3;
    float qKing[NA] = {};
#else
    static constexpr int NQ = 2;
#endif
#endif

    F1Net() {
        auto deq = [](int16_t const* q, float const* s, int rows, int per, std::vector<float>& out) {
            out.resize(static_cast<size_t>(rows) * per);
            for (int r = 0; r < rows; r++)
                for (int i = 0; i < per; i++) out[static_cast<size_t>(r) * per + i] = q[static_cast<size_t>(r) * per + i] * s[r];
        };
        std::vector<float> w1, w2, wf;
        deq(F1_W_C1_Q, F1_W_C1_S, C, FC * 9, w1);
        deq(F1_W_C2_Q, F1_W_C2_S, C, C * 9, w2);
        deq(F1_W_F1_Q, F1_W_F1_S, H, NIN, wf);
        deq(F1_W_POL_Q, F1_W_POL_S, NA, H, wp);
        deq(F1_W_AUX_Q, F1_W_AUX_S, 5, H, wa);
        deq(F1_W_VAL_Q, F1_W_VAL_S, 3, H, wv);
#ifdef F1_HAS_Q
        deq(F1_W_Q_Q, F1_W_Q_S, NQ * NA, H, wq);
#endif
        auto convT = [](std::vector<float> const& w, int cin, std::vector<float>& out) {  // output channel innermost
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
    // rl_net.hpp's convFast: 3x3, zero padding, relu; the zero inputs skipped (output channel innermost)
    static void conv(float const* in, int cin, float const* wT, float const* b, float* out) {
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

    // This turn's observation, quantised like the training records (x100 as a byte, the round as round / 2).
    void run(Init const& init, Turn const& t) {
        Features f;
        featurize(init, t, f);
        for (float& v : f.v) v = std::round(std::min(2.55f, std::max(0.0f, v)) * 100.0f) / 100.0f;
        forward(f.v, t.round);
    }

    void forward(float const* x, int round) {
        conv(x, FC, w1T.data(), F1_B_C1, a1);
        conv(a1, C, w2T.data(), F1_B_C2, a2);
        float const rnd = static_cast<float>(std::min(round / 2, 255) * 2) / 500.0f;
        float const* sc = &x[FC * 49];
        alignas(16) float hs[H];
        for (int j = 0; j < H; j++) hs[j] = F1_B_F1[j];
        for (int i = 0; i < C * 49; i++)
            if (a2[i] != 0.0f) axpy(hs, &wfT[static_cast<size_t>(i) * H], a2[i], H);
        for (int i = 0; i < FS; i++)
            if (sc[i] != 0.0f) axpy(hs, &wfT[static_cast<size_t>(C * 49 + i) * H], sc[i], H);
        axpy(hs, &wfT[static_cast<size_t>(C * 49 + FS) * H], rnd, H);
        for (int i = 0; i < F1_NPRIV; i++)
            if (priv[i] != 0.0f) axpy(hs, &wfT[static_cast<size_t>(C * 49 + FS + 1 + i) * H], priv[i], H);
        for (int j = 0; j < H; j++) h[j] = hs[j] > 0 ? hs[j] : 0.0f;
        auto dot = [&](std::vector<float> const& w, int row, float bias) {
            float s = bias;
            for (int j = 0; j < H; j++) s += w[static_cast<size_t>(row) * H + j] * h[j];
            return s;
        };
        float mx = -1e30f;
        for (int k = 0; k < NA; k++) mx = std::max(mx, logits[k] = dot(wp, k, F1_B_POL[k]));
        float z = 0;
        for (int k = 0; k < NA; k++) z += prob[k] = std::exp(logits[k] - mx);
        for (int k = 0; k < NA; k++) prob[k] /= z;
        splitFrac = 1.0f / (1.0f + std::exp(-dot(wa, 0, F1_B_AUX[0])));
        float s2[4], m2 = -1e30f, z2 = 0;
        for (int k = 0; k < 4; k++) m2 = std::max(m2, s2[k] = dot(wa, 1 + k, F1_B_AUX[1 + k]));
        for (int k = 0; k < 4; k++) z2 += step2[k] = std::exp(s2[k] - m2);
        for (int k = 0; k < 4; k++) step2[k] /= z2;
        win = 1.0f / (1.0f + std::exp(-dot(wv, 0, F1_B_VAL[0])));
        kingGap = dot(wv, 1, F1_B_VAL[1]) * 32.0f;
        totalGap = dot(wv, 2, F1_B_VAL[2]) * 128.0f;
#ifdef F1_HAS_Q
        for (int k = 0; k < NA; k++) {
#ifdef F1_Q_RET
            qEat[k] = dot(wq, k, F1_B_Q[k]);
            qKing[k] = dot(wq, 2 * NA + k, F1_B_Q[2 * NA + k]) * 4.0f;
#else
            qEat[k] = 1.0f / (1.0f + std::exp(-dot(wq, k, F1_B_Q[k])));
#endif
            qDie[k] = 1.0f / (1.0f + std::exp(-dot(wq, NA + k, F1_B_Q[NA + k])));
#ifdef F1_Q_TEAM
            qKG[k] = dot(wq, 3 * NA + k, F1_B_Q[3 * NA + k]) * 16.0f;
            qKD[k] = 1.0f / (1.0f + std::exp(-dot(wq, 4 * NA + k, F1_B_Q[4 * NA + k])));
            qTot[k] = dot(wq, 5 * NA + k, F1_B_Q[5 * NA + k]) * 64.0f;
#endif
        }
#endif
    }

    // P(the first step goes to relative direction rel), any step count
    float moveProb(int rel) const { return prob[MOVE1 + rel] + prob[MOVE2 + rel] + prob[MOVE3 + rel]; }
    float splitProb() const { return prob[SPLIT2] + prob[SPLIT34] + prob[SPLIT5]; }
    // the split's n for this length: 2, else the fraction's n clamped to the bucket the net prefers
    int splitN(int length) const {
        int const hi = length - 2;  // the parent keeps at least 2
        if (hi < 2) return 0;
        int best = SPLIT2;
        if (prob[SPLIT34] > prob[best]) best = SPLIT34;
        if (prob[SPLIT5] > prob[best]) best = SPLIT5;
        int n = static_cast<int>(std::lround(splitFrac * length));
        if (best == SPLIT2) n = 2;
        else if (best == SPLIT34) n = std::clamp(n, 3, 4);
        else n = std::max(n, 5);
        return std::clamp(n, 2, hi);
    }
};

}  // namespace bc
