// Map-network VALUE facts (BC_MAPNET_VALUE, 1 in the final build): a dragon that knows 2+ beds in a heat sector tells the
// team once (the value half of the map network).
//   c++ -std=c++20 -O2 -I bots/abyss tests/native/mapnet_value_test.cpp -o mapnet_value_test && ./mapnet_value_test
// The VALUE fact's encoding (a REGION flagged in aux bit 11), World::noteValue / valueRoom (one entry a heat sector), the
// hearing, the scan of the sectors' known beds, the relay, and Policy::planValue's target, on a synthetic 20 x 20 board.
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

#if BC_MN_VALUE
// dragon `id` (3 long) at (10, 10), round `round`, every tile seen, no beds
static World dragon(int id, int round) {
    World w;
    Init in;
    in.id = id;
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
    for (int c = 0; c < w.board.NC; c++) w.seenRound[c] = round, w.board.bed[c] = 0;
    return w;
}

static void testEncoding() {
    comms::Fact f;
    f.type = comms::REGION;
    f.x = 7, f.y = 3, f.val = 5 | 1 << 3, f.aux = 9 | 1 << 7 | 1 << 11, f.round = 120;
    uint64_t const enc = comms::encode(f, 'A', 120);
    comms::Fact g;
    CHECK(comms::decode(enc, 'A', 121, g));
    CHECK(g.type == comms::REGION && g.x == 7 && g.y == 3 && g.round == 120);
    CHECK((g.aux >> 11 & 1) && (g.aux & 15) == 9 && (g.aux >> 7 & 1));
}

static void testTable() {
    World w = dragon(6, 100);
    Board const& b = w.board;
    int const a = b.id(2, 2), a2 = b.id(3, 2);  // the same heat sector
    CHECK(w.heatOf(a) == w.heatOf(a2));
    w.noteValue(a, 4, 0, false, 90);
    CHECK(w.valueRooms.size() == 1 && w.valueRoom(a2) && w.valueRoom(a2)->beds == 4);
    w.noteValue(a2, 6, 0, true, 95);                       // newer: replaces, the anchor too
    CHECK(w.valueRooms.size() == 1 && w.valueRooms[0].anchor == a2 && w.valueRooms[0].beds == 6 && w.valueRooms[0].held);
    w.noteValue(a, 2, 0, false, 80);                       // older: ignored
    CHECK(w.valueRooms[0].beds == 6 && w.valueRooms[0].round == 95);
    w.noteValue(b.id(15, 15), 3, 0, false, 99);            // another sector
    CHECK(w.valueRooms.size() == 2);
}

static void testHear() {
    World w = dragon(6, 100);
    comms::Fact v;
    v.type = comms::REGION;
    v.x = 15, v.y = 15, v.val = 5 | 1 << 3, v.aux = 7 | 1 << 11, v.round = 98;
    w.hearMapnet(v, w.board.id(15, 15));
    CHECK(w.valueRooms.size() == 1 && w.valueRooms[0].beds == 7 && !w.valueRooms[0].held && w.valueRooms[0].round == 98);
    comms::Fact r = v;  // a plain REGION (no flag): not a VALUE
    r.x = 2, r.y = 2, r.aux = 7;
    w.hearMapnet(r, w.board.id(2, 2));
    CHECK(w.valueRooms.size() == 1);
}

static void testScan() {
    // 3 beds in one sector, 1 in another: the first goes out once, anchored at its bed nearest the sector's centre
    World w = dragon(6, 100);
    Board& b = w.board;
    for (int x : {1, 2, 3}) b.bed[b.id(x, 1)] = 1;
    b.bed[b.id(15, 15)] = 1;
    w.observeMapnet();
    CHECK(w.valueRound_ == 100 && w.valueRooms.size() == 1 && w.valueRooms[0].beds == 3);
    CHECK(w.valueFact_.type == comms::REGION && (w.valueFact_.aux >> 11 & 1) && (w.valueFact_.aux & 15) == 3);
    CHECK(w.heatOf(w.valueRooms[0].anchor) == w.heatOf(b.id(1, 1)));
    w.t.round = 102;
    b.bed[b.id(15, 16)] = 1;                               // a second bed in (15, 15)'s sector: now worth telling
    w.observeMapnet();                                     // within kValueEvery: no scan
    CHECK(w.valueRound_ == 100 && w.valueRooms.size() == 1);
    w.t.round = 103;
    w.observeMapnet();                                     // ... the next scan is due at r104
    CHECK(w.valueRound_ == 100);
    w.t.round = 104;
    w.observeMapnet();                                     // the scan: the new sector goes out
    CHECK(w.valueRound_ == 104 && w.valueRooms.size() == 2);
    w.t.round = 108;
    w.observeMapnet();                                     // known already: nothing new
    CHECK(w.valueRound_ == 104 && w.valueRooms.size() == 2);
    // a mirrored (never seen) bed counts; held: our head in that sector
    World m = dragon(6, 100);
    Board& mb = m.board;
    mb.bed[mb.id(11, 11)] = mb.bed[mb.id(10, 12)] = 1;  // both in her own sector (2, 2)
    m.seenRound[mb.id(10, 12)] = -1, m.mirrored[mb.id(10, 12)] = 1;
    m.observeMapnet();
    CHECK(m.valueRooms.size() == 1 && m.valueRooms[0].beds == 2 && m.valueRooms[0].held);
}

