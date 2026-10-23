// Train / serve consistency check: replays recorded turns through a bot folder's own code - io.hpp's readTurn, the
// bot's featurizer and network code - and compares every turn with an expectation file written by make_vectors.py
// (the training featurizer's bytes, featurize_f2, and ref_forward.py's independent float64 forward of the weights).
//
// Two modes, chosen at compile time:
//   F1 / F2 nets (default): f2_run.hpp's f2Run (F2Memory::update / features from the dragon's first turn on, then
//     F1Net::run). The F1 net's code is rl_f1.hpp, or another copy named by F1_NET_HEADER.
//   9-class nets (-DCHECK_RLNET): rl_net.hpp's RLNet::run (the opening prior's code path).
//
//   # the released bot's move prior (its own r4net.hpp + r4_weights.hpp), against expect_r4.txt:
//   c++ -std=c++17 -O2 -ffp-contract=off -DF1_NET_HEADER='"r4net.hpp"' -I bots/abyss -I rl/runtime \
//       rl/eval/consistency/check.cpp -o check_r4 && ./check_r4 turns.txt expect_r4.txt
//   # the released bot's opening prior (rl_net.hpp + rl_weights.hpp):
//   c++ -std=c++17 -O2 -ffp-contract=off -DCHECK_RLNET -I bots/abyss rl/eval/consistency/check.cpp -o check_rl
//
// The first -I folder must hold io.hpp (+ common.hpp) and rl_features.hpp - the code under test - and the weights the
// expectations were made with. -ffp-contract=off keeps a native build from fusing a += w * x into FMAs the WASM build
// does not have (the tolerances absorb it either way).
// Exit status 0 when the feature (and memory) bytes match on every turn and every output is within its tolerance:
// --tol (default 1e-5) absolute on the probabilities (prob, splitFrac, step2, win, qDie, qKD, aux); 1e-4 relative to
// max(1, |value|) on the logits and the value logit; 1e-3 relative on the regression heads scaled up after the dot
// product (qEat, qKing, kingGap x 32, totalGap x 128, qKG x 16, qTot x 64), where float32 accumulation alone reaches ~2e-4.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "io.hpp"
#ifdef CHECK_RLNET
#include "rl_net.hpp"
#else
#include "f2_run.hpp"  // F1_NET_HEADER (rl_f1.hpp) + f2_memory.hpp
#endif

using namespace bc;

