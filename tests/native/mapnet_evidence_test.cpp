// Map-network evidence grading (BC_MAPNET_EVIDENCE, 2 in the final build): an inferred TRAP gate needs evidence.
//   c++ -std=c++20 -O2 -I bots/abyss tests/native/mapnet_evidence_test.cpp -o mapnet_evidence_test && ./mapnet_evidence_test
// Mode 2 judges the prober's silence over 12 rounds (RegionMap::kProbeWait); ablation.hpp sets the switch unconditionally.
// RegionMap::tick with the evidence test, the 60-round TRAP age, the prober's id on a PROBING (sent and heard),
// World::trapEvidence (the prober heard or seen after the probe; the landing in a known bed room) and the bed-room CLEARED
// relay first, on a synthetic 20 x 20 board: the west side of (5, 5) is a portal landing on (1, 1), a 1-tile room unless
// its south side is open into the corridor (1, 2)..(1, 4) (a bed at (1, 3) when asked); the landing unseen unless asked.
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

#if BC_MN_EVIDENCE
static Edge kelp() {
    Edge e;
    e.kind = 1;
    return e;
}

struct Scene {
    bool roomOpen = false;     // (1, 1)'s south side open into the corridor (1, 2)..(1, 4)
    bool bed = false;          // a bed at (1, 3)
    bool landingSeen = false;  // a seen 1-tile landing is DEAD already (World::gatePortals): inference is for the unseen ones
};

static World world(Scene const& sc, int round = 50) {
    World w;
    Init in;
    in.id = 4;
    in.team = 'A';
    in.w = in.h = 20;
    w.start(in);
    w.t.round = round;
    w.t.length = 3;
    w.t.units = 6;
    w.head = w.board.id(10, 10);
    w.body = {w.head, w.board.id(9, 10), w.board.id(8, 10)};
    w.occ.assign(w.board.NC, -1);
    w.occHeadId.assign(w.board.NC, -1);
    Board& b = w.board;
    for (int c = 0; c < b.NC; c++) w.seenRound[c] = round, b.bed[c] = 0;
    Edge pe;  // (5, 5)'s west side, landing on (1, 1)
    pe.kind = 2, pe.partnerOrient = 1, pe.px = 2, pe.py = 1;
    b.vE[b.id(5, 5)] = pe;
    Edge pr;  // (1, 1)'s east side, landing back on (5, 5)
    pr.kind = 2, pr.partnerOrient = 1, pr.px = 5, pr.py = 5;
    b.vE[b.id(2, 1)] = pr;
    b.hE[b.id(1, 1)] = kelp(), b.vE[b.id(1, 1)] = kelp();          // the room: north, west
    if (!sc.roomOpen) b.hE[b.id(1, 2)] = kelp();                      // ... south
    for (int y = 2; y <= 4; y++) b.vE[b.id(1, y)] = kelp(), b.vE[b.id(2, y)] = kelp();  // the corridor's sides
    b.hE[b.id(1, 5)] = kelp();                                        // ... its end
    if (sc.bed) b.bed[b.id(1, 3)] = 1;
    if (!sc.landingSeen)
        for (int y = 1; y <= 4; y++) w.seenRound[b.id(1, y)] = -1;
    w.rebuildGated();
    w.mn.init(b.NC);
    return w;
}

static int slot(World const& w) { return w.board.id(5, 5) * 4 + W; }

static void testTick() {
    World w = world({});
    int const k = slot(w);
    CHECK(w.setGate(w.board.id(5, 5), W, comms::G_PROBING, 50, -1, 7) && w.mn.gateProber[k] == 7);
    int const due = 50 + RegionMap::kProbeWait + 1;          // the first round past the wait
    CHECK(RegionMap::kProbeWait == (BC_MAPNET_EVIDENCE >= 2 ? 12 : 6));
    CHECK(!w.mn.tick(due - 1, [](int) { return true; }) && w.mn.gate[k] == comms::G_PROBING);  // within kProbeWait
    CHECK(w.mn.tick(due, [](int) { return true; }) && w.mn.gate[k] == comms::G_TRAP);          // timed out, earned
    CHECK(!w.mn.tick(due + RegionMap::kTrapAge, [](int) { return true; }) && w.mn.gate[k] == comms::G_TRAP);
    CHECK(w.mn.tick(due + 1 + RegionMap::kTrapAge, [](int) { return true; }) && w.mn.gate[k] == comms::G_UNKNOWN);  // forgotten after kTrapAge
    CHECK(RegionMap::kTrapAge == 60);
    World l = world({});
    l.setGate(l.board.id(5, 5), W, comms::G_PROBING, 50, -1, 7);
    CHECK(l.mn.tick(due, [](int) { return false; }) && l.mn.gate[k] == comms::G_UNKNOWN && l.mn.gateList.empty());  // lapsed
}

