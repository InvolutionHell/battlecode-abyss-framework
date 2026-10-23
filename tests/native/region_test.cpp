// Room module (region.hpp, RegionMap::recompute; compiled with BC_MAPNET_ROOMS, an inactive sub-switch of the map
// network in the final build): unit tests on synthetic boards, the incremental update checked against a full rebuild on
// real maps, and --dump MAP: the module's rooms (and portal landings) of a map file at full knowledge.
//   c++ -std=c++20 -O2 -DBC_MAPNET_ROOMS=1 -I bots/abyss tests/native/region_test.cpp -o region_test && ./region_test
//   ./region_test --maps DIR         the real-map case reads DIR/<name>.map (default: $ABYSS_MAPS); without the maps it is
//                                    skipped, and the synthetic tests still run
//   ./region_test --dump maps/portals.map
// The official maps ship with the unswbc toolkit: `unswbc maps DIR` copies them into DIR. REGION_TEST_DEBUG=1 prints the
// first mismatch of a real-map case.
#include "region.hpp"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <array>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace bc;

static int fails = 0, checks = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        checks++;                                                   \
        if (!(c)) {                                                 \
            fails++;                                                \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                           \
    } while (0)

// a map file at full knowledge: edges (kelp walls, portals paired by id), beds (TILE x y lo hi with hi > 0), symmetry; EDGE indices
// as the engine numbers them (rows of W + 1 edges, even rows the top edges, odd rows the left edges)
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

// synthetic boards: W x H, all open; walls / portals by tile side
static Board open(int W, int H) {
    Board b;
    b.init(W, H);
    b.bed.assign(b.NC, 0);
    return b;
}
static void wall(Board& b, int x, int y, int d) {
    Edge& e = d == N ? b.hE[b.id(x, y)] : d == S ? b.hE[b.id(x, y + 1)] : d == W ? b.vE[b.id(x, y)] : b.vE[b.id(x + 1, y)];
    e = Edge{};
    e.kind = 1;
}
static void box(Board& b, std::vector<std::pair<int, int>> const& tiles) {  // walls around a set of tiles (inner sides stay open)
    auto in = [&](int x, int y) {
        for (auto const& t : tiles)
            if (b.id(t.first, t.second) == b.id(x, y)) return true;
        return false;
    };
    for (auto const& [x, y] : tiles)
        for (int d = 0; d < 4; d++)
            if (!in(x + DX[d], y + DY[d])) wall(b, x, y, d);
}
static void portal(Board& b, int ox, int oy, int od, int px, int py, int pd, bool paired = true) {
    // the side od of tile (ox, oy) and the side pd of (px, py), as top / left edges
    auto edgeOf = [&](int x, int y, int d, int& o, int& ex, int& ey) {
        if (d == N) o = 0, ex = x, ey = y;
        else if (d == S) o = 0, ex = x, ey = y + 1;
        else if (d == W) o = 1, ex = x, ey = y;
        else o = 1, ex = x + 1, ey = y;
        ex = ((ex % b.W) + b.W) % b.W, ey = ((ey % b.H) + b.H) % b.H;
    };
    int o1, x1, y1, o2, x2, y2;
    edgeOf(ox, oy, od, o1, x1, y1);
    edgeOf(px, py, pd, o2, x2, y2);
    Edge& a = o1 == 0 ? b.hE[b.id(x1, y1)] : b.vE[b.id(x1, y1)];
    Edge& c = o2 == 0 ? b.hE[b.id(x2, y2)] : b.vE[b.id(x2, y2)];
    a = Edge{};
    c = Edge{};
    a.kind = c.kind = 2;
    if (paired) {
        a.partnerOrient = static_cast<int8_t>(o2), a.px = static_cast<int16_t>(x2), a.py = static_cast<int16_t>(y2);
        c.partnerOrient = static_cast<int8_t>(o1), c.px = static_cast<int16_t>(x1), c.py = static_cast<int16_t>(y1);
    }
}

// the synthetic boards are written without World's touches: every tile touched, then the incremental path from the pristine state
// a recompute takes at most kDirtyBudget touched tiles (all of them when fewer); drain() calls it until none is left
static int budgetCalls = 0, budgetBad = 0;
static bool drain(RegionMap& m, Board const& b, std::vector<int> const& seen, std::vector<int8_t> const& mir) {
    bool any = false;
    do {
        size_t before = m.dirty_.size();
        any |= m.recompute(b, seen, mir);
        budgetCalls++;
        budgetBad += !(before - m.dirty_.size() == std::min<size_t>(before, RegionMap::kDirtyBudget) || m.statFull);
    } while (!m.dirty_.empty());
    return any;
}
static bool recomputeAll(RegionMap& m, Board const& b, std::vector<int> const& seen, std::vector<int8_t> const& mir) {
    m.touchAll(b.NC);
    return drain(m, b, seen, mir);
}

