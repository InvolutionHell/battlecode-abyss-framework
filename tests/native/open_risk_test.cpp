// Opening risk (BC_OPEN_RISK, inactive in the final build): before r100 a non-queen of L <= 2 stops yielding to teammates
// (bit 1), stops waiting at beds (bit 2), may take a teammate's last way out (bit 4, never the queen's) and feels the fast
// beds - it parks at the known bed of the best estimated rate / distance (bit 8).
//   c++ -std=c++20 -O2 -DBC_OPEN_RISK=15 -I bots/abyss tests/native/open_risk_test.cpp -o open_risk_test && ./open_risk_test
//   (also =1 / 2 / 4 / 8, one bit at a time)
// A synthetic 20 x 20 open board: our dragon (id 2 unless set, L2) head (3, 10), body west; the switch is told apart by
// Policy::L_ (2: in the rule's range, 3: not - scoreFrom treats both as small, smallLen 3), by the round (99 / 100) and by
// the queen (id 0).
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

#if BC_OPEN_RISK
static constexpr int kOpenRiskFastGapT = kOpenRiskFastGap;
#endif

static int at(int x, int y) { return y * 20 + x; }

#if BC_OPEN_RISK
struct Scene {
    int id = 2, round = 60, L = 2;
    int mateX = -1, mateY = -1;  // a non-queen teammate in view (id 4)
    int queenX = -1, queenY = -1;  // our queen in view (id 0)
    std::vector<int> walls;      // tiles held by a neutral body (id 77, ours) - to box a teammate in
};

static World* gw = nullptr;

static Policy* make(Scene const& sc, Params p = Params()) {
    static World w;
    static Params keep;
    w = World();
    keep = p;
    Init in;
    in.id = sc.id;
    in.team = 'A';
    in.w = in.h = 20;
    w.start(in);
    w.t.round = sc.round;
    w.t.length = 2;
    w.t.units = 6;
    w.firstRound = w.lastEatRound = 0;
    w.head = at(3, 10);
    w.body = {at(3, 10), at(2, 10)};
    w.occ.assign(w.board.NC, -1);
    w.occHeadId.assign(w.board.NC, -1);
    for (int c : w.body) w.occ[c] = sc.id;
    for (int c = 0; c < w.board.NC; c++) w.seenRound[c] = w.visRound[c] = sc.round, w.board.bed[c] = 0;
    w.ourQueen = 0;
    auto put = [&](int id, int x, int y) {
        w.occ[at(x, y)] = id;
        w.occHeadId[at(x, y)] = id;
        w.others.push_back({id, 'A', at(x, y), N, 1});
    };
    if (sc.mateX >= 0) put(4, sc.mateX, sc.mateY);
    if (sc.queenX >= 0 && sc.id != 0) put(0, sc.queenX, sc.queenY);
    for (int c : sc.walls) w.occ[c] = 77;
    gw = &w;
    static Policy* pol = nullptr;
    delete pol;
    pol = new Policy(w, keep, 1e18);
    pol->L_ = sc.L;
    pol->queenMe_ = BC_QUEEN && sc.id < 2;
    return pol;
}

static void build(Policy* p) {
    p->buildBase();
    p->buildTargets();
    p->buildFriendFields();
    p->planTeam();
}

static double scoreE(Policy* p) {
    std::vector<int> none;
    return p->scoreFrom(gw->body, 2, E, 0, none).score;
}

static void testGate() {
    Scene sc;
    sc.round = 99;
    CHECK(make(sc)->openRisk(BC_OPEN_RISK));         // r99, L2, non-queen: on
    sc.round = 100;
    CHECK(!make(sc)->openRisk(BC_OPEN_RISK));        // r100: off
    Scene l3;
    l3.L = 3;
    CHECK(!make(l3)->openRisk(BC_OPEN_RISK));        // L3: off
    Scene q;
    q.id = 0;
    CHECK(!make(q)->openRisk(BC_OPEN_RISK) || !BC_QUEEN);  // the queen: off
    CHECK(!make(Scene())->openRisk(16));              // a bit the switch does not have: off
}

// bit 1: a pearl 3 steps east that a teammate is nearer to (friendDist_ 0) still pulls an L2 non-queen; L3 / the queen yield it
static double yieldLoss(Scene sc) {
    Policy* p = make(sc);
    build(p);
    p->targets_.push_back({at(6, 10), 1.0, -1, 0.0});
    double const free = scoreE(p);
    p->friendDist_[at(6, 10)] = 0;
    p->friendId_[at(6, 10)] = 4;
    return free - scoreE(p);
}

static void testYield() {
    Scene sc;
    double const on = yieldLoss(sc);
    Scene l3;
    l3.L = 3;
    double const off = yieldLoss(l3);
    CHECK(off > 1e-6);                                // out of range: the teammate's pearl is worth nothing to us
    CHECK(std::abs(on) < 1e-9 || !(BC_OPEN_RISK & 1));  // bit 1: no loss
    CHECK(on > 1e-6 || (BC_OPEN_RISK & 1));
    Scene late;
    late.round = 100;
    CHECK(yieldLoss(late) > 1e-6);                    // r100: yields again
    Scene q;
    q.id = 0;
    CHECK(yieldLoss(q) > 1e-6 || !BC_QUEEN);          // the queen keeps her rules
    // a fountain (gap 8, wHot 2) 3 steps east with a teammate on it (friendDist_ 0 <= hotYield): bit 1 keeps its pull, L3 leaves it
    auto fountain = [&](Scene f) {
        Policy* p = make(f);
        gw->board.bed[at(6, 10)] = 1;
        gw->board.gapMin[at(6, 10)] = gw->board.gapMax[at(6, 10)] = 8;
        build(p);
        p->friendDist_[at(6, 10)] = 0;
        double const e = scoreE(p);
        p->hot_.clear();
        return e - scoreE(p);
    };
    Scene f2, f3;
    f3.L = 3;
    CHECK((fountain(f2) > 1e-6) == ((BC_OPEN_RISK & 1) != 0));
    CHECK(std::abs(fountain(f3)) < 1e-12);
}

