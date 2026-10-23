// Portal gates (BC_QGATE and BC_DEADPORTAL, both 1 in the final build): World::gatePortals on a synthetic 7x7 board. The
// queen treats a portal as a wall unless its landing is known (seen or mirrored) and roomy - not a 1-tile room, a one-edge
// corridor, a dead end, or a closed room smaller than max(6, L + 2); a seen 1-tile room is a wall for every dragon
// (BC_DEADPORTAL). Only portal slots of the step table change.
//   c++ -std=c++20 -O2 -I bots/abyss tests/native/portal_gate_test.cpp -o portal_gate_test && ./portal_gate_test
#include "world.hpp"

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

// Tile (1,1) steps S through a portal on the top edge of (1,2); its partner is the top edge of (4,4), so the landing is
// n = (4,4) (Board::stepRaw, horizontal partner, d == S). `room` walls n's other three sides: a 1-tile room.
struct Setup {
    World w;
    int c = 0, n = 0, slot = 0;
    Setup(int id, bool room) {
        Init in;
        in.id = id;
        in.team = id & 1 ? 'B' : 'A';
        in.w = in.h = 7;
        w.start(in);
        Board& b = w.board;
        c = b.id(1, 1);
        n = b.id(4, 4);
        slot = c * 4 + S;
        Edge pe;
        pe.kind = 2;
        pe.partnerOrient = 0;
        pe.px = 4;
        pe.py = 4;
        b.hE[b.id(1, 2)] = pe;
        Edge back;
        back.kind = 2;
        back.partnerOrient = 0;
        back.px = 1;
        back.py = 2;
        b.hE[n] = back;
        if (room) {
            Edge kelp;
            kelp.kind = 1;
            b.hE[b.id(4, 5)] = kelp;  // S side of n
            b.vE[n] = kelp;           // W side
            b.vE[b.id(5, 4)] = kelp;  // E side
        }
        w.rebuildGated();
    }
};

static void testDeadRoom() {
    // landing seen, 1-tile room: a wall for the queen and for every dragon (BC_DEADPORTAL)
    for (int id : {0, 1, 5}) {
        Setup s(id, true);
        CHECK(s.w.board.stepRaw(s.c, S) == s.n);
        CHECK(s.w.board.nb[s.slot] == (id < 2 ? -1 : s.n));  // not known yet: an ordinary dragon keeps it, the queen does not
        s.w.seenRound[s.n] = 3;
        s.w.rebuildGated();
        CHECK(s.w.openEdges(s.n) == 0);
        CHECK(s.w.board.nb[s.slot] == -1);
    }
    // the same room known by its mirror only
    Setup m(7, true);
    m.w.mirrored[m.n] = 1;
    m.w.rebuildGated();
    CHECK(m.w.board.nb[m.slot] == -1);
}

static void testUnknownLanding() {
    Setup q(0, false), d(6, false);
    CHECK(q.w.board.nb[q.slot] == -1);  // never seen, no mirror: the queen does not jump blind
    CHECK(d.w.board.nb[d.slot] == d.n);  // an ordinary dragon still may
    // seen later without any edge change: applyGate alone opens it again (idempotent gate)
    q.w.seenRound[q.n] = 9;
    q.w.applyGate();
    CHECK(q.w.board.nb[q.slot] == q.n);
}

static void testOpenLanding() {
    for (int id : {1, 4}) {
        Setup s(id, false);
        s.w.seenRound[s.n] = 2;
        s.w.rebuildGated();
        CHECK(s.w.openEdges(s.n) == 3);
        CHECK(s.w.board.nb[s.slot] == s.n);
    }
    // mirrored counts as seen
    Setup m(0, false);
    m.w.mirrored[m.n] = 1;
    m.w.rebuildGated();
    CHECK(m.w.board.nb[m.slot] == m.n);
}

