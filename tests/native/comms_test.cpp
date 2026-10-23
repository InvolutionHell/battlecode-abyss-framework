// Sonar protocol (comms.hpp): round trip and authentication of the tagged 64-bit facts.
// Every plain fact type survives encode -> decode; a message is accepted in its send round and the next one only; the
// other team's key rejects it; a single flipped payload bit breaks the tag.
//   c++ -std=c++20 -O2 -I bots/abyss tests/native/comms_test.cpp -o comms_test && ./comms_test
#include <cassert>
#include <cstdio>
#include "comms.hpp"
using namespace bc::comms;
int main() {
    int bad = 0;
    for (int r = 1; r < 500; r += 7)
        for (int type = 0; type < 4; type++) {
            Fact f{type, (r * 3) % 64, (r * 5) % 64, (r * 11) % 256, (r * 13) % 4096, r - (r % 5)};
            uint64_t m = encode(f, 'A', r);
            Fact g;
            if (!decode(m, 'A', r, g) || g.type != f.type || g.x != f.x || g.y != f.y || g.val != f.val || g.aux != f.aux || g.round != f.round) bad++;
            if (!decode(m, 'A', r + 1, g)) bad++;          // next round still accepted
            if (decode(m, 'A', r + 2, g)) bad++;           // stale after that
            if (decode(m, 'B', r, g)) bad++;               // other team rejects
            if (decode(m ^ 1, 'A', r, g)) bad++;           // tampered payload
        }
    std::printf("failures: %d\n", bad);
    return bad != 0;
}