// bit 2: a bed 3 steps east spawning in 8 rounds - BC_ECON_BED's wait value (go early, wait next to it) against the plain countdown discount
static double bedValue(Scene sc) {
    Policy* p = make(sc);
    build(p);
    double const none = scoreE(p);
    p->targets_.push_back({at(6, 10), 0.0, 8, 0.0});
    return scoreE(p) - none;
}

static void testWait() {
    Scene sc;
    Scene l3;
    l3.L = 3;
    double const on = bedValue(sc), off = bedValue(l3);
    CHECK(off > 0 && on > 0);
    CHECK((on < off - 1e-9) == ((BC_OPEN_RISK & 2) != 0));  // bit 2: worth less (counted from the spawn)
    // idle-to-bed: an L2 that has not eaten for 60 rounds, a bed (gap 20) 3 steps east
    Scene idle;
    idle.round = 60;
    Policy* p = make(idle);
    gw->board.bed[at(6, 10)] = 1;
    gw->maxCd[at(6, 10)] = 20;
    build(p);
    p->planIdleBed();
    CHECK((p->idleBedCell_ == -1) == ((BC_OPEN_RISK & 2) != 0));
    Scene idle3 = idle;
    idle3.L = 3;
    Policy* p3 = make(idle3);
    gw->board.bed[at(6, 10)] = 1;
    gw->maxCd[at(6, 10)] = 20;
    build(p3);
    p3->planIdleBed();
    CHECK(p3->idleBedCell_ == at(6, 10));             // L3: it goes
}

// bit 4: a teammate at (8, 10) boxed in by walls N / S / E has one way out, (7, 10) - barred to us unless bit 4; never the queen's
static void testExit() {
    Scene sc;
    sc.mateX = 8, sc.mateY = 10;
    sc.walls = {at(8, 9), at(8, 11), at(9, 10)};
    Policy* p = make(sc);
    p->buildBase();
    CHECK((p->base_[at(7, 10)] >= INF) == !(BC_OPEN_RISK & 4));
    Scene l3 = sc;
    l3.L = 3;
    Policy* p3 = make(l3);
    p3->buildBase();
    CHECK(p3->base_[at(7, 10)] >= INF);               // L3: barred
    Scene q = sc;
    q.mateX = -1;
    q.queenX = 8, q.queenY = 10;                      // the queen boxed the same way: barred whatever the bits
    Policy* pq = make(q);
    pq->buildBase();
    CHECK(pq->base_[at(7, 10)] >= INF);
}

// bit 8: fast-bed parking - a bed's gap is the largest countdown seen on it or its mirror (point symmetry: (x, y) -> (19 - x, 19 - y))
static int fast(Scene sc, std::vector<std::pair<int, int>> beds, int sym = 0, double* pull = nullptr) {  // beds: (cell, maxCd)
    Policy* p = make(sc);
    gw->board.sym = sym;
    for (auto const& [c, g] : beds) gw->board.bed[c] = 1, gw->maxCd[c] = static_cast<int16_t>(g);
    build(p);
    p->planFastBed();
    if (pull) {
        double const e = scoreE(p);
        int const keep = p->fastBedCell_;
        p->fastBedCell_ = -1;
        *pull = e - scoreE(p);
        p->fastBedCell_ = keep;
    }
    return p->fastBedCell_;
}

static void testFastBed() {
    bool const on = (BC_OPEN_RISK & 8) != 0;
    Scene sc;
    double pull = 0;
    CHECK(fast(sc, {{at(6, 10), 20}}, 0, &pull) == (on ? at(6, 10) : -1));
    CHECK(!on || pull > 1e-6);                         // the step east (toward it) gains
    CHECK(fast(sc, {{at(6, 10), kOpenRiskFastGapT + 1}}) == -1);  // too slow
    CHECK(fast(sc, {{at(6, 10), 0}}) == -1);           // gap unknown
    CHECK(fast(sc, {{at(6, 10), 0}, {at(13, 9), 20}}, 3) == (on ? at(6, 10) : -1));  // its mirror's countdown tells
    CHECK(fast(sc, {{at(6, 10), 30}, {at(3, 14), 10}}) == (on ? at(3, 14) : -1));    // rate / distance: 1/(10 x 5) beats 1/(30 x 4)
    Scene mate;
    mate.mateX = 7, mate.mateY = 10;                   // a teammate 1 from the bed (we are 3): its bed
    CHECK(fast(mate, {{at(6, 10), 20}}) == -1);
    Scene l3;
    l3.L = 3;
    CHECK(fast(l3, {{at(6, 10), 20}}) == -1);
    Scene late;
    late.round = 100;
    CHECK(fast(late, {{at(6, 10), 20}}) == -1);
    Scene q;
    q.id = 0;
    CHECK(fast(q, {{at(6, 10), 20}}) == -1 || !BC_QUEEN);
}
#endif

int main() {
#if BC_OPEN_RISK
    testGate();
    testYield();
    testWait();
    testExit();
    testFastBed();
#endif
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
