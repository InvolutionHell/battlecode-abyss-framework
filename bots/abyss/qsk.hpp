#pragma once
// The Queen Safety Kernel (QSK, BC_QUEEN_QSK).
//
// The queen picks her landing by hard tiers; the search's own scores only order the moves inside a tier.
//   margin m(c) = min over threat sources t of (d_eff(t, c) - Rmax(t)), one multi-source Dijkstra per turn: every source
//   starts at -Rmax(t), a step costs 1 (0 onto a seen pearl: a pearl on the way pays a paid step), values above kCap are
//   not expanded - the reach has no upper bound and the cost stays one pass. Rmax(L) = ceil(L/4) + L - 2 (rules 1.2.3).
//   basic sources: visible enemy heads (a body cut by the view's edge: L = max(visible + 1, longest enemy known, 6),
//     unbounded when nothing is known), tail tips of whole enemy chains of >= 4 (a child of L - 2 moves the round it is
//     born), heads seen 1-3 rounds ago and out of sight now (reach + ceil(L/4) per round), portal mouths within 4 (any
//     dragon may come through blind: reach 2).
//   strict sources (T1 / T2 only): also body-derived heads (a split chain turns segment j, 2 <= j <= L - 4, into a head of
//     L - j - 2 the same round) and fog (tiles within 6 of her head not in view now nor last round,
//     outside sectors our side holds: virtual heads of max(longest enemy known, 6)).
//   landings: a time-expanded BFS of her own moves inside this turn's 7x7 window (her body leaves its tail one cell per
//     step that eats nothing; no step back onto her path; a portal only as the last step, onto a visible exit), free steps
//     1..ceil(L/4); paid paths (up to min(L - 2, 4) more) and the SPLIT 2 hold only when T1-T3 are empty.
//   tiers: T1 mS >= 1, no visible enemy head within Chebyshev 3, room >= L + 8, >= 2 exits, no head next to her;
//          T2 mS >= 1, room >= L + 8, >= 2 exits;   T3 m >= 1, room >= L + 2;
//          T4 m >= 1 among the hold / paid paths (at most 1 paid a turn, she keeps 3; the hold keeps 4), the least paid;
//          T5 the free landing with the largest m;
//          none: the search's trapped handling (portal first, then the boxed reverse).
// CPU: two Dijkstra passes over the board, one window BFS, one capped flood per distinct landing (<= L + 8 tiles).
#include <algorithm>
#include <queue>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "world.hpp"

namespace bc {

struct QskOut {
    bool ok = false;
    int tier = 0;              // 1..5
    bool hold = false;         // SPLIT 2: she stays, the child takes her last 2 segments
    int dir = -1;              // the first step
    int dest = -1;             // the landing tile (1-step moves; multi-step: cells.back())
    std::string path;          // the MOVE's directions (2+ steps: a sprint)
    std::vector<int> cells;    // the tiles of the path, in order
    std::vector<char> eats;    // whether each step eats
    int paid = 0;              // segments paid (steps beyond ceil(L/4))
    int m = 0, mStrict = 0, room = 0, exits = 0, steps = 0;
#if BC_QUEEN_EXITFIX
    int xcls = -1;             // T5 by the calibrated margins: 2 safe under the mid lengths, 1 under the low ones, 0 neither
#endif
};

struct Qsk {
    static constexpr int kCap = 4;      // margins above this are "safe enough" and not expanded
    static constexpr int kMaxPay = 1;   // T4: segments she may pay in a turn
    static constexpr int kKeepLen = 3;  // ... never below this length by paying
    static constexpr int kHoldKeep = 4; // the SPLIT 2 hold only when she keeps this many
    static constexpr int kInf = 1 << 28;
    World const& w;
    Board const& b;
    int L;
    int head;
    std::vector<int> const& body;       // head first
    std::vector<int> mB, mS;            // margins (basic / strict), kInf beyond kCap
#if BC_QUEEN_EXITFIX
    // BC_QUEEN_EXITFIX: in replay reviews many queen deaths on an enemy's turn had a landing outside every enemy's
    // paid reach. The kernel's basic model gives every chain cut by her window the length max(visible + 1, longest
    // known, 6); measured over many ladder replays (thousands of turn x cut-chain samples with the head in her
    // window), a cut chain with 1 visible segment is 2-3 long in ~91% (r < 300; P(L >= 6) 1-2%), with 2 visible 3-4
    // long in ~93% (P(L >= 6) 4-9%), with 3 or more at least 4 and 25-50% >= 6. With every landing inside such a
    // phantom reach T5 took the first-listed largest margin. T5 now ranks the free landings by margins under
    // calibrated lengths: low (cut = visible + 1, at least 3; memory heads as seen) and mid (visible + 2, at least 4,
    // for 1-2 visible segments before r300; the kernel's own assumption otherwise). T1-T4 are untouched.
    std::vector<std::pair<int, int>> certSrc, lowSrc, midSrc;  // the certain threats (whole visible chains) / the low / the mid lengths
    std::vector<int> mC, mL, mM;  // margins under them, computed at T5 only
#endif
    std::vector<int> enemyHeads;        // visible enemy heads
    std::vector<char> passTail;         // tiles another dragon's tail will leave before a threat moves through
    int longestKnown = 0;
    // Slack (replay reviews: most queen kills followed her own step toward the killer): the free-reach slack after
    // her move = min over enemy heads (terrain steps - ceil(Le / 4)); <= 1 -> 8-11% killed within 5 rounds, 4-5 ->
    // 2.7%, >= 6 -> < 0.7%. slack >= 3 is a hard filter whenever some landing has it, and larger is preferred.
    bool slackOn = false;
    static constexpr int kSlackCap = 10;
    std::vector<std::pair<int, int>> slackSrc;
    std::vector<int> mF;  // slack per tile (kInf beyond kSlackCap)
    bool grow = false;                  // growth arm: with no threat within reach + 3 she heads for the nearest pearl
#if BC_QFEED_EAT
    bool feed350 = false;               // BC_QFEED_EAT: inside her best tier a 1-step landing that eats comes first
                                        // (never a worse tier)
#endif
    std::vector<int> const* home = nullptr;  // BC_QUEEN home: per-tile interior score (higher: deeper on our side),
                                             // or none
    int homeBucket(int c) const { return home ? ((*home)[c] + 64) / 3 : 0; }
    std::vector<int> foodDist;          // steps to the nearest pearl seen in the last 5 rounds (free tiles), INF none
    std::vector<std::pair<int, int>> basicSrc;  // the basic threat sources (tile, reach) of this turn (tests / trace)
#if BC_QSK_PATHHEAT
    bool aboutPath = false;             // BC_QSK_PATHHEAT: this turn's "enemies about" by path (tests / trace)
#endif
    long work = 0;                      // a rough count of tiles touched (trace / tests)

