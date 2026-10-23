// Bed keeping (BC_BED_KEEP, inactive in the final build): a non-queen keeps a fast bed within kKeepSteps whose cluster has
// room for it, and while it does, targets beyond kKeepR of the bed and the far pulls do not count.
//   c++ -std=c++20 -O2 -DBC_BED_KEEP=1 -I bots/abyss tests/native/bed_keep_test.cpp -o bed_keep_test && ./bed_keep_test
//   (also =3: bit 2 adds the on-time rule; and -DBC_BED_KEEP=3 -DBC_KEEP_GAP=3 -DBC_TIER2=1: the second-tier beds)
// A synthetic 20 x 20 open board: our dragon (id 2 unless set, L2) head (3, 10), body west.
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

#if BC_BED_KEEP
struct Bed { int x, y, maxCd, gap = 0, next = -1; bool pearl = false; };
struct Scene {
    int id = 2, round = 60, sym = 0;
    std::vector<Bed> beds;
    int mateX = -1, mateY = -1;  // a non-queen teammate in view (id 4)
};

static World* gw = nullptr;

static Policy* make(Scene const& sc) {
    static World w;
    static Params p;
    w = World();
    p = Params();
    Init in;
    in.id = sc.id;
    in.team = 'A';
    in.w = in.h = 20;
    w.start(in);
    w.board.sym = sc.sym;
    w.t.round = sc.round;
    w.t.length = 2;
    w.t.units = 6;
    w.head = at(3, 10);
    w.body = {at(3, 10), at(2, 10)};
    w.occ.assign(w.board.NC, -1);
    w.occHeadId.assign(w.board.NC, -1);
    for (int c : w.body) w.occ[c] = sc.id;
    for (int c = 0; c < w.board.NC; c++) w.seenRound[c] = w.visRound[c] = sc.round, w.board.bed[c] = 0;
    for (Bed const& bd : sc.beds) {
        int const c = at(bd.x, bd.y);
        w.board.bed[c] = 1;
        w.maxCd[c] = static_cast<int16_t>(bd.maxCd);
        if (bd.gap > 0) w.board.gapMin[c] = w.board.gapMax[c] = static_cast<int16_t>(bd.gap);
        w.next[c] = bd.next;
        w.seenPearl[c] = bd.pearl;
    }
    w.ourQueen = 0;
    if (sc.mateX >= 0) {
        int const c = at(sc.mateX, sc.mateY);
        w.occ[c] = 4;
        w.occHeadId[c] = 4;
        w.others.push_back({4, 'A', c, N, 1});
    }
    gw = &w;
    static Policy* pol = nullptr;
    delete pol;
    pol = new Policy(w, p, 1e18);
    pol->L_ = 2;
    pol->queenMe_ = BC_QUEEN && sc.id < 2;
    pol->buildBase();
    pol->buildTargets();
    pol->buildFriendFields();
    pol->planTeam();
    return pol;
}

static int keep(Scene const& sc) {
    Policy* p = make(sc);
    p->planKeep();
    return p->keepCell_;
}

static double scoreE(Policy* p) {
    std::vector<int> none;
    return p->scoreFrom(gw->body, 2, E, 0, none).score;
}

static void testChoice() {
    Scene sc;
    sc.beds = {{6, 10, 2}};
    CHECK(keep(sc) == at(6, 10));                     // a fountain 3 steps east
    Scene slow;
    slow.beds = {{6, 10, kKeepGap + 1}};
    CHECK(keep(slow) == (BC_TIER2 ? at(6, 10) : -1));  // too slow (BC_TIER2: a second-tier bed)
    Scene unknown;
    unknown.beds = {{6, 10, 0}};
    CHECK(keep(unknown) == -1);                       // gap unknown
    Scene learned;
    learned.beds = {{6, 10, 0, 3}};
    CHECK(keep(learned) == at(6, 10));                // the learned gap (a fountain heard over sonar)
    Scene far;
    far.beds = {{3 + kKeepSteps + 1, 10, 2}};
    CHECK(keep(far) == (BC_TIER2 ? at(3 + kKeepSteps + 1, 10) : -1));  // beyond kKeepSteps (BC_TIER2: within kTier2Steps)
    Scene edge;
    edge.beds = {{3 + kKeepSteps, 10, 2}};
    CHECK(keep(edge) == at(3 + kKeepSteps, 10));      // at kKeepSteps
    Scene mirror;
    mirror.sym = 3;                                   // point symmetry: (6, 10) <-> (13, 9)
    mirror.beds = {{6, 10, 0}, {13, 9, 2}};
    CHECK(keep(mirror) == at(6, 10));                 // its mirror's countdown tells
    Scene nearest;
    nearest.beds = {{7, 10, 2}, {5, 10, 2}};
    CHECK(keep(nearest) == at(5, 10));                // the nearest
    Scene q = sc;
    q.id = 0;
    CHECK(keep(q) == -1 || !BC_QUEEN);                // the queen never
    Scene late = sc;
    late.round = kKeepUntil;
    CHECK(keep(late) == -1);
    Policy* p = make(sc);
    p->feedHead_ = at(10, 10);                        // feeding: no keeping
    p->planKeep();
    CHECK(p->keepCell_ == -1);
}

static void testRoom() {
    Scene one;
    one.beds = {{6, 10, 2}};
    one.mateX = 7, one.mateY = 10;                    // a teammate 1 from the bed (we are 3): one bed, taken
    CHECK(keep(one) == -1);
    Scene two = one;
    two.beds = {{6, 10, 2}, {6, 12, 2}};              // two fast beds within kKeepR: room for us
    CHECK(keep(two) != -1);
    Scene farMate = one;
    farMate.mateX = 12, farMate.mateY = 10;           // the teammate farther from it than we are: ours
    CHECK(keep(farMate) == at(6, 10));
}

