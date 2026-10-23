// Map network (BC_MAPNET, 1 in the final build): GATE / REGION / TRAPSET codec, the gate state machine and its timeouts,
// mirroring, pairing a portal from a CLEARED landing, DEAD / TRAP gates as walls, the queen's bed-room allowance and the
// relay. The timeouts are read from RegionMap (kProbeWait, kTrapAge), which depend on BC_MAPNET_EVIDENCE (2 in the final
// build: a PROBING waits 12 rounds; the evidence test itself is in mapnet_evidence_test.cpp).
//   c++ -std=c++20 -O2 -I bots/abyss tests/native/mapnet_test.cpp -o mapnet_test && ./mapnet_test
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

static void testCodec() {
    for (int type : {comms::GATE, comms::REGION})
        for (int x : {0, 1, 63})
            for (int y : {0, 37, 63})
                for (int val : {0, 5, 31})
                    for (int aux : {0, 1234, 4095})
                        for (int round : {0, 255, 499}) {
                            comms::Fact f;
                            f.type = type, f.x = x, f.y = y, f.val = val, f.aux = aux, f.round = round;
                            uint64_t m = comms::encode(f, 'A', round);
                            comms::Fact g;
                            CHECK(comms::decode(m, 'A', round, g));
                            CHECK(g.type == type && g.x == x && g.y == y && g.val == val && g.aux == aux && g.round == round);
                            comms::Fact h;
                            CHECK(!comms::decode(m, 'B', round, h) || h.type != type);  // the other team's key rejects it
                        }
    // a plain SYM fact still decodes as SYM (ext 0)
    comms::Fact s;
    s.type = comms::SYM, s.sub = 0, s.val = 5;
    comms::Fact g;
    CHECK(comms::decode(comms::encode(s, 'A', 10), 'A', 10, g) && g.type == comms::SYM && g.val == 5);
}

static World fresh(int id, int w = 8, int h = 8) {
    World wd;
    Init in;
    in.id = id;
    in.team = id & 1 ? 'B' : 'A';
    in.w = w, in.h = h;
    wd.start(in);
    wd.t.round = 10;
    return wd;
}

static void testStates() {
    World w = fresh(5);
    int c = w.board.id(2, 2);
    CHECK(w.portalState(c, N) == comms::G_UNKNOWN);
    CHECK(w.setGate(c, N, comms::G_PROBING, 10));
    CHECK(w.portalState(c, N) == comms::G_PROBING);
    w.t.round = 10 + RegionMap::kProbeWait;
    w.observeMapnet();                       // kProbeWait rounds: still waiting
    CHECK(w.portalState(c, N) == comms::G_PROBING);
    w.t.round = 10 + RegionMap::kProbeWait + 1;
    w.observeMapnet();                       // one more without CLEARED (and, with evidence grading, nothing against it): a trap
    CHECK(w.portalState(c, N) == comms::G_TRAP);
    CHECK(w.setGate(c, N, comms::G_CLEARED, w.t.round + 1, w.board.id(5, 5)));  // a late CLEARED wins
    CHECK(w.portalState(c, N) == comms::G_CLEARED);
    CHECK(!w.setGate(c, N, comms::G_TRAP, 30));                       // and stays
    // an inferred TRAP is forgotten after kTrapAge
    int c2 = w.board.id(3, 3);
    w.setGate(c2, E, comms::G_TRAP, 20);
    w.t.round = 20 + RegionMap::kTrapAge + 1;
    w.observeMapnet();
    CHECK(w.portalState(c2, E) == comms::G_UNKNOWN);
    // DEAD is permanent
    w.setGate(c2, W, comms::G_DEAD, 20);
    w.t.round = 900;
    w.observeMapnet();
    CHECK(w.portalState(c2, W) == comms::G_DEAD);
}

static void testClaim() {
    // a CLAIM (the prober two steps out) holds the portal, then lapses: it never turns into a TRAP
    World w = fresh(5);
    int c = w.board.id(4, 2);
    w.t.round = 50;
    CHECK(w.setGate(c, S, comms::G_CLAIM, 50));
    w.t.round = 58;
    w.observeMapnet();
    CHECK(w.portalState(c, S) == comms::G_CLAIM);
    w.t.round = 61;
    w.observeMapnet();
    CHECK(w.portalState(c, S) == comms::G_UNKNOWN);
    // a PROBING outranks a CLAIM and does infer
    w.setGate(c, S, comms::G_CLAIM, 61);
    CHECK(w.setGate(c, S, comms::G_PROBING, 62));
    w.t.round = 62 + RegionMap::kProbeWait + 1;
    w.observeMapnet();
    CHECK(w.portalState(c, S) == comms::G_TRAP);
    // a CLAIM never downgrades a known state
    CHECK(!w.setGate(c, S, comms::G_CLAIM, w.t.round + 1));
}

