// Few-unit opening mode (BC_EXPAND_OPENING, inactive in the final build): on maps with few starting dragons and the food
// behind portals, the opening switches to a mode that probes doors, relaxes the queen's portal gate and buds small dragons.
//   c++ -std=c++20 -O2 -DBC_EXPAND_OPENING=1 -I bots/abyss tests/native/expand_opening_test.cpp -o expand_opening_test && ./expand_opening_test
// Policy::planOpenMode (enter / leave, the band, the hunter), the queen's relaxed gate (World::gatePortals), the probe it
// starts (planPortals), the mother's bud (trySplit) and the eat count, on a synthetic 20 x 20 open board.
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

#if BC_EXPAND_OPENING
// a `len`-long dragon (id) with its head at (10, 10) facing east, round `round`, `units` on the team; every tile seen unless `fog`
static World dragon(int id, int len, int round, int units, bool fog = false) {
    World w;
    Init in;
    in.id = id;
    in.team = 'A';
    in.w = in.h = 20;
    w.start(in);
    w.t.round = round;
    w.t.length = len;
    w.t.units = units;
    w.head = w.board.id(10, 10);
    for (int i = 0; i < len; i++) w.body.push_back(w.board.id(10 - i, 10));
    w.occ.assign(w.board.NC, -1);
    w.occHeadId.assign(w.board.NC, -1);
    if (!fog)
        for (int c = 0; c < w.board.NC; c++) w.seenRound[c] = round;
    return w;
}

static void torusField(Policy& pol, Board const& b, int x, int y) {
    pol.myDist_.assign(b.NC, INF);
    for (int c = 0; c < b.NC; c++) {
        int dx = std::abs(b.X(c) - x), dy = std::abs(b.Y(c) - y);
        pol.myDist_[c] = std::min(dx, 20 - dx) + std::min(dy, 20 - dy);
    }
}

struct Mode {
    int id = 2, len = 3, round = 20, units = 2, eats = 0, enemyLong = 0;
    int bed = -1;          // a known bed at this cell (seen, or mirrored with bedMirrored), -1 none
    int bed2 = -1;         // a second known bed (seen), -1 none
    bool bedMirrored = false;
    bool bedFar = false;   // never-seen strips cut the head's band off from the bed: not in reach over known tiles
};

static bool mode(Mode const& m, int* beds = nullptr) {
    World w = dragon(m.id, m.len, m.round, m.units);
    w.eatCount = m.eats;
    if (m.enemyLong > 0) w.enemyLongest = m.enemyLong, w.enemyLongestRound = m.round;
    if (m.bed >= 0) {
        w.board.bed[m.bed] = 1;
        if (m.bedMirrored) w.seenRound[m.bed] = -1, w.mirrored[m.bed] = 1;  // never seen, known by its mirror
    }
    if (m.bed2 >= 0) w.board.bed[m.bed2] = 1;
    if (m.bedFar)  // never-seen columns 12-13 and, the board being a torus, 6-7 too: the head's band (8-11) is cut off
        for (int y = 0; y < 20; y++)
            for (int x : {6, 7, 12, 13}) w.seenRound[w.board.id(x, y)] = -1;
    Params p;
    Policy pol(w, p, 1e18);
    pol.L_ = m.len;
    pol.queenMe_ = BC_QUEEN && m.id < 2;
    torusField(pol, w.board, 10, 10);
    pol.planOpenMode();
    if (beds) *beds = pol.eoBeds_;
    return w.openMode;
}

