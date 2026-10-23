// Opening expansion (BC_OPEN_EXPAND 1 with BC_OPEN_EXPAND_GATE 28 in the final build): in the opening a non-queen heads for
// the known bed area with the most free beds, away from the enemy's start; none known: the unseen map centre. The gate
// (bits 4 | 8 | 16) admits maps with <= 2 starting dragons and <= 700 tiles, dragons born later on maps <= 700 tiles, and
// any map <= 700 tiles.
//   c++ -std=c++20 -O2 -I bots/abyss tests/native/open_expand_test.cpp -o open_expand_test && ./open_expand_test
// ablation.hpp redefines both switches in its final overrides, so -D flags do not change them; the branches for other gate
// values compile only in a copy of the bot with another ablation.hpp.
// Policy::planOpenExpand and its pull in scoreFrom on a synthetic 20 x 20 open board, point symmetry: our dragon (id 2,
// 2 long) at (3, 10) with its body west, born there; our queen (id 0) in view at (3, 8); home beds (1..5, 12) next to her
// (home, and held by her); a small central area (9..10, 9..10: 4 beds); a big mirrored area round the enemy's start (the
// mirror of (3, 10) is (16, 9): beds 15..17 x 8..10, 9 beds) - nearer beds x fade would pick it without the enemy-start rule.
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

#if BC_OPEN_EXPAND
struct Scene {
    int id = 2, round = 30;
    bool queen = true;                 // our queen in view at (3, 8)
    int mateX = -1, mateY = -1;        // another non-queen of ours in view (id 4)
    int foeX = -1, foeY = -1;          // an enemy head in view (id 41)
    int heardX = -1, heardY = -1, heardAge = 0;  // an enemy head heard (id 43)
    bool home = true, central = true, enemySide = true;
    bool centreUnseen = false;         // the map centre (10, 10) never seen
    int sym = 3;
    int firstX = 3, firstY = 10;       // this process's first head (default: where it is now)
    int bedX = -1, bedY = -1;          // one more bed
    int centralCd = -1;                // the central beds' countdown as seen (-1 unknown)
    int enemyMirrorCd = -1;            // the countdown seen on the mirror tiles of the enemy-side area (2..4, 9..11)
    int startUnits = 2;                // our starting dragons (BC_OPEN_EXPAND_GATE bit 4)
};

static int plan(Scene const& sc, double* toward = nullptr) {
    World w;
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
    if (sc.home)
        for (int x = 1; x <= 5; x++) w.board.bed[at(x, 12)] = 1;
    if (sc.central)
        for (int y = 9; y <= 10; y++)
            for (int x = 9; x <= 10; x++) w.board.bed[at(x, y)] = 1;
    if (sc.enemySide)
        for (int y = 8; y <= 10; y++)
            for (int x = 15; x <= 17; x++) {
                w.board.bed[at(x, y)] = 1;
                w.seenRound[at(x, y)] = -1;
                w.mirrored[at(x, y)] = 1;
            }
    if (sc.centreUnseen) w.seenRound[at(10, 10)] = -1;
    if (sc.bedX >= 0) w.board.bed[at(sc.bedX, sc.bedY)] = 1;
    if (sc.central)
        for (int y = 9; y <= 10; y++)
            for (int x = 9; x <= 10; x++) w.next[at(x, y)] = sc.centralCd;
    for (int y = 9; y <= 11; y++)
        for (int x = 2; x <= 4; x++) w.next[at(x, y)] = sc.enemyMirrorCd;
    w.ourQueen = 0;
    w.startUnits = sc.startUnits;      // two starting dragons on a 400-tile board: BC_OPEN_EXPAND_GATE bit 4 lets it through
    auto put = [&](int id, char team, int x, int y) {
        w.occ[at(x, y)] = id;
        w.occHeadId[at(x, y)] = id;
        w.others.push_back({id, team, at(x, y), N, 1});
    };
    if (sc.queen) put(0, 'A', 3, 8);
    if (sc.mateX >= 0) put(4, 'A', sc.mateX, sc.mateY);
    if (sc.foeX >= 0) put(41, 'B', sc.foeX, sc.foeY);
    if (sc.heardX >= 0) w.know.enemies = {{at(sc.heardX, sc.heardY), 3, 43, sc.round - sc.heardAge}};
    w.firstHead = at(sc.firstX, sc.firstY);
    Params p;
    Policy pol(w, p, 1e18);
    pol.L_ = 2;
    pol.queenMe_ = BC_QUEEN && sc.id < 2;
    pol.buildBase();
    pol.buildTargets();
    pol.buildFriendFields();
    pol.planOpenExpand();
    if (toward) {  // the pull's part of scoreFrom: a step E (toward the central area) minus a step N
        std::vector<int> none;
        Choice e = pol.scoreFrom(w.body, 2, E, 0, none), n = pol.scoreFrom(w.body, 2, N, 0, none);
        int const keep = pol.expandTarget_;
        pol.expandTarget_ = -1;
        Choice e0 = pol.scoreFrom(w.body, 2, E, 0, none), n0 = pol.scoreFrom(w.body, 2, N, 0, none);
        pol.expandTarget_ = keep;
        *toward = (e.score - n.score) - (e0.score - n0.score);
    }
    return pol.expandTarget_;
}