static RegionMap::Room const* roomAt(RegionMap const& m, Board const& b, int x, int y) {
    int r = m.regionOf(b.id(x, y));
    return r < 0 ? nullptr : &m.rooms[r];
}

static void testOpenAndTrap() {
    Board b = open(8, 8);
    // a 1-tile room at (1,1): walls N / W / S, its east side a portal to the north side of (5,5)
    box(b, {{1, 1}});
    portal(b, 1, 1, E, 5, 5, N);
    b.bed[b.id(1, 1)] = 1;
    b.bed[b.id(0, 0)] = -1;                                                     // a bed not known yet: not counted
    b.rebuild();
    std::vector<int> seen(b.NC, 0);
    std::vector<int8_t> mir(b.NC, 0);
    RegionMap m;
    m.init(b.NC);
    CHECK(recomputeAll(m, b, seen, mir));
    CHECK(m.rooms.size() == 2);
    auto const* t = roomAt(m, b, 1, 1);
    CHECK(t && t->size == 1 && t->beds == 1 && t->exits == 1 && t->closed && t->anchor == b.id(1, 1));
    auto const* o = roomAt(m, b, 0, 0);
    // the open rest: a portal edge is a side of two tiles - (2,1) W (the room's door) and both faces of the far end (5,5) N / (5,4) S
    CHECK(o && o->size == 63 && o->exits == 3 && o->closed && o->anchor == 0 && o->beds == 0);
    CHECK(m.regionOf(b.id(1, 1)) != m.regionOf(b.id(2, 1)));
    CHECK(!recomputeAll(m, b, seen, mir));                                       // nothing changed
    CHECK(!m.recompute(b, seen, mir));                                          // nothing touched
}

static void testRoomAndUnknown() {
    Board b = open(10, 10);
    box(b, {{2, 2}, {3, 2}, {2, 3}, {3, 3}});
    portal(b, 2, 2, N, 7, 7, N);
    portal(b, 3, 3, E, 7, 1, W);
    b.rebuild();
    std::vector<int> seen(b.NC, -1);
    std::vector<int8_t> mir(b.NC, 0);
    for (int y = 0; y < 6; y++)
        for (int x = 0; x < 6; x++) seen[b.id(x, y)] = 3;  // a 6x6 corner known
    RegionMap m;
    m.init(b.NC);
    recomputeAll(m, b, seen, mir);
    auto const* r = roomAt(m, b, 2, 2);
    CHECK(r && r->size == 4 && r->exits == 2 && r->closed);                     // the 2x2 room: two portal sides, all known
    auto const* o = roomAt(m, b, 0, 0);
    CHECK(o && o->size == 32 && !o->closed);                                    // the known rest borders unknown tiles
    CHECK(o && o->exits == 6 * 4 + 2);                                          // 6 open sides on each border + 2 portal faces
    CHECK(m.regionOf(b.id(8, 8)) == -1);                                        // unknown: no room
    // the rest seen through the mirror counts as known
    for (int c = 0; c < b.NC; c++)
        if (seen[c] < 0) mir[c] = 1;
    CHECK(recomputeAll(m, b, seen, mir));
    o = roomAt(m, b, 0, 0);
    CHECK(o && o->size == 96 && o->closed && o->exits == 6);                     // the doors' faces + both faces of the far ends
}

static void testUnpairedAndVisit() {
    Board b = open(8, 8);
    box(b, {{1, 1}, {2, 1}});
    portal(b, 1, 1, W, 5, 5, N, false);                                          // ends seen, not paired yet
    b.rebuild();
    std::vector<int> seen(b.NC, 0);
    std::vector<int8_t> mir(b.NC, 0);
    RegionMap m;
    m.init(b.NC);
    recomputeAll(m, b, seen, mir);
    auto const* r = roomAt(m, b, 1, 1);
    CHECK(r && r->size == 2 && r->exits == 1 && !r->closed);
    m.visit(b.id(2, 1), 40);
    CHECK(roomAt(m, b, 1, 1)->lastVisit == 40);
    portal(b, 1, 1, W, 5, 5, N, true);                                           // the pair is learnt: recomputed, closed now
    b.rebuild();
    CHECK(recomputeAll(m, b, seen, mir));
    r = roomAt(m, b, 1, 1);
    CHECK(r && r->closed && r->lastVisit == 40);                                 // lastVisit kept by anchor
    wall(b, 6, 6, E);                                                            // a new wall elsewhere: recomputed
    b.rebuild();
    CHECK(recomputeAll(m, b, seen, mir));
}

