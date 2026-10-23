#pragma once
// Decoder of the released bot's sonar protocol for the memory inputs of f2_memory.hpp (F2_HEARD), with no dependencies,
// so the offline featurizer (rl/featurize/featurize_f2.cpp, replaying our own games) and a bot read the same facts. A
// copy of bots/abyss/comms.hpp's mix / tag / decode (protocol 3: [16-bit tag][48-bit payload], the tag a keyed hash of
// the team, the send round and the payload); keep the two in step if the protocol changes.
#include <cstdint>

namespace bc {
namespace f2comms {

enum Type : int { SELF = 0, ENEMY = 1, HOT = 2, LONGEST = 3, PORTAL = 4, TEAM = 5, EXPLORED = 6, SYM = 7 };

struct Fact {
    int type = 0, x = 0, y = 0, val = 0, aux = 0, round = 0, sub = 0;
};

inline uint64_t mix(uint64_t z) {
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

inline uint64_t tag(char team, int sendRound, uint64_t payload) {
    uint64_t key = 884776606060600832ull ^ static_cast<uint64_t>(team);  // the same key value as bots/abyss/comms.hpp
    return mix(key ^ mix(payload ^ (static_cast<uint64_t>(sendRound & 0xFFFF) << 48))) >> 48;
}

// Ours if the tag checks out for this round or the last; the positional facts only (EXPLORED / SYM heat left out,
// SYM sub 3 = the longest enemy dragon known: x, y, len in val, id in aux, round).
inline bool decode(uint64_t m, char team, int round, Fact& f) {
    uint64_t const p = m & ((1ull << 48) - 1), t = m >> 48;
    if (t != tag(team, round, p) && t != tag(team, round - 1, p)) return false;
    f.type = static_cast<int>(p & 7);
    if (f.type == EXPLORED) return false;
    if (f.type == SYM) {
        f.sub = static_cast<int>(p >> 3 & 3);
        if (f.sub != 3) return false;
        uint64_t const data = p >> 5;
        f.x = static_cast<int>(data & 63);
        f.y = static_cast<int>(data >> 6 & 63);
        f.val = static_cast<int>(data >> 12 & 255);
        f.aux = static_cast<int>(data >> 20 & 4095);
        int const r9 = static_cast<int>(data >> 32 & 511);
        f.round = round - ((round - r9) & 511);
        return true;
    }
    f.x = static_cast<int>(p >> 3 & 63);
    f.y = static_cast<int>(p >> 9 & 63);
    f.val = static_cast<int>(p >> 15 & 255);
    f.aux = static_cast<int>(p >> 23 & 4095);
    int const r9 = static_cast<int>(p >> 35 & 511);
    f.round = round - ((round - r9) & 511);
    return true;
}

}  // namespace f2comms
}  // namespace bc