static bool central(int c) { return c >= 0 && c % 20 >= 9 && c % 20 <= 10 && c / 20 >= 9 && c / 20 <= 10; }

static void testTarget() {
    Scene sc;
    double toward = 0;
    CHECK(central(plan(sc, &toward)));              // the central area, not the home beds (by the queen) nor the enemy's start
    CHECK(toward > 0.0);                            // and its pull favours the step toward it
    Scene nosym;
    nosym.sym = 0;                                  // the enemy's start unknown: the big mirrored area wins on beds
    CHECK(plan(nosym) / 20 >= 8 && plan(nosym) % 20 >= 15);
    Scene noq;
    noq.queen = false;                              // the queen out of view and unheard: the home beds (within 5 of our first head) still
    CHECK(central(plan(noq)));                      // are home, not an area to expand to
    Scene moved;
    moved.queen = false;
    moved.firstX = 3, moved.firstY = 3;             // born at (3, 3): the home beds are 9 away (free), the enemy's start (16, 16) is 6 from the
    CHECK(plan(moved) % 20 >= 15);                  // mirrored area - its 9 beds win
}

static void testHome() {
    Scene edge;
    edge.central = edge.enemySide = false;
    edge.bedX = 8, edge.bedY = 10;                  // a lone bed 5 from our first head (and 5 from the queen): home
    CHECK(plan(edge) == -1);
    Scene past;
    past.central = past.enemySide = false;
    past.bedX = 9, past.bedY = 10;                  // 6 from it: an area of one bed
    CHECK(plan(past) == at(9, 10));
}

static void testCountdown() {
    Scene slow;
    slow.centralCd = kOpenExpandSoon + 1;           // the central beds refill past the opening's horizon: not an area
    CHECK(!central(plan(slow)));
    Scene soon;
    soon.centralCd = kOpenExpandSoon;               // at the horizon: an area
    CHECK(central(plan(soon)));
    Scene moved;
    moved.queen = false;
    moved.firstX = 3, moved.firstY = 3;
    moved.enemyMirrorCd = 300;                      // as testTarget's `moved`, but the mirrored area's mirror tiles show a long countdown
    CHECK(plan(moved) / 20 == 12);                  // (mirror tiles share one): the home beds instead
    Scene weigh;                                    // (the board wraps: no enemy-side area here, it is 6 steps west of us)
    weigh.queen = weigh.enemySide = false;
    weigh.firstX = 3, weigh.firstY = 3;
    weigh.centralCd = 5;                            // known to refill soon, the 4 central beds count 4 (x .85) against the 5 unknown home
    CHECK(central(plan(weigh)));                    // beds' 2.5 (x .95) ...
    weigh.centralCd = -1;                           // ... unknown, 2 x .85: the home beds
    CHECK(plan(weigh) / 20 == 12);
}

static void testHeld() {
    Scene mate;
    mate.mateX = 10, mate.mateY = 12;              // a teammate within 3 of the central area holds it; no queen known (no escort rule), no
    mate.queen = mate.home = false;                 // home beds: nothing left (the centre is seen)
    CHECK(plan(mate) == -1);
    Scene foe;
    foe.foeX = 11, foe.foeY = 11;                   // an enemy head next to it
    CHECK(!central(plan(foe)));
    Scene heard;
    heard.heardX = 11, heard.heardY = 11, heard.heardAge = kOpenExpandAge;  // heard at the age limit: still theirs
    CHECK(!central(plan(heard)));
    Scene old;
    old.heardX = 11, old.heardY = 11, old.heardAge = kOpenExpandAge + 1;    // too old a word
    CHECK(central(plan(old)));
}

static void testEscort() {
    Scene far;
    far.mateX = 12, far.mateY = 3;                  // queen + another non-queen farther from her than us: we are the escort, we stay
    CHECK(plan(far) == -1);
    Scene nearer;
    nearer.mateX = 4, nearer.mateY = 7;             // the other one is nearer her: it escorts, we go
    CHECK(central(plan(nearer)));
    Scene tie;
    tie.mateX = 1, tie.mateY = 10;                  // as near as us (2): the lower id escorts - us (2 < 4)
    CHECK(plan(tie) == -1);
}

static void testWhen() {
    Scene late;
    late.round = kOpenExpandUntil + 1;
    CHECK(plan(late) == -1);
    Scene last;
    last.round = kOpenExpandUntil;
    CHECK(central(plan(last)));
    Scene queen;
    queen.id = 0;                                   // the queen keeps her own rules
    queen.queen = false;
    CHECK(plan(queen) == -1 || !BC_QUEEN);
}