namespace {

int hexByte(char const* p) {
    auto v = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
    return v(p[0]) * 16 + v(p[1]);
}

struct Stat {
    char const* name;
    int size;
    bool relative;
    double tol;  // < 0: --tol
    double maxErr = 0;
    long bad = 0;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: check turns.txt expect.txt [--tol 1e-5] [--verbose]\n");
        return 2;
    }
    double tol = 1e-5;
    bool verbose = false;
    for (int i = 3; i < argc; i++) {
        if (!std::strcmp(argv[i], "--tol") && i + 1 < argc) tol = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--verbose")) verbose = true;
    }
    if (!std::freopen(argv[1], "r", stdin)) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    std::ifstream ex(argv[2]);
    if (!ex) {
        std::fprintf(stderr, "cannot open %s\n", argv[2]);
        return 2;
    }
    Reader in;
    Turn t;
    std::string game;
#ifdef CHECK_RLNET
    static RLNet net;
    constexpr int NMEM = 0;
#ifdef RL_HAS_AUX
    constexpr int NAUX = RL_NAUX;
#else
    constexpr int NAUX = 0;
#endif
    std::vector<Stat> st = {{"logits", RLNet::NOUT, true, 1e-4}, {"prob", NCLASS, false, -1}, {"value", 1, true, 1e-4},
                            {"aux", NAUX, false, -1}};
#else
    static F1Net net;
    std::map<int, F2Memory> mem;
    constexpr int NMEM = F1_NPRIV > 0 ? F2_NMEM : 0;  // memory bytes are compared only for nets that read them
    std::vector<Stat> st = {{"logits", 16, true, 1e-4},  {"prob", 16, false, -1},     {"splitFrac", 1, false, -1},
                            {"step2", 4, false, -1},     {"win", 1, false, -1},       {"kingGap", 1, true, 1e-3},
                            {"totalGap", 1, true, 1e-3}, {"qEat", 16, true, 1e-3},    {"qDie", 16, false, -1},
                            {"qKing", 16, true, 1e-3},   {"qKG", 16, true, 1e-3},     {"qKD", 16, false, -1},
                            {"qTot", 16, true, 1e-3}};
#endif
    long turns = 0, featBad = 0, memBad = 0;
    while (in.next()) {
        if (!in.is("REC")) continue;
        if (game != in.str(1)) {
            game = in.str(1);
#ifndef CHECK_RLNET
            mem.clear();  // a new game: every dragon's memory starts empty
#endif
        }
        Init init;
        init.id = in.num(2);
        init.team = in.str(3)[0];
        init.w = in.num(4);
        init.h = in.num(5);
        init.unitLimit = in.num(6);
        if (!readTurn(in, t)) break;
        while (in.next() && !in.is("END")) {}
        std::string line;
        if (!std::getline(ex, line)) {
            std::fprintf(stderr, "expect file ends before turn %ld\n", turns);
            return 1;
        }
        std::istringstream ls(line);
        std::string eg, fhex, mhex;
        int eid = 0, ernd = 0;
        ls >> eg >> eid >> ernd >> fhex >> mhex;
        if (eg != game || eid != init.id || ernd != t.round || fhex.size() != 2 * FDIM ||
            (NMEM > 0 ? mhex.size() != 2 * static_cast<size_t>(NMEM) : mhex != "-")) {
            std::fprintf(stderr, "turn %ld: the expect file has %s %d r%d (%zu memory hex digits), turns.txt %s %d r%d\n",
                         turns, eg.c_str(), eid, ernd, mhex.size(), game.c_str(), init.id, t.round);
            return 1;
        }
        // the bot's call, then the bytes the training records hold for this turn, for the comparison
#ifdef CHECK_RLNET
        net.run(init, t);
#else
        F2Memory& mm = mem[init.id];
        f2Run(net, mm, init, t);
#endif
        Features f;
        featurize(init, t, f);
        int fd = 0;
        for (int i = 0; i < FDIM; i++)
            fd += std::lround(std::min(2.55f, std::max(0.0f, f.v[i])) * 100) != hexByte(&fhex[2 * i]);
        int md = 0;
#ifndef CHECK_RLNET
        if (NMEM > 0) {
            float m[F2_NMEM];
            mm.features(init, t, m);
            for (int k = 0; k < NMEM; k++) md += std::lround(m[k] * 255) != hexByte(&mhex[2 * k]);
        }
#endif
        featBad += fd != 0;
        memBad += md != 0;
        if ((fd || md) && verbose)
            std::fprintf(stderr, "turn %ld (%s dragon %d r%d): %d feature bytes, %d memory bytes differ\n", turns,
                         game.c_str(), init.id, t.round, fd, md);
        std::vector<float> got;
        auto add = [&](float const* v, int n) { got.insert(got.end(), v, v + n); };
#ifdef CHECK_RLNET
        add(net.logits, RLNet::NOUT);
        add(net.prob, NCLASS);
        add(&net.value, 1);
#ifdef RL_HAS_AUX
        add(net.aux, RL_NAUX);
#endif
#else
        add(net.logits, 16);
        add(net.prob, 16);
        add(&net.splitFrac, 1);
        add(net.step2, 4);
        add(&net.win, 1);
        add(&net.kingGap, 1);
        add(&net.totalGap, 1);
#ifdef F1_HAS_Q
        add(net.qEat, 16);
        add(net.qDie, 16);
#if defined(F1_Q_RET) || defined(F1_Q_TEAM)
        add(net.qKing, 16);
#endif
#ifdef F1_Q_TEAM
        add(net.qKG, 16);
        add(net.qKD, 16);
        add(net.qTot, 16);
#endif
#endif
#endif
        size_t pos = 0;
        for (Stat& s : st) {
            if (pos >= got.size()) break;
            double worst = 0;
            for (int k = 0; k < s.size; k++, pos++) {
                double e;
                if (!(ls >> e)) {
                    std::fprintf(stderr, "turn %ld: expect line too short at %s\n", turns, s.name);
                    return 1;
                }
                double err = std::fabs(got[pos] - e);
                if (s.relative) err /= std::max(1.0, std::fabs(e));
                worst = std::max(worst, err);
            }
            s.maxErr = std::max(s.maxErr, worst);
            if (worst > (s.tol < 0 ? tol : s.tol)) {
                s.bad++;
                if (verbose)
                    std::fprintf(stderr, "turn %ld (%s dragon %d r%d): %s off by %.3g\n", turns, game.c_str(), init.id,
                                 t.round, s.name, worst);
            }
        }
        turns++;
    }
    bool ok = turns > 0 && featBad == 0 && memBad == 0;
    std::printf("%ld turns: feature bytes differ on %ld, memory bytes on %ld (%d memory inputs compared)\n", turns,
                featBad, memBad, NMEM);
    for (size_t s = 0; s < st.size(); s++) {
        if (st[s].maxErr == 0 && st[s].bad == 0 && s >= 3) continue;  // heads the net does not have
        std::printf("  %-9s max %s error %.3g, turns over %.0e: %ld\n", st[s].name, st[s].relative ? "relative" : "absolute",
                    st[s].maxErr, st[s].tol < 0 ? tol : st[s].tol, st[s].bad);
        ok = ok && st[s].bad == 0;
    }
    std::printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
