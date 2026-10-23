// Room harvest (BC_HARVEST, 1 in the final build): an idle short non-queen harvests a closed 2-8 tile bed room behind a
// paired portal - it eats inside and splits its rear out.
//   c++ -std=c++20 -O2 -I bots/abyss tests/native/harvest_test.cpp -o harvest_test && ./harvest_test
//   (also -DBC_EXPAND_OPENING=1: the unit limit inside the few-unit opening mode)
// Policy::findHarvestRooms (closed, 2-8 tiles, a fast bed by countdown or mirror; not open, slow, 1-tile, too big),
// planHarvest (who goes, which entry; never the queen), harvestAct (the step in, the split of the rear outside, eat / move
// inside, the last split, the unit limit) on a synthetic 20 x 20 board: an L-shaped room (5,5) (5,6) (6,6) behind a paired
// portal on the east side of (10,5). ablation.hpp sets BC_HARVEST unconditionally (the mode-2 branches are not compiled).
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

#if BC_HARVEST
static Edge kelp() {
    Edge e;
    e.kind = 1;
    return e;
}

struct Room {
    int gap = 1;            // the bed's largest countdown seen (cdObs 3); 0: none seen
    int mirrorGap = 0;      // the mirrored gap (with gap 0)
    bool open = false;      // (6,6)'s east side opens onto a never-seen tile
    bool oneTile = false;   // (5,5) shut off from the rest
    bool big = false;       // the room grows to 9 tiles
    bool wide = false;      // a 2 x 3 room (5..6, 5..7) instead of the L
};

// a `len`-long dragon (id) with the given body (head first), facing `dir`, round 20, `units`; every tile seen
static void build(World& w, Room const& rm, int id, std::vector<int> body, int dir, int units) {
    Init in;
    in.id = id;
    in.team = 'A';
    in.w = in.h = 20;
    in.unitLimit = 64;
    w.start(in);
    w.t.round = 20;
    w.t.length = static_cast<int>(body.size());
    w.t.units = units;
    w.t.dir = dir;
    w.head = body[0];
    w.body = body;
    w.occ.assign(w.board.NC, -1);
    w.occHeadId.assign(w.board.NC, -1);
    Board& b = w.board;
    for (int c = 0; c < b.NC; c++) w.seenRound[c] = 20, w.visRound[c] = 20;
    Edge pe;  // the outside end: the east side of (10,5) = the west side of (11,5), landing on (5,5)
    pe.kind = 2;
    pe.partnerOrient = 1;
    pe.px = 5, pe.py = 5;
    b.vE[b.id(11, 5)] = pe;
    Edge pr;  // the room's end: the west side of (5,5), landing back on (10,5)
    pr.kind = 2;
    pr.partnerOrient = 1;
    pr.px = 11, pr.py = 5;
    b.vE[b.id(5, 5)] = pr;
    b.hE[b.id(5, 5)] = kelp(), b.vE[b.id(6, 5)] = kelp();                              // (5,5): north, east
    b.vE[b.id(5, 6)] = kelp(), b.hE[b.id(5, 7)] = kelp();                              // (5,6): west, south
    b.hE[b.id(6, 6)] = kelp(), b.hE[b.id(6, 7)] = kelp();                              // (6,6): north, south
    if (rm.wide) {
        Edge open;
        b.vE[b.id(6, 5)] = open, b.hE[b.id(5, 7)] = open, b.hE[b.id(6, 6)] = open, b.hE[b.id(6, 7)] = open;  // inside the 2 x 3
        b.hE[b.id(6, 5)] = kelp(), b.vE[b.id(7, 5)] = kelp(), b.vE[b.id(7, 6)] = kelp(), b.vE[b.id(7, 7)] = kelp();
        b.vE[b.id(5, 7)] = kelp(), b.hE[b.id(5, 8)] = kelp(), b.hE[b.id(6, 8)] = kelp();
    } else if (rm.open) {
        // (6,6)'s east side open onto (7,6), never seen, itself shut on its other sides: the room would be 4 tiles with an unknown one
        w.seenRound[b.id(7, 6)] = -1;
        b.hE[b.id(7, 6)] = kelp(), b.hE[b.id(7, 7)] = kelp(), b.vE[b.id(8, 6)] = kelp();
    } else if (rm.big) {
        for (int x = 7; x <= 12; x++) b.hE[b.id(x, 6)] = kelp(), b.hE[b.id(x, 7)] = kelp();  // a corridor (7..12, 6): 9 tiles
        b.vE[b.id(13, 6)] = kelp();
    } else {
        b.vE[b.id(7, 6)] = kelp();
    }
    if (rm.oneTile) {
        b.hE[b.id(5, 6)] = kelp();
        b.bed[b.id(5, 5)] = 1, w.cdObs[b.id(5, 5)] = 3, w.maxCd[b.id(5, 5)] = 1;  // a fast bed in the 1-tile room itself
    }
    int bed = b.id(5, 6);
    b.bed[bed] = 1;
    if (rm.gap > 0) w.cdObs[bed] = 3, w.maxCd[bed] = static_cast<int16_t>(rm.gap);
    if (rm.mirrorGap > 0) b.gapMax[bed] = static_cast<int16_t>(rm.mirrorGap);
    w.rebuildGated();
}