static void testCentre() {
    Scene none;
    none.central = none.enemySide = false;          // only the home beds (held by the queen): the unseen centre
    none.centreUnseen = true;
    CHECK(plan(none) == at(10, 10));
    Scene seen;
    seen.central = seen.enemySide = false;          // ... seen already: nothing
    CHECK(plan(seen) == -1);
}
#endif

#if BC_OPEN_EXPAND && BC_OPEN_EXPAND_GATE
// the gate alone: a W x H open board, our non-queen (id 2) at (3, 3), `beds` known beds at (5 + i, 3), startUnits (-1: born later)
static int gateOn(int W, int H, int startUnits, int round, int beds, int* decided = nullptr, int preset = -1) {
    World w;
    Init in;
    in.id = 2;
    in.team = 'A';
    in.w = W;
    in.h = H;
    w.start(in);
    w.t.round = round;
    w.t.length = 2;
    w.t.units = 4;
    w.startUnits = startUnits;
    w.expandGate = preset;
    auto id = [&](int x, int y) { return y * W + x; };
    w.head = id(3, 3);
    w.body = {id(3, 3), id(2, 3)};
    w.occ.assign(w.board.NC, -1);
    w.occHeadId.assign(w.board.NC, -1);
    for (int c : w.body) w.occ[c] = 2;
    for (int c = 0; c < w.board.NC; c++) w.seenRound[c] = w.visRound[c] = round, w.board.bed[c] = 0;
    for (int i = 0; i < beds; i++) w.board.bed[id(5 + i, 3)] = 1;
    Params p;
    Policy pol(w, p, 1e18);
    pol.L_ = 2;
    pol.buildBase();
    pol.buildTargets();
    pol.buildFriendFields();
    bool const on = pol.expandGateOn();
    if (decided) *decided = w.expandGate;
    return on;
}

static void testGate() {
    int dec = 0;
#if BC_OPEN_EXPAND_GATE & 4
    CHECK(gateOn(20, 20, 2, 30, 10));               // 2 starters, 400 tiles: on (whatever the beds)
    CHECK(gateOn(28, 25, 2, 30, 10));               // 700 tiles: on
    CHECK(!gateOn(29, 25, 2, 30, 10) || (BC_OPEN_EXPAND_GATE & 1));  // 725 tiles (bit 1 off: 10 beds near anyway)
    CHECK(gateOn(20, 20, 3, 30, 10) == ((BC_OPEN_EXPAND_GATE & 16) != 0));  // 3 starters: off (bit 16: on)
    CHECK(gateOn(20, 20, -1, 30, 10) == ((BC_OPEN_EXPAND_GATE & 8) != 0));  // born later: bit 8 decides on a small map
    CHECK(!gateOn(30, 30, -1, 30, 10));             // ... never on a big one
#if !(BC_OPEN_EXPAND_GATE & 16)
    Scene three;
    three.startUnits = 3;                           // the whole rule gated: no target with 3 starting dragons
    CHECK(plan(three, nullptr) == -1);
#endif
#endif
#if BC_OPEN_EXPAND_GATE & 16
    CHECK(gateOn(32, 16, 5, 30, 10));               // Devil: 512 tiles, UNIT_COUNT 5 at r0 (mates split before this turn) - on
    CHECK(gateOn(28, 25, 9, 30, 10));               // 700 tiles, any start: on
    CHECK(gateOn(20, 20, -1, 30, 10));              // born later: on
    CHECK(!gateOn(29, 25, 9, 30, 10) || (BC_OPEN_EXPAND_GATE & 1));  // 725 tiles: off
    CHECK(!gateOn(25, 35, 2, 30, 10) || (BC_OPEN_EXPAND_GATE & 1));  // Queen Of Spades (25 x 35): off
    Scene three16;
    three16.startUnits = 5;                         // the rule's scene (400 tiles) with 5 at r0: a target
    CHECK(plan(three16, nullptr) != -1);
#endif
#if BC_OPEN_EXPAND_GATE & 1
    CHECK(!gateOn(40, 40, 9, 5, 2, &dec) && dec == -1);  // before r10: off, undecided
    CHECK(gateOn(40, 40, 9, 10, 3, &dec) && dec == 1);   // r10: 3 known beds within 10 steps - on
    CHECK(!gateOn(40, 40, 9, 10, 4, &dec) && dec == 0);  // 4 - off
#endif
    CHECK(!gateOn(20, 20, 2, 30, 0, &dec, 0) && dec == 0);  // decided off: stays off
    CHECK(gateOn(40, 40, 9, 30, 10, &dec, 1) && dec == 1);  // decided on: stays on
}
#endif

int main() {
#if BC_OPEN_EXPAND && BC_OPEN_EXPAND_GATE
    testGate();
#endif
#if BC_OPEN_EXPAND && (!BC_OPEN_EXPAND_GATE || (BC_OPEN_EXPAND_GATE & 4))  // (bit 1 alone: the rule's scenes have many beds near - gated off)
    testTarget();
    testHome();
    testCountdown();
    testHeld();
    testEscort();
    testWhen();
    testCentre();
#endif
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