static void testMode() {
    Board b;
    b.init(20, 20);
    int bed = b.id(14, 10);
    Mode m;
    CHECK(mode(m));                                    // 2 units, r20, nothing eaten, no bed: on
    m.round = kEoBedCheck - 1;
    CHECK(!mode(m));                                   // before r10: off
    m.round = kEoBedCheck;
    CHECK(mode(m));                                    // r10, no bed in reach: on
    m.bed = bed;
    CHECK(!mode(m));                                   // r10 with a bed in reach (and r < 12): off
    m.round = kEoEatCheck;
    CHECK(mode(m));                                    // r12, nothing eaten: on, bed or not
    m.eats = 1;
    CHECK(!mode(m));                                   // ... one eaten and a bed in reach: off
    m.bedFar = true;
    CHECK(mode(m));                                    // ... the bed beyond never-seen ground (the nav would cross it): on
    m.bedFar = false;
    m.bedMirrored = true;
    int beds = -1;
    CHECK(!mode(m, &beds) && beds == 1);               // a mirrored bed (never seen) is a known bed
    m = Mode{};
    m.units = kEoMaxUnits + 1;
    CHECK(!mode(m));                                   // 3 units: off
    m.round = kEoLenCheck;
    m.enemyLong = 6;
    CHECK(mode(m));                                    // r40, 3 long against 6: on whatever the units
    m.enemyLong = 5;
    CHECK(!mode(m));                                   // ... against 5: off
    m.round = kEoLenCheck - 1;
    m.enemyLong = 6;
    CHECK(!mode(m));                                   // ... before r40: off
    m = Mode{};
    m.units = kEoExitUnits;
    m.round = kEoLenCheck;
    m.enemyLong = 9;
    CHECK(!mode(m));                                   // 6 units: out
    m.units = 5;
    CHECK(mode(m));
    m.round = kEoUntil;
    CHECK(!mode(m));                                   // r150: out
    m = Mode{};
    m.round = kEoEatCheck;
    m.eats = kEoExitEats;
    m.round = kEoLenCheck;
    m.enemyLong = 9;
    m.units = 3;
    m.bed = bed;
    CHECK(mode(m));                                    // 3 eaten, 1 bed in reach: still on
    m.bed2 = bed + 1;
    CHECK(!mode(m));                                   // ... 2 beds in reach: out (the exit reads the search: entered by the length clause)
}

// the queen (id 0) at (10, 10); a paired portal on the east side of (11, 10) lands on (5, 5) in a closed 2 x 2 room (5..6, 5..6) whose
// landing has 2 open sides: a small pocket - walled by the queen's gate, open to the mother. `known`: the room seen
static int gateStep(bool mother, bool known, bool oneSide = false, bool viaMode = false) {
    World w = dragon(0, 3, 30, 2);
    Board& b = w.board;
    auto cell = [&](int x, int y) { return b.id(x, y); };
    Edge pe;
    pe.kind = 2;
    pe.partnerOrient = 1;
    pe.px = 5, pe.py = 5;
    b.vE[cell(12, 10)] = pe;  // the west side of (12, 10) = the east side of (11, 10) ...
    Edge pf;
    pf.kind = 2;
    pf.partnerOrient = 1;
    pf.px = 12, pf.py = 10;
    b.vE[cell(5, 5)] = pf;    // ... paired with the west side of (5, 5)
    Edge kelp;
    kelp.kind = 1;
    for (int x = 5; x <= 6; x++) b.hE[cell(x, 5)] = kelp, b.hE[cell(x, 7)] = kelp;  // north and south of the room
    b.vE[cell(5, 6)] = kelp, b.vE[cell(7, 5)] = kelp, b.vE[cell(7, 6)] = kelp;      // west of (5, 6), east of the room
    if (oneSide) b.hE[cell(5, 6)] = kelp;                                            // (5, 5) keeps only its east side
    for (int x = 5; x <= 6; x++)
        for (int y = 5; y <= 6; y++) w.seenRound[cell(x, y)] = known ? 30 : -1;
    w.rebuildGated();
    if (viaMode) {  // the queen's own mode switches it (2 units, r30, nothing eaten, no bed: on)
        Params p;
        Policy pol(w, p, 1e18);
        pol.L_ = 3;
        pol.queenMe_ = BC_QUEEN;
        torusField(pol, b, 10, 10);
        pol.planOpenMode();
    } else {
        w.motherGate = mother;
        w.rebuildGated();
    }
    return b.nb[cell(11, 10) * 4 + E];
}

static void testGate() {
    Board b;
    b.init(20, 20);
    int land = b.id(5, 5);
    if (BC_QGATE) CHECK(gateStep(false, true) == -1);     // her gate: a small closed pocket is a wall
    CHECK(gateStep(true, true) == (BC_EO_QUEEN_MOTHER ? land : -1));  // the mother: open (the part off: walled)
    CHECK(gateStep(true, false) == -1);                    // an unknown landing stays a wall
    CHECK(gateStep(true, true, true) == -1);               // ... and one with a single open side
    CHECK(gateStep(false, true, false, true) == (BC_QUEEN && BC_EO_QUEEN_MOTHER ? land : -1));  // her mode opens it on the board at once
    // the mode switches the queen's gate (planOpenMode, applied at once to the board)
    World w = dragon(0, 3, 20, 2);
    Params p;
    Policy pol(w, p, 1e18);
    pol.L_ = 3;
    pol.queenMe_ = BC_QUEEN;
    torusField(pol, w.board, 10, 10);
    pol.planOpenMode();
    CHECK(w.openMode && w.motherGate == (BC_QUEEN && BC_EO_QUEEN_MOTHER));
    w.t.units = kEoExitUnits;
    pol.planOpenMode();
    CHECK(!w.openMode && !w.motherGate);
}

