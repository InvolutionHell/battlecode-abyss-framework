// Queen grow-up (BC_QUEEN_GROWUP, inactive in the final build): a queen that spent many early turns boxed in asks the team
// to let her grow.
//   c++ -std=c++20 -O2 -DBC_QUEEN_GROWUP=1 -I bots/abyss tests/native/queen_growup_test.cpp -o queen_growup_test && ./queen_growup_test
//   (also -DBC_QUEEN_GROWUP=2, and -DBC_GROWUP_FEED=1 for the feeder)
// World::noteQueenTurn's latch, qGrowAsk's window, her ask on the SELF fact (sent, heard, the length restored),
// Policy::planGrowUp (who leaves her food), growEat, growArm, trySplit's guard and BC_GROWUP_FEED's feeder, on a synthetic
// 20 x 20 open board.
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

#if BC_QUEEN_GROWUP
static constexpr bool kMode2 = BC_QUEEN_GROWUP >= 2;

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

// her turns r0-99 with `tight` of them at `tightSides` free sides (the rest at 2), then her turn at kGrowCheck: the latch
static bool latch(int tight, int tightSides = 1) {
    World w = dragon(0, 3, 0);
    for (int r = 0; r < kGrowCheck; r++) {
        w.t.round = r;
        w.noteQueenTurn(r < tight ? tightSides : 2);
    }
    bool before = w.qGrow;
    w.t.round = kGrowCheck;
    w.noteQueenTurn(4);
    return !before && w.qGrow;
}

static void testLatch() {
    CHECK(latch(10));                 // 10 of 100 turns boxed: kGrowShare
    CHECK(latch(9) == kMode2);        // 9: mode 2 only
    CHECK(latch(0) == kMode2);
    CHECK(latch(10, 0));              // no free side counts as boxed too
    CHECK(latch(100, 2) == kMode2);   // 2 free sides is not boxed
    // decided once, at kGrowCheck: boxed turns after it change nothing
    World w = dragon(0, 3, 0);
    for (int r = 0; r < kGrowCheck; r++) w.t.round = r, w.noteQueenTurn(2);
    for (int r = kGrowCheck; r < kGrowCheck + 50; r++) w.t.round = r, w.noteQueenTurn(0);
    CHECK(w.qGrow == kMode2);
}

static void testAsk() {
    World w = dragon(0, 3, 150);
    CHECK(!w.qGrowAsk());  // not latched
    w.qGrow = true;
    CHECK(w.qGrowAsk());
    w.t.round = kGrowUntil - 1;
    CHECK(w.qGrowAsk());
    w.t.round = kGrowUntil;
    CHECK(!w.qGrowAsk());  // the window ends
    w.t.round = 150;
    w.t.length = kGrowUpTo - 1;
    CHECK(w.qGrowAsk());
    w.t.length = kGrowUpTo;
    CHECK(!w.qGrowAsk());  // grown
}

// the queen (id 0, `len` long, latched or not) sends her facts; a teammate (id 6) hears the SELF one: its round of her ask, if any
static int heard(int len, bool asks, int senderId = 0, int* friendLen = nullptr) {
    Params p;
    World q = dragon(senderId, len, 150);
    q.qGrow = asks;
    std::vector<comms::Fact> facts = q.tell(p);
    World m = dragon(6, 3, 150);
    m.t.msgs.push_back(comms::encode(facts[0], 'A', 150));
    m.hear(p);
    if (friendLen) {
        *friendLen = -1;
        for (auto const& e : m.know.friends)
            if (e.id == senderId) *friendLen = e.len;
    }
    return m.qGrowHeard;
}

static void testComms() {
    Params p;
    World q = dragon(0, 4, 150);
    q.qGrow = true;
    std::vector<comms::Fact> facts = q.tell(p);
    CHECK(facts[0].type == comms::SELF && facts[0].val == 4 + kGrowAskVal && facts[0].aux == 0);  // her ask rides on her length
    q.qGrow = false;
    CHECK(q.tell(p)[0].val == 4);
    int len = 0;
    CHECK(heard(4, true, 0, &len) == 150 && len == 4);  // heard, and the length restored for everything else
    CHECK(heard(4, false, 0, &len) == -1 && len == 4);
    CHECK(heard(kGrowUpTo, true) == -1);                 // grown: no ask
    // a teammate 130 long is no queen: its length is its length
    CHECK(heard(130, false, 6 + 1, &len) == -1 && len == 130);
}

// a teammate (id 6) at round `round` with our queen's head in view at (12, 10), `vis` visible segments; her ask last heard `heardAt`
static int yieldHead(int round, int heardAt, int vis = 4, char queenTeam = 'A', bool queenMe = false) {
    World w = dragon(queenMe ? 0 : 6, 3, round);
    w.qGrowHeard = heardAt;
    int qh = w.board.id(12, 10);
    w.others.push_back({queenTeam == 'A' ? 0 : 1, queenTeam, qh, 0, vis});
    Params p;
    Policy pol(w, p, 1e18);
    pol.queenMe_ = queenMe;
    pol.growHead_ = -1;
    pol.planGrowUp();
    return pol.growHead_;
}

