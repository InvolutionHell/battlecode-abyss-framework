// Protocol records -> training samples for the 9-class policy / value net (rl/train/pretrain.py; the bot's
// rl_weights.hpp format). It uses the very same parser (io.hpp) and featurizer (rl_features.hpp) as the bot.
//
//   stdin:  REC <game> <id> <team> <w> <h> <unitLimit> <outcome bits> <verb> <arg>
//           then the round block exactly as the engine sent it, then END      (rl/data/build_rl.py writes this)
//   stdout: one record per labelled turn, 6 + FDIM bytes:
//           0 label (0-3 single step relative to the facing, 4-7 multi-step move by its first step, 8 split),
//           1 split n, 2 length, 3 steps, 4 outcome bits, 5 round / 2, then the FDIM features quantised as
//           round(clip(v, 0, 2.55) * 100).
// Outcome bits: bit 0 the game's result for this side; bits 1-4 and 7 the auxiliary labels when build_rl.py --aux adds
// them (rl/data/aux_labels.py); bits 5-6 the row's sample-weight class. Turns without an action (timeout, the default
// suicide) are skipped: the 9-class head has no class for them.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "rl_features.hpp"

using namespace bc;

int main() {
    Reader in;
    Turn t;
    Features f;
    static unsigned char rec[6 + FDIM];
    long kept = 0, skipped = 0;
    while (in.next()) {
        if (!in.is("REC")) continue;
        Init init;
        init.id = in.num(2);
        init.team = in.str(3)[0];
        init.w = in.num(4);
        init.h = in.num(5);
        init.unitLimit = in.num(6);
        int outcome = in.num(7);
        std::string verb = in.str(8), arg = in.str(9);
        if (!readTurn(in, t)) break;
        while (in.next() && !in.is("END")) {}
        int label = -1, n = 0, steps = 0;
        if (verb == "MOVE" && !arg.empty()) {
            steps = static_cast<int>(arg.size());
            int rel = relDir(dirFromChar(arg[0]), t.dir);
            label = (steps >= 2 ? 4 : 0) + rel;
        } else if (verb == "SPLIT") {
            label = 8;
            n = std::atoi(arg.c_str());
        }
        if (label < 0) {
            skipped++;
            continue;
        }
        featurize(init, t, f);
        rec[0] = static_cast<unsigned char>(label);
        rec[1] = static_cast<unsigned char>(std::min(n, 255));
        rec[2] = static_cast<unsigned char>(std::min(t.length, 255));
        rec[3] = static_cast<unsigned char>(std::min(steps, 255));
        rec[4] = static_cast<unsigned char>(outcome & 0xFF);
        rec[5] = static_cast<unsigned char>(std::min(t.round / 2, 255));
        for (int i = 0; i < FDIM; i++)
            rec[6 + i] = static_cast<unsigned char>(std::lround(std::min(2.55f, std::max(0.0f, f.v[i])) * 100));
        if (std::fwrite(rec, 1, sizeof rec, stdout) != sizeof rec) {  // a full disk must not truncate a shard silently
            std::fprintf(stderr, "featurize_rl: write failed after %ld samples\n", kept);
            return 1;
        }
        kept++;
    }
    if (std::fflush(stdout) != 0 || std::ferror(stdout)) {
        std::fprintf(stderr, "featurize_rl: write failed at the end (%ld samples)\n", kept);
        return 1;
    }
    std::fprintf(stderr, "featurize_rl: %ld samples, %ld skipped\n", kept, skipped);
    return 0;
}