// kelp on the west sides of x = 13 and x = 19 cuts the strip x 13-18 off the torus; with `portal`, a paired portal on the east side of
// (12, 5) lands on (14, 8) inside it
static void fence(World& w, bool portal) {
    Board& b = w.board;
    Edge kelp;
    kelp.kind = 1;
    for (int y = 0; y < 20; y++) b.vE[b.id(13, y)] = kelp, b.vE[b.id(19, y)] = kelp;
    if (portal) {
        Edge pe;
        pe.kind = 2;
        pe.partnerOrient = 1;
        pe.px = 14, pe.py = 8;
        b.vE[b.id(13, 5)] = pe;
        Edge pf;
        pf.kind = 2;
        pf.partnerOrient = 1;
        pf.px = 13, pf.py = 5;
        b.vE[b.id(14, 8)] = pf;
    }
    w.rebuildGated();
}

// a non-queen in the mode: the band it heads for; 3 mirrored beds around (15, 15) inside the fenced strip, one at (12, 10) in its own
// region; `clusterFar`: a never-seen ring (13-18 x 13-18's edge) cuts the cluster off over known tiles; `portal`: the way in
static int band(bool sym, bool clusterFar = false, int id = 2, bool inMode = true, bool portal = true, bool gap = false) {
    World w = dragon(id, 3, 20, inMode ? 2 : 4);
    Board& b = w.board;
    fence(w, portal);
    if (gap) {  // an open gap in the fence at (13, 15), never seen: its own region stops at the unknown tile
        b.vE[b.id(13, 15)] = Edge{};
        w.seenRound[b.id(13, 15)] = -1;
        w.rebuildGated();
    }
    b.sym = sym ? 3 : -1;
    int cl[3] = {b.id(15, 15), b.id(16, 15), b.id(15, 16)};
    for (int c : cl) b.bed[c] = 1, w.mirrored[c] = 1;
    b.bed[b.id(12, 10)] = 1, w.mirrored[b.id(12, 10)] = 1;
    if (clusterFar)
        for (int x = 13; x <= 18; x++)
            for (int y = 13; y <= 18; y++)
                if (x == 13 || x == 18 || y == 13 || y == 18) w.seenRound[b.id(x, y)] = -1;
    Params p;
    Policy pol(w, p, 1e18);
    pol.L_ = 3;
    pol.queenMe_ = BC_QUEEN && id < 2;
    torusField(pol, b, 10, 10);
    w.eatCount = 0;
    pol.planOpenMode();
    return pol.eoMirror_;
}

static void testBand() {
    Board b;
    b.init(20, 20);
    if (!BC_EO_MIRROR_TARGET) {
        CHECK(band(true) == -1);                         // the part off: no band
        return;
    }
    int got = band(true);
    CHECK(got == b.id(15, 15) || got == b.id(16, 15) || got == b.id(15, 16));  // the cluster behind the portal; the single bed is local
    CHECK(got == b.id(15, 15));                                                  // ... its nearest tile through the portal
    CHECK(band(true, true) == -1);                       // the cluster out of reach, the single in its own region: none (probe)
    CHECK(band(true, false, 2, true, false) == -1);      // no portal into the strip: none
    CHECK(band(true, false, 2, true, true, true) == b.id(15, 15));  // a never-seen gap does not make the strip its own region
    CHECK(band(false) == -1);                            // no symmetry yet: none (the probe instead)
    CHECK(band(true, false, 0) == -1 || !BC_QUEEN);      // never the queen
    CHECK(band(true, false, 2, false) == -1);            // out of the mode: none
}

// ANTIHUNT: an enemy head (visible `vis`) `ch` tiles east of the head, `units` on the team
static int hunter(int vis, int ch, int units, int id = 2) {
    World w = dragon(id, 3, 20, units);
    Params p;
    Policy pol(w, p, 1e18);
    pol.L_ = 3;
    pol.queenMe_ = BC_QUEEN && id < 2;
    torusField(pol, w.board, 10, 10);
    pol.enemies_.push_back({9, w.board.id(10 + ch, 10), vis, pol.myDist_});
    if (units > kEoMaxUnits) w.enemyLongest = 7, w.enemyLongestRound = 20, w.t.round = kEoLenCheck;  // keep the mode on
    pol.planOpenMode();
    return pol.eoHunter_;
}