static void testNarrowAndDeadEnd() {
    // one open edge (a corridor): the queen walls it, others keep it
    for (int id : {0, 8}) {
        Setup s(id, false);
        Edge kelp;
        kelp.kind = 1;
        s.w.board.hE[s.w.board.id(4, 5)] = kelp;
        s.w.board.vE[s.n] = kelp;
        s.w.seenRound[s.n] = 2;
        s.w.rebuildGated();
        CHECK(s.w.openEdges(s.n) == 1);
        CHECK(s.w.board.nb[s.slot] == (id < 2 ? -1 : s.n));
    }
    // a dead-end landing (Board::deadEnd): the queen walls it, others keep it; deadEnd is not recomputed by the gate
    for (int id : {1, 9}) {
        Setup s(id, false);
        s.w.seenRound[s.n] = 2;
        s.w.rebuildGated();
        s.w.board.deadEnd[s.n] = 1;
        s.w.applyGate();
        CHECK(s.w.board.nb[s.slot] == (id < 2 ? -1 : s.n));
        CHECK(s.w.board.deadEnd[s.n] == 1);
    }
}

// A closed block of tiles x 4..5, y 4..4+rows-1 around the landing n = (4,4) (the portal is on n's top edge), every tile seen
// unless `unseen` names one.
static void pocket(Setup& s, int rows, int unseen = -1) {
    Board& b = s.w.board;
    Edge kelp;
    kelp.kind = 1;
    b.hE[b.id(5, 4)] = kelp;                       // top of the block beside the portal
    b.hE[b.id(4, 4 + rows)] = kelp;                // bottom
    b.hE[b.id(5, 4 + rows)] = kelp;
    for (int y = 4; y < 4 + rows; y++) {
        b.vE[b.id(4, y)] = kelp;                   // left
        b.vE[b.id(6, y)] = kelp;                   // right
        for (int x = 4; x <= 5; x++)
            if (b.id(x, y) != unseen) s.w.seenRound[b.id(x, y)] = 1;
    }
    s.w.rebuildGated();
}

static void testSmallPocket() {
    // a closed 2x2 room behind the portal: a wall for the queen (4 < max(6, L+2)), open for others
    for (int id : {0, 7}) {
        Setup s(id, false);
        pocket(s, 2);
        CHECK(s.w.openEdges(s.n) == 2);
        CHECK(s.w.board.nb[s.slot] == (id < 2 ? -1 : s.n));
    }
    // the same room with one tile never seen: not known to be closed, open for her
    Setup u(1, false);
    pocket(u, 2, u.w.board.id(5, 5));
    CHECK(u.w.board.nb[u.slot] == u.n);
}

static void testPocketGrowsWithQueen() {
    // a closed 2x3 room (6 tiles): open for a length-2 queen (limit 6), a wall once she is 5 long (limit 7)
    Setup s(0, false);
    s.w.t.length = 2;
    pocket(s, 3);
    CHECK(s.w.board.nb[s.slot] == s.n);
    s.w.t.length = 5;
    s.w.applyGate();
    CHECK(s.w.board.nb[s.slot] == -1);
    CHECK(s.w.gateLen_ == 5);
}

static void testOtherEdgesUntouched() {
    // the gate only rewrites portal slots: every non-portal slot equals stepRaw
    Setup s(0, true);
    s.w.seenRound[s.n] = 1;
    s.w.rebuildGated();
    Board const& b = s.w.board;
    for (int c = 0; c < b.NC; c++)
        for (int d = 0; d < 4; d++)
            if (b.side(c, d).kind != 2) CHECK(b.nb[c * 4 + d] == b.stepRaw(c, d));
    CHECK(b.hE[b.id(1, 2)].kind == 2 && b.hE[b.id(1, 2)].px == 4);  // the Edge itself is untouched
}

int main() {
    static_assert(BC_QGATE == 1 && BC_DEADPORTAL == 1, "build with both portal gates on (the final build's setting)");
    testDeadRoom();
    testUnknownLanding();
    testOpenLanding();
    testNarrowAndDeadEnd();
    testSmallPocket();
    testPocketGrowsWithQueen();
    testOtherEdgesUntouched();
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
