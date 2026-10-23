// Longest reserve (BC_LONGEST_RESERVE, 3 in the final build): from kReserveFrom the team's longest dragon keeps every segment.
//   c++ -std=c++20 -O2 -I bots/abyss tests/native/longest_reserve_test.cpp -o longest_reserve_test && ./longest_reserve_test
// World::longestReserve (rounds, the queen, the LONGEST facts' freshness, ties), Policy::trySplit's guard and the early
// feeding phase of mode 2 (not used: modes 1 and 3 keep the default feeding round) on a synthetic 20 x 20 board. The
// feeders of mode 3 are tested in reserve_feed_test.cpp. ablation.hpp sets the switch unconditionally.
#define private public
#include "policy.hpp"
#undef private

#include <cstdio>

static int fails = 0, checks = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        checks++;                                                   \
        if (!(c)) {                                                 \
            fails++;                                                \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                           \
    } while (0)

using namespace bc;

static World dragon(int id, int len, int round) {
    World w;
    Init in;
    in.id = id;
    in.team = 'A';
    in.w = in.h = 20;
    w.start(in);
    w.t.round = round;
    w.t.length = len;
    w.t.units = 10;
    w.head = w.board.id(10, 10);
    for (int i = 0; i < len; i++) w.body.push_back(w.board.id(10 - i, 10));
    w.occ.assign(w.board.NC, -1);
    w.occHeadId.assign(w.board.NC, -1);
    return w;
}

static void testReserve() {
    World w = dragon(6, 9, 350);
    CHECK(w.longestReserve());                        // no fact at all, 9 long (>= kReserveMinLen)
    w.t.length = kReserveMinLen;
    CHECK(w.longestReserve());                        // ... exactly kReserveMinLen
    w.t.length = 7;
    CHECK(!w.longestReserve());                       // ... 7 long: not without a fact
    w.know.longest = {w.board.id(3, 3), 12, 8, 345};  // a fresh fact: dragon 8 is 12 long
    CHECK(!w.longestReserve());
    w.t.length = 13;
    CHECK(w.longestReserve());                        // longer than the fact's
    w.t.length = 12;
    CHECK(w.longestReserve());                        // a tie: the lower id (6 < 8)
    w.know.longest.id = 4;
    CHECK(!w.longestReserve());                       // ... 4 < 6
    w.know.longest = {w.board.id(10, 10), 12, 6, 349};
    CHECK(w.longestReserve());                        // the fact names me
    w.know.longest = {w.board.id(3, 3), 30, 8, 350 - kReserveAge - 1};
    w.t.length = 9;
    CHECK(w.longestReserve());                        // stale: the length rule
    w.t.round = kReserveFrom - 1;
    w.know.longest = {-1, 0, -1, -1};
    CHECK(!w.longestReserve());                       // before kReserveFrom, though 9 long and no fact
    w.t.round = kReserveFrom;
    CHECK(w.longestReserve());                        // ... from it
    World q = dragon(0, 20, 400);
    CHECK(!q.longestReserve() || !BC_QUEEN);          // never the queen
    World q1 = dragon(1, 20, 400);
    CHECK(!q1.longestReserve() || !BC_QUEEN);
}

static void testNoSplit() {
    // an 8-long dragon at r350 on an open board: the swarm split (until swarmUntil) halves it; as the reserve it keeps every segment
    World w = dragon(6, 8, 350);
    Params p;
    p.swarmUntil = 400;
    p.lateNoSplit = 400;
    p.feedRound = 400;
    p.esUntil = 300;
    Policy pol(w, p, 1e18);
    pol.L_ = 8;
    pol.base_.assign(w.board.NC, 0);
    Choice best;
    best.why = "forage";
    best.dest = w.board.id(11, 10);
    Choice out;
    CHECK(pol.trySplit(out, best) && out.split && out.n == 4);
    pol.setReserve(true);
    Choice out2;
    CHECK(!pol.trySplit(out2, best) && !out2.split);
}

static void testEarlyFeed() {
    Params p;
    p.feedRound = 400;
    bool changed = reserveEarlyFeed(p);
    if (BC_LONGEST_RESERVE == 2) CHECK(changed && p.feedRound == kReserveFrom);
    else CHECK(!changed && p.feedRound == 400);  // modes 1 and 3: no early feeding phase
    p.feedRound = 250;  // already earlier: kept
    CHECK(!reserveEarlyFeed(p) && p.feedRound == 250);
}

int main() {
    testEarlyFeed();
    testReserve();
    testNoSplit();
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