static void testMirrorOnlyRoom() {
    // the 2x2 room known only from the mirror (no tile of it, nor around it, seen): a room of its own
    Board b = open(10, 10);
    box(b, {{2, 2}, {3, 2}, {2, 3}, {3, 3}});
    portal(b, 2, 2, N, 7, 7, N);
    b.rebuild();
    std::vector<int> seen(b.NC, -1);
    std::vector<int8_t> mir(b.NC, 0);
    for (int x = 2; x < 4; x++)
        for (int y = 2; y < 4; y++) mir[b.id(x, y)] = 1;
    RegionMap m;
    m.init(b.NC);
    recomputeAll(m, b, seen, mir);
    CHECK(m.rooms.size() == 1);
    auto const* r = roomAt(m, b, 3, 3);
    CHECK(r && r->size == 4 && r->exits == 1 && r->closed && r->anchor == b.id(2, 2));
    CHECK(m.regionOf(b.id(0, 0)) == -1);
}

static void testSplitAndMerge() {
    // an open 8x8 torus cut by two wall rows: the room splits in two (the half with the old anchor keeps lastVisit, like a full
    // rebuild); one wall row opened again: one room
    Board b = open(8, 8);
    std::vector<int> seen(b.NC, 0);
    std::vector<int8_t> mir(b.NC, 0);
    RegionMap m;
    m.init(b.NC);
    recomputeAll(m, b, seen, mir);
    m.visit(b.id(5, 5), 30);
    for (int x = 0; x < 8; x++) wall(b, x, 3, S), wall(b, x, 7, S), m.touch(b.id(x, 4), b.NC), m.touch(b.id(x, 0), b.NC);
    CHECK(m.recompute(b, seen, mir));
    CHECK(m.rooms.size() == 2);
    auto const* top = roomAt(m, b, 0, 0);
    auto const* low = roomAt(m, b, 0, 5);
    CHECK(top && top->size == 32 && top->anchor == 0 && top->lastVisit == 30 && top->closed && top->exits == 0);
    CHECK(low && low->size == 32 && low->anchor == b.id(0, 4) && low->lastVisit == -1);
    for (int x = 0; x < 8; x++) b.hE[b.id(x, 4)] = Edge{}, m.touch(b.id(x, 4), b.NC);
    CHECK(m.recompute(b, seen, mir));
    CHECK(m.rooms.size() == 1 && m.rooms[0].size == 64 && m.rooms[0].anchor == 0 && m.rooms[0].lastVisit == 30);
    m.rebuild(b, seen, mir);                                                    // the full rebuild keeps lastVisit by anchor too
    CHECK(m.rooms.size() == 1 && m.rooms[0].lastVisit == 30 && m.audit(b, seen, mir) == 0);
}

static void testMissedWrite() {
    // a missed touch: two wall rows written without telling the module; the sweep (1/16 of the map a call) finds every
    // changed record within 16 calls, and the rooms then equal a full rebuild
    Board b = open(8, 8);
    std::vector<int> seen(b.NC, 0);
    std::vector<int8_t> mir(b.NC, 0);
    RegionMap m;
    m.init(b.NC);
    recomputeAll(m, b, seen, mir);
    CHECK(m.rooms.size() == 1);
    for (int x = 0; x < 8; x++) wall(b, x, 3, S), wall(b, x, 7, S);
    int swept = 0;
    for (int i = 0; i < 16; i++) {
        m.recompute(b, seen, mir);
        swept += m.statSwept;
    }
    CHECK(swept == 16);
    CHECK(m.rooms.size() == 2 && m.audit(b, seen, mir) == 0);
    RegionMap ref;
    ref.init(b.NC);
    ref.rebuild(b, seen, mir);
    auto const* r = roomAt(m, b, 0, 0);
    auto const* q = roomAt(ref, b, 0, 0);
    CHECK(r && q && r->size == 32 && q->size == 32 && r->anchor == q->anchor && r->closed);
}

