#pragma once
// A dragon's own non-local memory as extra net inputs ("F2"), built only from what this dragon has seen: its 7x7 views
// since it was born (a split child starts empty, like the engine's one process per dragon; no sonar unless F2_HEARD).
// One implementation for both sides: rl/featurize/featurize_f2.cpp replays each dragon's turns in order through it, and
// a bot calls mem.update(init, t) every turn, then mem.features(init, t, net.priv) before net.run(init, t) (rl_f1.hpp
// with F2 weights, F1_NPRIV = F2_NMEM; f2_run.hpp does the three calls), so training sees exactly what the bot computes.
// Remembered: pearls seen and not seen gone since, beds with their predicted next spawn (the countdown in view), kelp and
// portal edges seen (both treated as walls: a portal's partner is unknown), enemy heads seen (the newest 8), the longest
// teammate seen (visible length), the tiles ever in view. Inputs (F2_NMEM, each quantised to a byte like the records):
//   0-3   nearest known pearl through each first step (relative to the facing: forward, right, back, left), exp(-d / 10)
//   4-7   nearest bed by max(steps, rounds to its spawn) through each first step (a bed past its spawn and out of view
//         counts as a pearl), exp(-t / 10)
//   8-11  nearest enemy head seen in the last 10 rounds, exp(-d / 10)
//   12-15 the longest teammate seen in the last 30 rounds, if longer than us, exp(-d / 10)
//   16 explored share, 17 known pearls / 16, 18 known beds / 32, 19 longest teammate known, 20 our length / its length,
//   21 exp(-rounds since an enemy head was seen / 20), 22 rounds alive / 100, 23 reserved (0)
// Distances: BFS over the torus from the tile after the first step, through edges not known as walls, around the bodies
// in view, at most F2_DEPTH steps. Cost: four BFS over at most W x H tiles a turn.
// F2_HEARD (define it before including): also what our own sonar messages told this dragon (f2_comms.hpp decodes the
// released bot's protocol; rays of other bots fail the tag check): the LONGEST fact (our longest dragon), ENEMY heads, HOT
// beds, SELF heads of teammates and the longest enemy dragon (SYM sub 3), 20 more inputs (F2_NMEM 44):
//   24-27 the heard longest teammate (heard in the last 30 rounds, longer than us) through each first step, exp(-d / 10)
//   28-31 the nearest heard enemy head (last 10 rounds), 32-35 the nearest heard hot bed (last 60 rounds)
//   36 heard longest teammate known, 37 its length / 64, 38 heard longest enemy known (last 50 rounds), 39 its length
//   / 64, 40 our length / the longest enemy's (1 when unknown), 41 teammates heard in the last 20 rounds / 16,
//   42 exp(-rounds since the longest teammate was heard / 20), 43 reserved (0)
// Only games of bots that speak this protocol carry decodable messages: records from other bots' games have these
// inputs at 0, so a policy trained on such records never saw them non-zero (see rl/README.md, lessons).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#ifndef F2_FEATURES_INCLUDED  // rl/featurize/featurize_f2.cpp includes rl_features.hpp itself
#include "rl_features.hpp"
#endif
#ifdef F2_HEARD
#include "f2_comms.hpp"
#endif

namespace bc {

#ifdef F2_HEARD
inline constexpr int F2_NMEM = 44;
#else
inline constexpr int F2_NMEM = 24;
#endif
inline constexpr int F2_DEPTH = 48;

struct F2Memory {
    int w = 0, h = 0, born = -1, explored = 0;
    std::vector<int16_t> seenAt;   // last round in view, -1 never
    std::vector<int16_t> bedNext;  // a known bed's predicted next spawn round, -1 not a known bed
    std::vector<uint8_t> pearl;    // a pearl seen here, the tile not seen empty since
    std::vector<uint8_t> wallN, wallW;  // a kelp or portal edge seen on the tile's north / west side
    std::vector<int> beds;         // known bed tiles
    struct Seen { int id, tile, round, len; };
    std::vector<Seen> enemies;     // enemy heads seen, newest last
    Seen king{-1, -1, -1, 0};      // the longest teammate seen
#ifdef F2_HEARD
    Seen hKing{-1, -1, -1000, 0}, hEKing{-1, -1, -1000, 0};  // LONGEST (our longest) and SYM sub 3 (the enemy's) as heard
    std::vector<Seen> hEnemies, hHot, hMates;                 // ENEMY heads, HOT beds (id = the tile), SELF heads
    static void upsert(std::vector<Seen>& v, Seen s, size_t cap) {
        for (Seen& x : v)
            if (x.id == s.id) {
                if (s.round >= x.round) x = s;
                return;
            }
        v.push_back(s);
        if (v.size() > cap) {
            size_t old = 0;
            for (size_t i = 1; i < v.size(); i++)
                if (v[i].round < v[old].round) old = i;
            v.erase(v.begin() + static_cast<long>(old));
        }
    }
#endif
    std::vector<int16_t> dist;     // BFS scratch
    std::vector<int> queue;
    std::vector<uint8_t> blocked;