static void testSeenDead() {
    // a paired portal whose landing we saw as a 1-tile room is recorded DEAD (and so relayed to the team)
    World w = fresh(8);
    Board& b = w.board;
    w.pairByLanding(b.id(1, 1), S, b.id(4, 4));
    Edge kelp;
    kelp.kind = 1;
    b.hE[b.id(4, 5)] = kelp, b.vE[b.id(4, 4)] = kelp, b.vE[b.id(5, 4)] = kelp;
    w.seenRound[b.id(4, 4)] = 9;
    w.rebuildGated();
    CHECK(w.portalState(b.id(1, 1), S) == comms::G_DEAD);
    std::vector<comms::Fact> r;
    CHECK(w.tellMapnetRelay(r) && r.size() == 1 && (r[0].val >> 2) == comms::G_DEAD && r[0].x == 1 && r[0].y == 1);
}

static void testMirror() {
    World w = fresh(4);
    w.board.sym = 3;  // point symmetry: (x, y) -> (W-1-x, H-1-y), N <-> S, E <-> W
    int c = w.board.id(1, 2);
    w.setGate(c, N, comms::G_DEAD, 12);
    CHECK(w.portalState(w.board.id(6, 5), S) == comms::G_DEAD);
    w.board.sym = 1;  // top-bottom: N <-> S only
    int d = w.board.id(2, 1);
    w.setGate(d, E, comms::G_TRAP, 12);
    CHECK(w.portalState(w.board.id(2, 6), E) == comms::G_TRAP);
}

static void testPairByLanding() {
    // stepping S from (1,1) through the portal on the top edge of (1,2) lands on (5,4): after a CLEARED the step is paired
    World w = fresh(6);
    Board& b = w.board;
    Edge pe;
    pe.kind = 2;
    b.hE[b.id(1, 2)] = pe;
    b.rebuild();
    int c = b.id(1, 1);
    CHECK(b.stepRaw(c, S) == -1);
    comms::Fact f;
    f.type = comms::GATE, f.x = 1, f.y = 1, f.val = S | comms::G_CLEARED << 2, f.aux = 5 | 4 << 6, f.round = 10;
    w.hearMapnet(f, c);
    CHECK(b.stepRaw(c, S) == b.id(5, 4));
    CHECK(b.nb[c * 4 + S] == b.id(5, 4));
    CHECK(w.portalState(c, S) == comms::G_CLEARED);
    // and the way back from the landing leads to (1,1)
    CHECK(b.stepRaw(b.id(5, 4), N) == c);
    // the same for a vertical portal, stepping W from (6,6) to land on (2,3)
    World v = fresh(7);
    Edge pv;
    pv.kind = 2;
    v.board.vE[v.board.id(6, 6)] = pv;
    comms::Fact g;
    g.type = comms::GATE, g.x = 6, g.y = 6, g.val = W | comms::G_CLEARED << 2, g.aux = 2 | 3 << 6, g.round = 10;
    v.hearMapnet(g, v.board.id(6, 6));
    CHECK(v.board.stepRaw(v.board.id(6, 6), W) == v.board.id(2, 3));
    CHECK(v.board.stepRaw(v.board.id(2, 3), E) == v.board.id(6, 6));
}

static void testWallsAndAllowance() {
    // a paired portal: (1,1) S -> landing (5,4); a DEAD / TRAP gate makes it a wall for an ordinary dragon
    World w = fresh(9);
    Board& b = w.board;
    w.pairByLanding(b.id(1, 1), S, b.id(5, 4));
    for (int c = 0; c < b.NC; c++) w.seenRound[c] = 5;
    w.rebuildGated();
    int slot = b.id(1, 1) * 4 + S;
    CHECK(b.nb[slot] == b.id(5, 4));
    w.setGate(b.id(1, 1), S, comms::G_TRAP, 11);
    w.applyGate();
    CHECK(b.nb[slot] == -1);

    // the queen: a closed 2x2 room with two beds behind the portal; walled at length 6 (pocket rule), allowed at length 3
    World q = fresh(0);
    Board& qb = q.board;
    q.pairByLanding(qb.id(1, 1), S, qb.id(4, 4));
    Edge kelp;
    kelp.kind = 1;
    qb.hE[qb.id(5, 4)] = kelp;
    qb.hE[qb.id(4, 6)] = kelp, qb.hE[qb.id(5, 6)] = kelp;
    qb.vE[qb.id(4, 4)] = kelp, qb.vE[qb.id(4, 5)] = kelp;
    qb.vE[qb.id(6, 4)] = kelp, qb.vE[qb.id(6, 5)] = kelp;
    for (int c = 0; c < qb.NC; c++) q.seenRound[c] = 5;
    qb.bed[qb.id(4, 5)] = 1, qb.bed[qb.id(5, 5)] = 1;
    int qslot = qb.id(1, 1) * 4 + S;
    q.t.length = 3;
    q.rebuildGated();
    CHECK(qb.nb[qslot] == qb.id(4, 4));     // 4 tiles >= L + 1, 2 beds: she may go in
    q.t.length = 6;
    q.rebuildGated();
    CHECK(qb.nb[qslot] == -1);              // 4 < L + 1: a wall
    qb.bed[qb.id(5, 5)] = 0;
    q.t.length = 3;
    q.rebuildGated();
    CHECK(qb.nb[qslot] == -1);              // one bed only: the pocket rule holds
}

