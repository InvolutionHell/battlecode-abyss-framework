// flooder: the sparring bot of the worst-case CPU census (see tools/abyss_tools/run/cpu_census.py).
// Every dragon casts four sonar rays a turn with random u64 values, so the bot under test reads a full inbox every
// turn, and turns right every round, so a length-2 body circles a 2x2 square and never hits itself. From round
// CENSUS_END on it sends no action and dies (noValidAction): the census games end by elimination instead of running
// 500 rounds. Not a competition bot; no helper.hpp, no LOG output.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifndef CENSUS_END
#define CENSUS_END 60
#endif

namespace {

char line[512];

bool next() { return std::fgets(line, sizeof line, stdin) != nullptr; }

bool is(char const* label) {
    size_t n = std::strlen(label);
    return std::strncmp(line, label, n) == 0 && (line[n] == ' ' || line[n] == '\n' || line[n] == '\r' || line[n] == '\0');
}

int numAt(int k) {  // the k-th whitespace-separated token as an int (0 = the label)
    char* p = line;
    for (int i = 0; i < k; i++) {
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
    }
    return std::atoi(p);
}

uint64_t rng = 0x9E3779B97F4A7C15ull;

uint64_t next64() {  // xorshift64*
    rng ^= rng >> 12;
    rng ^= rng << 25;
    rng ^= rng >> 27;
    return rng * 0x2545F4914F6CDD1Dull;
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IOFBF, 1 << 14);
    int id = 0;
    for (int got = 0; got < 4 && next();) {
        if (is("ID")) id = numAt(1), got++;
        else if (is("TEAM") || is("MAP") || is("UNIT_LIMIT")) got++;
    }
    rng ^= static_cast<uint64_t>(id + 1) * 0xD1B54A32D192ED03ull;
    char const* dirs = "NESW";
    for (;;) {
        // the round block: ROUND, DIR, LENGTH, UNIT_COUNT, NUM_MSGS + values, [ECHOES], 49 tiles, DRAGON_BODIES + parts,
        // 8 + 7 edge rows
        while (next() && !is("ROUND")) {
            if (is("ENDGAME")) return 0;
        }
        if (std::feof(stdin)) return 0;
        int round = numAt(1);
        if (!next()) return 0;
        int facing = static_cast<int>(std::strchr(dirs, line[4] ? line[4] : 'N') - dirs);
        if (facing < 0 || facing > 3) facing = 0;
        next();  // LENGTH
        next();  // UNIT_COUNT
        next();  // NUM_MSGS
        int msgs = numAt(1);
        for (int i = 0; i < msgs; i++) next();
        next();
        if (is("ECHOES")) next();
        for (int i = 1; i < 49; i++) next();
        next();  // DRAGON_BODIES
        int parts = numAt(1);
        for (int i = 0; i < parts; i++) next();
        for (int i = 0; i < 15; i++) next();
        std::string out;
        if (round < CENSUS_END) {
            out += "MOVE ";
            out += dirs[(facing + 1) % 4];
            out += '\n';
            for (int d = 0; d < 4; d++) {
                out += "SONAR ";
                out += dirs[d];
                out += ' ';
                out += std::to_string(next64());
                out += '\n';
            }
        }
        out += "PROTOCOL 3\nENDTURN\n";
        std::fwrite(out.data(), 1, out.size(), stdout);
        std::fflush(stdout);
    }
}
