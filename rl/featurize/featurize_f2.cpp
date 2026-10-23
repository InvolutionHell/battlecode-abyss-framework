// Protocol records -> F1 records plus the dragon's own non-local memory ("F2": rl/runtime/f2_memory.hpp, the same code
// a bot runs). Every turn of every dragon of the side comes in, in game order: each turn updates that dragon's memory,
// and only the turns marked keep = 1 become records, so the memory is built from the dragon's own views exactly as in
// play.
//
//   stdin:  REC <game> <id> <team> <w> <h> <unitLimit> <outcome bits> <kgap> <tgap> <facts> <keep> <verb> <arg>
//           then the round block, then END                                   (rl/data/build_f2.py writes this)
//   stdout: featurize_f1.cpp's 16 header bytes and FDIM features, then F2_NMEM memory inputs quantised as
//           round(m * 255). Build with -DF2_HEARD for the 44-input variant that also decodes our own sonar messages.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

#include "rl_features.hpp"
#define F2_FEATURES_INCLUDED
#include "../runtime/f2_memory.hpp"

using namespace bc;

int main() {
    Reader in;
    Turn t;
    Features f;
    static unsigned char rec[16 + FDIM + F2_NMEM];
    long kept = 0, skipped = 0, turns = 0;
    auto clip = [](int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; };
    std::map<int, F2Memory> mem;  // this game's dragons
    std::string game;
    float m[F2_NMEM];
    while (in.next()) {
        if (!in.is("REC")) continue;
        if (game != in.str(1)) game = in.str(1), mem.clear();
        Init init;
        init.id = in.num(2);
        init.team = in.str(3)[0];
        init.w = in.num(4);
        init.h = in.num(5);
        init.unitLimit = in.num(6);
        int const outcome = in.num(7), kgap = in.num(8), tgap = in.num(9);
        int facts[6] = {}, flags = 0;
        std::sscanf(in.str(10), "%d,%d,%d,%d,%d,%d,%d", &facts[0], &facts[1], &facts[2], &facts[3], &facts[4], &facts[5],
                    &flags);
        bool const keep = in.num(11) == 1;
        std::string verb = in.str(12), arg = in.str(13);
        if (!readTurn(in, t)) break;
        while (in.next() && !in.is("END")) {}
        turns++;
        F2Memory& mm = mem[init.id];
        mm.update(init, t);
        if (!keep) continue;
        int label = -1, n = 0, steps = 0, dir2 = 255;
        if (verb == "MOVE" && !arg.empty()) {
            steps = static_cast<int>(arg.size());
            int const d1 = dirFromChar(arg[0]);
            label = (std::min(steps, 3) - 1) * 4 + relDir(d1, t.dir);
            if (steps >= 2) dir2 = relDir(dirFromChar(arg[1]), d1);
        } else if (verb == "SPLIT") {
            n = std::atoi(arg.c_str());
            label = n <= 2 ? 12 : n <= 4 ? 13 : 14;
        } else if (verb == "NONE") {
            label = 15;
        }
        if (label < 0) {
            skipped++;
            continue;
        }
        featurize(init, t, f);
        mm.features(init, t, m);
        rec[0] = static_cast<unsigned char>(label);
        rec[1] = static_cast<unsigned char>(clip(n, 0, 255));
        rec[2] = static_cast<unsigned char>(clip(t.length, 0, 255));
        rec[3] = static_cast<unsigned char>(clip(steps, 0, 255));
        rec[4] = static_cast<unsigned char>(outcome & 0xFF);
        rec[5] = static_cast<unsigned char>(clip(t.round / 2, 0, 255));
        rec[6] = static_cast<unsigned char>(clip(kgap, -127, 127) + 128);
        rec[7] = static_cast<unsigned char>(clip(tgap, -127, 127) + 128);
        rec[8] = static_cast<unsigned char>(dir2);
        for (int k = 0; k < 6; k++) rec[9 + k] = static_cast<unsigned char>(clip(facts[k], 0, 255));
        rec[15] = static_cast<unsigned char>(flags & 0xFF);
        for (int i = 0; i < FDIM; i++)
            rec[16 + i] = static_cast<unsigned char>(std::lround(std::min(2.55f, std::max(0.0f, f.v[i])) * 100));
        for (int k = 0; k < F2_NMEM; k++) rec[16 + FDIM + k] = static_cast<unsigned char>(std::lround(m[k] * 255));
        if (std::fwrite(rec, 1, sizeof rec, stdout) != sizeof rec) {
            std::fprintf(stderr, "featurize_f2: write failed after %ld samples\n", kept);
            return 1;
        }
        kept++;
    }
    if (std::fflush(stdout) != 0 || std::ferror(stdout)) {
        std::fprintf(stderr, "featurize_f2: write failed at the end (%ld samples)\n", kept);
        return 1;
    }
    std::fprintf(stderr, "featurize_f2: %ld samples of %ld turns, %ld skipped\n", kept, turns, skipped);
    return 0;
}
