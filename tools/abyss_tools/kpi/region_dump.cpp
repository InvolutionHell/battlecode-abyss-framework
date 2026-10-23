// region_dump: the bot's room module (region.hpp RegionMap) on a map file at full knowledge, for
// abyss_tools.kpi.region_graph --check, which compares every room and every portal landing with its own Python graph.
//
//   c++ -std=c++20 -O1 -DBC_MAPNET=1 -DBC_MAPNET_ROOMS=1 -I bots/abyss region_dump.cpp -o region_dump
//   ./region_dump --dump bots/maps/portals.map
//
// Output: `room <anchor> <size> <beds> <exits> <closed>` per room and `step <tile id> <dir index> <landing id or -1>` per
// portal side. Edge indices are numbered as the engine numbers them: rows of W + 1, even rows the top edges of a tile row,
// odd rows the left edges.
#include "region.hpp"

#include <array>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace bc;

// a map file at full knowledge: edges (kelp walls, portals paired by id), beds (TILE x y lo hi with hi > 0), symmetry
static bool loadMap(std::string const& path, Board& b) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    int W = 0, H = 0;
    std::vector<std::string> lines;
    while (std::getline(in, line)) lines.push_back(line);
    for (auto const& l : lines)
        if (l.rfind("MAP ", 0) == 0) std::sscanf(l.c_str(), "MAP %d %d", &W, &H);
    b.init(W, H);
    b.bed.assign(b.NC, 0);
    std::map<int, std::vector<std::array<int, 3>>> ends;  // portal id -> (orient 0 h / 1 v, x, y)
    for (auto const& l : lines) {
        if (l.rfind("SYMMETRY", 0) == 0) {
            std::string s = l.substr(9);
            while (!s.empty() && (s.back() == '\r' || s.back() == ' ')) s.pop_back();
            b.sym = s == "x" ? 1 : s == "y" ? 2 : s == "xy" ? 3 : 0;
        } else if (l.rfind("TILE ", 0) == 0) {
            int x, y, lo, hi;
            if (std::sscanf(l.c_str(), "TILE %d %d %d %d", &x, &y, &lo, &hi) == 4 && hi > 0) b.bed[b.id(x, y)] = 1;
        } else if (l.rfind("EDGE ", 0) == 0) {
            int idx, kind, pid;
            if (std::sscanf(l.c_str(), "EDGE %d %d %d", &idx, &kind, &pid) != 3 || kind == 0) continue;
            int stride = W + 1, col = idx % stride, row = idx / stride;
            int o, x, y;
            if (row % 2 == 0) {
                if (col == W || row == 2 * H) continue;
                o = 0, x = col, y = row / 2;
            } else {
                if (col == W) continue;
                o = 1, x = col, y = (row - 1) / 2;
            }
            Edge& e = o == 0 ? b.hE[b.id(x, y)] : b.vE[b.id(x, y)];
            e.kind = static_cast<int8_t>(kind == 1 ? 1 : 2);
            if (kind == 2) ends[pid].push_back({o, x, y});
        }
    }
    for (auto const& [pid, v] : ends) {
        if (v.size() != 2) continue;
        for (int i = 0; i < 2; i++) {
            auto const& me = v[i];
            auto const& other = v[1 - i];
            Edge& e = me[0] == 0 ? b.hE[b.id(me[1], me[2])] : b.vE[b.id(me[1], me[2])];
            e.partnerOrient = static_cast<int8_t>(other[0]);
            e.px = static_cast<int16_t>(other[1]);
            e.py = static_cast<int16_t>(other[2]);
        }
    }
    b.rebuild();
    return true;
}

static int dump(std::string const& path) {
    Board b;
    if (!loadMap(path, b)) {
        std::printf("cannot read %s\n", path.c_str());
        return 2;
    }
    std::vector<int> seen(b.NC, 0);
    std::vector<int8_t> mir(b.NC, 0);
    RegionMap m;
    m.init(b.NC);
    m.touchAll(b.NC);
    while (!m.dirty_.empty()) m.recompute(b, seen, mir);
    for (RegionMap::Room const& r : m.rooms)
        std::printf("room %d %d %d %d %d\n", r.anchor, r.size, r.beds, r.exits, r.closed ? 1 : 0);
    for (int c = 0; c < b.NC; c++)
        for (int d = 0; d < 4; d++)
            if (b.side(c, d).kind == 2) std::printf("step %d %d %d\n", c, d, b.stepRaw(c, d));
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--dump") return dump(argv[2]);
    std::printf("usage: %s --dump MAP\n", argv[0]);
    return 2;
}
