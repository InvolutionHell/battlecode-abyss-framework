// Opening food probe (BC_BEDRAID, inactive in the final build): a hungry small dragon in the opening probes an unpaired
// portal.
//   c++ -std=c++20 -O2 -DBC_BEDRAID=1 -I bots/abyss tests/native/bedraid_test.cpp -o bedraid_test && ./bedraid_test
//   (also without the flag: the final build's behaviour, no probe under kSurvivalUnits)
// Policy::planPortals on a synthetic 20 x 20 open board with one unpaired portal end 4 steps east of a 2-long dragon in a
// 2-unit team (a portal-locked start: under kSurvivalUnits, the room open).
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

#if BC_BEDRAID
static constexpr int kRaidStarveTest = kRaidStarve;
#else
static constexpr int kRaidStarveTest = 8;
#endif

struct Scene {
    World w;
    Params p;
};

// a 2-long dragon (id, head (10, 10) facing east), 2 units, round `round`, last eat `eaten`; the east side of (14, 10) is a portal
// end whose partner is unknown
static void setup(Scene& s, int id, int round, int eaten) {
    World& w = s.w;
    Init in;
    in.id = id;
    in.team = 'A';
    in.w = in.h = 20;
    w.start(in);
    w.t.round = round;
    w.t.length = 2;
    w.t.units = 2;
    w.lastEatRound = eaten;
    w.head = w.board.id(10, 10);
    w.body = {w.board.id(10, 10), w.board.id(9, 10)};
    w.occ.assign(w.board.NC, -1);
    w.occHeadId.assign(w.board.NC, -1);
    for (int c = 0; c < w.board.NC; c++) w.seenRound[c] = round;
    Edge& e = w.board.vE[w.board.id(15, 10)];
    e = Edge{};
    e.kind = 2;  // partnerOrient -1: unpaired
    w.board.rebuild();
}

static bool probes(int id, int round, int eaten) {
    Scene s;
    setup(s, id, round, eaten);
    Policy pol(s.w, s.p, 1e18);
    pol.L_ = 2;
    pol.queenMe_ = BC_QUEEN && id < 2;  // decide() sets it before planPortals
    Board const& b = s.w.board;
    pol.myDist_.assign(b.NC, INF);
    for (int c = 0; c < b.NC; c++) {
        int dx = std::abs(b.X(c) - 10), dy = std::abs(b.Y(c) - 10);
        pol.myDist_[c] = std::min(dx, 20 - dx) + std::min(dy, 20 - dy);
    }
    pol.planPortals();
    return pol.prober_ && pol.probeEntry_ == b.id(14, 10) && pol.probeDir_ == E && pol.escaping_;
}

int main() {
    bool on = BC_BEDRAID != 0;
    CHECK(probes(2, 20, 0) == on);                  // hungry 20 rounds in the opening: it probes (switch off: under kSurvivalUnits, no)
    CHECK(!probes(2, 20, 15));                      // ate 5 rounds ago: no
    CHECK(probes(2, 20, 20 - kRaidStarveTest) == on);  // exactly kRaidStarve rounds: yes
    CHECK(!probes(2, 150, 140));                    // after the opening: no (10 rounds hungry, under p2Starve)
    CHECK(probes(2, 100, 90) == on);                // ... and inside it: yes
    CHECK(!probes(0, 20, 0) || !BC_QUEEN);          // never the queen
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
