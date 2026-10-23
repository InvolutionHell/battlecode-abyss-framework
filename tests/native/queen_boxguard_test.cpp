// Queen box guard (BC_QUEEN_BOXGUARD, 1 in the final build): nobody steps into our queen's last way out.
//   c++ -std=c++20 -O2 -I bots/abyss tests/native/queen_boxguard_test.cpp -o queen_boxguard_test && ./queen_boxguard_test
//   (also -DBC_ECON_TRACE=1: the sacrifice tag)
// Bit 1: Policy::planBoxGuard and the cost of the step onto her last free side. Bit 2 (not in the final build; ablation.hpp
// sets the switch unconditionally): QSK's tier-3 filter of one-exit landings whose exit has a teammate's head next to it.
// On a synthetic 20 x 20 board, our queen (id 0) at (10, 10) with her body west.
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

static int at(int x, int y) { return y * 20 + x; }

static Edge kelp() {
    Edge e;
    e.kind = 1;
    return e;
}

// the base world: dragon `id` (3 long) with head `me` trailing east; our queen at (10, 10), body (9, 10), (8, 10); `blocks`: other bodies
static World world(int id, std::vector<int> me, std::vector<int> const& blocks, int queenTeam = 'A') {
    World w;
    Init in;
    in.id = id;
    in.team = 'A';
    in.w = in.h = 20;
    w.start(in);
    w.t.round = 200;
    w.t.length = static_cast<int>(me.size());
    w.t.units = 8;
    w.head = me[0];
    w.body = me;
    w.occ.assign(w.board.NC, -1);
    w.occHeadId.assign(w.board.NC, -1);
    for (int c = 0; c < w.board.NC; c++) w.seenRound[c] = w.visRound[c] = 200;
    for (int c : me) w.occ[c] = id;
    if (id != 0) {
        w.others.push_back({queenTeam == 'A' ? 0 : 1, static_cast<char>(queenTeam), at(10, 10), 0, 3});
        w.occ[at(10, 10)] = w.occ[at(9, 10)] = w.occ[at(8, 10)] = 0;
        w.occHeadId[at(10, 10)] = 0;
    }
    for (int c : blocks) w.occ[c] = 9;
    return w;
}

#if BC_QUEEN_BOXGUARD & 1
static int exitOf(World& w, bool queenMe = false) {
    Params p;
    Policy pol(w, p, 1e18);
    pol.L_ = 3;
    pol.queenMe_ = queenMe;
    pol.boxExit_ = 77;  // a stale one: always reset
    pol.planBoxGuard();
    return pol.boxExit_;
}

// decide() for a dragon (id 6, head (12, 10), body east) trapped but for (11, 10): its N and S held, E its own neck
static Choice trapped(int queenFree, bool guardOn) {
    std::vector<int> const me = {at(12, 10), at(13, 10), at(14, 10)};
    std::vector<int> blocks = {at(12, 9), at(12, 11)};
    if (queenFree == 1) blocks.push_back(at(10, 9)), blocks.push_back(at(10, 11));  // her N and S held: (11, 10) her last side
    World w = world(6, me, blocks);
    w.t.dir = W;
    Params p;
    Policy pol(w, p, 1e18);
    Choice c = pol.decide();
    (void)guardOn;
    return c;
}