    static int rmax(int len) { return len < 2 ? 0 : (len + 3) / 4 + len - 2; }

    Qsk(World const& world, int len) : w(world), b(world.board), L(len), head(world.head), body(world.body) {}

    bool pearlAt(int c) const { return w.seenRound[c] == w.t.round && w.seenPearl[c]; }
    int cheb(int a, int c) const { return b.cheb(a, c); }

    // Chains of the visible dragons (not us): head first when the head is in view.
    struct Chain { int id = -1; bool enemy = false, hasHead = false, truncated = false; std::vector<int> cells; };
    std::vector<Chain> chains() const {
        std::vector<Chain> out;
        std::vector<Part> const& P = w.t.parts;
        std::vector<int> ids;
        for (Part const& p : P)
            if (p.id != w.init.id && std::find(ids.begin(), ids.end(), p.id) == ids.end()) ids.push_back(p.id);
        for (int id : ids) {
            Chain ch;
            ch.id = id;
            std::vector<int> cells, facing;
            int hc = -1;
            for (Part const& p : P)
                if (p.id == id) {
                    ch.enemy = p.team != w.init.team;
                    int c = b.id(p.x, p.y);
                    if (p.head) hc = c;
                    else cells.push_back(c), facing.push_back(p.facing);
                }
            std::vector<char> used(cells.size(), 0);
            if (hc >= 0) {
                ch.hasHead = true;
                ch.cells.push_back(hc);
                int cur = hc;
                for (;;) {
                    int nx = -1;
                    for (size_t i = 0; i < cells.size(); i++)
                        if (!used[i] && b.nb[cells[i] * 4 + facing[i]] == cur) { nx = static_cast<int>(i); break; }
                    if (nx < 0) break;
                    used[nx] = 1;
                    cur = cells[nx];
                    ch.cells.push_back(cur);
                }
                bool rest = false;
                for (char u : used) rest |= !u;
                ch.truncated = rest || cheb(ch.cells.back(), head) >= 3;
            } else {
                // headless fragment: its tail end is the segment nobody points to
                for (size_t i = 0; i < cells.size(); i++) {
                    bool pointed = false;
                    for (size_t j = 0; j < cells.size(); j++)
                        if (j != i && b.nb[cells[j] * 4 + facing[j]] == cells[i]) pointed = true;
                    if (!pointed) { ch.cells.push_back(cells[i]); break; }
                }
                ch.truncated = true;
            }
            out.push_back(ch);
        }
        return out;
    }

    // One multi-source pass: val[c] = min over sources (steps to c - R), kept only while <= kCap.
    void margins(std::vector<std::pair<int, int>> const& src, std::vector<int>& val, int cap = kCap, bool pearlFree = true) {
        val.assign(b.NC, kInf);
        using Item = std::pair<int, int>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
        for (auto const& s : src)
            if (-s.second < val[s.first]) val[s.first] = -s.second, pq.push({-s.second, s.first});
        while (!pq.empty()) {
            auto [v, u] = pq.top();
            pq.pop();
            if (v != val[u]) continue;
            work++;
            for (int d = 0; d < 4; d++) {
                int n = b.nb[u * 4 + d];
                if (n < 0) continue;
                bool ours = std::find(body.begin(), body.end(), n) != body.end();  // her body: passable (conservative)
                if (w.occ[n] >= 0 && !passTail[n] && !ours) continue;
                int nv = v + (pearlFree && pearlAt(n) ? 0 : 1);
                if (nv > cap || nv >= val[n]) continue;
                val[n] = nv;
                pq.push({nv, n});
            }
        }
    }