static void testHunter() {
    if (!BC_EO_ANTIHUNT) {
        CHECK(hunter(kEoHuntLen, kEoHuntR, 2) == -1);    // the part off: never
        return;
    }
    CHECK(hunter(kEoHuntLen, kEoHuntR, 2) == 0);         // 5 long, 4 away: flee
    CHECK(hunter(kEoHuntLen - 1, 2, 2) == -1);           // 4 long: no
    CHECK(hunter(kEoHuntLen, kEoHuntR + 1, 2) == -1);    // 5 away: no
    CHECK(hunter(kEoHuntLen, 2, kEoHuntUnits) == 0);     // 3 units (in the mode by the length rule): flee
    CHECK(hunter(kEoHuntLen, 2, kEoHuntUnits + 1) == -1);  // 4 units: no
    CHECK(hunter(kEoHuntLen, 2, 2, 0) == -1 || !BC_QUEEN);  // never the queen (QSK's)
}

// the probe the mode starts: a 2-long non-queen, 2 units, an unpaired portal end on the north side of (10, 7) (3 steps), ate at birth
// only; `band`: 0 none, 1 a mirrored bed in its own region, 2 a band behind the fence's portal
static bool probes(bool sym, int bandKind = 0) {
    World w = dragon(2, 2, 20, 2);
    w.lastEatRound = 15;  // not hungry by the other probe rules' starvation tests
    if (bandKind == 2) fence(w, true);
    Edge& e = w.board.hE[w.board.id(10, 7)];
    e = Edge{};
    e.kind = 2;
    w.board.sym = sym ? 3 : -1;
    if (bandKind == 1) w.board.bed[w.board.id(3, 3)] = 1, w.mirrored[w.board.id(3, 3)] = 1;
    if (bandKind == 2) w.board.bed[w.board.id(15, 15)] = 1, w.mirrored[w.board.id(15, 15)] = 1;
    w.rebuildGated();
    Params p;
    Policy pol(w, p, 1e18);
    pol.L_ = 2;
    torusField(pol, w.board, 10, 10);
    pol.planOpenMode();
    pol.planPortals();
    return pol.prober_ && pol.probeEntry_ == w.board.id(10, 7) && pol.escaping_;
}

static void testProbe() {
    bool on = BC_EO_MIRROR_TARGET != 0;
    CHECK(probes(false) == on);                          // no symmetry: the nearest unknown portal
    CHECK(probes(true, 1) == on);                        // a mirrored bed in its own region is no band: the probe
    CHECK(!probes(true, 2));                             // a band behind a known portal: the band, no probe
}

static void testBudAndEats() {
    // the mother buds 2 at length 6 (the swarm would split 3); out of the mode the swarm's 3
    for (bool inMode : {true, false}) {
        World w = dragon(0, 6, 20, inMode ? 2 : 4);
        Params p;
        p.swarmUntil = 400;
        p.lateNoSplit = 400;
        p.feedRound = 400;
        p.esUntil = 300;
        Policy pol(w, p, 1e18);
        pol.L_ = 6;
        pol.queenMe_ = BC_QUEEN;
        pol.base_.assign(w.board.NC, 0);
        torusField(pol, w.board, 10, 10);
        pol.planOpenMode();
        Choice best;
        best.why = "forage";
        best.dest = w.board.id(11, 10);
        Choice out;
        bool split = pol.trySplit(out, best);
        if (inMode && BC_EO_QUEEN_MOTHER) CHECK(split && out.n == 2);
        if (!inMode) CHECK(split && out.n == 3);
    }
    World w = dragon(2, 3, 20, 2);
    w.advanceBody(w.board.id(11, 10), true);
    CHECK(w.eatCount == 1);
    w.advanceBody(w.board.id(12, 10), false);
    CHECK(w.eatCount == 1);
    w.t.length = 4;
    w.advancePath({w.board.id(13, 10), w.board.id(14, 10), w.board.id(15, 10)}, {1, 0, 1}, 0);
    CHECK(w.eatCount == 3);  // the 2 eating steps of 3
}
#endif

int main() {
#if BC_EXPAND_OPENING
    testMode();
    testGate();
    testBand();
    testHunter();
    testProbe();
    testBudAndEats();
#endif
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
