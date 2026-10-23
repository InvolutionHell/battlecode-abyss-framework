// Reserve feeders (BC_LONGEST_RESERVE 3, the final build's mode): the longest reserve of longest_reserve_test.cpp plus
// its feeders - the two nearest teammates leave it the food.
//   c++ -std=c++20 -O2 -I bots/abyss tests/native/reserve_feed_test.cpp -o reserve_feed_test && ./reserve_feed_test
// Policy::planReserveFeed (who feeds), its eat weight (a pearl within kFeedYield of the reserve's head is worth nothing to
// a feeder), and mode 3 without mode 2's early feeding phase, on a synthetic 20 x 20 open board.
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

#if BC_LONGEST_RESERVE >= 3
// dragon `id` (3 long) at (10, 10) facing east, round `round`; the reserve (id 8, 15 long) known by a fresh LONGEST fact at (14, 10)
static World dragon(int id, int round, int reserveAt = 14 + 20 * 10) {
    World w;
    Init in;
    in.id = id;
    in.team = 'A';
    in.w = in.h = 20;
    w.start(in);
    w.t.round = round;
    w.t.length = 3;
    w.t.units = 10;
    w.head = w.board.id(10, 10);
    for (int i = 0; i < 3; i++) w.body.push_back(w.board.id(10 - i, 10));
    w.occ.assign(w.board.NC, -1);
    w.occHeadId.assign(w.board.NC, -1);
    for (int c = 0; c < w.board.NC; c++) w.seenRound[c] = round;
    w.know.longest = {reserveAt, 15, 8, round - 1};
    return w;
}

// planReserveFeed with an open-board path field; the head this dragon feeds (-1 none)
static int feeds(World& w, std::vector<World::Seen> others = {}, bool reserve = false) {
    w.others = others;
    Params p;
    Policy pol(w, p, 1e18);
    pol.L_ = 3;
    pol.queenMe_ = BC_QUEEN && w.init.id < 2;
    pol.reserveMe_ = reserve;
    Board const& b = w.board;
    pol.myDist_.assign(b.NC, INF);
    for (int c = 0; c < b.NC; c++) {
        int dx = std::abs(b.X(c) - 10), dy = std::abs(b.Y(c) - 10);
        pol.myDist_[c] = std::min(dx, 20 - dx) + std::min(dy, 20 - dy);
    }
    pol.planReserveFeed();
    return pol.reserveFeedHead_;
}

static void testWho() {
    int const R = 14 + 20 * 10;
    World w = dragon(6, 350);
    CHECK(feeds(w) == R);                                   // 4 steps from it, nobody nearer: a feeder
    World early = dragon(6, kReserveFrom - 1);
    CHECK(feeds(early) == -1);                              // before kReserveFrom
    World q = dragon(0, 350);
    CHECK(feeds(q) == -1 || !BC_QUEEN);                     // never the queen
    World r = dragon(6, 350);
    CHECK(feeds(r, {}, true) == -1);                        // the reserve itself
    World me = dragon(8, 350);
    CHECK(feeds(me) == -1);                                 // the fact names me
    World stale = dragon(6, 350);
    stale.know.longest.round = 350 - kReserveAge - 1;
    CHECK(feeds(stale) == -1);                              // a stale fact
    World far = dragon(6, 350, 10 + 20 * 0);                // the reserve at (10, 0): 10 steps by the torus, at kFeedNear
    CHECK(feeds(far) == 10);
    World tooFar = dragon(6, 350, 3 + 20 * 4);              // (3, 4): 13 steps
    CHECK(feeds(tooFar) == -1);
    // two of ours nearer to it (seen): not a feeder; one nearer: still one
    World two = dragon(6, 350);
    CHECK(feeds(two, {{20, 'A', two.board.id(13, 10), 0, 3}, {21, 'A', two.board.id(15, 11), 0, 3}}) == -1);
    World one = dragon(6, 350);
    CHECK(feeds(one, {{20, 'A', one.board.id(13, 10), 0, 3}}) == R);
    // ... an enemy nearer does not count; a teammate heard nearer within 10 rounds does
    World en = dragon(6, 350);
    CHECK(feeds(en, {{20, 'B', en.board.id(13, 10), 0, 3}, {21, 'B', en.board.id(15, 11), 0, 3}}) == R);
    World heard = dragon(6, 350);
    heard.know.friends = {{heard.board.id(13, 10), 3, 20, 345}, {heard.board.id(15, 11), 3, 21, 345}};
    CHECK(feeds(heard) == -1);
    heard.know.friends[0].round = 339;
    CHECK(feeds(heard) == R);                               // ... one of them stale
    // the reserve in view: its live head, not the fact's cell
    World seen = dragon(6, 350);
    CHECK(feeds(seen, {{8, 'A', seen.board.id(12, 12), 0, 15}}) == seen.board.id(12, 12));
}

// a feeder leaves the pearls within kFeedYield of the reserve's head: scoreFrom's eat weight
static void testYield() {
    for (int at : {12, 14}) {  // a pearl at (11, 10): 1 step from the reserve at (12, 10) (yielded), 3 at (14, 10) (yielded too); (11, 10)
        World w = dragon(6, 350, at + 20 * 10);
        w.seenPearl[w.board.id(11, 10)] = 1;
        w.visRound[w.board.id(11, 10)] = 350;
        w.others = {};
        Params p;
        Policy pol(w, p, 1e18);
        pol.L_ = 3;
        pol.buildBase();
        pol.buildTargets();
        Board const& b = w.board;
        pol.myDist_.assign(b.NC, 0);
        pol.planReserveFeed();
        CHECK(pol.reserveFeedHead_ >= 0);
        std::vector<int> none;
        Choice c = pol.scoreFrom(w.body, 3, E, 0, none);
        CHECK(c.eat && c.stepTerm <= 0.0);                 // its eat earns nothing
        pol.reserveFeedHead_ = -1;
        Choice d = pol.scoreFrom(w.body, 3, E, 0, none);
        CHECK(d.eat && d.stepTerm > c.stepTerm);           // a non-feeder's does
    }
}

// mode 3 keeps the default feeding round (mode 2's early start starved the army in testing)
static void testNoFeedPhase() {
    Params p;
    p.feedRound = 400;
    CHECK(!reserveEarlyFeed(p) && p.feedRound == 400);
}
#endif

int main() {
#if BC_LONGEST_RESERVE >= 3
    testWho();
    testYield();
    testNoFeedPhase();
#endif
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
