#pragma once
// Observation features shared by the offline training featurizer and by play (rl_net.hpp), so the two can never drift
// apart. The view is rotated so our facing is "up": actions are learned relative to facing (0 forward, 1 right, 2
// back, 3 left).
#include "io.hpp"

namespace bc {

inline constexpr int FC = 17;  // grid channels
inline constexpr int FS = 10;  // scalars
inline constexpr int FDIM = FC * 49 + FS;

struct Features {
    float v[FDIM];
    float* grid(int c, int r, int col) { return &v[(c * 7 + r) * 7 + col]; }
};

// Screen coordinates (y down). One quarter turn that brings our facing to North.
inline void rotateOnce(int& dx, int& dy) {
    int t = dx;
    dx = dy;
    dy = -t;
}

inline int relDir(int abs, int facing) { return (abs - facing) & 3; }
inline int absDir(int rel, int facing) { return (rel + facing) & 3; }

inline void featurize(Init const& init, Turn const& t, Features& f) {
    std::fill(std::begin(f.v), std::end(f.v), 0.0f);
    int const face = t.dir;
    int const hx = t.tiles[24].x, hy = t.tiles[24].y;  // vision centre = our head
    auto place = [&](int ax, int ay, int& col, int& row) {
        int dx = ax - hx, dy = ay - hy;
        // wrap onto -3..3 (the map is a torus and at least 7 wide)
        if (dx > 3) dx -= init.w;
        if (dx < -3) dx += init.w;
        if (dy > 3) dy -= init.h;
        if (dy < -3) dy += init.h;
        for (int k = 0; k < face; k++) rotateOnce(dx, dy);
        col = dx + 3;
        row = dy + 3;
    };
    for (int i = 0; i < 49; i++) {
        VisTile const& v = t.tiles[i];
        int col, row;
        place(v.x, v.y, col, row);
        if (col < 0 || col > 6 || row < 0 || row > 6) continue;
        *f.grid(0, row, col) = v.pearl ? 1.0f : 0.0f;
        if (v.pearlIn >= 0) {
            *f.grid(1, row, col) = 1.0f;
            *f.grid(2, row, col) = std::exp(-v.pearlIn / 8.0f);
        }
    }
    for (Part const& p : t.parts) {
        int col, row;
        place(p.x, p.y, col, row);
        if (col < 0 || col > 6 || row < 0 || row > 6) continue;
        bool mine = p.id == init.id;
        bool friendly = p.team == init.team;
        int ch = mine ? 3 : friendly ? 4 : 5;
        *f.grid(ch, row, col) = 1.0f;
        if (p.head && !mine) {
            *f.grid(friendly ? 6 : 7, row, col) = 1.0f;
            *f.grid(16, row, col) = (relDir(p.facing, face) + 1) / 4.0f;
        }
    }
    // Edges: the absolute side of a vision tile becomes a side relative to our facing.
    auto edge = [&](int col, int row, int absSide, EdgeObs const& e) {
        if (e.kind == 0) return;
        int ax = hx + col - 3, ay = hy + row - 3, c, r;
        place(ax, ay, c, r);
        if (c < 0 || c > 6 || r < 0 || r > 6) return;
        int side = relDir(absSide, face);
        *f.grid((e.kind == 1 ? 8 : 12) + side, r, c) = 1.0f;
    };
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 7; c++) {
            if (r < 7) edge(c, r, N, t.hRows[r][c]);
            if (r > 0) edge(c, r - 1, S, t.hRows[r][c]);
        }
    for (int r = 0; r < 7; r++)
        for (int c = 0; c < 8; c++) {
            if (c < 7) edge(c, r, W, t.vRows[r][c]);
            if (c > 0) edge(c - 1, r, E, t.vRows[r][c]);
        }
    float* s = &f.v[FC * 49];
    s[0] = t.round / 500.0f;
    s[1] = std::min(t.length, 32) / 16.0f;
    s[2] = t.units / 64.0f;
    s[3] = std::min<int>(static_cast<int>(t.msgs.size()), 4) / 4.0f;
    for (int k = 0; k < 5; k++) s[4 + k] = std::min(t.echoes[k], 4) / 2.0f;
    s[9] = t.round >= 400 ? 1.0f : 0.0f;
}

// Action classes: 0-3 single step (relative dir), 4-7 multi-step sprint (first step), 8 split.
inline constexpr int NCLASS = 9;

}  // namespace bc
