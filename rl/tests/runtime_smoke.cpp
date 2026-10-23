// Compile-and-run smoke test of the runtime headers on tiny dummy weights (rl/tests/dummy_weights.py): every network
// runs on a hand-made turn, the outputs must be finite and normalised, and RLNet's vectorised pass (forwardFast, the one
// the bots use) must equal its scalar reference pass.
//   python3 rl/tests/dummy_weights.py W
//   c++ -std=c++17 -O2 -DRL_KEEP_SCALAR -I rl/runtime -I rl/featurize -I W rl/tests/runtime_smoke.cpp -o smoke && ./smoke
//   c++ -std=c++17 -O2 -DSMOKE_F2 -DF1_WEIGHTS_HEADER='"f2_weights.hpp"' -I rl/runtime -I rl/featurize -I W \
//       rl/tests/runtime_smoke.cpp -o smoke_f2 && ./smoke_f2
// Prints PASS and exits 0, or names the failed check and exits 1.
#include <cmath>
#include <cstdio>

#include "f2_run.hpp"
#ifndef SMOKE_F2
#include "rl_aux.hpp"
#include "rl_late.hpp"
#include "rl_prior.hpp"
#endif

using namespace bc;

namespace {

int fails = 0;

void expect(bool ok, char const* what) {
    if (!ok) {
        std::printf("FAILED: %s\n", what);
        fails++;
    }
}

bool normalised(float const* p, int n) {
    double s = 0;
    for (int i = 0; i < n; i++) {
        if (!std::isfinite(p[i]) || p[i] < 0) return false;
        s += p[i];
    }
    return std::fabs(s - 1.0) < 1e-5;
}

// A 20 x 20 torus, our dragon (id 2, team A) heading east with its head at (10, 10); a pearl two tiles ahead, a bed
// with a countdown, an enemy head, kelp on two edges.
void makeTurn(Init& init, Turn& t, int round) {
    init.id = 2;
    init.team = 'A';
    init.w = init.h = 20;
    t = Turn{};
    t.round = round;
    t.dir = E;
    t.length = 4;
    t.units = 3;
    t.echoes[0] = 1;
    for (int r = 0; r < 7; r++)
        for (int c = 0; c < 7; c++) {
            VisTile& v = t.tiles[r * 7 + c];
            v.x = 10 + c - 3;
            v.y = 10 + r - 3;
            v.pearl = (c == 5 && r == 3);
            v.pearlIn = (c == 1 && r == 1) ? 6 : -1;
            t.hRows[r][c] = {0, -1};
            t.vRows[r][c] = {0, -1};
        }
    for (int c = 0; c < 7; c++) t.hRows[7][c] = {0, -1};
    for (int r = 0; r < 7; r++) t.vRows[r][7] = {0, -1};
    t.hRows[2][4] = {1, -1};
    t.vRows[5][2] = {1, -1};
    t.parts = {{'A', 2, 10, 10, E, true}, {'A', 2, 9, 10, E, false}, {'A', 2, 8, 10, E, false}, {'A', 2, 7, 10, E, false},
               {'B', 5, 12, 8, S, true}, {'B', 5, 12, 7, S, false}};
}

}  // namespace