static int B(World const& w, int x, int y) { return w.board.id(x, y); }

static int rooms(Room const& rm) {
    World w;
    build(w, rm, 2, {0, 1}, E, 2);
    w.body = {B(w, 10, 10), B(w, 9, 10)}, w.head = w.body[0];
    Params p;
    Policy pol(w, p, 1e18);
    pol.findHarvestRooms();
    return static_cast<int>(pol.hRooms_.size());
}

static void testRooms() {
    Room r;
    CHECK(rooms(r) == 1);
    {
        World w;
        build(w, r, 2, {0, 1}, E, 2);
        Params p;
        Policy pol(w, p, 1e18);
        pol.findHarvestRooms();
        CHECK(pol.hRooms_.size() == 1 && pol.hRooms_[0].entry == B(w, 10, 5) && pol.hRooms_[0].dir == E &&
              pol.hRooms_[0].land == B(w, 5, 5) && pol.hRooms_[0].tiles.size() == 3);
    }
    Room g = r;
    g.gap = kHarvestGap;
    CHECK(rooms(g) == 1);   // refilling within kHarvestGap
    g.gap = kHarvestGap + 1;
    CHECK(rooms(g) == 0);   // slower: no
    Room m;
    m.gap = 0, m.mirrorGap = 2;
    CHECK(rooms(m) == 1);   // no countdown seen, the mirrored gap 2
    m.mirrorGap = 0;
    CHECK(rooms(m) == 0);   // nothing known of its refill
    Room o;
    o.open = true;
    CHECK(rooms(o) == 0);   // open onto a never-seen tile: not closed
    Room one;
    one.oneTile = true;
    CHECK(rooms(one) == 0);  // a 1-tile room: a trap (its bed is cut off)
    Room big;
    big.big = true;
    CHECK(rooms(big) == 0);  // 9 tiles
    Room wide;
    wide.wide = true;
    CHECK(rooms(wide) == 1);  // a 2 x 3 room: 6 tiles
}

// planHarvest for a dragon (id, len) at (10,10) facing east, `units` (round 20); a teammate's head in the room with `mate`; its last meal
// and its birth round (-1: none)
static int entry(int id, int len, int units, bool mate = false, int lastEat = -1, int born = -1, int gate = 0) {
    World w;
    Room r;
    std::vector<int> body;
    for (int i = 0; i < len; i++) body.push_back(10 - i + 20 * 10);  // (10,10), (9,10), ...
    build(w, r, id, body, E, units);
    if (mate) w.others.push_back({7, 'A', B(w, 6, 6), 0, 2});
    w.lastEatRound = lastEat;
    w.firstRound = born;
#if BC_MAPNET
    if (gate > 0) w.setGate(B(w, 10, 5), E, gate, 20);  // the door: the east side of (10,5)
    if (gate < 0) {  // the door's portal id (7) in the TRAPSET mask, heard from the team: the east side of (10,5) = vertical edge x 11
        w.portalEnds.push_back({7, {{1, 11, 5}}});
        w.trapMaskNow_ = 1ull << 7;
    }
#endif
    Params p;
    Policy pol(w, p, 1e18);
    pol.L_ = len;
    pol.queenMe_ = BC_QUEEN && id < 2;
    Board const& b = w.board;
    pol.myDist_.assign(b.NC, INF);
    for (int c = 0; c < b.NC; c++) {
        int dx = std::abs(b.X(c) - 10), dy = std::abs(b.Y(c) - 10);
        pol.myDist_[c] = std::min(dx, 20 - dx) + std::min(dy, 20 - dy);
    }
    pol.planHarvest();
    return pol.harvestEntry_;
}

