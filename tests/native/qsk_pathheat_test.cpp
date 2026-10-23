// QSK path heat (BC_QSK_PATHHEAT, inactive in the final build): QSK's "enemies about" test by path, and portal mouths as
// threat sources only then.
//   c++ -std=c++20 -O2 -DBC_QSK_PATHHEAT=1 -I bots/abyss tests/native/qsk_pathheat_test.cpp -o qsk_pathheat_test && ./qsk_pathheat_test
// Qsk::threats on a synthetic 20 x 20 board: our queen (4 long) at (10, 10), a paired portal on the east side of (12, 10)
// landing on (3, 3), every tile seen.
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

#if BC_QSK_PATHHEAT
static Edge kelp() {
    Edge e;
    e.kind = 1;
    return e;
}

struct Case {
    int heard = -1;          // an enemy head heard at this tile this round (-1 none)
    int heardAge = 0;        // ... that many rounds ago
    bool wall = false;       // a wall row y = 13 from x = 0 to 19: (10, 15) is 5 by Chebyshev, far by path
    bool sectorHeat = false; // her sector enemy-held
    bool farHeat = false;    // the portal's far side (3, 3) enemy-held
};

static Qsk run(World& w, Case const& k) {
    Init in;
    in.id = 0;
    in.team = 'A';
    in.w = in.h = 20;
    w.start(in);
    w.t.round = 100;
    w.t.length = 4;
    w.t.units = 6;
    w.head = w.board.id(10, 10);
    w.body = {w.head, w.board.id(9, 10), w.board.id(8, 10), w.board.id(7, 10)};
    w.occ.assign(w.board.NC, -1);
    w.occHeadId.assign(w.board.NC, -1);
    for (int c = 0; c < w.board.NC; c++) w.seenRound[c] = w.visRound[c] = 100;
    Board& b = w.board;
    Edge pe;  // the east side of (12, 10), landing on (3, 3)
    pe.kind = 2, pe.partnerOrient = 1, pe.px = 4, pe.py = 3;
    b.vE[b.id(13, 10)] = pe;
    Edge pr;
    pr.kind = 2, pr.partnerOrient = 1, pr.px = 13, pr.py = 10;
    b.vE[b.id(4, 3)] = pr;
    if (k.wall)  // y = 13's north sides walled for every x, and y = 7's (the torus' way round): her band is rows 7..12
        for (int x = 0; x < 20; x++) b.hE[b.id(x, 13)] = kelp(), b.hE[b.id(x, 7)] = kelp();
    b.rebuild();
    if (k.heard >= 0) w.know.enemies.push_back({k.heard, 5, 9, w.t.round - k.heardAge});
    if (k.sectorHeat) w.enemyHeat[w.heatOf(w.head)] = World::kHeatOn;
    if (k.farHeat) w.enemyHeat[w.heatOf(b.id(3, 3))] = World::kHeatOn;
    Qsk q(w, 4);
    q.threats();
    return q;
}

static bool mouthSource(Qsk const& q, World const& w) {
    for (auto const& s : q.basicSrc)
        if ((s.first == w.board.id(12, 10) || s.first == w.board.id(13, 10)) && s.second == 2) return true;
    return false;
}

static void testAbout() {
    {   // nobody about: no mouth sources (QSK without the switch would have two: (12, 10) and (13, 10))
        World w;
        Qsk q = run(w, {});
        CHECK(!q.aboutPath && !mouthSource(q, w));
    }
    {   // an enemy head heard 5 path steps away: about, the mouths count
        World w;
        Case k;
        k.heard = 15 + 20 * 10;  // (15, 10)
        Qsk q = run(w, k);
        CHECK(q.aboutPath && mouthSource(q, w));
    }
    {   // ... heard 11 rounds ago: stale
        World w;
        Case k;
        k.heard = 15 + 20 * 10;
        k.heardAge = 11;
        Qsk q = run(w, k);
        CHECK(!q.aboutPath && !mouthSource(q, w));
    }
    {   // 5 by Chebyshev but behind a wall row (out of her band, 8+ steps away by path): not about
        World w;
        Case k;
        k.heard = 10 + 20 * 15;  // (10, 15)
        k.wall = true;
        Qsk q = run(w, k);
        CHECK(!q.aboutPath && !mouthSource(q, w));
    }
    {   // ... the same head with no wall: 5 steps, about
        World w;
        Case k;
        k.heard = 10 + 20 * 15;
        Qsk q = run(w, k);
        CHECK(q.aboutPath && mouthSource(q, w));
    }
    {   // her sector enemy-held: about
        World w;
        Case k;
        k.sectorHeat = true;
        Qsk q = run(w, k);
        CHECK(q.aboutPath && mouthSource(q, w));
    }
    {   // nobody about, but the paired mouth's far side is enemy-held: that mouth counts
        World w;
        Case k;
        k.farHeat = true;
        Qsk q = run(w, k);
        CHECK(!q.aboutPath && mouthSource(q, w));
    }
}
#endif

int main() {
#if BC_QSK_PATHHEAT
    testAbout();
#endif
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