int main() {
    Init init;
    Turn t;
    static F1Net f1;
    static F2Memory mem;
    for (int round = 30; round < 34; round++) {  // a few turns, so the memory accumulates
        makeTurn(init, t, round);
        f2Run(f1, mem, init, t);
    }
    expect(normalised(f1.prob, F1Net::NA), "F1Net prob is a distribution");
    expect(normalised(f1.step2, 4), "F1Net step2 is a distribution");
    expect(f1.splitFrac > 0 && f1.splitFrac < 1 && f1.win > 0 && f1.win < 1, "F1Net splitFrac / win in (0, 1)");
    double moves = 0;
    for (int r = 0; r < 4; r++) moves += f1.moveProb(r);
    expect(std::fabs(moves + f1.splitProb() + f1.prob[F1Net::NONE] - 1.0) < 1e-5, "F1Net moveProb + splitProb + none = 1");
    expect(f1.splitN(8) >= 2 && f1.splitN(8) <= 6, "F1Net splitN within the length");
#ifdef F1_HAS_Q
    bool qok = true;
    for (int k = 0; k < F1Net::NA; k++) qok = qok && std::isfinite(f1.qEat[k]) && f1.qDie[k] > 0 && f1.qDie[k] < 1;
    expect(qok, "F1Net Q heads finite");
    float m[F2_NMEM];
    mem.features(init, t, m);
    bool mok = true;
    for (int k = 0; k < F2_NMEM; k++) mok = mok && m[k] >= 0 && m[k] <= 1 && std::fabs(m[k] * 255 - std::round(m[k] * 255)) < 1e-3;
    expect(mok, "F2Memory features quantised to bytes in [0, 1]");
    // inputs 0-3 = forward, right, back, left: the pearl is straight ahead, the back step is our own neck
    expect(m[2] == 0 && m[0] > m[1] && m[0] > m[3], "F2Memory: the known pearl ahead is nearest, the back step blocked");
    std::printf("F2 net: P(no action) %.4f, splitProb %.4f, qDie[0] %.4f, memory[0..3] %.3f %.3f %.3f %.3f\n",
                f1.prob[F1Net::NONE], f1.splitProb(), f1.qDie[0], m[0], m[1], m[2], m[3]);
#else
    std::printf("F1 net: P(forward) %.4f, splitProb %.4f, win %.4f\n", f1.moveProb(0), f1.splitProb(), f1.win);
    // the 9-class prior, its scalar reference pass, the auxiliary and late nets
    g_rl.priorUntil = g_rl.netUntil = 500;
    g_rl.run(init, t);
    expect(g_rl.on && g_rl.priorOn, "RLPrior runs before priorUntil");
    expect(normalised(g_rl.prob, NCLASS), "RLPrior prob is a distribution");
    expect(std::isfinite(g_rl.logDir(N, t.dir)) && g_rl.logDir(N, t.dir) <= 0, "RLPrior logDir is a log probability");
    static RLNet net;
    Features f;
    featurize(init, t, f);
    for (float& v : f.v) v = std::round(std::min(2.55f, std::max(0.0f, v)) * 100.0f) / 100.0f;
    net.forwardFast(f.v, t.round);
    float fast[NCLASS + 1];
    for (int k = 0; k <= NCLASS; k++) fast[k] = net.logits[k];
    float const vfast = net.value;
    net.forward(f.v, t.round);  // the scalar pass (RL_KEEP_SCALAR keeps its weights)
    double d = std::fabs(vfast - net.value);
    for (int k = 0; k <= NCLASS; k++) d = std::max(d, static_cast<double>(std::fabs(fast[k] - net.logits[k])));
    expect(d < 1e-4, "RLNet forwardFast == the scalar forward");
    std::printf("RL net: P(split) %.4f, value %.4f, forwardFast vs scalar max |diff| %.2g\n", g_rl.pSplit(), net.value, d);
#ifdef RL_HAS_AUX
    expect(g_rl.aux[0] > 0 && g_rl.aux[0] < 1, "RLPrior aux in (0, 1)");
#endif
    static RLNetAux aux;
    aux.run(init, t);
    bool aok = true;
    for (float v : aux.p) aok = aok && v > 0 && v < 1;
    expect(aok, "RLNetAux aux in (0, 1)");
    g_rll.from = 0;
    g_rll.run(init, t);
    expect(g_rll.on && std::isfinite(g_rll.logDir(E, t.dir)), "RLLatePrior runs from `from`");
#endif
    std::printf(fails ? "FAIL\n" : "PASS\n");
    return fails ? 1 : 0;
}
