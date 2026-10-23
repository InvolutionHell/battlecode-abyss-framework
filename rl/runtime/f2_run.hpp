#pragma once
// The one call per dragon-turn for an F1 / F2 net inside a bot: the dragon's own memory (F2 nets only), then the net on
// this turn's observation. rl/eval/consistency/check.cpp tests exactly this path against the training featurizer, so a
// bot should call it rather than its own glue.
//   static F1Net net;  static F2Memory mem;   // one of each per dragon process, the memory from its first turn on
//   f2Run(net, mem, init, turn);              // every turn, before anything reads net.prob / net.q*
// Inputs as in the training records: F2Memory::features is already quantised to 1/255, F1Net::run rounds the
// observation to 1/100 and the round to round / 2. An F1 net without memory inputs (F1_NPRIV == 0, like the released
// bot's move prior) ignores the memory; the memory is still updated so the call is the same for both kinds of net.
// Nets trained with train_f1.py --priv (8 privileged inputs) are not covered: the bot has to estimate those itself.
//
// The F1 net's code: rl_f1.hpp by default; define F1_NET_HEADER to use another copy of the same network code (for
// example -DF1_NET_HEADER='"r4net.hpp"' with -I bots/abyss tests the released bot's own copy).
#ifndef F1_NET_HEADER
#define F1_NET_HEADER "rl_f1.hpp"
#endif
#include F1_NET_HEADER
#include "f2_memory.hpp"

namespace bc {

static_assert(F1_NPRIV == 0 || F1_NPRIV == F2_NMEM,
              "f2Run feeds F2Memory's inputs: an F2 net needs F1_NPRIV == F2_NMEM (24, or 44 with F2_HEARD)");

inline void f2Run(F1Net& net, F2Memory& mem, Init const& init, Turn const& t) {
    mem.update(init, t);  // every turn, as featurize_f2.cpp replays the training games
    if constexpr (F1_NPRIV == F2_NMEM) mem.features(init, t, net.priv);  // no side effects on the memory
    net.run(init, t);  // featurize + the records' quantisation + forward
}

}  // namespace bc
