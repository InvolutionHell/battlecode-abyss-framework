// Queen breathing room (BC_QUEEN_BREATHE, 2 in the final build): the queen keeps 2 free sides.
//   c++ -std=c++20 -O2 -I bots/abyss tests/native/queen_breathe_test.cpp -o queen_breathe_test && ./queen_breathe_test
// QSK's landing filter (Qsk::dropTight), its escape after 2 boxed turns, the end-to-end pick against the pick without the
// switch, and the teammates' veto (Policy::breatheVeto; mode 2 adds the bubble: every tile around her head), on a synthetic
// 20 x 20 open board. ablation.hpp sets this switch unconditionally, so a -D flag does not change it; the mode-1 and
// mode-3 branches below are only compiled in a copy of the bot whose ablation.hpp sets another mode.
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

struct Seg { int x, y, f; };

// our queen (4 long, head (10, 10), body trailing west), dragons given head first as (x, y, facing); team 'A' is ours
static World queenWorld(std::vector<std::pair<char, std::vector<Seg>>> const& dragons, bool fogFree = true) {
    World w;
    Init in;
    in.id = 0;
    in.team = 'A';
    in.w = in.h = 20;
    w.start(in);
    w.t.round = 120;
    w.t.length = 4;
    w.t.units = 6;
    w.head = w.board.id(10, 10);
    for (int i = 0; i < 4; i++) w.body.push_back(w.board.id(10 - i, 10));
    w.occ.assign(w.board.NC, -1);
    w.occHeadId.assign(w.board.NC, -1);
    for (int c = 0; c < w.board.NC; c++)
        if (fogFree || w.board.cheb(c, w.head) <= 3) w.seenRound[c] = w.visRound[c] = w.t.round;
    int id = 6;
    for (auto const& [team, segs] : dragons) {
        for (size_t i = 0; i < segs.size(); i++) {
            int c = w.board.id(segs[i].x, segs[i].y);
            w.occ[c] = id;
            if (i == 0) w.occHeadId[c] = id;
            if (w.board.cheb(c, w.head) <= 3) w.t.parts.push_back({team, id, segs[i].x, segs[i].y, segs[i].f, i == 0});
        }
        id++;
    }
    return w;
}
static void wall(World& w, int x, int y, int d) {
    Board& b = w.board;
    Edge& e = d == N ? b.hE[b.id(x, y)] : d == S ? b.hE[b.id(x, y + 1)] : d == W ? b.vE[b.id(x, y)] : b.vE[b.id(x + 1, y)];
    e = Edge{};
    e.kind = 1;
    b.rebuild();
}
static bool hasCell(std::vector<Qsk::Land> const& v, int c) {
    for (auto const& g : v)
        if (g.cell == c) return true;
    return false;
}

#if BC_QUEEN_BREATHE
static void testFilter() {
    // S (10, 11) keeps one free side: a wall south and a teammate's body west, her own body north after the step. N and E keep 3 each,
    // nothing threatens them: S is dropped, N and E stay
    World w = queenWorld({{'A', {{8, 12, N}, {9, 12, W}, {9, 11, S}}}});
    wall(w, 10, 11, S);
    Qsk q(w, 4);
    q.threats();
    auto F = q.landings(1, 1);
    CHECK(F.size() == 3 && hasCell(F, w.board.id(10, 11)));
    int r = q.dropTight(F);
    CHECK(r == 1 && F.size() == 2 && !hasCell(F, w.board.id(10, 11)) && hasCell(F, w.board.id(10, 9)) && hasCell(F, w.board.id(11, 10)));
    // nothing tight: nothing dropped
    World o = queenWorld({});
    Qsk qo(o, 4);
    qo.threats();
    auto Fo = qo.landings(1, 1);
    CHECK(qo.dropTight(Fo) == 0 && Fo.size() == 3);
}

#if BC_QUEEN_BREATHE >= 3
static void testSecure() {
    // N (10, 9) has 3 free sides, but a teammate's head at (11, 8) is next to two of them (10, 8) and (11, 9): 1 secure side; S (10, 11) keeps
    // 3 secure ones - an enemy's head at (9, 12) next to two of them does not count (the margins handle enemies): N is dropped, S stays
    World w = queenWorld({{'A', {{11, 8, E}, {12, 8, W}}}, {'B', {{9, 12, W}, {8, 12, E}}}});
    Qsk q(w, 4);
    q.threats();
    auto F = q.landings(1, 1);
    CHECK(F.size() == 3);
    CHECK(q.dropTight(F) == 1 && !hasCell(F, w.board.id(10, 9)) && hasCell(F, w.board.id(10, 11)));
}
#endif

