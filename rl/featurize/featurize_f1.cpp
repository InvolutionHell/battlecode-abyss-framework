// Protocol records -> training samples for the full-action-head ("F1") net (rl/train/train_f1.py; the bot's
// r4_weights.hpp format). Same features as featurize_rl.cpp, so the bot's rl_features.hpp serves both nets.
//
//   stdin:  REC <game> <id> <team> <w> <h> <unitLimit> <outcome bits> <kgap> <tgap>
//               <ownLong,enLong,ownN,enN,ownTot,enTot,flags> <verb> <arg>
//           then the round block, then END (the facts are one token: io.hpp's Reader keeps 16 tokens a line)
//   stdout: 16 header bytes + FDIM feature bytes per turn:
//           0  label: 0-11 move = (step bucket 1 / 2 / 3+) * 4 + first step relative to the facing;
//              12-14 split n = 2 / 3-4 / 5+; 15 no action (timeout or the default suicide)
//           1  split n, 2 length, 3 steps, 4 outcome bits (as featurize_rl.cpp), 5 round / 2
//           6  final longest-dragon gap + 128 (own - enemy), 7 final total-length gap / 4 + 128
//           8  the second step relative to the first (255: fewer than two steps)
//           9-14 facts of the replay at this turn (own / enemy longest, dragon counts, totals / 4); privileged: the bot
//              can only estimate them. build_f2.py --returns / --team-labels reuses some of these bytes for targets.
//           15 flags: bit 0 longest of its team; bits 1-7 the sampler's keep rate in % (the trainer weights by 1 / rate)
#include <cmath>
#include <cstdio>
#include <cstring>

#include "rl_features.hpp"

using namespace bc;

int main() {
    Reader in;
    Turn t;
    Features f;
    static unsigned char rec[16 + FDIM];
    long kept = 0, skipped = 0;
    auto clip = [](int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; };
    while (in.next()) {
        if (!in.is("REC")) continue;
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
        std::string verb = in.str(11), arg = in.str(12);
        if (!readTurn(in, t)) break;
        while (in.next() && !in.is("END")) {}
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
        if (std::fwrite(rec, 1, sizeof rec, stdout) != sizeof rec) {
            std::fprintf(stderr, "featurize_f1: write failed after %ld samples\n", kept);
            return 1;
        }
        kept++;
    }
    if (std::fflush(stdout) != 0 || std::ferror(stdout)) {
        std::fprintf(stderr, "featurize_f1: write failed at the end (%ld samples)\n", kept);
        return 1;
    }
    std::fprintf(stderr, "featurize_f1: %ld samples, %ld skipped\n", kept, skipped);
    return 0;
}
