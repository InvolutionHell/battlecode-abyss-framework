// Fast-bed field (BC_T64_FIELD, inactive in the final build): a non-queen within BC_T64_R steps of a known-fast bed plays
// the move-prior net (r4net.hpp) at weight BC_T64_W instead of BC_R4_W. This tests the field test, Policy::t64Field(): the
// bed must be known (bed == 1) with a gap estimate 0 < gapMax <= BC_T64_GAP, within BC_T64_R steps ALONG THE WALKABLE GRAPH
// (a wall in between counts, not the straight line), and with BC_T64_FASTEST of the fastest class the board knows
// (gap <= BC_T64_TOL x the smallest known gap).
//   c++ -std=c++20 -O2 -DBC_T64_FIELD=1 -I bots/abyss tests/native/fast_bed_field_test.cpp -o fast_bed_field_test && ./fast_bed_field_test
//   (also -DBC_T64_FASTEST=0 and -DBC_T64_R=6; the fixtures assume R >= 3, GAP 20, TOL 1.5; without BC_T64_FIELD the test
//   only checks that the switch is 0, as in the final build)
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

#if BC_T64_FIELD
static int at(int x, int y) { return y * 20 + x; }

struct Scene {
    int hx = 3, hy = 10;
    std::vector<std::array<int, 3>> beds;   // x, y, gapMax (bed == 1)
    std::vector<int> wallsV;                // a kelp wall on the left edge of tile (x, y): x * 100 + y
};

static Policy* make(Scene const& sc) {
    static World w;
    static Params keep;
    w = World();
    Init in;
    in.id = 2;
    in.team = 'A';
    in.w = in.h = 20;
    w.start(in);
    w.t.round = 60;
    w.t.length = 4;
    w.t.units = 6;
    w.head = at(sc.hx, sc.hy);
    w.body = {w.head, at(sc.hx - 1, sc.hy)};
    for (int c = 0; c < w.board.NC; c++) w.board.bed[c] = 0, w.board.gapMax[c] = 0, w.board.gapMin[c] = 0;
    for (auto const& b : sc.beds) w.board.bed[at(b[0], b[1])] = 1, w.board.gapMax[at(b[0], b[1])] = w.board.gapMin[at(b[0], b[1])] = static_cast<int16_t>(b[2]);
    for (int v : sc.wallsV) w.board.vE[at(v / 100, v % 100)].kind = 1;
    w.board.rebuild();
    static Policy* pol = nullptr;
    delete pol;
    keep = Params();
    pol = new Policy(w, keep, 1e18);
    return pol;
}

static void testDistance() {
    for (int dx = 0; dx <= BC_T64_R + 2; dx++) {   // a fountain dx steps east of the head on an open board
        Scene s;
        s.beds = {{3 + dx, 10, 1}};
        CHECK(make(s)->t64Field() == (dx <= BC_T64_R));
    }
    Scene diag;                                     // the walk is Manhattan: a bed at (+2, +2) is 4 steps away
    diag.beds = {{5, 12, 1}};
    CHECK(make(diag)->t64Field() == (BC_T64_R >= 4));
    Scene here;                                     // standing on it
    here.beds = {{3, 10, 1}};
    CHECK(make(here)->t64Field());
    Scene wrap;                                     // the board is a torus: a bed 2 steps west of tile 0
    wrap.hx = 0;
    wrap.beds = {{18, 10, 1}};
    CHECK(make(wrap)->t64Field());
}

static void testWalls() {
    Scene s;
    s.beds = {{5, 10, 1}};                          // straight line 2 steps; a long wall on the left edge of x = 4 from row 2 to 18 forces a detour of more than 3
    for (int y = 2; y <= 18; y++) s.wallsV.push_back(4 * 100 + y);
    CHECK(!make(s)->t64Field());
    Scene gap = s;                                  // a door in the wall at row 10: the walk is 2 steps again
    gap.wallsV.erase(std::remove(gap.wallsV.begin(), gap.wallsV.end(), 4 * 100 + 10), gap.wallsV.end());
    CHECK(make(gap)->t64Field());
}

static void testKnown() {
    Scene unknown;                                  // a bed seen but with no gap estimate (gapMax 0): not known-fast
    unknown.beds = {{5, 10, 0}};
    CHECK(!make(unknown)->t64Field());
    Scene slow;                                     // above BC_T64_GAP
    slow.beds = {{5, 10, BC_T64_GAP + 1}};
    CHECK(!make(slow)->t64Field());
    Scene edge;                                     // exactly BC_T64_GAP: known-fast (the fastest class if it is the only one known)
    edge.beds = {{5, 10, BC_T64_GAP}};
    CHECK(make(edge)->t64Field());
    Scene notBed;                                   // a gap estimate on a tile that is not a bed
    Policy* p = make(notBed);
    p->w_.board.gapMax[at(5, 10)] = 1;
    CHECK(!p->t64Field());
}

static void testFastest() {
    Scene s;                                        // a gap-12 bed 2 steps away, a fountain (gap 1) known far away
    s.beds = {{5, 10, 12}, {15, 15, 1}};
    CHECK(make(s)->t64Field() == !BC_T64_FASTEST);
    Scene only;                                     // the gap-12 bed is the fastest this dragon knows
    only.beds = {{5, 10, 12}, {15, 15, 14}};
    CHECK(make(only)->t64Field());
    Scene tol;                                      // within the tolerance: gap 1 near, gap 1 far -> on; a gap-2 bed near with a gap-1 fountain known: 2 > 1.5 x 1 -> off with FASTEST
    tol.beds = {{5, 10, 2}, {15, 15, 1}};
    CHECK(make(tol)->t64Field() == (!BC_T64_FASTEST || 2 <= 1 * BC_T64_TOL));
    Scene near2;
    near2.beds = {{5, 10, 1}, {15, 15, 1}};
    CHECK(make(near2)->t64Field());
    Scene edgeTol;                                  // exactly at the tolerance: gap 3 near, gap 2 far known: 3 <= 2 x 1.5
    edgeTol.beds = {{5, 10, 3}, {15, 15, 2}};
    CHECK(make(edgeTol)->t64Field() == (!BC_T64_FASTEST || 3 <= 2 * BC_T64_TOL));
    Scene twoNear;                                  // two beds in the walk, gaps 1 and 2, a gap-1 fountain far away: the nearest class is gap 1
    twoNear.beds = {{4, 10, 1}, {5, 10, 2}, {15, 15, 1}};
    CHECK(make(twoNear)->t64Field());
    Scene twoNearBig;                               // two beds in the walk, gaps 5 and 1: the smaller counts
    twoNearBig.beds = {{4, 10, 5}, {5, 10, 1}, {15, 15, 1}};
    CHECK(make(twoNearBig)->t64Field());
    Scene unknownFar;                               // a far bed with no gap estimate must not count as the fastest (gap 0)
    unknownFar.beds = {{5, 10, 5}, {15, 15, 0}};
    CHECK(make(unknownFar)->t64Field());
    Scene none;                                     // no known-fast bed anywhere
    CHECK(!make(none)->t64Field());
}
#endif

int main() {
#if BC_T64_FIELD
    testDistance();
    testWalls();
    testKnown();
    testFastest();
#else
    CHECK(BC_T64_FIELD == 0);
#endif
    std::printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