    void threats() {
        longestKnown = std::max(w.enemyKing.len, w.t.round - w.enemyLongestRound <= 100 ? w.enemyLongest : 0);
        passTail.assign(b.NC, 0);
        std::vector<std::pair<int, int>> basic, strict;
        for (Chain const& ch : chains()) {
            int n = static_cast<int>(ch.cells.size());
            if (ch.hasHead && !ch.truncated)  // a whole chain: its last ceil(L/4) segments leave before the next mover
                for (int i = std::max(1, n - (n + 3) / 4); i < n; i++) passTail[ch.cells[i]] = 1;
            if (!ch.enemy) continue;
            int le = ch.truncated ? (longestKnown > 0 ? std::max({n + 1, longestKnown, 6}) : 99) : n;
            if (ch.hasHead) {
                enemyHeads.push_back(ch.cells[0]);
                basic.push_back({ch.cells[0], rmax(le)});
#if BC_QUEEN_EXITFIX
                {
                    int lowLe = n, midLe = n;
                    if (ch.truncated) {
                        lowLe = std::max(n + 1, 3);
                        midLe = (n <= 2 && w.t.round < 300) ? std::max(n + 2, 4) : le;
                    }
                    lowSrc.push_back({ch.cells[0], rmax(lowLe)});
                    midSrc.push_back({ch.cells[0], rmax(midLe)});
                    if (!ch.truncated) certSrc.push_back({ch.cells[0], rmax(n)});
                }
#endif
                slackSrc.push_back({ch.cells[0], (std::min(le, 99) + 3) / 4});
                for (int j = 2; j <= le - 4 && j < n; j++) strict.push_back({ch.cells[j], rmax(le - j - 2)});
                if (!ch.truncated && n >= 4) basic.push_back({ch.cells.back(), rmax(n - 2)});
#if BC_QUEEN_EXITFIX
                if (!ch.truncated && n >= 4) lowSrc.push_back({ch.cells.back(), rmax(n - 2)}), midSrc.push_back({ch.cells.back(), rmax(n - 2)}), certSrc.push_back({ch.cells.back(), rmax(n - 2)});
#endif
            } else if (!ch.cells.empty() && cheb(ch.cells[0], head) < 3) {
                basic.push_back({ch.cells[0], rmax(std::max(n + 1, 6) - 2)});  // a tail tip whose head is out of view
#if BC_QUEEN_EXITFIX
                lowSrc.push_back({ch.cells[0], rmax(std::max(n + 1, 4) - 2)});
                midSrc.push_back({ch.cells[0], rmax(std::max(n + 1, 6) - 2)});
#endif
            }
        }
        // heads seen 1-3 rounds ago, out of sight now
        for (auto const& e : w.know.enemies) {
            int age = w.t.round - e.round;
            if (age < 1 || age > 3 || w.visible(e.cell)) continue;
            int le = std::max(e.len, 4);
            basic.push_back({e.cell, rmax(le) + (le + 3) / 4 * age});
            slackSrc.push_back({e.cell, (le + 3) / 4 * (age + 1)});
#if BC_QUEEN_EXITFIX
            {
                int le2 = std::max(e.len, 2);
                lowSrc.push_back({e.cell, rmax(le2) + (le2 + 3) / 4 * age});
                midSrc.push_back({e.cell, rmax(le) + (le + 3) / 4 * age});
            }
#endif
        }
        // portal mouths: a dragon of either team stepping through blind lands on the tile beside the portal edge
        // (seen in testing: our own dragon came through onto her head); reach 2 covers a free second step after it
#if BC_QSK_PATHHEAT
        // BC_QSK_PATHHEAT: "enemies about" by path steps on the known map (within 8, portals included), or her sector
        // enemy-held
        std::vector<int> pd(b.NC, kInf);
        {
            std::vector<int> q{head};
            pd[head] = 0;
            for (size_t i = 0; i < q.size(); i++) {
                if (pd[q[i]] >= 8) continue;
                for (int d = 0; d < 4; d++) {
                    int n = b.nb[q[i] * 4 + d];
                    if (n >= 0 && pd[n] == kInf) pd[n] = pd[q[i]] + 1, q.push_back(n);
                }
            }
        }
        bool about = w.enemyHeat[w.heatOf(head)] >= World::kHeatOn;
        for (auto const& e : w.know.enemies)
            if (w.t.round - e.round <= 10 && e.cell >= 0 && pd[e.cell] <= 8) about = true;
        for (int eh : enemyHeads)
            if (pd[eh] <= 8) about = true;
        aboutPath = about;
#endif
        for (int dy = -4; dy <= 4; dy++)
            for (int dx = -4; dx <= 4; dx++) {
                int c = b.id(b.X(head) + dx, b.Y(head) + dy);
                for (int d = 0; d < 4; d++)
                    if (b.side(c, d).kind == 2) {
#if BC_QSK_PATHHEAT
                        if (!about) {  // BC_QSK_PATHHEAT: only while enemies are about, or a paired mouth whose far
                                       // side is enemy-held
                            int land = b.stepRaw(c, d);
                            if (land < 0 || w.enemyHeat[w.heatOf(land)] < World::kHeatOn) continue;
                        }
#endif
                        basic.push_back({c, 2});
#if BC_QUEEN_EXITFIX
                        lowSrc.push_back({c, 2});
                        midSrc.push_back({c, 2});
#endif
                        break;
                    }
            }
        basicSrc = basic;
        strict.insert(strict.end(), basic.begin(), basic.end());
        // fog: tiles within 6 of her head, in view neither now nor last round, outside sectors our side holds - only
        // while enemies are about (a head seen or heard within 10 rounds and 8 tiles, or the enemy holds her sector):
        // fog everywhere kept her in the middle of her own view, eating nothing (in testing she was 3 long at r50)
#if BC_QSK_PATHHEAT
        bool const enemiesAbout = about;
#else
        bool enemiesAbout = w.enemyHeat[w.heatOf(head)] >= World::kHeatOn;
        for (auto const& e : w.know.enemies)
            if (w.t.round - e.round <= 10 && cheb(e.cell, head) <= 8) enemiesAbout = true;
#endif
        int fogR = rmax(std::max(longestKnown, 6));
        for (int dy = -6; dy <= 6 && enemiesAbout; dy++)
            for (int dx = -6; dx <= 6; dx++) {
                int c = b.id(b.X(head) + dx, b.Y(head) + dy);
                if (w.seenRound[c] >= w.t.round - 1) continue;
                if (w.friendHeat[w.heatOf(c)] >= World::kHeatOn) continue;
                strict.push_back({c, fogR});
            }
        margins(basic, mB);
        margins(strict, mS);
        if (slackOn) margins(slackSrc, mF, kSlackCap, false);
    }
    int slackOf(int c) const { return mF.empty() || mF[c] >= kInf ? kSlackCap + 1 : mF[c]; }