static void testEntry() {
    int e = 5 * 20 + 10;  // (10,5)
    CHECK(entry(2, 3, 2) == e);                 // 3 long, 2 units: under half the limit
    CHECK(entry(2, 2, 2) == e);                 // 2 long
    CHECK(entry(2, 4, 2) == -1);                // 4 long: no
    CHECK(entry(2, 1, 2) == -1);                // 1 long: no
    CHECK(entry(2, 3, 31) == e);                // 31 units: under 32
    CHECK(entry(2, 3, 32) == -1 || BC_EXPAND_OPENING);  // 32: only in the expand mode (not on here: 32 units leaves it)
    CHECK(entry(2, 3, 63) == -1);               // no room for two more units
    CHECK(entry(2, 3, 2, true) == -1);          // a teammate's head in the room
    CHECK(entry(0, 3, 2) == -1 || !BC_QUEEN);   // never the queen
    // only an idle dragon: no meal for kHarvestIdle (12) rounds at round 20, its birth counting as a meal
    CHECK(entry(2, 3, 2, false, 8) == e);       // last ate at r8: 12 rounds
    CHECK(entry(2, 3, 2, false, 9) == -1);      // r9: 11 rounds
    CHECK(entry(2, 3, 2, false, -1, 8) == e);   // born at r8, never ate
    CHECK(entry(2, 3, 2, false, -1, 9) == -1);  // born at r9
    CHECK(entry(2, 3, 2, false, 5, 9) == -1);   // the later of the two counts
#if BC_MAPNET
    // the door checks: a DEAD or TRAP gate on the door: no entry; CLEARED / PROBING: in
    CHECK(entry(2, 3, 2, false, -1, -1, comms::G_TRAP) == -1);
    CHECK(entry(2, 3, 2, false, -1, -1, comms::G_DEAD) == -1);
    CHECK(entry(2, 3, 2, false, -1, -1, comms::G_CLEARED) == e);
    CHECK(entry(2, 3, 2, false, -1, -1, comms::G_PROBING) == e);
    // its portal id in the TRAPSET but this face no gate: in (the id is the edge's, shared by the face into a trap room, as on
    // maps whose portal pairs lead both into a food room and into a trap room)
    CHECK(entry(2, 3, 2, false, -1, -1, -1) == e);
#endif
}

// harvestAct for a dragon (id 2) with the body given (head first), facing `dir`; a pearl in view at `pearl` (-1 none)
static Choice act(std::vector<std::pair<int, int>> xy, int dir, int units = 2, int pearl = -1, bool* acted = nullptr, bool wide = false,
                  int gate = 0) {
    World w;
    Room r;
    r.wide = wide;
    std::vector<int> body;
    for (auto const& p : xy) body.push_back(p.first + 20 * p.second);
    build(w, r, 2, body, dir, units);
    if (pearl >= 0) w.seenPearl[pearl] = 1;
#if BC_MAPNET
    if (gate) w.setGate(B(w, 10, 5), E, gate, 20);
#endif
    w.harvestRound = 19;  // it stepped in earlier (the entry step sets it); the room logic stays on whatever its length
    Params p;
    Policy pol(w, p, 1e18);
    pol.L_ = static_cast<int>(body.size());
    Board const& b = w.board;
    pol.myDist_.assign(b.NC, 1);
    pol.planHarvest();
    Choice c;
    bool a = pol.harvestAct(c);
    if (acted) *acted = a;
    if (a && c.why == "harvest-in") CHECK(w.harvestRound == 20);
    return c;
}