static void testRelay() {
    World w = dragon(6, 300);
    w.noteValue(w.board.id(2, 2), 4, 0, false, 290);       // fresh
    w.noteValue(w.board.id(15, 15), 5, 0, false, 150);     // older than kValueAge
    std::vector<comms::Fact> out;
    int sent = 0, fresh = 0;
    for (int r = 300; r < 310; r++) {
        w.t.round = r;
        out.clear();
        if (w.tellValueRelay(out)) {
            sent++;
            fresh += out[0].x == 2 && out[0].y == 2 && (out[0].aux >> 11 & 1) && out[0].round == 290;
        }
    }
    CHECK(sent == 10 && fresh == 10);                      // only the fresh entry
    World e = dragon(6, 300);
    std::vector<comms::Fact> none;
    CHECK(!e.tellValueRelay(none) && none.empty());
}

// planValue with an open-board path field from (10, 10)
static int target(World& w, bool queen = false) {
    Params p;
    Policy pol(w, p, 1e18);
    pol.L_ = 3;
    pol.queenMe_ = queen;
    Board const& b = w.board;
    pol.myDist_.assign(b.NC, INF);
    for (int c = 0; c < b.NC; c++) {
        int dx = std::abs(b.X(c) - 10), dy = std::abs(b.Y(c) - 10);
        pol.myDist_[c] = std::min(dx, 20 - dx) + std::min(dy, 20 - dy);
    }
    pol.planValue();
    return pol.valueTarget_;
}

static void testTarget() {
    World w = dragon(6, 200);
    Board const& b = w.board;
    int const near = b.id(14, 10), far = b.id(2, 2);
    w.noteValue(near, 4, 0, false, 190);
    w.noteValue(far, 4, 0, false, 190);
    CHECK(target(w) == near);                              // equal beds: the nearer
    World h = dragon(6, 200);
    h.noteValue(near, 4, 0, true, 190);                    // held by the fact (fresh)
    h.noteValue(far, 4, 0, false, 190);
    CHECK(target(h) == far);
    h.valueRooms[0].round = 200 - kValueHeldAge - 1;       // ... its held bit stale
    CHECK(target(h) == near);
    World s = dragon(6, 200);
    s.noteValue(near, 4, 0, false, 190);
    s.noteValue(far, 4, 0, false, 190);
    s.others = {{20, 'A', s.board.id(15, 11), 0, 3}};     // one of ours there (seen)
    CHECK(target(s) == far);
    World own = dragon(6, 200);
    own.noteValue(own.board.id(10, 13), 6, 0, false, 190);  // in our own sector, 3 steps: no pull
    CHECK(w.heatOf(own.board.id(10, 13)) == w.heatOf(own.head) && target(own) == -1);
#if BC_MAPNET
    World dz = dragon(6, 200);
    dz.noteValue(near, 4, 0, false, 190);
    dz.noteValue(far, 4, 0, false, 190);
    dz.mn.init(dz.board.NC);
    dz.mn.noteDanger(near, 199);                           // a dangerous area: the other one
    CHECK(dz.danger(near) > 0.5 && target(dz) == far);
#endif
    World q = dragon(0, 200);
    q.noteValue(near, 4, 0, false, 190);
    CHECK(target(q, true) == -1 || !BC_QUEEN);             // never the queen
    World big = dragon(6, 200);
    big.noteValue(near, 2, 0, false, 190);
    big.noteValue(big.board.id(10, 15), 6, 0, false, 190);  // 5 away, 6 beds: worth more than 2 at 4
    CHECK(target(big) == big.board.id(10, 15));
}
#endif

int main() {
#if BC_MN_VALUE
    testEncoding();
    testTable();
    testHear();
    testScan();
    testRelay();
    testTarget();
#endif
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
