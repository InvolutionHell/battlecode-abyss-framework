// Queen keep-away (BC_QUEEN_KEEPAWAY, 1 in the final build): among the landings QSK keeps, the queen takes the one farther
// by path from the nearest known enemy head.
//   c++ -std=c++20 -O2 -I bots/abyss tests/native/queen_keepaway_test.cpp -o queen_keepaway_test && ./queen_keepaway_test
// Qsk::run on a synthetic 20 x 20 open board: our queen (id 0, 3 long) at (10, 10), her body west; her landings N (10, 9),
// E (11, 10), S (10, 11); the search's values S 10 > N 5 > E 0; an enemy head heard out of her view.
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

#if BC_QUEEN_KEEPAWAY
static World queen(int round = 150) {
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
    return w;
}

static int pick(World& w, int* tier = nullptr) {
    Qsk q(w, 3);
    double tot[4] = {0, 0, 0, 0};
    tot[S] = 10, tot[N] = 5, tot[E] = 0;
    bool legal[4] = {true, true, true, false};
    QskOut o = q.run(tot, legal, 1e18);
    if (tier) *tier = o.tier;
    return o.ok ? o.dest : -2;
}

static void testKeep() {
    int t = 0;
    World none = queen();
    CHECK(pick(none) == at(10, 11));                           // no enemy known: the values' S
    World heard = queen();
    heard.know.enemies = {{at(10, 16), 3, 40, 148}};           // heard 2 rounds ago at (10, 16): S 5 steps from it, N and E 7
    int const p = pick(heard, &t);
    CHECK(p == at(10, 9) && t == 1);                           // away from it: N (ties E on distance, the values break the tie)
    World old = queen();
    old.know.enemies = {{at(10, 16), 3, 40, 150 - kKeepAwayAge - 1}};
    CHECK(pick(old) == at(10, 11));                            // too old a word: the values' S
    World edge = queen();
    edge.know.enemies = {{at(10, 16), 3, 40, 150 - kKeepAwayAge}};
    CHECK(pick(edge) == at(10, 9));                            // at the age limit: still counts
    World far = queen();
    far.know.enemies = {{at(10, 3), 3, 40, 149}};              // north, 7 from her: N 6, S 8, E 8 -> S or E; the values pick S
    CHECK(pick(far) == at(10, 11));
    World capped = queen();
    capped.know.enemies = {{at(0, 15), 3, 40, 149}};           // N 16 steps from it, S and E 14: all past the cap - no preference, the values' S
    CHECK(pick(capped) == at(10, 11));
}
#endif

int main() {
#if BC_QUEEN_KEEPAWAY
    testKeep();
#endif
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
