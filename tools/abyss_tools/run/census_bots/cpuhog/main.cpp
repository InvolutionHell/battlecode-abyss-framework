// cpuhog: the self-test of the worst-case CPU census (abyss_tools.run.cpu_census --scenarios selftest).
// It burns a known number of points per turn on the sandbox's virtual clock (1 ns per point), so the census must report:
//   round % 10 == 5          spin to 95.5M, then take the fallback (the cheapest legal move) and print "LOG fb95 selftest":
//                            the turn ends just over 95M with the output cost -> counted over the hard line and as a fallback
//   round == 7 and id == 1   spin to 101M: the sandbox kills the dragon ("exceeded CPU limit") -> one timeout
//   any other round          spin to 50M
// Every dragon turns right every round (a length-2 body circles a 2x2 square) and stops acting at round CENSUS_END, so the
// game ends by elimination. Not a competition bot; no helper.hpp.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifndef CENSUS_END
#define CENSUS_END 30
#endif

namespace {

char line[512];

bool next() { return std::fgets(line, sizeof line, stdin) != nullptr; }

bool is(char const* label) {
    size_t n = std::strlen(label);
    return std::strncmp(line, label, n) == 0 && (line[n] == ' ' || line[n] == '\n' || line[n] == '\r' || line[n] == '\0');
}

int numAt(int k) {
    char* p = line;
    for (int i = 0; i < k; i++) {
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
    }
    return std::atoi(p);
}

double clockNow() {
    static auto const start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

volatile uint64_t sink = 0;

void spinUntil(double t) {
    while (clockNow() < t) {
        for (int i = 0; i < 64; i++) sink = sink * 6364136223846793005ull + 1442695040888963407ull;
    }
}

}  // namespace

int main() {
    clockNow();
    std::setvbuf(stdout, nullptr, _IOFBF, 1 << 14);
    int id = 0;
    for (int got = 0; got < 4 && next();) {
        if (is("ID")) id = numAt(1), got++;
        else if (is("TEAM") || is("MAP") || is("UNIT_LIMIT")) got++;
    }
    char const* dirs = "NESW";
    bool first = true;
    for (;;) {
        while (next() && !is("ROUND")) {
            if (is("ENDGAME")) return 0;
        }
        if (std::feof(stdin)) return 0;
        double turnStart = first ? 0.0 : clockNow();
        first = false;
        int round = numAt(1);
        if (!next()) return 0;
        int facing = static_cast<int>(std::strchr(dirs, line[4] ? line[4] : 'N') - dirs);
        if (facing < 0 || facing > 3) facing = 0;
        next();
        next();
        next();
        int msgs = numAt(1);
        for (int i = 0; i < msgs; i++) next();
        next();
        if (is("ECHOES")) next();
        for (int i = 1; i < 49; i++) next();
        next();
        int parts = numAt(1);
        for (int i = 0; i < parts; i++) next();
        for (int i = 0; i < 15; i++) next();
        std::string out;
        if (round < CENSUS_END) {
            bool fallback = false;
            if (round == 7 && id == 1) {
                spinUntil(turnStart + 0.101);
            } else if (round % 10 == 5) {
                spinUntil(turnStart + 0.0955);
                fallback = clockNow() - turnStart > 0.095;  // the hard line: fall back at once
            } else {
                spinUntil(turnStart + 0.050);
            }
            out += "MOVE ";
            out += dirs[(facing + 1) % 4];
            out += '\n';
            if (fallback) out += "LOG fb95 selftest\n";
        }
        out += "PROTOCOL 3\nENDTURN\n";
        std::fwrite(out.data(), 1, out.size(), stdout);
        std::fflush(stdout);
    }
}