static void testNoSafeNoEscape() {
    // N (10, 9) keeps one free side (walls N and E); S is closed (a teammate's body on it); E keeps 2 but is next to a whole 3-long enemy's
    // head (12, 10): no landing has 2 free sides and a margin >= 1. Boxed 0 or 1 turn: no change; boxed 2 turns: the most free sides (E)
    // only, even into the enemy's reach
    auto mk = [] {
        World w = queenWorld({{'A', {{10, 11, E}, {9, 11, E}}}, {'B', {{12, 10, W}, {12, 11, N}, {12, 12, N}}}});
        wall(w, 10, 9, N);
        wall(w, 10, 9, E);
        return w;
    };
    World w = mk();
    Qsk q(w, 4);
    q.threats();
    auto F = q.landings(1, 1);
    CHECK(F.size() == 2 && hasCell(F, w.board.id(10, 9)) && hasCell(F, w.board.id(11, 10)));
    CHECK(q.mOf(q.mB, w.board.id(11, 10)) < 1 && q.mOf(q.mB, w.board.id(10, 9)) >= 1);
    q.tightTurns = 1;
    CHECK(q.dropTight(F) == 0 && F.size() == 2);
    q.tightTurns = 2;
    CHECK(q.dropTight(F) == 2 && F.size() == 1 && F[0].cell == w.board.id(11, 10));
}

#endif

static void testRunPick() {
    // a 3-long enemy 2 east, fog about, the search's values prefer S. S is made tight (a wall south, a teammate's body west):
    // QSK picks N with the switch on (the same tier, 3 free sides), S without it
    double tot[4] = {1, 0, 2, -1};  // N E S W
    bool legal[4] = {true, true, true, false};
    World w = queenWorld({{'B', {{12, 10, W}, {12, 9, S}, {12, 8, S}}}, {'A', {{8, 12, N}, {9, 12, W}, {9, 11, S}}}}, false);
    w.know.enemies.push_back({w.board.id(12, 10), 3, 7, w.t.round});
    wall(w, 10, 11, S);
    Qsk q(w, 4);
    QskOut o = q.run(tot, legal, 1e18);
#if BC_QUEEN_BREATHE
    CHECK(o.ok && o.dest == w.board.id(10, 9) && q.breathe == 1);
#else
    CHECK(o.ok && o.dest == w.board.id(10, 11));
#endif
}

#if BC_QUEEN_BREATHE
static void testVeto() {
    // a teammate (id 2) sees our queen at (10, 10): her west side is her body, north another dragon's body; a step onto her east side
    // leaves her 1 free side (vetoed), one onto a tile off her sides is not, and with north free the same east step leaves her 2
    World w;
    Init in;
    in.id = 2;
    in.team = 'A';
    in.w = in.h = 20;
    w.start(in);
    w.t.round = 120;
    Params p;
    Policy pol(w, p, 1e18);
    Board const& b = w.board;
    pol.guardQh_ = b.id(10, 10);
    std::vector<int> blk(b.NC, 0);
    if (BC_QUEEN_BREATHE >= 2) {  // the bubble: every tile around her head, sides and corners, whatever her free sides
        CHECK(pol.breatheVeto(b.id(11, 10), blk) && pol.breatheVeto(b.id(11, 11), blk) && pol.breatheVeto(b.id(9, 9), blk));
        CHECK(!pol.breatheVeto(b.id(12, 10), blk) && !pol.breatheVeto(b.id(10, 12), blk));
        return;
    }
    blk[b.id(9, 10)] = INF;
    blk[b.id(10, 9)] = INF;
    CHECK(pol.breatheVeto(b.id(11, 10), blk));
    CHECK(!pol.breatheVeto(b.id(12, 10), blk));
    blk[b.id(10, 9)] = 0;
    CHECK(!pol.breatheVeto(b.id(11, 10), blk));
    blk[b.id(10, 11)] = 2;  // a tile freed only at move 2 counts as blocked
    CHECK(pol.breatheVeto(b.id(11, 10), blk));
    blk[b.id(10, 9)] = INF;  // north blocked again: east is her only free side
    CHECK(!pol.breatheVeto(b.id(12, 10), blk));  // a step off her sides is not vetoed, however boxed she is
}

#endif

int main() {
#if BC_QUEEN_BREATHE >= 3
    testSecure();
#endif
#if BC_QUEEN_BREATHE
    testFilter();
    testNoSafeNoEscape();
    testVeto();
#endif
    testRunPick();
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