    // Her moves this turn. A landing keeps its first (fewest-steps) path.
    struct Land { int cell, steps, eats, paid; std::string path; std::vector<int> cells; std::vector<char> eatv; bool portalEnd; };
    std::vector<Land> landings(int maxSteps, int free) const {
        std::vector<Land> out;
        std::vector<int> seen(b.NC, -1);
        std::vector<Land> layer{{head, 0, 0, 0, "", {}, {}, false}};
        for (int s = 1; s <= maxSteps && !layer.empty(); s++) {
            std::vector<Land> next;
            for (Land const& f : layer) {
                if (f.portalEnd) continue;
                int at = f.cells.empty() ? head : f.cells.back();
                int vacated = (s - 1) - f.eats;  // tail cells left so far (paid steps leave more: ignored, conservative)
                int occupiedUpTo = static_cast<int>(body.size()) - 1 - vacated;
                int paid = std::max(0, s - free);
                if (L + f.eats - paid < 2) continue;  // cannot pay this step
                for (int d = 0; d < 4; d++) {
                    int n = b.nb[at * 4 + d];
                    if (n < 0 || seen[n] >= 0 || n == head) continue;
                    bool portal = b.side(at, d).kind == 2;
                    if (!w.visible(n) || cheb(n, head) > 3) continue;  // inside this turn's window only
                    if (w.occ[n] >= 0) continue;                       // a body or a head (never a head-on)
                    if (std::find(f.cells.begin(), f.cells.end(), n) != f.cells.end()) continue;
                    bool own = false;
                    for (int i = 0; i <= occupiedUpTo && i < static_cast<int>(body.size()); i++)
                        if (body[i] == n) own = true;
                    if (own) continue;
                    Land g = f;
                    bool eat = pearlAt(n);
                    g.cell = n;
                    g.steps = s;
                    g.eats += eat;
                    g.paid = paid;
                    g.path.push_back(DCH[d]);
                    g.cells.push_back(n);
                    g.eatv.push_back(static_cast<char>(eat));
                    g.portalEnd = portal;
                    seen[n] = s;
                    out.push_back(g);
                    next.push_back(g);
                }
            }
            layer.swap(next);
        }
        return out;
    }

#if BC_QUEEN_NODEAD
    // BC_QUEEN_NODEAD: a landing on a dead-end tile (Board::deadEnd: tiles of tree-like branches, repeatedly dropping
    // tiles with one way on) is a trap for a short queen - she cannot turn round in it (stepping back is HitSelf, a
    // 2-4 long dragon has no reverse split to spare) and dies there within a few rounds: in testing nearly all such
    // dips were kernel landings (most at T5, a few at T3 where the lane's length passed the room test), and nearly
    // all died within 10 rounds, none left. While she stands on an open tile every landing on a dead-end tile is
    // dropped, unless nothing else is legal; the tiers and T5 then rank what is left as before.
    void dropDead(std::vector<Land>& v) const {
        if (b.deadEnd.empty() || b.deadEnd[head]) return;
        bool anyOpen = false;
        for (Land const& g : v) anyOpen |= !b.deadEnd[g.cell];
        if (!anyOpen) return;
        v.erase(std::remove_if(v.begin(), v.end(), [&](Land const& g) { return b.deadEnd[g.cell] != 0; }), v.end());
    }
#endif

#if BC_QUEEN_BREATHE
    // BC_QUEEN_BREATHE: she keeps 2 free sides. A landing's free sides count every body and tail as blocked (the
    // engine checks the destination before the tail moves) and her new body too. While some landing has >= 2 free
    // sides and a basic margin >= 1, the landings with fewer are dropped; with none such and her boxed (<= 1 free
    // side) for tightTurns >= 2 turns running, only the landings with the most free sides, then the most room, stay -
    // even into a richer enemy reach: in replay reviews a boxed queen died on her own move every time.
    int tightTurns = 0;
    int breathe = 0;  // which rule acted on the free landings this turn: 1 the filter, 2 the escape (tests / trace)
    // BC_QUEEN_BREATHE 3: a side next to a teammate's head in view counts as blocked too (it may step there this round, after her)
    std::vector<char> teamNext;
    int secureExits(int c, std::vector<int> const& walls) const {
        if (teamNext.empty()) return exitsOf(c, walls);
        int n = 0;
        for (int d = 0; d < 4; d++) {
            int y = b.nb[c * 4 + d];
            if (y < 0 || w.occ[y] >= 0 || teamNext[y] || std::find(walls.begin(), walls.end(), y) != walls.end()) continue;
            n++;
        }
        return n;
    }
    void buildTeamNext() {
        teamNext.assign(b.NC, 0);
        for (Chain const& ch : chains())
            if (!ch.enemy && ch.hasHead)
                for (int d = 0; d < 4; d++)
                    if (int y = b.nb[ch.cells[0] * 4 + d]; y >= 0) teamNext[y] = 1;
    }
    int dropTight(std::vector<Land>& v) {
        if (v.empty()) return 0;
        if (BC_QUEEN_BREATHE >= 3 && teamNext.empty()) buildTeamNext();
        std::vector<int> ex(v.size());
        int bestEx = 0;
        bool safe2 = false;
        for (size_t i = 0; i < v.size(); i++) {
            ex[i] = secureExits(v[i].cell, newBody(v[i]));
            bestEx = std::max(bestEx, ex[i]);
            safe2 |= ex[i] >= 2 && mOf(mB, v[i].cell) >= 1;
        }
        std::vector<Land> keep;
        if (safe2) {
            for (size_t i = 0; i < v.size(); i++)
                if (ex[i] >= 2) keep.push_back(v[i]);
        } else if (tightTurns >= 2) {
            std::vector<int> rm(v.size(), -1);
            int bestRoom = -1;
            for (size_t i = 0; i < v.size(); i++)
                if (ex[i] == bestEx) rm[i] = room(v[i].cell, newBody(v[i]), L + 8), bestRoom = std::max(bestRoom, rm[i]);
            for (size_t i = 0; i < v.size(); i++)
                if (ex[i] == bestEx && rm[i] == bestRoom) keep.push_back(v[i]);
        }
        if (keep.empty() || keep.size() == v.size()) return 0;
        v.swap(keep);
        return safe2 ? 1 : 2;
    }
#endif

