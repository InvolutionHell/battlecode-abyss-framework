// Contested sprint target (BC_SPRINT_TARGET bit 1, inactive in the final build): the free steps only toward a contested
// fresh pearl.
//   c++ -std=c++20 -O2 -DBC_SPRINT_TARGET=1 -I bots/abyss tests/native/sprint_target_test.cpp -o sprint_target_test && ./sprint_target_test
// Policy::freeSprint(contested) on a synthetic 20 x 20 open board: our dragon (id 6, 8 long: 2 free steps) at (10, 10) with
// its body west, the search's step forage E onto (11, 10), a pearl at (13, 10) seen this round; an enemy 2-long dragon, whole
// in view (a chain cut by the view counts as long - QSK cuts a chain whose last cell is 3+ from our head - and its margin
// check then refuses the landing, as without the switch).
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

#if BC_SPRINT_TARGET & 1
struct Scene {
    int ex = 12, ey = 12, tx = 11, ty = 12;  // the enemy head and its tail (both within 2 of our head: a whole 2-long chain); ex < 0: none
    int tf = E;                              // the tail faces the head (a body segment faces the one before it)
    int pearlRound = 100;    // the round the pearl was seen (100 = this round)
    bool bedSoon = false;    // instead of the pearl: a bed there that refills next round (the plain free sprint counts it as food; this rule only pearls)
    int id = 6;
};

static bool sprint(Scene const& sc, std::string* path = nullptr) {
    World w;
    Init in;
    in.id = sc.id;
    in.team = 'A';
    in.w = in.h = 20;
    w.start(in);
    w.t.round = 100;
    w.t.length = 8;
    w.t.units = 8;
    w.head = at(10, 10);
    for (int i = 0; i < 8; i++) w.body.push_back(at(10 - i, 10));
    w.occ.assign(w.board.NC, -1);
    w.occHeadId.assign(w.board.NC, -1);
    for (int c : w.body) w.occ[c] = sc.id;
    for (int c = 0; c < w.board.NC; c++) w.seenRound[c] = w.visRound[c] = 100;
    int const pearl = at(13, 10);
    if (sc.bedSoon) {
        w.board.bed[pearl] = 1;
        w.next[pearl] = 1;
    } else {
        w.seenPearl[pearl] = 1;
        w.seenRound[pearl] = sc.pearlRound;
    }
    if (sc.ex >= 0) {
        int const h = at(sc.ex, sc.ey), t = at(sc.tx, sc.ty);
        w.occ[h] = w.occ[t] = 40;
        w.occHeadId[h] = 40;
        w.others.push_back({40, 'B', h, W, 2});
        w.t.parts.push_back({'B', 40, sc.ex, sc.ey, W, true});
        w.t.parts.push_back({'B', 40, sc.tx, sc.ty, sc.tf, false});
    }
    Params p;
    Policy pol(w, p, 1e18);
    pol.L_ = 8;
    pol.queenMe_ = BC_QUEEN && sc.id < 2;
    pol.buildBase();
    pol.buildTargets();
    pol.buildFriendFields();
    Choice best;
    best.why = "forage";
    best.dir = E;
    best.dest = at(11, 10);
    Choice out;
    bool const ok = pol.freeSprint(out, best, true);
    if (path) *path = out.sprint;
    return ok;
}

static void testGrab() {
    std::string path;
    Scene sc;  // the enemy head at (12, 12): 3 steps from the pearl, we 3 - contested
    CHECK(sprint(sc, &path) && path == "EE");               // two free steps toward it
    Scene none;
    none.ex = -1;
    CHECK(!sprint(none));                                   // nobody contests it: the search's one step (the plain free sprint would run)
    Scene far;
    far.ex = 8, far.ey = 12, far.tx = 8, far.ty = 11, far.tf = S;  // 5 + 2 = 7 steps from the pearl vs our 3: more than kGrabSlack farther
    CHECK(!sprint(far));
    Scene edge;
    edge.ex = 10, edge.ey = 12, edge.tx = 9, edge.ty = 12;  // 3 + 2 = 5 steps from it vs our 3: at kGrabSlack - contested
    CHECK(sprint(edge));
    Scene past;
    past.ex = 9, past.ey = 12, past.tx = 8, past.ty = 12;  // 4 + 2 = 6 steps from it vs our 3: one past kGrabSlack - not contested
    CHECK(!sprint(past));
    Scene bed;
    bed.bedSoon = true;                                     // a bed refilling next round, contested: not a fresh pearl
    CHECK(!sprint(bed));
    Scene stale;
    stale.pearlRound = 99;                                  // seen last round: not a fresh pearl in view
    CHECK(!sprint(stale));
    Scene queen;
    queen.id = 0;                                           // the queen's multi-steps come from QSK only
    CHECK(!sprint(queen) || !BC_QUEEN || !BC_ECON_B0);
}
#endif

int main() {
#if BC_SPRINT_TARGET & 1
    testGrab();
#endif
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