static void testAct() {
    bool const chunk = BC_HARVEST >= 2;
    bool a = false;
    // at the entry facing east, a pearl on the landing: the step through the portal onto (5,5), eating
    Choice c = act({{10, 5}, {9, 5}, {8, 5}}, E, 2, 5 * 20 + 5, &a);
    CHECK(a && c.why == "harvest-in" && c.dir == E && c.dest == 5 * 20 + 5 && c.eat);
#if BC_MAPNET
    // ... the door a TRAP gate: it does not go in
    c = act({{10, 5}, {9, 5}, {8, 5}}, E, 2, 5 * 20 + 5, &a, false, comms::G_TRAP);
    CHECK(!a);
    // ... already inside when the door turns TRAP: the routine goes on (eat on)
    c = act({{5, 6}, {5, 5}, {10, 5}, {9, 5}}, S, 2, 6 * 20 + 6, &a, false, comms::G_TRAP);
    CHECK(a && c.why == "harvest-eat");
#endif
    // ... no pearl there: it waits (a step in that eats nothing would pull its tail in)
    c = act({{10, 5}, {9, 5}, {8, 5}}, E, 2, -1, &a);
    CHECK(!a);
    // ... facing west, the portal behind it: not back over its neck
    c = act({{10, 5}, {11, 5}}, W, 2, 5 * 20 + 5, &a);
    CHECK(!a);
    // inside, 4 long, rear 2 outside, a pearl at (6,6): eat on (the late split)
    c = act({{5, 6}, {5, 5}, {10, 5}, {9, 5}}, S, 2, 6 * 20 + 6, &a);
    CHECK(a && !c.split && c.eat && c.dest == 6 * 20 + 6 && c.why == "harvest-eat");
    // ... nothing ahead: split L - 2 = 2, the child's head the old tail outside
    c = act({{5, 6}, {5, 5}, {10, 5}, {9, 5}}, S, 2, -1, &a);
    CHECK(a && c.split && c.n == 2 && c.why == "harvest-split");
    // ... 6 long, its tail outside, nothing ahead: split 4 - the child takes the eaten mass
    c = act({{6, 6}, {5, 6}, {5, 5}, {10, 5}, {9, 5}, {8, 5}}, E, 2, -1, &a);
    CHECK(a && c.split && c.n == (chunk ? 2 : 4));  // mode 2: rear 3 outside - a chunk of 2 first
    // ... 5 long, rear 3 outside, a pearl ahead: mode 1 eats on; mode 2 splits a chunk of 2, keeping a tail outside
    c = act({{5, 6}, {5, 5}, {10, 5}, {9, 5}, {8, 5}}, S, 2, 6 * 20 + 6, &a);
    if (chunk) CHECK(a && c.split && c.n == 2 && c.why == "harvest-chunk");
    else CHECK(a && !c.split && c.eat);
    // ... 4 long just in (head on the landing), rear 3 outside, nothing ahead: split 2 (mode 2: as a chunk)
    c = act({{5, 5}, {10, 5}, {9, 5}, {8, 5}}, E, 2, -1, &a);
    CHECK(a && c.split && c.n == 2);
    // ... 3 long, nothing ahead: L - 2 = 1, no split; it moves
    c = act({{5, 5}, {10, 5}, {9, 5}}, E, 2, -1, &a);
    CHECK(a && !c.split && c.why == "harvest-move");
    // ... at the unit limit: no split
    c = act({{5, 6}, {5, 5}, {10, 5}, {9, 5}}, S, 64, -1, &a);
    CHECK(a && !c.split && c.why == "harvest-move");
    // trapped at (6,6), 4 long, the tail outside: split 2
    c = act({{6, 6}, {5, 6}, {5, 5}, {10, 5}}, E, 2, -1, &a);
    CHECK(a && c.split && c.n == 2);
    // trapped, 3 long, all inside: nothing to do (the parent dies: the harvest's cost)
    c = act({{6, 6}, {5, 6}, {5, 5}}, E, 2, -1, &a);
    CHECK(!a);
    // the 2 x 3 room: 4 long all inside with a free step, nothing ahead - the tail inside, so no split (its child would be born inside)
    c = act({{5, 7}, {5, 6}, {5, 5}, {6, 5}}, S, 2, -1, &a, true);
    CHECK(a && !c.split && c.dest == 7 * 20 + 6);
    // ... 4 long all inside, trapped: the last split of 2
    c = act({{6, 5}, {5, 5}, {5, 6}, {6, 6}}, E, 2, -1, &a, true);
    CHECK(a && c.split && c.n == 2 && c.why == "harvest-last");
    // ... two free steps, a pearl on the second in scan order (south): it takes the pearl
    c = act({{5, 6}, {5, 5}, {10, 5}}, S, 2, 7 * 20 + 5, &a, true);
    CHECK(a && c.eat && c.dest == 7 * 20 + 5);
}
#endif

int main() {
#if BC_HARVEST
    testRooms();
    testEntry();
    testAct();
#endif
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
