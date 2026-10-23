// Late queen feeding (BC_QUEEN_FEED350, inactive in the final build): from r350 the two non-queens nearest to a short queen
// leave her the pearls by her head.
//   c++ -std=c++20 -O2 -DBC_QUEEN_FEED350=1 -I bots/abyss tests/native/queen_feed350_test.cpp -o queen_feed350_test && ./queen_feed350_test
//   (also =2: her side too - her eat weight x 2 from r350 at L < 8, and in QSK's best tier a landing that eats first;
//    =3: then the landing nearest a pearl within 6 steps; =4: food out of her view counted too)
// Policy::planQueenFeed (who feeds, her place and length from sight or from her fact) and its eat weight (a pearl within
// kQFeedYield of her head is worth nothing to a feeder), on a synthetic 20 x 20 open board. The QSK preferences of modes
// 2-4 are also compiled in by BC_QUEEN_GROWFIRST (on in the final build; see queen_growfirst_test.cpp).
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

#if BC_QUEEN_FEED350
// dragon `id` (3 long) at (10, 10) facing east, round `round`
static World dragon(int id, int round) {
    World w;
    Init in;
    in.id = id;
    in.team = 'A';
    in.w = in.h = 20;
    w.start(in);
    w.t.round = round;
    w.t.length = 3;
    w.t.units = 10;
    w.head = w.board.id(10, 10);
    for (int i = 0; i < 3; i++) w.body.push_back(w.board.id(10 - i, 10));
    w.occ.assign(w.board.NC, -1);
    w.occHeadId.assign(w.board.NC, -1);
    for (int c = 0; c < w.board.NC; c++) w.seenRound[c] = round;
    return w;
}

static int at(int x, int y) { return y * 20 + x; }

// planQueenFeed with an open-board path field; the head this dragon feeds (-1 none)
static int feeds(World& w, std::vector<World::Seen> others = {}) {
    w.others = others;
    Params p;
    Policy pol(w, p, 1e18);
    pol.L_ = 3;
    pol.queenMe_ = BC_QUEEN && w.init.id < 2;
    Board const& b = w.board;
    pol.myDist_.assign(b.NC, INF);
    for (int c = 0; c < b.NC; c++) {
        int dx = std::abs(b.X(c) - 10), dy = std::abs(b.Y(c) - 10);
        pol.myDist_[c] = std::min(dx, 20 - dx) + std::min(dy, 20 - dy);
    }
    pol.queenFeedHead_ = 77;  // a stale one: always reset
    pol.planQueenFeed();
    return pol.queenFeedHead_;
}

static World::Seen queen(int x, int y, int vis) { return {0, 'A', at(x, y), 0, vis}; }

