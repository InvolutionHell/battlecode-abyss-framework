// Queen grows first (BC_QUEEN_GROWFIRST, 1 in the final build): before r200, while the queen is shorter than 5, she eats
// first (a higher eat weight, QSK food preferences).
//   c++ -std=c++20 -O2 -I bots/abyss tests/native/queen_growfirst_test.cpp -o queen_growfirst_test && ./queen_growfirst_test
// Policy::queenGrows (who / when), her eat weight in scoreFrom, and the QSK food preferences of queen_feed350_test.cpp that
// GROWFIRST compiles in alone (a landing that eats first, then the one nearest food within 6 steps, food out of view
// counted), on a synthetic 20 x 20 open board: our queen (id 0, 3 long) at (10, 10), her body west; her landings N (10, 9),
// E (11, 10), S (10, 11); the search's values N 10 > E 5 > S 0.
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

#if BC_QUEEN_GROWFIRST
static World queen(int round, int pearlY = -1) {
    World w;
    Init in;
    in.id = 0;
    in.team = 'A';
    in.w = in.h = 20;
    w.start(in);
    w.t.round = round;
    w.t.length = 3;
    w.t.units = 6;
    w.head = at(10, 10);
    w.body = {at(10, 10), at(9, 10), at(8, 10)};
    w.occ.assign(w.board.NC, -1);
    w.occHeadId.assign(w.board.NC, -1);
    for (int c : w.body) w.occ[c] = 0;
    for (int c = 0; c < w.board.NC; c++) w.seenRound[c] = w.visRound[c] = round;
    if (pearlY >= 0) w.seenPearl[at(10, pearlY)] = 1;
    return w;
}

static void testWho() {
    World w = queen(kGrowFirstUntil - 1);
    Params p;
    Policy pol(w, p, 1e18);
    pol.queenMe_ = true;
    pol.L_ = kGrowFirstLen - 1;
    CHECK(pol.queenGrows());
    pol.L_ = kGrowFirstLen;
    CHECK(!pol.queenGrows());                     // long enough
    pol.L_ = 3;
    pol.queenMe_ = false;
    CHECK(!pol.queenGrows());                     // not the queen
    World late = queen(kGrowFirstUntil);
    Policy pl(late, p, 1e18);
    pl.queenMe_ = true;
    pl.L_ = 3;
    CHECK(!pl.queenGrows());                      // r200 on: done
}

// her eat weight: scoreFrom's step term on a pearl at S, r199 against r200
static double eatTerm(int round) {
    World w = queen(round, 11);
    Params p;
    Policy pol(w, p, 1e18);
    pol.queenMe_ = true;
    pol.L_ = 3;
    pol.buildBase();
    pol.buildTargets();
    pol.myDist_.assign(w.board.NC, 0);
    std::vector<int> none;
    Choice c = pol.scoreFrom(w.body, 3, S, 0, none);
    return c.eat ? c.stepTerm : -1.0;
}

static void testEat() {
    double const before = eatTerm(kGrowFirstUntil - 1), after = eatTerm(kGrowFirstUntil);
    CHECK(after > 0.0 && std::abs(before - kGrowFirstEat * after) < 1e-9);
}

static int pick(World& w, bool feed) {
    Qsk q(w, 3);
    q.feed350 = feed;
    double tot[4] = {0, 0, 0, 0};
    tot[N] = 10, tot[E] = 5, tot[S] = 0;
    bool legal[4] = {true, true, true, false};
    QskOut o = q.run(tot, legal, 1e18);
    return o.ok ? o.dest : -2;
}

static void testQsk() {
    World eat = queen(150, 11);                   // a pearl at S
    CHECK(pick(eat, false) == at(10, 9) && pick(eat, true) == at(10, 11));   // the eating landing first
    World pull = queen(150, 13);                  // a pearl 2 beyond S
    CHECK(pick(pull, false) == at(10, 9) && pick(pull, true) == at(10, 11)); // the pull toward it
    World hot = queen(150);                       // food out of view: a teammate's HOT fact at (10, 13)
    hot.visRound[at(10, 13)] = 149;
    hot.know.hot = {{at(10, 13), 8, at(10, 13), 148}};
    CHECK(pick(hot, true) == at(10, 11));
}
#endif

int main() {
#if BC_QUEEN_GROWFIRST
    testWho();
    testEat();
    testQsk();
#endif
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