static void testEvidence() {
    int const r0 = 50;
    World w = world({});
    int const k = slot(w);
    w.setGate(w.board.id(5, 5), W, comms::G_PROBING, r0, -1, 7);
    CHECK(w.trapEvidence(k));                                  // a 1-tile room, the prober silent
    w.know.friends = {{w.board.id(3, 3), 3, 7, r0 - 1}};
    CHECK(w.trapEvidence(k));                                  // heard before the probe only
    w.know.friends = {{w.board.id(3, 3), 3, 7, r0}};
    CHECK(w.trapEvidence(k));                                  // ... or in its own crossing turn (its SELF fact of that round)
    w.know.friends = {{w.board.id(3, 3), 3, 7, r0 + 2}};
    CHECK(!w.trapEvidence(k));                                 // heard after it: it lived or turned back
    w.know.friends = {{w.board.id(3, 3), 3, 8, r0 + 2}};
    CHECK(w.trapEvidence(k));                                  // another teammate
    World bed = world({true, true, true});
    bed.setGate(bed.board.id(5, 5), W, comms::G_PROBING, r0, -1, 7);
    CHECK(!bed.trapEvidence(k));                               // the landing's room holds a known bed
    World open = world({true, false, true});
    open.setGate(open.board.id(5, 5), W, comms::G_PROBING, r0, -1, 7);
    CHECK(open.trapEvidence(k));                               // a room, no bed known: still a TRAP (only a known bed room counts)
    World fog = world({true, true, false});
    fog.setGate(fog.board.id(5, 5), W, comms::G_PROBING, r0, -1, 7);
    CHECK(fog.trapEvidence(k));                                // the landing unseen: nothing known against it
    World anon = world({});
    anon.setGate(anon.board.id(5, 5), W, comms::G_PROBING, r0, -1, -1);
    anon.know.friends = {{anon.board.id(3, 3), 3, 7, r0 + 2}};
    CHECK(anon.trapEvidence(k));                               // no prober id: the silence test cannot apply
}

static void testWire() {
    // a PROBING heard with its prober's id in aux
    World w = world({});
    comms::Fact f;
    f.type = comms::GATE;
    f.x = 5, f.y = 5, f.val = W | comms::G_PROBING << 2, f.aux = 7, f.round = 50;
    w.hearMapnet(f, w.board.id(5, 5));
    CHECK(w.mn.gate[slot(w)] == comms::G_PROBING && w.mn.gateProber[slot(w)] == 7);
    // ... and sent: our own crossing turn's PROBING carries our id
    World me = world({});
    me.announceCell = me.board.id(5, 5), me.announceDir = W, me.announceRound = 50, me.announceState = comms::G_PROBING;
    std::vector<comms::Fact> out;
    me.tellMapnetUrgent(out);
    CHECK(!out.empty() && out[0].type == comms::GATE && out[0].aux == 4);
    me.announceState = comms::G_CLAIM;
    out.clear();
    me.tellMapnetUrgent(out);
    CHECK(!out.empty() && out[0].aux == 0);                    // a CLAIM: no id
    // the whole turn: heard at r50, the prober silent -> TRAP once the wait is over; heard of at r53 -> lapsed
    int const due = 50 + RegionMap::kProbeWait + 1;
    World s = world({});
    s.hearMapnet(f, s.board.id(5, 5));
    s.t.round = due;
    s.observeMapnet();
    CHECK(s.mn.gate[slot(s)] == comms::G_TRAP);
    World h = world({});
    h.hearMapnet(f, h.board.id(5, 5));
    h.know.friends = {{h.board.id(3, 3), 3, 7, 53}};
    h.t.round = due;
    h.observeMapnet();
    CHECK(h.mn.gate[slot(h)] == comms::G_UNKNOWN);
}

static void testRelay() {
    // a CLEARED gate into the bed room and three DEAD gates elsewhere: the bed one on alternate relay turns
    World w = world({true, true, true});
    Board const& b = w.board;
    w.setGate(b.id(5, 5), W, comms::G_CLEARED, 40, b.id(1, 1));
    for (int x : {12, 14, 16}) w.setGate(b.id(x, 12), N, comms::G_DEAD, 41);
    w.observeMapnet();
    CHECK(w.bedGates_.size() == 1 && w.bedGates_[0] == slot(w));
    int bedSent = 0, deadSent = 0;
    for (int i = 0; i < 6; i++) {
        w.t.round = 50 + i;  // the rotation moves with the round
        std::vector<comms::Fact> out;
        CHECK(w.tellMapnetRelay(out) && out.size() == 1);
        bool const bedOne = out[0].x == 5 && out[0].y == 5 && (out[0].val >> 2) == comms::G_CLEARED;
        bedSent += bedOne;
        deadSent += (out[0].val >> 2) == comms::G_DEAD;
    }
    CHECK(bedSent >= 3 && deadSent >= 2);                     // the bed gate every other turn at least; the DEAD ones still go out
    World nb = world({true, false, true});                          // no bed in that room: not listed
    nb.setGate(nb.board.id(5, 5), W, comms::G_CLEARED, 40, nb.board.id(1, 1));
    nb.observeMapnet();
    CHECK(nb.bedGates_.empty());
}
#endif

int main() {
#if BC_MN_EVIDENCE
    testTick();
    testEvidence();
    testWire();
    testRelay();
#endif
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