static void testHold() {
    Scene sc;
    sc.beds = {{6, 10, 2}};
    Policy* p = make(sc);
    p->planKeep();
    CHECK(p->keepCell_ == at(6, 10));
    // a pearl 9 east of us (cheb 6 from the bed > kKeepR): worth nothing while keeping, something without
    double const base = scoreE(p);
    p->targets_.push_back({at(12, 10), 1.0, -1, 0.0});
    double const withFar = scoreE(p);
    CHECK(std::abs(withFar - base) < 1e-9);
    int const kc = p->keepCell_;
    p->keepCell_ = -1;
    double const freeBase = scoreE(p);
    p->targets_.pop_back();
    double const freeNo = scoreE(p);
    CHECK(freeBase - freeNo > 1e-6);
    // a pearl next to the bed (within kKeepR): it counts while keeping
    p->keepCell_ = kc;
    double const b0 = scoreE(p);
    p->targets_.push_back({at(7, 11), 1.0, -1, 0.0});
    CHECK(scoreE(p) - b0 > 1e-6);
    p->targets_.pop_back();
    // the idle-bed pull is off while keeping
    p->idleBedCell_ = at(10, 3);
    double const i1 = scoreE(p);
    p->idleBedCell_ = -1;
    CHECK(std::abs(i1 - scoreE(p)) < 1e-9);
    p->keepCell_ = -1;
    p->idleBedCell_ = at(10, 3);
    double const i2 = scoreE(p);
    p->idleBedCell_ = -1;
    CHECK(std::abs(i2 - scoreE(p)) > 1e-9);
    // the pull to the kept bed: the step east (toward it) gains
    double const n0 = scoreE(p);
    p->keepCell_ = kc;
    p->targets_.clear();                               // the bed's own target aside
    double const e1 = scoreE(p);
    p->keepCell_ = -1;
    CHECK(e1 - scoreE(p) > 1e-6);
    (void)n0;
}

#if BC_BED_KEEP & 2
// bit 2: the kept bed 3 steps east pulls only when due (countdown <= 3 + kKeepSlack, a pearl on it, or unknown)
static bool due(Bed bd, int sym = 0) {
    Scene sc;
    sc.sym = sym;
    sc.beds = {bd};
    if (sym) sc.beds.push_back({13, 9, 2});
    Policy* p = make(sc);
    p->planKeep();
    return p->keepCell_ == at(6, 10) && p->keepDue_;
}

static void testOnTime() {
    CHECK(!due({6, 10, 2, 0, 3 + kKeepSlack + 1}));   // spawns in 7, 3 steps away: early
    CHECK(due({6, 10, 2, 0, 3 + kKeepSlack}));        // in 6: due
    CHECK(due({6, 10, 2, 0, 9, true}));               // a pearl on it
    Scene m;
    m.sym = 3;
    m.beds = {{6, 10, 2, 0, -1}, {13, 9, 2, 0, 9}};   // its own countdown unseen, its mirror's 9: early
    Policy* p = make(m);
    p->planKeep();
    CHECK(p->keepCell_ == at(6, 10) && !p->keepDue_);
    // the pull only when due; the hold itself stays
    Scene sc;
    sc.beds = {{6, 10, 2, 0, 9}};
    Policy* q = make(sc);
    q->planKeep();
    q->targets_.clear();
    double const early = scoreE(q);
    q->keepDue_ = true;
    CHECK(scoreE(q) - early > 1e-6);
    q->keepDue_ = false;
    q->targets_.push_back({at(12, 10), 1.0, -1, 0.0});  // a far pearl still counts nothing while keeping early
    CHECK(std::abs(scoreE(q) - early) < 1e-9);
}
#endif

#if BC_TIER2
// BC_TIER2: no first-tier bed with room - the nearest second-tier bed (gap <= kTier2Gap) within kTier2Steps with room
static void testTier2() {
    Scene t2;
    t2.beds = {{3 + 8, 10, kTier2Gap}};
    CHECK(keep(t2) == at(11, 10));                    // 8 steps, gap 12
    Scene slow;
    slow.beds = {{11, 10, kTier2Gap + 1}};
    CHECK(keep(slow) == -1);
    Scene pair;
    pair.beds = {{11, 10, kTier2Gap + 1}, {12, 10, kTier2Gap}};  // the too-slow one is nearer: the gap-12 bed beside it
    CHECK(keep(pair) == at(12, 10));
    Scene far;
    far.beds = {{3 + kTier2Steps + 1, 10, 5}};
    CHECK(keep(far) == -1);
    Scene first;
    first.beds = {{8, 10, 2}, {5, 10, kKeepGap + 1}};  // a first-tier bed 5 off beats a second-tier bed 2 off
    CHECK(keep(first) == at(8, 10));
    Scene full;
    full.beds = {{8, 10, 2}, {5, 10, kKeepGap + 1}};
    full.mateX = 9, full.mateY = 10;                  // the fountain's one bed is a teammate's: the second tier
    CHECK(keep(full) == at(5, 10));
}
#endif
#endif

int main() {
#if BC_BED_KEEP
    testChoice();
    testRoom();
    testHold();
#if BC_BED_KEEP & 2
    testOnTime();
#endif
#if BC_TIER2
    testTier2();
#endif
#endif
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