    // Free tiles reachable from c with her new body (and the child's cells for a hold) as walls, counted up to cap.
    int room(int c, std::vector<int> const& walls, int cap) {
        std::vector<int> q{c};
        std::vector<char> v(b.NC, 0);
        v[c] = 1;
        for (int x : walls) v[x] = 1;
        int n = 0;
        for (size_t i = 0; i < q.size() && n < cap; i++) {
            work++;
            for (int d = 0; d < 4; d++) {
                int y = b.nb[q[i] * 4 + d];
                if (y < 0 || v[y] || w.occ[y] >= 0) continue;
                v[y] = 1;
                n++;
                q.push_back(y);
            }
        }
        return n;
    }
    int exitsOf(int c, std::vector<int> const& walls) const {
        int n = 0;
        for (int d = 0; d < 4; d++) {
            int y = b.nb[c * 4 + d];
            if (y < 0 || w.occ[y] >= 0 || std::find(walls.begin(), walls.end(), y) != walls.end()) continue;
            n++;
        }
        return n;
    }
#if BC_QUEEN_BOXGUARD & 2
    std::vector<int> boxRiskK;  // BC_QUEEN_BOXGUARD & 2: this turn's risky tier-3 landings (index into the landings)
    // the one exit of landing c (her new body `walls` blocked) has a teammate's head in view next to it (it may step in before she moves)
    bool boxRisk(int c, std::vector<int> const& walls) const {
        int exit = -1;
        for (int d = 0; d < 4; d++) {
            int y = b.nb[c * 4 + d];
            if (y < 0 || w.occ[y] >= 0 || std::find(walls.begin(), walls.end(), y) != walls.end()) continue;
            exit = y;
        }
        if (exit < 0) return false;
        for (Chain const& ch : chains())
            if (!ch.enemy && ch.hasHead)
                for (int d = 0; d < 4; d++)
                    if (b.nb[ch.cells[0] * 4 + d] == exit) return true;
        return false;
    }
#endif
#if BC_QUEEN_KEEPAWAY
    std::vector<int> keepDist;  // BC_QUEEN_KEEPAWAY: path steps from the nearest known enemy head (walls and paired
                                // portals, bodies ignored), capped
    void buildKeep() {
        keepDist.assign(b.NC, kKeepAwayCap);
        std::vector<int> q;
        auto source = [&](int c) {
            if (c >= 0 && c < b.NC && keepDist[c] != 0) keepDist[c] = 0, q.push_back(c);
        };
        for (int h : enemyHeads) source(h);
        for (auto const& e : w.know.enemies)
            if (w.t.round - e.round <= kKeepAwayAge) source(e.cell);
        for (size_t i = 0; i < q.size(); i++) {
            int const c = q[i];
            for (int d = 0; d < 4; d++) {
                int n = b.nb[c * 4 + d];
                if (n < 0 || keepDist[n] <= keepDist[c] + 1) continue;
                keepDist[n] = keepDist[c] + 1;
                q.push_back(n);
            }
        }
    }
#endif
    bool nextToHead(int c) const {
        for (int d = 0; d < 4; d++) {
            int y = b.nb[c * 4 + d];
            if (y >= 0 && w.occHeadId[y] >= 0) return true;
        }
        return false;
    }
    bool enemyHeadNear(int c) const {
        for (int h : enemyHeads)
            if (cheb(h, c) <= 3) return true;
        return false;
    }
    // Her body after a path (head first): the path reversed, then the old body minus the cells it left.
    std::vector<int> newBody(Land const& g) const {
        std::vector<int> nb2(g.cells.rbegin(), g.cells.rend());
        int keep = static_cast<int>(body.size()) - (g.steps - g.eats) - g.paid;
        for (int i = 0; i < keep && i < static_cast<int>(body.size()); i++) nb2.push_back(body[i]);
        return nb2;
    }
    int mOf(std::vector<int> const& m, int c) const { return m[c] >= kInf ? kCap + 1 : m[c]; }