static void testWho() {
    World w = dragon(6, 350);
    CHECK(feeds(w, {queen(14, 10, 5)}) == at(14, 10));     // in view, 5 long, 4 steps, nobody nearer: a feeder
    World early = dragon(6, kQFeedFrom - 1);
    CHECK(feeds(early, {queen(14, 10, 5)}) == -1);          // before r350
    World longQ = dragon(6, 350);
    CHECK(feeds(longQ, {queen(14, 10, kQFeedLen)}) == -1);  // 8 long: not short
    World shortQ = dragon(6, 350);
    CHECK(feeds(shortQ, {queen(14, 10, kQFeedLen - 1)}) == at(14, 10));
    World cut = dragon(6, 350);                              // 5 in view, her own fact says 9: not short
    cut.know.friends = {{at(14, 10), 9, 0, 349}};
    CHECK(feeds(cut, {queen(14, 10, 5)}) == -1);
    World q = dragon(0, 350);
    q.know.friends = {{at(13, 11), 4, 0, 349}};             // (an echo of her own id: the world never files her own facts)
    CHECK(feeds(q) == -1 || !BC_QUEEN);                     // never the queen
    World enemyQ = dragon(6, 350);                           // their queen (id 1, team B) is not ours
    CHECK(feeds(enemyQ, {{1, 'B', at(14, 10), 0, 3}}) == -1);
    // out of sight: her fact (cell, length) while fresh
    World heard = dragon(6, 350);
    heard.know.friends = {{at(13, 11), 4, 0, 345}};
    CHECK(feeds(heard) == at(13, 11));
    heard.know.friends[0].round = 350 - kQFeedAge - 1;
    CHECK(feeds(heard) == -1);                              // stale
    World none = dragon(6, 350);
    CHECK(feeds(none) == -1);                               // nothing known of her
    World gone = dragon(6, 350);                             // the team has not heard of her for kQueenGone rounds: dead
    gone.know.friends = {{at(13, 11), 4, 0, 345}};
    gone.know.queenRound = 350 - kQueenGone - 1;
    CHECK(feeds(gone) == -1);
    // reach: kQFeedNear steps
    World near6 = dragon(6, 350);
    CHECK(feeds(near6, {queen(10, 4, 3)}) == at(10, 4));    // 6 steps
    World far7 = dragon(6, 350);
    CHECK(feeds(far7, {queen(10, 3, 3)}) == -1);            // 7
    // two of ours nearer to her (seen): not a feeder; one: still one; enemies and she herself do not count
    World two = dragon(6, 350);
    CHECK(feeds(two, {queen(14, 10, 5), {20, 'A', at(13, 10), 0, 3}, {21, 'A', at(15, 11), 0, 3}}) == -1);
    World one = dragon(6, 350);
    CHECK(feeds(one, {queen(14, 10, 5), {20, 'A', at(13, 10), 0, 3}}) == at(14, 10));
    World en = dragon(6, 350);
    CHECK(feeds(en, {queen(14, 10, 5), {20, 'B', at(13, 10), 0, 3}, {21, 'B', at(15, 11), 0, 3}}) == at(14, 10));
    // ... a teammate heard nearer within kQFeedAge counts
    World hn = dragon(6, 350);
    hn.know.friends = {{at(13, 10), 3, 20, 345}, {at(15, 11), 3, 21, 345}};
    CHECK(feeds(hn, {queen(14, 10, 5)}) == -1);
    hn.know.friends[0].round = 350 - kQFeedAge - 1;
    CHECK(feeds(hn, {queen(14, 10, 5)}) == at(14, 10));     // ... one of them stale
}

// a feeder leaves the pearls within kQFeedYield of her head: scoreFrom's eat weight
static void testYield() {
    for (int qx : {12, 14}) {  // a pearl at (11, 10): 1 step from her at (12, 10), 3 from her at (14, 10) - both yielded
        World w = dragon(6, 350);
        w.seenPearl[w.board.id(11, 10)] = 1;
        w.visRound[w.board.id(11, 10)] = 350;
        w.know.friends = {{at(qx, 10), 4, 0, 349}};
        Params p;
        Policy pol(w, p, 1e18);
        pol.L_ = 3;
        pol.buildBase();
        pol.buildTargets();
        pol.myDist_.assign(w.board.NC, 0);
        pol.planQueenFeed();
        CHECK(pol.queenFeedHead_ == at(qx, 10));
        std::vector<int> none;
        Choice c = pol.scoreFrom(w.body, 3, E, 0, none);
        CHECK(c.eat && c.stepTerm <= 0.0);                 // its eat earns nothing
        pol.queenFeedHead_ = -1;
        Choice d = pol.scoreFrom(w.body, 3, E, 0, none);
        CHECK(d.eat && d.stepTerm > c.stepTerm);           // a non-feeder's does
    }
    World far = dragon(6, 350);                              // her at (15, 10): the pearl at (11, 10) is 4 from her - kept
    far.seenPearl[far.board.id(11, 10)] = 1;
    far.visRound[far.board.id(11, 10)] = 350;
    far.know.friends = {{at(15, 10), 4, 0, 349}};
    Params p;
    Policy pol(far, p, 1e18);
    pol.L_ = 3;
    pol.buildBase();
    pol.buildTargets();
    pol.myDist_.assign(far.board.NC, 0);
    pol.planQueenFeed();
    std::vector<int> none;
    Choice c = pol.scoreFrom(far.body, 3, E, 0, none);
    CHECK(pol.queenFeedHead_ == at(15, 10) && c.eat && c.stepTerm > 0.0);
}
#endif