// the incremental path against a fresh full rebuild: real maps revealed in random batches (some tiles through the mirror), the
// portals paired only half-way, a bed learnt late now and then, and edge records wrong at first (a cut one open, as the mirror leaves
// a portal; an open one walled) put right at a random later step - between known tiles that is a split or a merge. Only the tiles
// written are touched, as World does.
static int splits = 0, joins = 0;
static int incrementalCase(std::string const& path, unsigned seed) {
    Board full;
    if (!loadMap(path, full)) return -1;
    unsigned x = seed * 2654435761u + 1;
    auto rnd = [&](unsigned m) { x ^= x << 13, x ^= x >> 17, x ^= x << 5; return x % m; };
    Board b = full;
    std::vector<std::pair<int, int>> wrong;   // (record tile, 0 top / 1 left)
    for (int c = 0; c < b.NC; c++)
        for (int o = 0; o < 2; o++) {
            Edge& e = o == 0 ? b.hE[c] : b.vE[c];
            if (e.kind == 2) e.partnerOrient = -1;  // unpaired until half-way
            if (e.kind != 0 && rnd(6) == 0) e = Edge{}, wrong.push_back({c, o});
            else if (e.kind == 0 && rnd(40) == 0) e = Edge{}, e.kind = 1, wrong.push_back({c, o});
        }
    for (size_t i = wrong.size(); i > 1; i--) std::swap(wrong[i - 1], wrong[rnd(static_cast<unsigned>(i))]);
    std::vector<int> order(b.NC);
    for (int c = 0; c < b.NC; c++) order[c] = c;
    for (int i = b.NC - 1; i > 0; i--) std::swap(order[i], order[rnd(i + 1)]);
    std::vector<int> seen(b.NC, -1);
    std::vector<int8_t> mir(b.NC, 0);
    auto known = [&](int c) { return seen[c] >= 0 || mir[c]; };
    RegionMap inc;
    inc.init(b.NC);
    inc.touchAll(b.NC);                       // the map's records were written before: the snapshot learns them (nothing known)
    drain(inc, b, seen, mir);
    int steps = 0, bad = 0;
    bool paired = false;
    size_t fixed = 0;
    for (size_t at = 0; at < order.size() || fixed < wrong.size();) {
        size_t k = 1 + rnd(40);
        for (size_t j = 0; j < k && at < order.size(); j++, at++) {
            int c = order[at];
            if (rnd(5) == 0) mir[c] = 1;
            else seen[c] = 1;
            inc.touch(c, b.NC);
        }
        if (!paired && at * 2 >= order.size()) {  // the portals get paired: known edge records change
            paired = true;
            for (int c = 0; c < b.NC; c++)
                for (int o = 0; o < 2; o++) {
                    Edge& e = o == 0 ? b.hE[c] : b.vE[c];
                    Edge const& f = o == 0 ? full.hE[c] : full.vE[c];
                    if (e.kind == 2 && f.kind == 2) e = f, inc.touch(c, b.NC);
                }
        }
        for (size_t n = rnd(4); n > 0 && fixed < wrong.size(); n--, fixed++) {  // a wrong record put right
            auto [c, o] = wrong[fixed];
            Edge& e = o == 0 ? b.hE[c] : b.vE[c];
            Edge const& f = o == 0 ? full.hE[c] : full.vE[c];
            int other = o == 0 ? b.id(b.X(c), b.Y(c) - 1) : b.id(b.X(c) - 1, b.Y(c));
            if (known(c) && known(other)) (e.kind == 0 ? splits : joins)++;
            e = f;
            if (e.kind == 2 && !paired) e.partnerOrient = -1;
            inc.touch(c, b.NC);
        }
        if (rnd(23) == 0 && at > 0) {               // a bed learnt late on a known tile
            int c = order[rnd(static_cast<unsigned>(at))];
            b.bed[c] = b.bed[c] == 1 ? 0 : 1;
            inc.touch(c, b.NC);
        }
        drain(inc, b, seen, mir);
        RegionMap ref;
        ref.init(b.NC);
        ref.rebuild(b, seen, mir);
        steps++;
        auto canon = [](RegionMap const& m) {
            std::vector<std::array<int, 5>> v;
            for (auto const& r : m.rooms) v.push_back({r.anchor, r.size, r.beds, r.exits, r.closed ? 1 : 0});
            std::sort(v.begin(), v.end());
            return v;
        };
        bool same = canon(inc) == canon(ref) && inc.audit(b, seen, mir) == 0;
        for (int c = 0; c < b.NC && same; c++) {
            int a1 = inc.regionOf(c), a2 = ref.regionOf(c);
            if ((a1 < 0) != (a2 < 0) || (a1 >= 0 && inc.rooms[a1].anchor != ref.rooms[a2].anchor)) same = false;
        }
        for (size_t r = 0; r < inc.rooms.size() && same; r++)   // the member lists agree with roomOf
            for (int t : inc.members_[r]) same &= inc.roomOf[t] == static_cast<int>(r);
        if (!same && !bad && getenv("REGION_TEST_DEBUG")) {
            auto ci = canon(inc), cr = canon(ref);
            std::printf("%s seed %u step %d: rooms %zu / %zu, audit %d\n", path.c_str(), seed, steps, ci.size(), cr.size(), inc.audit(b, seen, mir));
            std::vector<std::array<int, 5>> d1, d2;
            std::set_difference(ci.begin(), ci.end(), cr.begin(), cr.end(), std::back_inserter(d1));
            std::set_difference(cr.begin(), cr.end(), ci.begin(), ci.end(), std::back_inserter(d2));
            for (size_t i = 0; i < d1.size() && i < 4; i++) std::printf("  inc %d %d %d %d %d\n", d1[i][0], d1[i][1], d1[i][2], d1[i][3], d1[i][4]);
            for (size_t i = 0; i < d2.size() && i < 4; i++) std::printf("  ref %d %d %d %d %d\n", d2[i][0], d2[i][1], d2[i][2], d2[i][3], d2[i][4]);
        }
        bad += !same;
    }
    CHECK(bad == 0);
    return steps;
}

