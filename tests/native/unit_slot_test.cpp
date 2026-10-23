// Unit slot and forage path (BC_UNIT_SLOT and BC_FORAGE_PATH, both 1 in the final build).
//   c++ -std=c++20 -O2 -I bots/abyss tests/native/unit_slot_test.cpp -o unit_slot_test && ./unit_slot_test
// (a) BC_UNIT_SLOT: a non-queen never takes the last free unit slot (it is the queen's) - Policy::unitCap and
// Policy::trySplit at the queen's slot; (b) BC_FORAGE_PATH: Policy::buildTargets' remote-yield distance of a teammate heard
// out of sight is path steps over the known map, not torus Manhattan - on a synthetic 20 x 20 board.
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

static World dragon(int id, int len, int round, int units) {
    World w;
    Init in;
    in.id = id;
    in.team = 'A';
    in.w = in.h = 20;
    in.unitLimit = 64;
    w.start(in);
    w.t.round = round;
    w.t.length = len;
    w.t.units = units;
    w.head = w.board.id(10, 10);
    for (int i = 0; i < len; i++) w.body.push_back(w.board.id(10 - i, 10));
    w.occ.assign(w.board.NC, -1);
    w.occHeadId.assign(w.board.NC, -1);
    for (int c = 0; c < w.board.NC; c++) w.seenRound[c] = round;
    return w;
}

#if BC_UNIT_SLOT
// an 8-long dragon at r200 on an open board: the swarm split halves it while there is room
static bool splits(int id, int units) {
    World w = dragon(id, 8, 200, units);
    Params p;
    p.swarmUntil = 400;
    p.lateNoSplit = 400;
    p.feedRound = 400;
    p.esUntil = 300;
    Policy pol(w, p, 1e18);
    pol.L_ = 8;
    pol.queenMe_ = BC_QUEEN && id < 2;
    pol.base_.assign(w.board.NC, 0);
    Choice best;
    best.why = "forage";
    best.dest = w.board.id(11, 10);
    Choice out;
    return pol.trySplit(out, best) && out.split;
}

// a 5-long dragon with its head at the end of a dead-end lane (x 8..12 at y 10, walled north, south and east): the dip's reverse
static bool dipSplits(int units) {
    World w = dragon(6, 5, 200, units);
    Board& b = w.board;
    w.head = b.id(12, 10);
    w.body = {b.id(12, 10), b.id(11, 10), b.id(10, 10), b.id(9, 10), b.id(8, 10)};
    for (int x = 8; x <= 12; x++) {
        Edge e;
        e.kind = 1;
        b.hE[b.id(x, 10)] = e, b.hE[b.id(x, 11)] = e;
    }
    Edge e;
    e.kind = 1;
    b.vE[b.id(13, 10)] = e;
    b.rebuild();
    Params p;
    Policy pol(w, p, 1e18);
    pol.L_ = 5;
    pol.base_.assign(b.NC, 0);
    pol.buildTargets();  // belief_ / countdown_ (the lane's food)
    Choice out;
    return pol.dipDone(out) && out.split;
}

static void testSlot() {
    World w = dragon(6, 8, 200, 10);
    Params p;
    Policy pol(w, p, 1e18);
    pol.queenMe_ = false;
    CHECK(pol.unitCap(64) == 63 && pol.unitCap(60) == 60);
    pol.queenMe_ = BC_QUEEN;
    CHECK(pol.unitCap(64) == (BC_QUEEN ? 64 : 63));
    CHECK(splits(6, 61) && !splits(6, 62));               // the economic split keeps its own cap (the king's 2 slots)
    CHECK(dipSplits(62));                                 // the dip's reverse at 62: room for one more and the queen's slot
    CHECK(!dipSplits(63));                                // 63: the last slot is the queen's (without the switch it splits here)
}
#endif

#if BC_FORAGE_PATH
static Edge kelp() {
    Edge e;
    e.kind = 1;
    return e;
}

// buildTargets with a pearl seen at (10, 14) and a teammate heard at `fx, fy` (out of sight); the remote distance to the pearl
static int remote(int fx, int fy, bool wall, int age = 0, bool fog = false) {
    World w = dragon(6, 3, 100, 6);
    Board& b = w.board;
    int const pearl = b.id(10, 14);
    w.seenPearl[pearl] = 1;
    w.visRound[pearl] = 100;
    if (wall)  // row y = 13's north sides walled (and y = 16's: the torus' way round): the pearl's band is rows 13..15
        for (int x = 0; x < 20; x++) b.hE[b.id(x, 13)] = kelp(), b.hE[b.id(x, 16)] = kelp();
    b.rebuild();
    int const f = b.id(fx, fy);
    w.know.friends.push_back({f, 3, 20, 100 - age});
    for (int c = 0; c < b.NC; c++) w.visRound[c] = c == pearl ? 100 : -1;  // the teammate out of sight
    if (fog)
        for (int y = 0; y < 20; y++) w.seenRound[b.id(12, y)] = -1;  // a never-seen column between
    Params p;
    p.remoteMaxAge = 20;
    Policy pol(w, p, 1e18);
    pol.L_ = 3;
    pol.buildTargets();
    return pol.remoteFriendDist_[pearl];
}

static void testPath() {
    CHECK(remote(10, 11, false) == 3);                    // 3 steps straight down, nothing between
    CHECK(remote(10, 11, false, 2) == 5);                 // ... + the fact's age
    CHECK(remote(10, 12, true) >= INF);                   // 2 by Manhattan, a wall between: no yield (Manhattan: 2)
    CHECK(remote(13, 14, false) == 3);                    // in the band, 3 east
    CHECK(remote(14, 14, false, 0, true) >= INF);         // 4 east, a never-seen column between: not over unknown tiles
    CHECK(remote(10, 3, false) == 9);                     // 9 steps round the torus: within kForagePathCap
    CHECK(remote(0, 4, false) >= INF);                    // 20 steps: beyond the cap, no yield (Manhattan: 20, a yield at slack)
    CHECK(remote(0, 12, false) == 12);                    // 10 + 2: at the cap
    CHECK(remote(0, 11, false) >= INF);                   // 10 + 3: one past it
}
#endif

int main() {
#if BC_UNIT_SLOT
    testSlot();
#endif
#if BC_FORAGE_PATH
    testPath();
#endif
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
