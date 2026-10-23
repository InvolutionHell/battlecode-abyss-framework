// Idle expansion (BC_EXPAND, inactive in the final build): an idle dragon contests a bed core.
//   c++ -std=c++20 -O2 -DBC_EXPAND=1 -I bots/abyss tests/native/expand_test.cpp -o expand_test && ./expand_test
// Policy::planExpand on a synthetic 20 x 20 open board: the target bed, a held bed skipped (a teammate's head in view, a
// teammate's field nearer or as near with a lower id), the unseen fallback, the idle threshold, the length cap, the queen,
// a dragon already in a core. (The opening expansion of the final build, BC_OPEN_EXPAND, is tested in open_expand_test.cpp.)
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

#if BC_EXPAND
// the scene: a `len`-long dragon (id) at (10, 10), round 100, last meal `eaten`; every tile seen unless `fog` (then only x < 13); beds at
// (14, 10), with `farBed` (5, 5), and at `extraBed` (-1 none); `teammateAtBed`: a teammate's head in view at (13, 11); `friendAt`: a
// teammate (id 5) out of view whose BFS field starts there (-1 none); `mirrorNear`: the bed (14, 10) known as a mirror image
struct Opt {
    int id = 2, eaten = 50, len = 3, extraBed = -1, friendAt = -1;
    bool fog = false, teammateAtBed = false, farBed = true, mirrorNear = false;
};

static std::vector<int> torusDist(Board const& b, int from) {
    std::vector<int> d(b.NC);
    for (int c = 0; c < b.NC; c++) {
        int dx = std::abs(b.X(c) - b.X(from)), dy = std::abs(b.Y(c) - b.Y(from));
        d[c] = std::min(dx, 20 - dx) + std::min(dy, 20 - dy);
    }
    return d;
}

static int target(Opt const& o) {
    World w;
    Params p;
    Init in;
    in.id = o.id;
    in.team = 'A';
    in.w = in.h = 20;
    w.start(in);
    w.t.round = 100;
    w.t.length = o.len;
    w.t.units = 6;
    w.lastEatRound = o.eaten;
    w.firstRound = 0;
    w.head = w.board.id(10, 10);
    for (int i = 0; i < o.len; i++) w.body.push_back(w.board.id(10 - i, 10));
    w.occ.assign(w.board.NC, -1);
    w.occHeadId.assign(w.board.NC, -1);
    for (int c = 0; c < w.board.NC; c++)
        if (!o.fog || w.board.X(c) < 13) w.seenRound[c] = 99;
    w.board.bed[w.board.id(14, 10)] = 1;
    if (o.mirrorNear) w.mirrored[w.board.id(14, 10)] = 1;
    if (o.farBed) w.board.bed[w.board.id(5, 5)] = 1;
    if (o.extraBed >= 0) w.board.bed[o.extraBed] = 1;
    if (o.teammateAtBed) {  // within kExpandHeld of the bed (14, 10)
        int c = w.board.id(13, 11);
        w.occ[c] = w.occHeadId[c] = 7;
        w.others.push_back({7, 'A', c, 0, 0});
    }
    Policy pol(w, p, 1e18);
    pol.L_ = o.len;
    pol.queenMe_ = BC_QUEEN && o.id < 2;
    Board const& b = w.board;
    pol.myDist_ = torusDist(b, w.head);
    if (o.friendAt >= 0) pol.friends_.push_back({5, false, o.friendAt, torusDist(b, o.friendAt)});
    pol.planExpand();
    return pol.expandCell_;
}
#endif

int main() {
#if BC_EXPAND
    Board b;
    b.init(20, 20);
    Opt o;
    CHECK(target(o) == b.id(14, 10));  // idle 50 rounds: the nearest bed (4 steps; (5, 5) is 10)
    {
        Opt h = o;
        h.teammateAtBed = true;
        CHECK(target(h) == b.id(5, 5));  // the near one held by a teammate in view: the other
    }
    {
        Opt f = o;
        f.friendAt = b.id(16, 10);
        CHECK(target(f) == b.id(5, 5));  // a teammate's field 2 steps from it (mine 4): held
        f.friendAt = b.id(18, 10);
        CHECK(target(f) == b.id(14, 10));  // 4 steps, as near as me, but id 5 > 2: mine
        f.id = 7;
        CHECK(target(f) == b.id(5, 5));  // ... and with my id 7 > 5: theirs
    }
    {
        Opt i = o;
        i.eaten = 100 - kExpandIdle;
        CHECK(target(i) == b.id(14, 10));  // exactly kExpandIdle rounds idle
        i.eaten++;
        CHECK(target(i) == -1);  // one round less: not idle
    }
    {
        Opt l = o;
        l.len = kExpandMaxLen;
        CHECK(target(l) == b.id(14, 10));  // kExpandMaxLen long: goes
        l.len++;
        CHECK(target(l) == -1);  // one longer: stays
    }
    {
        Opt q = o;
        q.id = 0;
        CHECK(target(q) == -1 || !BC_QUEEN);  // never the queen
    }
    {
        // the near bed in the fog (x >= 13 unseen, not mirrored) is no known bed: the far known one first, before any unseen tile
        Opt f = o;
        f.fog = true;
        CHECK(target(f) == b.id(5, 5));
        f.mirrorNear = true;
        CHECK(target(f) == b.id(14, 10));  // ... unless mirrored: a known bed
        f.mirrorNear = false;
        f.farBed = false;  // no known free bed: the nearest never-seen tile, (13, 10) 3 steps east
        CHECK(target(f) == b.id(13, 10));
    }
    {
        // a known bed within kExpandHeld steps: already in a core, it stays - 3 steps (the edge), 2, under its head; 4 steps is outside
        Opt c = o;
        c.extraBed = b.id(13, 10);
        CHECK(target(c) == -1);
        c.extraBed = b.id(10, 12);
        CHECK(target(c) == -1);
        c.extraBed = b.id(10, 10);
        CHECK(target(c) == -1);
        c.extraBed = b.id(10, 6);
        CHECK(target(c) != -1);
    }
#endif
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