// the official maps this case reads (portal-, kelp- and room-heavy ones); all of them must be present in the maps folder
static char const* const kRealMaps[] = {"portals", "stripes", "maze", "islands", "default", "trauma", "weakhold", "australia"};

static bool haveMaps(std::string const& dir) {
    if (dir.empty()) return false;
    for (char const* m : kRealMaps)
        if (!std::ifstream(dir + "/" + m + ".map")) return false;
    return true;
}

static void testIncrementalMatchesFull(std::string const& dir) {
    int total = 0;
    for (char const* m : kRealMaps)
        for (unsigned seed = 1; seed <= 3; seed++) {
            int s = incrementalCase(dir + "/" + m + ".map", seed);
            CHECK(s > 0);
            total += s;
        }
    std::printf("incremental == full on %d reveal steps (%d splits, %d merges of known rooms)\n", total, splits, joins);
    CHECK(splits > 100 && joins > 20);
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--dump") return dump(argv[2]);
    std::string mapsDir;
    if (char const* env = std::getenv("ABYSS_MAPS")) mapsDir = env;
    if (argc == 3 && std::string(argv[1]) == "--maps") mapsDir = argv[2];
    bool const realMaps = haveMaps(mapsDir);
    testOpenAndTrap();
    testRoomAndUnknown();
    testUnpairedAndVisit();
    testMirrorOnlyRoom();
    testSplitAndMerge();
    testMissedWrite();
    if (realMaps) testIncrementalMatchesFull(mapsDir);
    else std::printf("SKIP incremental == full on real maps: no official maps (set ABYSS_MAPS or pass --maps DIR)\n");
    std::printf("the touched-tile budget held on %d calls\n", budgetCalls);
    CHECK(budgetBad == 0 && (budgetCalls > 2000 || !realMaps));  // the real maps make most of the calls
    {   // the audit leaves out the tiles still waiting on the touched list (a 64x64 map revealed at once: 512 a call)
        Board b = open(64, 64);
        std::vector<int> seen(b.NC, 0);
        std::vector<int8_t> mir(b.NC, 0);
        RegionMap m;
        m.init(b.NC);
        m.touchAll(b.NC);
        m.recompute(b, seen, mir);
        CHECK(m.dirty_.size() == static_cast<size_t>(b.NC - RegionMap::kDirtyBudget) && m.audit(b, seen, mir) == 0);
        CHECK(m.rooms.size() == 1 && m.rooms[0].size == RegionMap::kDirtyBudget && !m.rooms[0].closed);
        drain(m, b, seen, mir);
        CHECK(m.rooms.size() == 1 && m.rooms[0].size == b.NC && m.rooms[0].closed && m.audit(b, seen, mir) == 0);
    }
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
