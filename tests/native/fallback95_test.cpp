// 95M CPU fallback (BC_FALLBACK95, on in the final build): unit tests of the check points that do not need the
// sandbox's CPU meter - the turn start, the line, the fallback move, Policy's lean-test check point, the ray drop and
// the trace marker. The metered end-to-end check is the CPU census (tools/abyss_tools/run/cpu_census.py) run with
// --define BC_T13_BURN=1 --forced t13burn: that test build burns the clock past the line at one check point a round,
// and every forced turn must print the fallback marker.
//   c++ -std=c++20 -O2 -DBC_FALLBACK95=1 -DBC_V11_TRACE=1 -I bots/abyss tests/native/fallback95_test.cpp -o fallback95_test && ./fallback95_test
// BC_V11_TRACE 1 turns on the "LOG fb95 <check point>" marker that testOut checks (it is off in the final build).
#define private public
#include "policy.hpp"
#undef private

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <vector>

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

// a 20 x 20 open board: our dragon (4 long) head (10, 10), body trailing west
static World openWorld() {
    World w;
    Init in;
    in.id = 5;
    in.team = 'A';
    in.w = in.h = 20;
    w.start(in);
    w.t.round = 30;
    w.t.length = 4;
    w.t.units = 3;
    w.t.dir = E;
    w.head = w.board.id(10, 10);
    for (int i = 0; i < 4; i++) w.body.push_back(w.board.id(10 - i, 10));
    w.occ.assign(w.board.NC, -1);
    w.occHeadId.assign(w.board.NC, -1);
    for (int c = 0; c < w.board.NC; c++) w.seenRound[c] = w.visRound[c] = 30;
    return w;
}

static void testLine() {
    fb95::t0 = 2.0;
    CHECK(fb95::hard() == 2.0 + 0.095);
    CHECK(!fb95::past(2.0 + 0.094));
    fb95::t0 = 0.0;
    CHECK(!fb95::past(0.095));        // the line itself is still inside (exact at t0 = 0)
    CHECK(fb95::past(0.0951));
    fb95::t0 = 2.0;
    CHECK(fb95::past(2.0 + 0.0951));
    fb95::t0 = 0.0;
}

static void testMove() {
    World w = openWorld();
    Params p;
    Policy pol(w, p, 1e18);
    Choice c = pol.fb95Move();
    CHECK(c.why == "fb95" && !c.split && c.sprint.empty());
    CHECK(c.dir == N && c.dest == w.board.id(10, 9));  // the first free neighbour in N, E, S, W order
    w.occ[w.board.id(10, 9)] = 7;                      // another dragon north
    c = pol.fb95Move();
    CHECK(c.dir == E && c.dest == w.board.id(11, 10));
    w.occ[w.board.id(11, 10)] = 7;
    w.occ[w.board.id(10, 11)] = 7;                     // north, east, south taken; west is our own neck
    c = pol.fb95Move();
    CHECK(c.dest == -1 && c.dir == E);                 // nothing free: straight on (a MOVE, never no action)
    w.occ[w.board.id(10, 11)] = -1;                    // south free again, with a pearl seen there this round
    w.seenPearl[w.board.id(10, 11)] = 1;
    c = pol.fb95Move();
    CHECK(c.dir == S && c.dest == w.board.id(10, 11) && c.eat);
    w.seenRound[w.board.id(10, 11)] = 29;              // a pearl seen last round is not counted as eaten
    c = pol.fb95Move();
    CHECK(c.dir == S && !c.eat);
}

static void testPolicyCheckPoint() {
    World w = openWorld();
    Params p;
    fb95::hit = nullptr;
    fb95::t0 = clockNow() - 1.0;                       // the turn started a second ago: far past 95M
    Policy pol(w, p, clockNow() + 0.05);
    Choice c = pol.decide();
    CHECK(c.why == "fb95");
    CHECK(fb95::hit != nullptr && std::string(fb95::hit) == "policy");
    CHECK(c.dest >= 0 && w.occ[c.dest] < 0);
    fb95::hit = nullptr;
    fb95::t0 = clockNow();                             // a fresh turn: no fallback, the search decides
    Policy pol2(w, p, clockNow() + 0.02);
    Choice d = pol2.decide();
    CHECK(d.why != "fb95");
    CHECK(fb95::hit == nullptr);
    fb95::t0 = 0.0;
}

static void testOut() {
    Out out;
    out.move(N);
    out.sonar(0, 123);
    out.buf += "LOG keep me\n";
    out.sonar(2, 456);
    fb95::hit = nullptr;
    fb95::dropRays(out);
    CHECK(out.buf == "MOVE N\nLOG keep me\n");
    fb95::mark(out, "flush");
    CHECK(out.buf == "MOVE N\nLOG keep me\nLOG fb95 flush\n");
    CHECK(fb95::hit != nullptr && std::string(fb95::hit) == "flush");
    fb95::mark(out, "rays");                           // the first check point that fell back stays the turn's hit
    CHECK(std::string(fb95::hit) == "flush");
    fb95::hit = nullptr;
}

// two round blocks through readTurn: the first turn keeps t0 = 0 (its meter started with the process), the second sets it
static void testTurnStart() {
    std::string block;
    auto add = [&](int round) {
        block += "ROUND " + std::to_string(round) + "\nDIR E\nLENGTH 4\nUNIT_COUNT 3\nNUM_MSGS 1\n42\n";
        for (int i = 0; i < 49; i++) block += std::to_string(i % 7) + " " + std::to_string(i / 7) + " 0 -1\n";
        block += "DRAGON_BODIES 0\n";
        for (int r = 0; r < 8; r++) block += ". . . . . . .\n";
        for (int r = 0; r < 7; r++) block += ". . . . . . . .\n";
    };
    add(0);
    add(1);
    char const* dir = std::getenv("TMPDIR");
    std::string tmpl = std::string(dir && *dir ? dir : "/tmp") + "/abyss_fb95_XXXXXX";
    std::vector<char> pathBuf(tmpl.begin(), tmpl.end());
    pathBuf.push_back('\0');
    char* path = pathBuf.data();
    int fd = mkstemp(path);
    if (fd < 0) {                                      // no temp file: a failure, not a skipped check
        checks++, fails++;
        std::printf("FAIL %s:%d  cannot create %s\n", __FILE__, __LINE__, path);
        return;
    }
    FILE* f = fdopen(fd, "w");
    std::fputs(block.c_str(), f);
    std::fclose(f);
    CHECK(std::freopen(path, "r", stdin) != nullptr);
    Reader in;
    Turn t;
    fb95::t0 = 0.0;
    CHECK(readTurn(in, t) && t.round == 0 && t.msgs.size() == 1);
    CHECK(fb95::t0 == 0.0);
    double before = clockNow();
    CHECK(readTurn(in, t) && t.round == 1);
    CHECK(fb95::t0 >= before && fb95::t0 <= clockNow());
    std::remove(path);
    fb95::t0 = 0.0;
}

int main() {
    clockNow();
    testLine();
    testMove();
    testPolicyCheckPoint();
    testOut();
    testTurnStart();
    std::printf("%d / %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