    void buildFood() {
        foodDist.assign(b.NC, kInf);
        std::vector<int> q;
        for (int c = 0; c < b.NC; c++)
            if (w.seenPearl[c] && w.seenRound[c] >= w.t.round - 5 && w.occ[c] < 0) foodDist[c] = 0, q.push_back(c);
#if BC_QFEED_SRC
        // BC_QFEED_SRC: food she has not seen lately - a teammate's HOT fact (a fountain) heard within kQFeedHotAge
        // rounds, a bed out of view whose tracked countdown ran out unseen, and a never-seen mirrored bed whose
        // refill gap is known and past (the shared map's half)
        auto source = [&](int c) {
            if (c >= 0 && c < b.NC && foodDist[c] != 0 && w.occ[c] < 0) foodDist[c] = 0, q.push_back(c);
        };
        if (feed350) {
            for (auto const& h : w.know.hot)
                if (w.t.round - h.round <= kQFeedHotAge && w.visRound[h.cell] != w.t.round) source(h.cell);
            for (int c = 0; c < b.NC; c++) {
                if (b.bed[c] != 1 || w.visRound[c] == w.t.round) continue;
                bool const due = (w.seenRound[c] >= 0 && w.lastSpawn[c] > w.seenRound[c]) ||
                                 (w.seenRound[c] < 0 && w.mirrored[c] && b.gapMax[c] > 0 && w.t.round >= b.gapMax[c]);
                if (due) source(c);
            }
        }
#endif
        for (size_t i = 0; i < q.size(); i++)
            for (int d = 0; d < 4; d++) {
                int n = b.nb[q[i] * 4 + d];
                if (n < 0 || foodDist[n] != kInf || w.occ[n] >= 0) continue;
                foodDist[n] = foodDist[q[i]] + 1;
                q.push_back(n);
            }
    }
    // total[d]: the search value of the 1-step move d (legal ones only), used inside a tier.
    QskOut run(double const* total, bool const* legal1, double clockLimit) {
        QskOut o;
        if (body.empty() || L < 2) return o;
        threats();
        int free = (L + 3) / 4;
        auto fill = [&](Land const& g, int tier, int mb, int ms, int rm, int ex) {
            o = QskOut();
            o.ok = true;
            o.tier = tier;
            o.dir = dirFromChar(g.path[0]);
            o.dest = g.cell;
            o.path = g.path;
            o.cells = g.cells;
            o.eats = g.eatv;
            o.paid = g.paid;
            o.m = mb;
            o.mStrict = ms;
            o.room = rm;
            o.exits = ex;
            o.steps = g.steps;
        };
        // T1-T3 over the free landings
        std::vector<Land> F = landings(free, free);
#if BC_QUEEN_NODEAD
        dropDead(F);
#endif
#if BC_QUEEN_BREATHE
        breathe = dropTight(F);
#endif
        if (slackOn) {  // the hard slack filter: when some landing keeps slack >= 3, only those
            bool any = false;
            for (Land const& g : F) any |= slackOf(g.cell) >= 3;
            if (any) F.erase(std::remove_if(F.begin(), F.end(), [&](Land const& g) { return slackOf(g.cell) < 3; }), F.end());
        }
        struct Scored { int k, tier, mb, ms, rm, ex; };
        std::vector<Scored> sc;
        for (int k = 0; k < static_cast<int>(F.size()); k++) {
            if (clockNow() > clockLimit) break;
            Land const& g = F[k];
            std::vector<int> nb2 = newBody(g);
            int newL = L + g.eats;
            int mb = mOf(mB, g.cell), ms = mOf(mS, g.cell);
            int rm = room(g.cell, nb2, newL + 8);
            int ex = exitsOf(g.cell, nb2);
            int tier = 9;
            if (ms >= 1 && !enemyHeadNear(g.cell) && rm >= newL + 8 && ex >= 2 && !nextToHead(g.cell)) tier = 1;
            else if (ms >= 1 && rm >= newL + 8 && ex >= 2) tier = 2;
            else if (mb >= 1 && rm >= newL + 2) tier = 3;
            sc.push_back({k, tier, mb, ms, rm, ex});
#if BC_QUEEN_BOXGUARD & 2
            if (tier == 3 && ex == 1 && boxRisk(g.cell, nb2)) boxRiskK.push_back(k);
#endif
        }
#if BC_QUEEN_BOXGUARD & 2
        // BC_QUEEN_BOXGUARD & 2: a tier-3 landing with one exit next to a teammate's head is dropped while another
        // tier-3 landing is left
        if (!boxRiskK.empty()) {
            auto risky = [&](Scored const& z) { return z.tier == 3 && std::find(boxRiskK.begin(), boxRiskK.end(), z.k) != boxRiskK.end(); };
            bool other = false;
            for (Scored const& z : sc) other |= z.tier == 3 && !risky(z);
            bool better = false;
            for (Scored const& z : sc) better |= z.tier < 3;
            if (other && !better) sc.erase(std::remove_if(sc.begin(), sc.end(), risky), sc.end());
        }
#endif
        int bestTier = 9;
        for (Scored const& s : sc) bestTier = std::min(bestTier, s.tier);
        if (bestTier <= 3) {
            // inside the tier: the strict margin first (capped; in T3 it is what keeps her out of fresh fog - in
            // testing the search led her into unseen tiles and 4 heads showed up at once), then the search's own
            // 1-step choice; multi-step landings only when no 1-step one is that safe, by margin, food, fewer steps
            // slack first (capped at 8: >= 6 is already < 0.7% risk in replays), then the strict margin
            int bestSl = -kInf;
            for (Scored const& s : sc)
                if (s.tier == bestTier) bestSl = std::max(bestSl, std::min(slackOf(F[s.k].cell), 8));
            for (auto it = sc.begin(); it != sc.end();)
                it = (it->tier == bestTier && std::min(slackOf(F[it->k].cell), 8) < bestSl) ? sc.erase(it) : it + 1;
            int bestMs = -kInf;
            for (Scored const& s : sc)
                if (s.tier == bestTier) bestMs = std::max(bestMs, std::min(s.ms, kCap));
            Scored const* pick = nullptr;
            double pv = -1e18;
            // growth arm: no threat within her reach + 3 - the nearest pearl first (the search's values left her
            // nothing: her teammates claim every target; in a test trace she ate 0 pearls in 287 rounds)
            bool food = grow && mOf(mB, head) >= 3;
            if (food) buildFood();
#if BC_QUEEN_KEEPAWAY
            buildKeep();
#endif
#if BC_QFEED_PULL
            if (feed350 && !food) buildFood();  // BC_QFEED_PULL: her pull to the nearest food
#endif
            for (Scored const& s : sc) {
                Land const& g = F[s.k];
                if (s.tier != bestTier || g.steps != 1 || (!food && std::min(s.ms, kCap) < bestMs)) continue;
                int d = dirFromChar(g.path[0]);
                if (legal1 && !legal1[d]) continue;
                double v = (total ? total[d] : 0.0) - (food ? 1000.0 * std::min(foodDist[g.cell], 60) : 0.0) +
                           1e7 * homeBucket(g.cell);  // BC_QUEEN home: our side's interior before food and the search
                                                      // value
#if BC_QUEEN_KEEPAWAY
                v += kKeepAwayW * keepDist[g.cell];  // BC_QUEEN_KEEPAWAY: farther from the nearest enemy head first
#endif
#if BC_QFEED_EAT
                if (feed350 && g.eats > 0) v += 1e9;  // BC_QFEED_EAT: a landing that eats first, among those this
                                                      // order keeps (her best tier, slack, margin)
#endif
#if BC_QFEED_PULL
                // BC_QFEED_PULL: then the landing nearest a pearl within kQFeedPull steps (over free tiles); 5e7 a
                // step beats her home bucket (1e7 a bucket: adjacent landings' home scores differ by <= 4, <= 2
                // buckets)
                if (feed350 && foodDist[g.cell] <= kQFeedPull) v += 5e8 + 5e7 * (kQFeedPull - foodDist[g.cell]);
#endif
                if (!pick || v > pv) pick = &s, pv = v;
            }
            auto key = [&](Scored const& z) {
                return std::make_tuple(std::min(z.ms, kCap), homeBucket(F[z.k].cell), std::min(z.mb, kCap), F[z.k].eats, -F[z.k].steps);
            };
            if (!pick)
                for (Scored const& s : sc)
                    if (s.tier == bestTier && (!pick || key(s) > key(*pick))) pick = &s;
            if (pick) fill(F[pick->k], bestTier, pick->mb, pick->ms, pick->rm, pick->ex);
            return o;
        }
        // T4: m >= 1 by paying - the SPLIT 2 hold or a paid path - the least paid, then the larger margin
        if (clockNow() > clockLimit) return o;
        // at most kMaxPay segments a turn and she keeps kKeepLen: in a swarm a looser T4 (up to min(L - 2, 4) paid,
        // the hold at 4) drained her to 2 in three turns (5 -> 2 in a test), and a 2-long queen cannot run
        int extra = std::max(0, std::min(kMaxPay, L - kKeepLen));
        std::vector<Land> P = extra > 0 ? landings(free + extra, free) : std::vector<Land>{};
#if BC_QUEEN_NODEAD
        dropDead(P);
#endif
#if BC_QUEEN_BREATHE
        dropTight(P);
#endif
        int bestPay = kInf, bestM = -kInf;
        Land const* bestLand = nullptr;
        bool holdOk = L - 2 >= kHoldKeep && w.t.units < w.init.unitLimit;  // an illegal split kills the parent
        int holdM = mOf(mB, head);
        for (Land const& g : P) {
            if (g.paid == 0) continue;
            int mb = mOf(mB, g.cell);
            if (mb < 1) continue;
            if (g.paid < bestPay || (g.paid == bestPay && mb > bestM)) bestPay = g.paid, bestM = mb, bestLand = &g;
        }
        bool hold = holdOk && holdM >= 1 && (2 < bestPay || (2 == bestPay && holdM > bestM));
        if (hold) {
            o = QskOut();
            o.ok = true;
            o.tier = 4;
            o.hold = true;
            o.dest = head;
            o.m = holdM;
            return o;
        }
        if (bestLand) {
            std::vector<int> nb2 = newBody(*bestLand);
            fill(*bestLand, 4, bestM, mOf(mS, bestLand->cell), 0, exitsOf(bestLand->cell, nb2));
            return o;
        }
        // T5: the largest margin among the free landings (paying for a margin that stays below 1 buys nothing); the paid
        // paths and the hold only when no free step is legal
#if BC_QUEEN_EXITFIX
        if (!F.empty()) {
            margins(certSrc, mC);
            margins(lowSrc, mL);
            margins(midSrc, mM);
            // the certain threats rank only when they tell the landings apart (some reachable by a whole visible
            // chain, some not): with none in view every landing is "certain-safe" and the order would fall to the low
            // lengths alone, which in a counterfactual replay study was worse than the kernel's own order in a few
            // changed turns
            bool certSome = false, certNone = false;
            for (Land const& g : F) (mOf(mC, g.cell) >= 1 ? certNone : certSome) = true;
            bool certSplits = certSome && certNone;
            int xk = -1, bcls = 0;
            std::tuple<int, int, int, int, int, int, int, int> bkey;
            for (int k = 0; k < static_cast<int>(F.size()); k++) {
                Land const& g = F[k];
                int mcert = mOf(mC, g.cell), mlo = mOf(mL, g.cell), mmid = mOf(mM, g.cell), mb = mOf(mB, g.cell);
                int cert = certSplits && mcert >= 1 ? 1 : 0;
                int cls = mmid >= 1 ? 2 : mlo >= 1 ? 1 : 0;
                if (cls == 0 && !cert && BC_QUEEN_EXITFIX < 2) continue;
                bool known = k < static_cast<int>(sc.size());
                int rm = known ? sc[k].rm : 0, ex = known ? sc[k].ex : 0;
                auto key = std::make_tuple(cert, cls, !known || rm >= L + g.eats + 2 ? 1 : 0, std::min(mlo, kCap), std::min(mb, kCap), std::min(ex, 2), g.eats, -g.steps);
                if (xk < 0 || key > bkey) xk = k, bkey = key, bcls = cls;
            }
            if (xk >= 0) {
                Land const& g = F[xk];
                fill(g, 5, mOf(mB, g.cell), mOf(mS, g.cell), 0, 0);
                o.xcls = bcls;
                return o;
            }
        }
#endif
        int bm = -kInf;
        Land const* bl = nullptr;
        for (Land const& g : F) if (mOf(mB, g.cell) > bm) bm = mOf(mB, g.cell), bl = &g;
        if (!bl)
            for (Land const& g : P) if (mOf(mB, g.cell) > bm) bm = mOf(mB, g.cell), bl = &g;
        if (!bl && holdOk) {
            o = QskOut();
            o.ok = true;
            o.tier = 5;
            o.hold = true;
            o.dest = head;
            o.m = holdM;
            return o;
        }
        if (bl) fill(*bl, 5, bm, mOf(mS, bl->cell), 0, 0);
        return o;
    }
};

}  // namespace bc