static void testTrapset() {
    // codec: every bit of the 38-bit mask survives; a SYM fact is untouched
    for (uint64_t m : {1ull, 1ull << 37, (1ull << 38) - 1, 0x2AAAAAAAAAull & ((1ull << 38) - 1)}) {
        comms::Fact f;
        f.type = comms::TRAPSET, f.bits = m;
        comms::Fact g;
        CHECK(comms::decode(comms::encode(f, 'B', 99), 'B', 99, g) && g.type == comms::TRAPSET && g.bits == m);
    }
    // a portal end known by id: the step from (2,2) north crosses the top edge of (2,2), portal id 7
    World w = fresh(6);
    w.board.hE[w.board.id(2, 2)].kind = 2;
    w.notePortal(7, 0, 2, 2);
    int c = w.board.id(2, 2);
    CHECK(w.pidOfStep(c, N) == 7);
    CHECK(!w.trapStep(c, N));
    comms::Fact t;
    t.type = comms::TRAPSET, t.bits = 1ull << 7, t.round = 10;
    w.hearMapnet(t, 0);
    CHECK(w.trapStep(c, N));                 // heard: a trap by its id
    CHECK(!w.trapStep(w.board.id(3, 3), N)); // another step: not
    // our own DEAD gate puts its portal id in the mask we send
    World v = fresh(4);
    v.board.vE[v.board.id(5, 1)].kind = 2;
    v.notePortal(12, 1, 5, 1);               // the left edge of (5,1): the step from (4,1) east
    v.setGate(v.board.id(4, 1), E, comms::G_DEAD, 10);
    v.observeMapnet();
    CHECK(v.trapMaskNow_ == 1ull << 12);
    std::vector<comms::Fact> out;
    v.t.round = 10;
    v.tellMapnetUrgent(out);
    CHECK(!out.empty() && out.back().type == comms::TRAPSET && out.back().bits == 1ull << 12);
    // a CLAIM never enters the mask
    v.setGate(v.board.id(4, 1), W, comms::G_CLAIM, 10);
    v.observeMapnet();
    CHECK(v.trapMaskNow_ == 1ull << 12);
}

static void testTell() {
    World w = fresh(3);
    w.t.round = 40;
    w.announceCell = w.board.id(2, 3), w.announceDir = E, w.announceRound = 40, w.announceState = comms::G_PROBING;
    std::vector<comms::Fact> out;
    w.tellMapnetUrgent(out);
    CHECK(out.size() == 1 && out[0].type == comms::GATE && out[0].x == 2 && out[0].y == 3 &&
          (out[0].val & 3) == E && (out[0].val >> 2) == comms::G_PROBING);
    // the relay slot sends known states but never a PROBING or a CLAIM
    std::vector<comms::Fact> r;
    w.setGate(w.board.id(4, 4), N, comms::G_PROBING, 40);
    w.setGate(w.board.id(4, 6), N, comms::G_CLAIM, 40);
    CHECK(!w.tellMapnetRelay(r) && r.empty());
    w.setGate(w.board.id(4, 5), N, comms::G_DEAD, 40);
    CHECK(w.tellMapnetRelay(r) && r.size() == 1 && (r[0].val >> 2) == comms::G_DEAD);
}

int main() {
    static_assert(BC_MAPNET == 1, "build with BC_MAPNET on (the final build's setting)");
    testCodec();
    testStates();
    testClaim();
    testSeenDead();
    testMirror();
    testPairByLanding();
    testWallsAndAllowance();
    testTrapset();
    testTell();
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