static void testPlanGrowUp() {
    Board b;
    b.init(20, 20);
    int qh = b.id(12, 10);
    CHECK(yieldHead(150, -1) == (kMode2 ? qh : -1));      // never heard: mode 2 only
    CHECK(yieldHead(150, 145) == qh);                     // heard 5 rounds ago
    CHECK(yieldHead(150, 150 - kGrowHeardAge) == qh);     // ... kGrowHeardAge ago
    CHECK(yieldHead(150, 150 - kGrowHeardAge - 1) == (kMode2 ? qh : -1));  // ... one more: stale
    CHECK(yieldHead(kGrowUntil, kGrowUntil) == -1);       // the window ends
    CHECK(yieldHead(kGrowUntil - 1, kGrowUntil - 1) == qh);
    CHECK(yieldHead(kGrowCheck - 1, kGrowCheck - 1) == -1);  // ... and starts at kGrowCheck
    CHECK(yieldHead(150, 145, kGrowUpTo) == -1);          // she looks grown
    CHECK(yieldHead(150, 145, kGrowUpTo - 1) == qh);
    CHECK(yieldHead(150, 145, 4, 'B') == -1);             // their queen
    CHECK(yieldHead(150, 145, 4, 'A', true) == -1);       // the queen herself
    // her own process: growMe_ follows the latch
    World w = dragon(0, 3, 150);
    Params p;
    Policy pol(w, p, 1e18);
    pol.queenMe_ = true;
    pol.planGrowUp();
    CHECK(!pol.growMe_);
    w.qGrow = true;
    pol.planGrowUp();
    CHECK(pol.growMe_);
}

static void testEatAndArm() {
    // a teammate with her head at (12, 10) in its yield: a pearl within kGrowYield of it is hers
    World w = dragon(6, 3, 150);
    Params p;
    Policy pol(w, p, 1e18);
    pol.growHead_ = w.board.id(12, 10);
    CHECK(pol.growEat(w.board.id(12 + kGrowYield, 10), 3, 1.0) == 0.0);
    CHECK(pol.growEat(w.board.id(12 + kGrowYield + 1, 10), 3, 1.0) == 1.0);
    // her: x2.5 under 5 long, x1.5 from 5, while she asks
    World q = dragon(0, 3, 150);
    q.qGrow = true;
    Policy qp(q, p, 1e18);
    qp.queenMe_ = true;
    qp.L_ = 3;
    CHECK(qp.growEat(q.board.id(3, 3), 3, 1.0) == 2.5);
    CHECK(qp.growEat(q.board.id(3, 3), 5, 1.0) == 1.5);
    CHECK(qp.growArm());
    q.t.round = kGrowUntil;
    CHECK(qp.growEat(q.board.id(3, 3), 3, 1.0) == 1.0);  // the window ended
    CHECK(!qp.growArm());
    q.t.round = 150;
    q.qGrow = false;
    CHECK(!qp.growArm());
}

static void testNoSplit() {
    // our queen 8 long at r150 on an open board: the swarm (until swarmUntil) halves her; once she grows she keeps every segment
    World w = dragon(0, 8, 150);
    Params p;
    p.swarmUntil = 400;
    p.lateNoSplit = 400;
    p.feedRound = 400;
    p.esUntil = 300;
    Policy pol(w, p, 1e18);
    pol.L_ = 8;
    pol.queenMe_ = true;
    pol.base_.assign(w.board.NC, 0);
    Choice best;
    best.why = "forage";
    best.dest = w.board.id(11, 10);
    Choice out;
    CHECK(pol.trySplit(out, best) && out.split && out.n == 4);
    w.qGrow = true;
    pol.planGrowUp();
    Choice out2;
    CHECK(!pol.trySplit(out2, best) && !out2.split);
}
#if BC_GROWUP_FEED
// BC_GROWUP_FEED: a teammate (id 6, `len` long, head (10, 10)) with our asking queen's head at `qx`, 10; `units` on the team
static bool feeds(int len = 3, int qx = 12, int units = 20, bool pearl = false, bool enemy = false, bool queenMe = false, bool asks = true) {
    World w = dragon(6, len, 150);
    w.t.units = units;
    Board const& b = w.board;
    int qh = b.id(qx, 10);
    if (pearl) {  // a pearl in view 3 from her head: her drops not eaten yet
        int c = b.id(qx, 13);
        w.visRound[c] = 150;
        w.seenPearl[c] = 1;
    }
    if (enemy) w.others.push_back({9, 'B', b.id(qx + 2, 10), 0, 3});  // 2 from her head
    Params p;
    Policy pol(w, p, 1e18);
    pol.L_ = len;
    pol.queenMe_ = queenMe;
    pol.growHead_ = asks ? qh : -1;
    pol.myDist_.assign(b.NC, INF);
    for (int c = 0; c < b.NC; c++) {
        int dx = std::abs(b.X(c) - 10), dy = std::abs(b.Y(c) - 10);
        pol.myDist_[c] = std::min(dx, 20 - dx) + std::min(dy, 20 - dy);
    }
    return pol.growFeed();
}

static void testFeed() {
    CHECK(feeds());                                    // 1 step from a tile next to her head (feedDist)
    CHECK(!feeds(3, 13));                              // 2 steps: no pull, no feed
    CHECK(feeds(kGrowFeedMaxLen) && !feeds(kGrowFeedMaxLen + 1));
    CHECK(feeds(2) && !feeds(1));
    CHECK(feeds(3, 12, kGrowFeedKeep + 1) && !feeds(3, 12, kGrowFeedKeep));
    CHECK(!feeds(3, 12, 20, true));                    // her drops not eaten yet
    CHECK(!feeds(3, 12, 20, false, true));             // an enemy head within 2 of her
    CHECK(!feeds(3, 12, 20, false, false, true));      // the queen herself
    CHECK(!feeds(3, 12, 20, false, false, false, false));  // she does not ask
}
#endif
#endif

int main() {
#if BC_QUEEN_GROWUP
    testLatch();
    testAsk();
    testComms();
    testPlanGrowUp();
    testEatAndArm();
    testNoSplit();
#if BC_GROWUP_FEED
    testFeed();
#endif
#endif
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
