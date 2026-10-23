#pragma once

#include "common.hpp"

namespace bc {

// One boundary between two tiles. hE[c] is the top edge of tile c, vE[c] its left edge.
struct Edge {
    int8_t kind = 0;      // 0 empty, 1 kelp, 2 portal
    int8_t partnerOrient = -1;  // portal partner: 0 horizontal, 1 vertical, -1 unknown
    int16_t px = -1, py = -1;
};

struct Board {
    int W = 0, H = 0, NC = 0;
    int sym = -1;  // 0 none, 1 x, 2 y, 3 xy, -1 unknown
    std::vector<Edge> hE, vE;
    std::vector<int8_t> bed;  // spawns pearls (1), not (0), unknown (-1)
    std::vector<int16_t> gapMin, gapMax;
    std::vector<int> nb;      // nb[c*4+d]: tile after stepping, -1 if blocked/unknown portal
    std::vector<int8_t> portalSide;  // tile has a portal on one of its sides (a landing tile)

    void init(int w, int h) {
        W = w; H = h; NC = w * h;
        hE.assign(NC, Edge{});
        vE.assign(NC, Edge{});
        bed.assign(NC, -1);
        gapMin.assign(NC, 0);
        gapMax.assign(NC, 0);
        rebuild();
    }

    int id(int x, int y) const {
        x %= W; if (x < 0) x += W;
        y %= H; if (y < 0) y += H;
        return y * W + x;
    }
    int X(int c) const { return c % W; }
    int Y(int c) const { return c / W; }

    Edge const& side(int c, int d) const {
        int x = X(c), y = Y(c);
        switch (d) {
        case N: return hE[c];
        case S: return hE[id(x, y + 1)];
        case Dir::W: return vE[c];
        default: return vE[id(x + 1, y)];
        }
    }

    // Mirrors the engine's TileAfterStep + TileAfterCrossing (engine/src/helpers.cc in the toolkit).
    int stepRaw(int c, int d) const {
        Edge const& e = side(c, d);
        if (e.kind == 1) return -1;
        if (e.kind == 2) {
            if (e.partnerOrient < 0) return -1;
            if (e.partnerOrient == 0) return id(e.px, d == S ? e.py : e.py - 1);
            return id(d == E ? e.px : e.px - 1, e.py);
        }
        return id(X(c) + DX[d], Y(c) + DY[d]);
    }

    void rebuild() {
        nb.assign(NC * 4, -1);
        portalSide.assign(NC, 0);
        for (int c = 0; c < NC; c++)
            for (int d = 0; d < 4; d++) {
                nb[c * 4 + d] = stepRaw(c, d);
                if (side(c, d).kind == 2) portalSide[c] = 1;
            }
        if (BC_DEADEND) computeDeadEnds();
    }

    // BC_DEADEND: tiles of tree-like branches of the known map (repeatedly drop tiles with one way on; an unpaired
    // portal counts as a way out). A snake can never turn round in one: it dies there or leaves by a reverse split, a
    // 2-long stub staying behind.
    std::vector<int8_t> deadEnd;
    void computeDeadEnds() {
        deadEnd.assign(NC, 0);
        std::vector<int> deg(NC, 0), stack;
        for (int c = 0; c < NC; c++) {
            for (int d = 0; d < 4; d++) {
                if (nb[c * 4 + d] >= 0) deg[c]++;
                else if (side(c, d).kind == 2) deg[c] += 2;
            }
            if (deg[c] <= 1) stack.push_back(c);
        }
        while (!stack.empty()) {
            int c = stack.back();
            stack.pop_back();
            if (deadEnd[c]) continue;
            deadEnd[c] = 1;
            for (int d = 0; d < 4; d++) {
                int n = nb[c * 4 + d];
                if (n >= 0 && !deadEnd[n] && --deg[n] <= 1) stack.push_back(n);
            }
        }
    }

    // Mirror image of tile c under symmetry s (the map header's SYMMETRY: 1 x = top-bottom, 2 y = left-right, 3 xy =
    // point). Every official map is exactly symmetric in beds, gaps and edges (checked on the official map pool).
    int mirrorAs(int c, int s) const {
        int x = X(c), y = Y(c);
        switch (s) {
        case 1: return id(x, H - 1 - y);
        case 2: return id(W - 1 - x, y);
        case 3: return id(W - 1 - x, H - 1 - y);
        }
        return c;
    }
    int mirror(int c) const { return mirrorAs(c, sym); }
    // The side of the mirror tile that is the image of side d.
    static int mirrorDir(int d, int s) {
        bool fx = s == 2 || s == 3, fy = s == 1 || s == 3;
        if ((d == E || d == Dir::W) && fx) return d ^ 2;
        if ((d == N || d == S) && fy) return d ^ 2;
        return d;
    }
    Edge& sideRef(int c, int d) {
        int x = X(c), y = Y(c);
        switch (d) {
        case N: return hE[c];
        case S: return hE[id(x, y + 1)];
        case Dir::W: return vE[c];
        default: return vE[id(x + 1, y)];
        }
    }

    // Torus Chebyshev distance, cheap "is it near" test.
    int cheb(int a, int b) const {
        int dx = std::abs(X(a) - X(b)), dy = std::abs(Y(a) - Y(b));
        dx = std::min(dx, W - dx);
        dy = std::min(dy, H - dy);
        return std::max(dx, dy);
    }
};

}  // namespace bc