#if BC_QUEEN_FEED350 >= 2
// mode 2, her side. Our queen (id 0, 3 long) at (10, 10), her body west: her 1-step landings N (10, 9), E (11, 10), S (10, 11); a pearl at S
static World queenWorld(int round, bool enemy = false, bool mate = false, int pearlY = 11) {
    World w;
    Init in;
    in.id = 0;
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
    for (int c : w.body) w.occ[c] = 0;
    for (int c = 0; c < w.board.NC; c++) w.seenRound[c] = w.visRound[c] = round;
    if (pearlY >= 0) w.seenPearl[w.board.id(10, pearlY)] = 1;
    if (enemy) {  // a 2-long enemy, head (10, 13) and tail (10, 12), whole in her view: its head within 3 of S and E, 4 from N - only N is tier 1
        int const h = w.board.id(10, 13), t = w.board.id(10, 12);
        w.occ[h] = w.occ[t] = 7;
        w.occHeadId[h] = 7;
        w.t.parts.push_back({'B', 7, 10, 13, S, true});
        w.t.parts.push_back({'B', 7, 10, 12, S, false});
    }
    if (mate) {  // a teammate's head at (10, 12), next to S only: S drops to tier 2 with no threat (its margins stay N's)
        int const h = w.board.id(10, 12);
        w.occ[h] = 5;
        w.occHeadId[h] = 5;
        w.t.parts.push_back({'A', 5, 10, 12, N, true});
    }
    return w;
}

// QSK's choice with the search's values N 10 > E 5 > S 0 (W: her body); its tier in `tier`; `home`: a home score per tile (or none)
static int qskPick(World& w, bool feed, int* tier = nullptr, std::vector<int> const* home = nullptr) {
    Qsk q(w, 3);
    q.feed350 = feed;
    q.home = home;
    double tot[4] = {0, 0, 0, 0};
    tot[N] = 10, tot[E] = 5, tot[S] = 0;
    bool legal[4] = {true, true, true, true};
    legal[W] = false;
    QskOut o = q.run(tot, legal, 1e18);
    if (tier) *tier = o.tier;
    return o.ok ? o.dest : -2;
}

static void testQueenSide() {
    World w = queenWorld(400);
    CHECK(qskPick(w, false) == w.board.id(10, 9));          // the values' choice: N
    CHECK(qskPick(w, true) == w.board.id(10, 11));          // feeding: the pearl at S, the same tier
    World e = queenWorld(400, true);
    int t = 0;
    CHECK(qskPick(e, true, &t) == e.board.id(10, 9) && t == 1);  // S in a worse tier (an enemy head within 3): never for food
    World m = queenWorld(400, false, true);
    CHECK(qskPick(m, true, &t) != m.board.id(10, 11) && t == 1);  // ... nor next to a head (tier 2 on the same margins; mode 3 may pull E)
    // who: our queen from kQFeedFrom, shorter than kQFeedLen
    World q = queenWorld(kQFeedFrom);
    Params p;
    Policy pol(q, p, 1e18);
    pol.queenMe_ = true;
    pol.L_ = kQFeedLen - 1;
    CHECK(pol.queenFeeds());
    pol.L_ = kQFeedLen;
    CHECK(!pol.queenFeeds());
    pol.L_ = 3;
    pol.queenMe_ = false;
    CHECK(!pol.queenFeeds());
    World early = queenWorld(kQFeedFrom - 1);
    Policy pe(early, p, 1e18);
    pe.queenMe_ = true;
    pe.L_ = 3;
    CHECK(!pe.queenFeeds());
}

// her eat weight x kQFeedEat: scoreFrom's step term on the pearl at S, r350 against r349
static double eatTerm(int round) {
    World w = queenWorld(round);
    w.visRound[w.board.id(10, 11)] = round;
    Params p;
    Policy pol(w, p, 1e18);
    pol.queenMe_ = true;
    pol.L_ = 3;
    pol.buildBase();
    pol.buildTargets();
    pol.myDist_.assign(w.board.NC, 0);
    std::vector<int> none;
    Choice c = pol.scoreFrom(w.body, 3, S, 0, none);
    return c.eat ? c.stepTerm : -1.0;
}

static void testQueenEat() {
    double const before = eatTerm(kQFeedFrom - 1), after = eatTerm(kQFeedFrom);
    CHECK(before > 0.0 && std::abs(after - kQFeedEat * before) < 1e-9);
}
#endif

