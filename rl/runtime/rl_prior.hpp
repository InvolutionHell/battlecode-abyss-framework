#pragma once
// The 9-class net as a drop-in move / split prior for any search bot that shares io.hpp's Init / Turn. Copy into the
// bot folder with rl_net.hpp, rl_features.hpp (rl/featurize/rl_features.hpp == bots/abyss/rl_features.hpp) and an
// rl_weights.hpp (rl/train/export_bot.py). Once per turn (each dragon is its own process):
//   bc::g_rl.run(init, turn);                                                  // before the search decides
//   if (g_rl.priorOn) cand.total += g_rl.w * g_rl.logDir(cand.dir, turn.dir);  // each legal candidate first step
//   if (g_rl.on && g_rl.pSplit() >= threshold) ...                             // the split trigger, if wanted
// Cost in the judge's sandbox at C 64 / H 128: about 9M CPU points a turn with forwardFast (of the 100M a turn limit).
// With an auxiliary-head net set netUntil = 500 to read aux[] all game; the move prior still stops at priorUntil.
// Scale: the search values an eaten pearl at about 1.0, so w = 0.05 shifts a confident choice by a small fraction of a
// pearl - the net nudges and the search decides. In our tests larger weights (0.3 and up) made the dragons follow the
// imitated movement habits instead of the pearls and lost; 0.02-0.05 on every move was the safe range.
#include <algorithm>
#include <cmath>

#include "rl_net.hpp"

namespace bc {

struct RLPrior {
    float w = 0.05f;       // search value per unit of log p
    int priorUntil = 150;  // the move prior is on before this round (an opening prior is trained on early rounds)
    int netUntil = 150;    // the net runs before this round (500 to read aux[] all game)
    bool on = false;       // the net ran this turn
    bool priorOn = false;  // ... and the move prior applies
    float prob[NCLASS] = {};  // 0-3 a step forward / right / back / left of our facing, 4-7 the same as a sprint's first
                              // step, 8 split
#ifdef RL_HAS_AUX
    float aux[RL_NAUX] = {};  // RLNet::AUX_SPLIT5 / AUX_EAT5 / AUX_DIE10 / AUX_KILL10 (probabilities)
#endif

    void run(Init const& init, Turn const& t) {
        on = t.round < netUntil;
        priorOn = on && t.round < priorUntil;
        if (!on) return;
        static RLNet net;  // weights dequantised once per process
        net.run(init, t);
        std::copy(net.prob, net.prob + NCLASS, prob);
#ifdef RL_HAS_AUX
        std::copy(net.aux, net.aux + RL_NAUX, aux);
#endif
    }
    // log p of a first step in absolute direction d (0 N, 1 E, 2 S, 3 W) given our facing
    float logDir(int d, int facing) const {
        int const rel = (d - facing) & 3;
        return std::log(prob[rel] + prob[4 + rel] + 1e-3f);
    }
    float pSplit() const { return prob[NCLASS - 1]; }
};

inline RLPrior g_rl;

}  // namespace bc
