// flood_shim.hpp: the synthetic inbox flood of the worst-case CPU census (abyss_tools.run.cpu_census).
//
// TEST BUILDS ONLY. cpu_census copies the bot under test into a scratch workspace, includes this header from the copy's
// sources and replaces their `std::fgets(` with `floodshim::fgets(`; the bot folder itself is never touched and the
// flooded copy is never submitted. Every round block's `NUM_MSGS n` line becomes `NUM_MSGS max(n, FLOOD_N)` and the extra
// values are served before the real ones, so the bot reads, parses and decodes a FLOOD_N-value inbox on every turn, inside
// the turn's metered points (the judge charges from reading the round block to the flush).
//
// Values: FLOOD_MODE 0 = random u64, 1 = replays of real values this process received earlier (plausible frames of the
// bot's own team; random until one was seen), 2 = alternate (default). Formatting uses a digit loop, not printf, to keep
// the shim's own cost small next to the bot's parse.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef FLOOD_N
#define FLOOD_N 512
#endif
#ifndef FLOOD_MODE
#define FLOOD_MODE 2
#endif

namespace floodshim {

struct State {
    int pending = 0;      // synthetic values still to serve this turn
    int real = 0;         // real values still to read this turn (recorded for replay)
    int served = 0;
    uint64_t rng = 0x243F6A8885A308D3ull;
    uint64_t ring[256];
    int ringN = 0, ringAt = 0;
};

inline State& st() {
    static State s;
    return s;
}

inline uint64_t rand64(State& s) {
    s.rng ^= s.rng >> 12;
    s.rng ^= s.rng << 25;
    s.rng ^= s.rng >> 27;
    return s.rng * 0x2545F4914F6CDD1Dull;
}

inline void put(char* buf, int n, uint64_t v) {
    char tmp[24];
    int k = 0;
    do {
        tmp[k++] = static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v);
    int i = 0;
    while (k && i < n - 2) buf[i++] = tmp[--k];
    buf[i++] = '\n';
    buf[i] = '\0';
}

inline char* fgets(char* buf, int n, FILE* f) {
    State& s = st();
    if (s.pending > 0) {
        s.pending--;
        bool replay = FLOOD_MODE == 1 || (FLOOD_MODE == 2 && (s.served & 1));
        uint64_t v = (replay && s.ringN) ? s.ring[(s.served / 2) % s.ringN] : rand64(s);
        s.served++;
        put(buf, n, v);
        return buf;
    }
    char* r = std::fgets(buf, n, f);
    if (!r) return r;
    if (s.real > 0) {
        s.real--;
        s.ring[s.ringAt] = std::strtoull(buf, nullptr, 10);
        s.ringAt = (s.ringAt + 1) % 256;
        if (s.ringN < 256) s.ringN++;
    } else if (std::strncmp(buf, "NUM_MSGS", 8) == 0) {
        int m = std::atoi(buf + 8);
        s.real = m;
        s.pending = m < FLOOD_N ? FLOOD_N - m : 0;
        std::snprintf(buf, static_cast<size_t>(n), "NUM_MSGS %d\n", m + s.pending);
    }
    return r;
}

}  // namespace floodshim
