// Queen yield (BC_QUEEN_YIELD, inactive in the final build): a teammate next to a hemmed-in queen steps away first (local,
// no comms).
//   c++ -std=c++20 -O2 -DBC_QUEEN_YIELD=1 -I bots/abyss tests/native/queen_yield_test.cpp -o queen_yield_test && ./queen_yield_test
// Policy::planYield (who yields) and the cost of its landings in scoreFrom, on a synthetic 20 x 20 open board: our queen
// (id 0) at (10, 10) with her body west, this dragon (id 6, 3 long) near her.
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

#if BC_QUEEN_YIELD
static int at(int x, int y) { return y * 20 + x; }

struct Scene {
    std::vector<int> me = {at(11, 10), at(12, 10), at(13, 10)};  // head first
    bool queen = true, queenMe = false;
    std::vector<int> blocks;  // other bodies (a teammate's) on her sides
};

static World world(Scene const& sc) {
    World w;
    Init in;
    in.id = sc.queenMe ? 0 : 6;
    in.team = 'A';
    in.w = in.h = 20;
    w.start(in);
    w.t.round = 200;
    w.t.length = 3;
    w.t.units = 8;
    w.head = sc.me[0];
    w.body = sc.me;
    w.occ.assign(w.board.NC, -1);
    w.occHeadId.assign(w.board.NC, -1);
    for (int c = 0; c < w.board.NC; c++) w.seenRound[c] = w.visRound[c] = 200;
    for (int c : w.body) w.occ[c] = 6;
    if (sc.queen) {
        w.others.push_back({0, 'A', at(10, 10), 0, 3});
        w.occ[at(10, 10)] = w.occ[at(9, 10)] = w.occ[at(8, 10)] = 0;
        w.occHeadId[at(10, 10)] = 0;
    }
    for (int c : sc.blocks) w.occ[c] = 9;
    return w;
}

static int yields(Scene const& sc) {
    World w = world(sc);
    Params p;
    Policy pol(w, p, 1e18);
    pol.L_ = 3;
    pol.queenMe_ = sc.queenMe;
    pol.yieldQh_ = 77;  // a stale one: always reset
    pol.planYield();
    return pol.yieldQh_;
}

static void testWho() {
    Scene sc;  // me at (11, 10), her east side: her free sides N and S (2)
    CHECK(yields(sc) == at(10, 10));
    Scene diag;  // me at (11, 11): diagonal, Chebyshev 1; her sides N, E, S free (3): no
    diag.me = {at(11, 11), at(12, 11), at(13, 11)};
    CHECK(yields(diag) == -1);
    diag.blocks = {at(10, 9)};  // ... her north side held by another body: 2 free - yield
    CHECK(yields(diag) == at(10, 10));
    Scene far;  // me at (12, 10): Chebyshev 2
    far.me = {at(12, 10), at(13, 10), at(14, 10)};
    far.blocks = {at(10, 9), at(10, 11)};  // (her N and S held: 1 free side - only the distance says no)
    CHECK(yields(far) == -1);
    Scene none;
    none.queen = false;
    CHECK(yields(none) == -1);  // no queen in view
    Scene q;
    q.queenMe = true;
    CHECK(yields(q) == -1);     // the queen herself
    {   // their queen (id 1, team B) in her place: not ours to yield to
        World w = world(sc);
        w.others[0] = {1, 'B', at(10, 10), 0, 3};
        Params p;
        Policy pol(w, p, 1e18);
        pol.L_ = 3;
        pol.queenMe_ = false;
        pol.planYield();
        CHECK(pol.yieldQh_ == -1);
    }
    Scene boxed;  // N and S held too: 0 free
    boxed.blocks = {at(10, 9), at(10, 11)};
    CHECK(yields(boxed) == at(10, 10));
}

// scoreFrom's step term of each first step from (11, 10): N to (11, 9) (Chebyshev 1 of her), E... is my body; S to (11, 11) (1)
static double term(Scene const& sc, int dir, bool yieldOn) {
    World w = world(sc);
    Params p;
    Policy pol(w, p, 1e18);
    pol.L_ = 3;
    pol.queenMe_ = false;
    pol.buildBase();
    pol.buildTargets();
    pol.myDist_.assign(w.board.NC, 0);
    pol.planYield();
    if (!yieldOn) pol.yieldQh_ = -1;
    std::vector<int> none;
    return pol.scoreFrom(w.body, 3, dir, 0, none).stepTerm;
}

static void testCost() {
    Scene sc;
    // N (11, 9): Chebyshev 1 of her -> kYieldW
    CHECK(std::abs(term(sc, N, false) - term(sc, N, true) - kYieldW) < 1e-9);
    // from (12, 10) heading off: a dragon at (12, 10)... (not yielding: Chebyshev 2) - build one that yields with a landing at 2:
    Scene d;  // me at (11, 11) with her north side blocked (yields); E to (12, 11): Chebyshev 2 -> half; S to (11, 12): 2 -> half
    d.me = {at(11, 11), at(11, 12), at(11, 13)};
    d.blocks = {at(10, 9)};
    CHECK(std::abs(term(d, E, false) - term(d, E, true) - kYieldW / 2) < 1e-9);
    CHECK(std::abs(term(d, N, false) - term(d, N, true) - kYieldW) < 1e-9);  // N to (11, 10): her side, Chebyshev 1
    Scene g;  // me at (11, 9) (Chebyshev 1, her N side held by another, E side free...) heading N to (11, 8): Chebyshev 2
    g.me = {at(11, 9), at(12, 9), at(13, 9)};
    g.blocks = {at(10, 9), at(10, 11)};
    CHECK(std::abs(term(g, N, false) - term(g, N, true) - kYieldW / 2) < 1e-9);
}
#endif

int main() {
#if BC_QUEEN_YIELD
    testWho();
    testCost();
#endif
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