    int tile(int x, int y) const { return ((y % h + h) % h) * w + ((x % w + w) % w); }

    void update(Init const& init, Turn const& t) {
        if (w == 0) {
            w = init.w, h = init.h;
            size_t const n = static_cast<size_t>(w) * h;
            seenAt.assign(n, -1), bedNext.assign(n, -1), pearl.assign(n, 0), wallN.assign(n, 0), wallW.assign(n, 0);
            dist.assign(n, -1), blocked.assign(n, 0);
            queue.reserve(n);
            born = t.round;
        }
        int const now = t.round, hx = t.tiles[24].x, hy = t.tiles[24].y;
        for (VisTile const& v : t.tiles) {
            int const i = tile(v.x, v.y);
            if (seenAt[i] < 0) explored++;
            seenAt[i] = static_cast<int16_t>(now);
            pearl[i] = v.pearl ? 1 : 0;
            if (v.pearlIn >= 0) {
                if (bedNext[i] < 0) beds.push_back(i);
                bedNext[i] = static_cast<int16_t>(now + v.pearlIn);
            }
        }
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 7; c++)
                if (t.hRows[r][c].kind != 0) wallN[tile(hx + c - 3, hy + r - 3)] = 1;
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 8; c++)
                if (t.vRows[r][c].kind != 0) wallW[tile(hx + c - 3, hy + r - 3)] = 1;
        // heads and visible lengths in view
        struct Vis { int id, tile, len; char team; };
        std::vector<Vis> vis;
        for (Part const& p : t.parts) {
            if (p.id == init.id) continue;
            auto it = std::find_if(vis.begin(), vis.end(), [&](Vis const& v) { return v.id == p.id; });
            if (it == vis.end()) vis.push_back({p.id, -1, 0, p.team}), it = vis.end() - 1;
            it->len++;
            if (p.head) it->tile = tile(p.x, p.y);
        }
        for (Vis const& v : vis) {
            if (v.tile < 0) continue;
            if (v.team != init.team) {
                auto it = std::find_if(enemies.begin(), enemies.end(), [&](Seen const& s) { return s.id == v.id; });
                if (it != enemies.end()) enemies.erase(it);
                enemies.push_back({v.id, v.tile, now, v.len});
                if (enemies.size() > 8) enemies.erase(enemies.begin());
            } else if (v.id == king.id || king.id < 0 || now - king.round > 30 || v.len > king.len) {
                king = {v.id, v.tile, now, std::max(v.len, v.id == king.id ? king.len : 0)};
            }
        }
#ifdef F2_HEARD
        for (uint64_t m : t.msgs) {
            f2comms::Fact f;
            if (!f2comms::decode(m, init.team, now, f)) continue;
            int const tl = tile(f.x, f.y);
            if (f.type == f2comms::LONGEST) {
                if (f.aux != (init.id & 4095) && f.round >= hKing.round) hKing = {f.aux, tl, f.round, f.val};
            } else if (f.type == f2comms::ENEMY) {
                upsert(hEnemies, {f.aux, tl, f.round, f.val}, 12);
            } else if (f.type == f2comms::HOT) {
                upsert(hHot, {tl, tl, f.round, f.val}, 12);
            } else if (f.type == f2comms::SELF) {
                if (f.aux != (init.id & 4095)) upsert(hMates, {f.aux, tl, f.round, f.val}, 16);
            } else if (f.type == f2comms::SYM && f.sub == 3) {
                if (f.round >= hEKing.round) hEKing = {f.aux, tl, f.round, f.val};
            }
        }
#endif
    }

    // BFS from `from` (one step taken), distances in steps from the head
    void bfs(int from) {
        std::fill(dist.begin(), dist.end(), static_cast<int16_t>(-1));
        queue.clear();
        dist[from] = 1;
        queue.push_back(from);
        for (size_t q = 0; q < queue.size(); q++) {
            int const a = queue[q], d = dist[a];
            if (d >= F2_DEPTH) continue;
            int const ax = a % w, ay = a / w;
            for (int k = 0; k < 4; k++) {
                int const b = tile(ax + DX[k], ay + DY[k]);
                if (dist[b] >= 0 || blocked[b]) continue;
                if (wall(a, b, k)) continue;
                dist[b] = static_cast<int16_t>(d + 1);
                queue.push_back(b);
            }
        }
    }
    // the edge crossed stepping from a to its neighbour b in absolute direction k
    bool wall(int a, int b, int k) const {
        switch (k) {
        case N: return wallN[a] != 0;
        case S: return wallN[b] != 0;
        case W: return wallW[a] != 0;
        default: return wallW[b] != 0;
        }
    }

    static float q8(float v) { return std::round(std::min(1.0f, std::max(0.0f, v)) * 255.0f) / 255.0f; }
    static float near(int d) { return d > 0 ? std::exp(-d / 10.0f) : 0.0f; }

    void features(Init const& init, Turn const& t, float* out) {
        std::fill(out, out + F2_NMEM, 0.0f);
        if (w == 0) return;
        int const now = t.round, head = tile(t.tiles[24].x, t.tiles[24].y);
        std::vector<int> body;
        for (Part const& p : t.parts) body.push_back(tile(p.x, p.y));
        for (int b : body) blocked[b] = 1;
        blocked[head] = 1;
        bool const kingOn = king.id >= 0 && now - king.round <= 30 && king.len > t.length;
#ifdef F2_HEARD
        bool const hKingOn = hKing.tile >= 0 && now - hKing.round <= 30 && hKing.len > t.length;
        bool const hEKingOn = hEKing.tile >= 0 && now - hEKing.round <= 50;
#endif
        for (int rel = 0; rel < 4; rel++) {
            int const d = absDir(rel, t.dir);
            int const n = tile(t.tiles[24].x + DX[d], t.tiles[24].y + DY[d]);
            if (blocked[n] || wall(head, n, d)) continue;
            bfs(n);
            int best = 0;
            for (size_t i = 0; i < queue.size() && !best; i++)
                if (pearl[queue[i]]) best = dist[queue[i]];
            out[rel] = near(best);
            int bt = 0;
            for (int b : beds) {
                if (dist[b] < 0 || pearl[b]) continue;  // unreachable, or a known pearl (inputs 0-3)
                int const spawn = bedNext[b] - now;
                if (spawn < 0 && seenAt[b] == now) continue;  // in view, past its countdown and empty: nothing there
                int const tt = std::max<int>(dist[b], spawn);  // past its spawn out of view: likely a pearl, the steps
                if (!bt || tt < bt) bt = tt;
            }
            out[4 + rel] = near(bt);
            int be = 0;
            for (Seen const& e : enemies)
                if (now - e.round <= 10 && dist[e.tile] > 0 && (!be || dist[e.tile] < be)) be = dist[e.tile];
            out[8 + rel] = near(be);
            if (kingOn && dist[king.tile] > 0) out[12 + rel] = near(dist[king.tile]);
#ifdef F2_HEARD
            if (hKingOn && dist[hKing.tile] > 0) out[24 + rel] = near(dist[hKing.tile]);
            int bh = 0, bb = 0;
            for (Seen const& e : hEnemies)
                if (now - e.round <= 10 && dist[e.tile] > 0 && (!bh || dist[e.tile] < bh)) bh = dist[e.tile];
            for (Seen const& e : hHot)
                if (now - e.round <= 60 && dist[e.tile] > 0 && (!bb || dist[e.tile] < bb)) bb = dist[e.tile];
            out[28 + rel] = near(bh);
            out[32 + rel] = near(bb);
#endif
        }
        for (int b : body) blocked[b] = 0;
        blocked[head] = 0;
        int pearls = 0;
        for (uint8_t p : pearl) pearls += p;
        out[16] = static_cast<float>(explored) / (static_cast<float>(w) * h);
        out[17] = std::min(1.0f, pearls / 16.0f);
        out[18] = std::min(1.0f, beds.size() / 32.0f);
        out[19] = kingOn ? 1.0f : 0.0f;
        out[20] = kingOn ? static_cast<float>(t.length) / king.len : 1.0f;
        int last = -1;
        for (Seen const& e : enemies) last = std::max(last, e.round);
        out[21] = last >= 0 ? std::exp(-(now - last) / 20.0f) : 0.0f;
        out[22] = std::min(1.0f, (now - born) / 100.0f);
#ifdef F2_HEARD
        out[36] = hKingOn ? 1.0f : 0.0f;
        out[37] = hKingOn ? std::min(1.0f, hKing.len / 64.0f) : 0.0f;
        out[38] = hEKingOn ? 1.0f : 0.0f;
        out[39] = hEKingOn ? std::min(1.0f, hEKing.len / 64.0f) : 0.0f;
        out[40] = hEKingOn && hEKing.len > 0 ? std::min(1.0f, static_cast<float>(t.length) / hEKing.len) : 1.0f;
        int mates = 0;
        for (Seen const& e : hMates) mates += now - e.round <= 20;
        out[41] = std::min(1.0f, mates / 16.0f);
        out[42] = hKing.tile >= 0 ? std::exp(-(now - hKing.round) / 20.0f) : 0.0f;
#endif
        for (int k = 0; k < F2_NMEM; k++) out[k] = q8(out[k]);
    }
};

}  // namespace bc