#if BC_QUEEN_FEED350 >= 3
// mode 3: no pearl next to her - the landing nearest a pearl within kQFeedPull steps, inside the kept landings
static void testPull() {
    int t = 0;
    World w = queenWorld(400, false, false, 13);             // a pearl at (10, 13): S 2 steps from it, N and E 4
    CHECK(qskPick(w, false) == w.board.id(10, 9));          // no feeding: the values' N
    CHECK(qskPick(w, true, &t) == w.board.id(10, 11) && t == 1);  // pulled S
    World far = queenWorld(400, false, false, 18);           // (10, 18): S 7 steps from it - beyond kQFeedPull
    CHECK(qskPick(far, true) == far.board.id(10, 9));
    World near6 = queenWorld(400, false, false, 17);         // (10, 17): S 6 steps - at the cap
    CHECK(qskPick(near6, true) == near6.board.id(10, 11));
    std::vector<int> home(w.board.NC, 0);                    // her home score (min(enemy steps, 30) - min(our steps, 30)): adjacent
    home[w.board.id(10, 9)] = 2, home[w.board.id(10, 11)] = -2;  // tiles differ by <= 4 - N 2 buckets (2e7) above S
    World h = queenWorld(400, false, false, 13);
    CHECK(qskPick(h, false, nullptr, &home) == h.board.id(10, 9));
    CHECK(qskPick(h, true, nullptr, &home) == h.board.id(10, 11));  // the food pull above the home bucket
    World m = queenWorld(400, false, true, 13);              // a teammate's head at (10, 12): S tier 2 - never pulled into it;
    CHECK(qskPick(m, true, &t) == m.board.id(11, 10) && t == 1);  // E (4 steps round it) beats N (6) inside tier 1
}
#endif

#if BC_QUEEN_FEED350 >= 4
// mode 4: food out of her view at (10, 13) (2 steps beyond S): no pearl seen anywhere; (10, 13) out of view this round
static World blind(int round) {
    World w = queenWorld(round, false, false, -1);
    int const c = w.board.id(10, 13);
    w.visRound[c] = round - 1;
    return w;
}

static void testSources() {
    int const c = 20 * 13 + 10;
    World none = blind(400);
    CHECK(qskPick(none, true) == none.board.id(10, 9));     // nothing known: the values' N
    World hot = blind(400);
    hot.know.hot = {{c, 8, c, 400 - kQFeedHotAge}};
    CHECK(qskPick(hot, true) == hot.board.id(10, 11));      // a teammate's HOT fact, at the age limit: pulled S
    World stale = blind(400);
    stale.know.hot = {{c, 8, c, 400 - kQFeedHotAge - 1}};
    CHECK(qskPick(stale, true) == stale.board.id(10, 9));   // too old
    World due = blind(400);
    due.board.bed[c] = 1, due.seenRound[c] = 300, due.lastSpawn[c] = 350;
    CHECK(qskPick(due, true) == due.board.id(10, 11));      // a bed whose countdown ran out after we last saw it
    World notDue = blind(400);
    notDue.board.bed[c] = 1, notDue.seenRound[c] = 300, notDue.lastSpawn[c] = 250;
    CHECK(qskPick(notDue, true) == notDue.board.id(10, 9)); // ... ran out before: we saw it empty since
    World mir = blind(400);
    mir.board.bed[c] = 1, mir.seenRound[c] = -1, mir.mirrored[c] = 1, mir.board.gapMax[c] = 20;
    CHECK(qskPick(mir, true) == mir.board.id(10, 11));      // a never-seen mirrored bed, its gap known and past
    mir.board.gapMax[c] = 0;
    CHECK(qskPick(mir, true) == mir.board.id(10, 9));       // ... its gap unknown
    World inView = blind(400);
    inView.board.bed[c] = 1, inView.seenRound[c] = 300, inView.lastSpawn[c] = 350, inView.visRound[c] = 400;
    CHECK(qskPick(inView, true) == inView.board.id(10, 9)); // in view now (and no pearl seen there): not food
    World off = blind(400);
    off.know.hot = {{c, 8, c, 398}};
    CHECK(qskPick(off, false) == off.board.id(10, 9));      // not feeding: no pull
}
#endif

int main() {
#if BC_QUEEN_FEED350
    testWho();
    testYield();
#endif
#if BC_QUEEN_FEED350 >= 2
    testQueenSide();
    testQueenEat();
#endif
#if BC_QUEEN_FEED350 >= 3
    testPull();
#endif
#if BC_QUEEN_FEED350 >= 4
    testSources();
#endif
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
