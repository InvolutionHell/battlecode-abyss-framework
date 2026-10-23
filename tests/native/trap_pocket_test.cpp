// Trap pocket (BC_TRAPPOCKET, inactive in the final build): a known tile whose only open side is its way in and whose
// other door is a portal into a dead room is a wall for a dragon of 2+.
//   c++ -std=c++20 -O2 -DBC_TRAPPOCKET=1 -I bots/abyss tests/native/trap_pocket_test.cpp -o trap_pocket_test && ./trap_pocket_test
// Policy::trapPocket and its effect on Policy::buildBase, on a synthetic 20 x 20 board: the pocket (5,5), walled north and
// south, open east; its west side a portal into the 1-tile room (1,1).
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

#if BC_TRAPPOCKET
static Edge kelp() {
    Edge e;
    e.kind = 1;
    return e;
}

struct Pocket {
    bool roomOpen = false;     // the room's south side open: no 1-tile room
    bool pocketOpen = false;   // the pocket's north side open: two ways out
    bool landingUnseen = false;
    bool pocketUnseen = false;
    bool northPortal = false;  // the pocket's north side a portal on to the open board ((10, 11)) instead of a wall
    bool noPortal = false;     // the pocket's west side a wall: a plain dead end
    int gate = 0;              // a gate state on the pocket's portal side
};

static World world(Pocket const& pk) {
    World w;
    Init in;
    in.id = 4;
    in.team = 'A';
    in.w = in.h = 20;
    w.start(in);
    w.t.round = 50;
    w.t.length = 2;
    w.t.units = 4;
    w.head = w.board.id(10, 10);
    w.body = {w.head, w.board.id(9, 10)};
    w.occ.assign(w.board.NC, -1);
    w.occHeadId.assign(w.board.NC, -1);
    for (int c = 0; c < w.board.NC; c++) w.seenRound[c] = 50;
    Board& b = w.board;
    Edge pe;  // the pocket's west side, landing on (1, 1)
    pe.kind = 2, pe.partnerOrient = 1, pe.px = 2, pe.py = 1;
    b.vE[b.id(5, 5)] = pe;
    Edge pr;  // the room's east side, landing back on (5, 5)
    pr.kind = 2, pr.partnerOrient = 1, pr.px = 5, pr.py = 5;
    b.vE[b.id(2, 1)] = pr;
    b.hE[b.id(1, 1)] = kelp(), b.vE[b.id(1, 1)] = kelp();      // the room: north, west
    if (!pk.roomOpen) b.hE[b.id(1, 2)] = kelp();                  // ... south
    if (pk.northPortal) {
        Edge np;  // the pocket's north side, landing on (10, 11)
        np.kind = 2, np.partnerOrient = 0, np.px = 10, np.py = 12;
        b.hE[b.id(5, 5)] = np;
    } else if (!pk.pocketOpen) {
        b.hE[b.id(5, 5)] = kelp();                                // the pocket: north
    }
    if (pk.noPortal) b.vE[b.id(5, 5)] = kelp();
    if (pk.pocketUnseen) w.seenRound[b.id(5, 5)] = -1;
    b.hE[b.id(5, 6)] = kelp();                                    // ... south
    if (pk.landingUnseen) w.seenRound[b.id(1, 1)] = -1;
    w.rebuildGated();
#if BC_MAPNET
    if (pk.gate) w.setGate(b.id(5, 5), W, pk.gate, 50);
#endif
    return w;
}

static bool pocketAt(Pocket const& pk, int x = 5, int y = 5) {
    World w = world(pk);
    Params p;
    Policy pol(w, p, 1e18);
    pol.L_ = 2;
    return pol.trapPocket(w.board.id(x, y));
}

static void testPocket() {
    CHECK(pocketAt({}));                                // one open side, its portal into a known 1-tile room
    CHECK(!pocketAt({}, 6, 5));                         // the tile before it: open
    CHECK(!pocketAt({}, 1, 1));                         // the room itself: no open side
    Pocket o;
    o.roomOpen = true;
    CHECK(!pocketAt(o));                                // the room opens south: a way on
    Pocket two;
    two.pocketOpen = true;
    CHECK(!pocketAt(two));                              // the pocket has two open sides
    Pocket np;
    np.northPortal = true;
    CHECK(!pocketAt(np));                               // a second portal on to the board: a way out
    Pocket dead;
    dead.noPortal = true;
    CHECK(!pocketAt(dead));                             // a plain dead end (no trap door): not a trap pocket
    Pocket hid;
    hid.pocketUnseen = true;
    CHECK(!pocketAt(hid));                              // the pocket tile itself never seen
    Pocket un;
    un.landingUnseen = true;
    CHECK(!pocketAt(un));                               // the landing never seen: not known to be a trap
#if BC_MAPNET
    un.gate = comms::G_DEAD;
    CHECK(pocketAt(un));                                // ... but the door is a DEAD gate (heard)
    un.gate = comms::G_CLEARED;
    CHECK(!pocketAt(un));
#endif
}

// buildBase: the pocket is a wall for a dragon of 2+, not for a 1-long one (it can turn round)
static void testBase() {
    for (int len : {1, 2, 3}) {
        World w = world({});
        w.t.length = len;
        if (len == 1) w.body = {w.head};
        Params p;
        Policy pol(w, p, 1e18);
        pol.L_ = len;
        pol.buildBase();
        CHECK((pol.base_[w.board.id(5, 5)] >= INF) == (len >= 2));
        CHECK(pol.base_[w.board.id(6, 5)] < INF);
    }
}
#endif

int main() {
#if BC_TRAPPOCKET
    testPocket();
    testBase();
#endif
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