static void testStepGuard() {
    std::vector<int> const me = {at(12, 10), at(13, 10), at(14, 10)};
    World one = world(6, me, {at(10, 9), at(10, 11)});        // her N and S held: E (11, 10) her last free side
    CHECK(exitOf(one) == at(11, 10));
    Choice c = trapped(1, true);                               // trapped, her last side my only free tile: never it (we die alone)
    CHECK(c.dest != at(11, 10) && c.why != "shared-exit");
    Choice d = trapped(3, true);                               // she has more: (11, 10) is just a free tile, ours to take
    CHECK(d.dest == at(11, 10));
    {   // her last exit on the trapped dragon's N side (the fallback's first direction): a dragon at (11, 11), body south, its E and W held;
        // our queen at (10, 10) with N and W held (her body west), S (10, 11) and E (11, 10)... E is her last free side - (11, 10) north of it
        std::vector<int> const up = {at(11, 11), at(11, 12), at(11, 13)};
        World w = world(6, up, {at(10, 9), at(12, 11), at(10, 11)});
        w.t.dir = N;
        Params p;
        Policy pol(w, p, 1e18);
        Choice c = pol.decide();
        CHECK(pol.boxExit_ == at(11, 10));
        CHECK(c.dest != at(11, 10));                           // never N into her exit, though N is the fallback's first pick
    }
#if BC_ECON_TRACE
    {   // the trace's sacrifice count: "qbS" on the turn the fallback gave up her last exit
        std::vector<int> const mine = {at(12, 10), at(13, 10), at(14, 10)};
        World w = world(6, mine, {at(12, 9), at(12, 11), at(10, 9), at(10, 11)});
        w.t.dir = W;
        Params p;
        Policy pol(w, p, 1e18);
        pol.decide();
        CHECK(pol.econTrace_.find(" qbS") != std::string::npos);
        World f = world(6, mine, {at(12, 9), at(12, 11)});      // she has 3 free sides: no sacrifice
        f.t.dir = W;
        Policy pf(f, p, 1e18);
        pf.decide();
        CHECK(pf.econTrace_.find(" qbS") == std::string::npos);
    }
#endif
    World two = world(6, me, {at(10, 9)});                     // two free sides: no
    CHECK(exitOf(two) == -1);
    World zero = world(6, me, {at(10, 9), at(10, 11), at(11, 10)});  // none left: nothing to keep
    CHECK(exitOf(zero) == -1);
    World q = world(0, {at(10, 10), at(9, 10), at(8, 10)}, {at(10, 9), at(10, 11)});
    CHECK(exitOf(q, true) == -1);                              // the queen herself
    World enemyQ = world(6, me, {at(10, 9), at(10, 11)}, 'B'); // their queen: not ours to guard
    CHECK(exitOf(enemyQ) == -1);
}
#endif

#if BC_QUEEN_BOXGUARD & 2
// the queen's own world: her 1-step landings N (10, 9) and S (10, 11) are corridor cells (walled east and west), exits (10, 8) / (10, 12);
// E (11, 10) is held by another body. Both landings have one exit: QSK's tier 3. `headAt`: teammate heads (team A, id 5 / 7) in view.
static World corridor(std::vector<int> const& headAt) {
    World w = world(0, {at(10, 10), at(9, 10), at(8, 10)}, {at(11, 10)});
    Board& b = w.board;
    for (int y : {9, 11}) b.vE[at(10, y)] = kelp(), b.vE[at(11, y)] = kelp();
    b.rebuild();
    int id = 5;
    for (int h : headAt) {
        w.occ[h] = id;
        w.occHeadId[h] = id;
        w.t.parts.push_back({'A', id, b.X(h), b.Y(h), N, true});
        id += 2;
    }
    return w;
}

static int qskPick(World& w, int* tier = nullptr) {
    Qsk q(w, 3);
    double tot[4] = {0, 0, 0, 0};
    tot[N] = 5, tot[S] = 10;  // the search's values prefer S
    bool legal[4] = {true, false, true, false};
    QskOut o = q.run(tot, legal, 1e18);
    if (tier) *tier = o.tier;
    return o.ok ? o.dest : -2;
}

static void testQskExitFilter() {
    int t = 0;
    World plain = corridor({});
    CHECK(qskPick(plain, &t) == at(10, 11) && t == 3);         // both one-exit, tier 3: the values' S
    World risky = corridor({at(11, 12)});                      // a teammate head next to S's exit (10, 12)
    CHECK(qskPick(risky, &t) == at(10, 9) && t == 3);          // S dropped: N
    World both = corridor({at(11, 12), at(11, 8)});            // ... and next to N's exit (10, 8): no other tier-3 landing left
    CHECK(qskPick(both, &t) == at(10, 11) && t == 3);          // nothing dropped: the values' S
    World far = corridor({at(12, 12)});                        // a head two away from the exit: not next to it
    CHECK(qskPick(far, &t) == at(10, 11));
}
#endif

int main() {
#if BC_QUEEN_BOXGUARD & 1
    testStepGuard();
#endif
#if BC_QUEEN_BOXGUARD & 2
    testQskExitFilter();
#endif
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
