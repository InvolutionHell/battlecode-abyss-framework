// Two nets on the same F1 records: a 9-class net (rl_net.hpp + rl_weights.hpp) and an F1 net (rl_f1.hpp + its
// weights), to compare their move predictions at natural weights (top-1 and NLL of the first-step direction on move
// records, weighted by 100 / keep rate) and to calibrate the F1 net's split trigger against the 9-class net's (the
// threshold that flags the same share of turns). With the released bot's two nets:
//   c++ -std=c++17 -O2 -I bots/abyss -I rl/runtime -DF1_WEIGHTS_HEADER='"r4_weights.hpp"' rl/eval/probe_nets.cpp -o probe
//   ./probe [max records] [csv out] < f1_shard.bin          (zstd -dc f1_shard.bin.zst | ./probe for compressed shards)
// Records: build_f1.py (16-byte header + 843 feature bytes). CSV columns: round, label, keep rate %, length, the 9-class
// net's split probability and its move mass by relative direction (single step + multi-step), the F1 net's split
// probability, move mass by direction (any step count) and no-action probability, its P(multi-step | direction), and
// the counts of enemy, pearl and friendly cells in view.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "rl_net.hpp"
#include "rl_f1.hpp"

using namespace bc;

int main(int argc, char** argv) {
    long const maxRec = argc > 1 ? std::atol(argv[1]) : 1L << 40;
    FILE* csv = argc > 2 ? std::fopen(argv[2], "w") : nullptr;
    static RLNet n9;
    static F1Net f1;
    constexpr int REC = 16 + FDIM;
    std::vector<unsigned char> rec(REC);
    long n = 0, agree = 0, n9Flag = 0;
    long hist1[11] = {}, hist2[11] = {};
    double wAll = 0, wOk1 = 0, wOk2 = 0, wNll1 = 0, wNll2 = 0;  // move records (labels 0-11), weight 100 / keep rate %
    if (csv) std::fprintf(csv, "round,label,rate,len,n9split,n9m0,n9m1,n9m2,n9m3,f1split,f1m0,f1m1,f1m2,f1m3,f1none,f1s0,f1s1,f1s2,f1s3,enemy,pearl,friend\n");
    while (n < maxRec && std::fread(rec.data(), 1, REC, stdin) == static_cast<size_t>(REC)) {
        float x[FDIM];
        for (int i = 0; i < FDIM; i++) x[i] = rec[16 + i] / 100.0f;
        int const round = rec[5] * 2, label = rec[0];
        n9.forwardFast(x, round);
        f1.forward(x, round);
        float m1[4], m2[4], sp[4];
        int en = 0, pe = 0, fr = 0;
        for (int i = 0; i < 49; i++) {
            en += x[(5 * 49) + i] > 0.5f || x[(7 * 49) + i] > 0.5f;
            pe += x[i] > 0.5f;
            fr += x[(4 * 49) + i] > 0.5f || x[(6 * 49) + i] > 0.5f;
        }
        int b1 = 0, b2 = 0;
        for (int r = 0; r < 4; r++) {
            m1[r] = n9.prob[r] + n9.prob[4 + r];
            m2[r] = f1.moveProb(r);
            sp[r] = m2[r] > 1e-6f ? (f1.prob[4 + r] + f1.prob[8 + r]) / m2[r] : 0.0f;
            if (m1[r] > m1[b1]) b1 = r;
            if (m2[r] > m2[b2]) b2 = r;
        }
        agree += b1 == b2;
        if (label < 12) {
            double const w = 100.0 / std::max(1, rec[15] >> 1);
            int const rel = label & 3;
            double s1 = 0, s2 = 0;
            for (int r = 0; r < 4; r++) s1 += m1[r], s2 += m2[r];
            wAll += w;
            wOk1 += w * (b1 == rel);
            wOk2 += w * (b2 == rel);
            wNll1 -= w * std::log(m1[rel] / s1 + 1e-6);
            wNll2 -= w * std::log(m2[rel] / s2 + 1e-6);
        }
        n9Flag += n9.prob[8] >= 0.4f;
        hist1[std::min(10, static_cast<int>(n9.prob[8] * 10))]++;
        hist2[std::min(10, static_cast<int>(f1.splitProb() * 10))]++;
        if (csv)
            std::fprintf(csv, "%d,%d,%d,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%d,%d\n", round, label, rec[15] >> 1, rec[2], n9.prob[8], m1[0], m1[1], m1[2], m1[3],
                         f1.splitProb(), m2[0], m2[1], m2[2], m2[3], f1.prob[15], sp[0], sp[1], sp[2], sp[3], en, pe, fr);
        n++;
    }
    std::printf("%ld records; top move agrees %.3f; 9-class split >= 0.4 on %.4f\n", n, static_cast<double>(agree) / std::max(1L, n), static_cast<double>(n9Flag) / std::max(1L, n));
    std::printf("move records (natural weights): 9-class top-1 %.4f nll %.4f | F1 top-1 %.4f nll %.4f\n", wOk1 / std::max(1.0, wAll), wNll1 / std::max(1.0, wAll), wOk2 / std::max(1.0, wAll), wNll2 / std::max(1.0, wAll));
    std::printf("9-class split prob deciles:");
    for (int k = 0; k <= 10; k++) std::printf(" %ld", hist1[k]);
    std::printf("\nF1 split prob deciles:");
    for (int k = 0; k <= 10; k++) std::printf(" %ld", hist2[k]);
    std::printf("\n");
    if (csv) std::fclose(csv);
    return 0;
}
