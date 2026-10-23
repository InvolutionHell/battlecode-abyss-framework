#pragma once
// Sonar coordination (protocol 3: up to four 64-bit sonars a turn, one per direction). Strong teams ping every turn;
// about half of all rays end on a teammate, so straight rows and columns are a real channel. Every dragon sends what
// it knows and relays what it heard, so facts spread over several hops.
//
// Message: [16-bit tag][48-bit payload]. The tag is a keyed hash of (team, send round, payload): enemies can read the
// rays that hit them; without the key they cannot forge ours (the key is public since this release: change it to reuse
// the protocol), a replay goes stale after a round, and in self-play the other team's copy of this bot rejects them.
// Payload: type(3) x(6) y(6) val(8) aux(12) round(9), low bits first.
//   SELF    sender head, val = its length, aux = its id, round = send round
//   ENEMY   an enemy head seen, val = visible length, aux = its id, round = when seen
//   HOT     a fast-refilling bed (fountain), val = its refill gap
//   LONGEST longest teammate heard of, val = its length, aux = its id, round = heard
//   TEAM    a local team's leader: x y = its head, val = team size, aux = leader id
//   EXPLORED the team's explored-sector bitmap (9 x 5 sectors, 45 bits after the type): the map
//           is split into sectors; one is explored once half its tiles have been in someone's view
//   PORTAL  a portal end, x y = the edge's tile, val = orientation (0 north side, 1 west
//           side), aux = portal id: teammates pair portals without seeing both ends
//   SYM     the map's symmetry (World::settleSymmetry): val = the candidates the sender has
//           not ruled out (bit s-1 for s = 1 x / 2 y / 3 xy); split children start with no memory
//           and pool their teammates' eliminations. Raw layout: type(3) sub(2) = 0, alive(3).
//   HEAT    (BC_HEAT, type 7, sub 1): presence on a coarse 5 x 4 sector grid, raw layout
//           type(3) sub(2) ours(20) theirs(20): sectors where the sender knows our / enemy heads lately
//   DROPS   (type 7, sub 2, "scavenger"): pearls a death dropped, raw layout type(3) sub(2)
//           x(6) y(6) count(8) round(9): the first drop tile seen, how many, when
//   EKING   dragon plans (type 7, sub 3): the longest enemy dragon known, raw layout type(3) sub(2) x(6) y(6)
//           len(8) id(12) round(9): its head, its visible length (a lower bound), its id, when seen
//   GATE    (BC_MAPNET, type 7 sub 0 with ext 1; a SYM fact keeps ext 0): a step through a portal, raw layout
//           type(3) sub(2) symval(3) = 0, ext(2), x(6) y(6) = the tile stepped from, val(5) = dir | state << 2, aux(12) =
//           the landing (x | y << 6, CLEARED only), round(9). States: 5 CLAIM (a teammate heads for this portal: nobody else
//           probes it, no inference), 1 PROBING (sent on the turn it steps in), 2 CLEARED (it landed alive: the portal is paired
//           by the landing), 3 DEAD (a landing seen with no open edge), 4 TRAP (a PROBING no CLEARED followed: inferred)
//   TRAPSET (BC_MAPNET, ext 3): bits = the portal ids (< 38) whose outside end leads into a dead room (seen DEAD or inferred
//           TRAP), 38 bits after the ext field: one fact carries every trap the sender knows (in testing most trap deaths
//           were dragons the per-gate facts never reached)
//   REGION  (BC_MAPNET, ext 2): a closed room seen whole, x y = its anchor (lowest tile id), val(5) = size class | status << 3
//           (0 empty, 1 beds, 2 enemy, 3 danger), aux = beds | exits << 4
#include <cstdint>
#include <vector>

#include "common.hpp"

namespace bc {

namespace comms {

enum Type : int { SELF = 0, ENEMY = 1, HOT = 2, LONGEST = 3, PORTAL = 4, TEAM = 5, EXPLORED = 6, SYM = 7 };
#if BC_MAPNET
inline constexpr int GATE = 8, REGION = 9, TRAPSET = 10;  // decoded types of the SYM sub-0 extension (BC_MAPNET)
inline constexpr int kTrapPids = 38;  // TRAPSET carries portal ids 0..37
enum GateState : int { G_UNKNOWN = 0, G_PROBING = 1, G_CLEARED = 2, G_DEAD = 3, G_TRAP = 4, G_CLAIM = 5 };
#endif
inline constexpr int kSectorsX = 9, kSectorsY = 5;  // 45 sectors = 45 bits

struct Fact {
    int type = 0, x = 0, y = 0, val = 0, aux = 0, round = 0;
    uint64_t bits = 0;  // EXPLORED; HEAT: ours | theirs << 20
    int sub = 0;        // type 7: 0 SYM, 1 HEAT, 2 DROPS, 3 EKING
};

inline uint64_t mix(uint64_t z) {
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

inline uint64_t tag(char team, int sendRound, uint64_t payload) {
    uint64_t key = 0xC47'5BA2'ADD1'5E00ull ^ static_cast<uint64_t>(team);  // fixed key, mixed with the team letter ('A' / 'B')
    return mix(key ^ mix(payload ^ (static_cast<uint64_t>(sendRound & 0xFFFF) << 48))) >> 48;
}

inline uint64_t encode(Fact const& f, char team, int sendRound) {
#if BC_MAPNET
    if (f.type == TRAPSET) {
        uint64_t data = 3ull << 3 | (f.bits & ((1ull << kTrapPids) - 1)) << 5;
        uint64_t p = static_cast<uint64_t>(SYM) | data << 5;
        return tag(team, sendRound, p) << 48 | p;
    }
    if (f.type == GATE || f.type == REGION) {
        uint64_t data = static_cast<uint64_t>(f.type == GATE ? 1 : 2) << 3 | static_cast<uint64_t>(f.x & 63) << 5 |
                        static_cast<uint64_t>(f.y & 63) << 11 | static_cast<uint64_t>(f.val & 31) << 17 |
                        static_cast<uint64_t>(f.aux & 4095) << 22 | static_cast<uint64_t>(f.round & 511) << 34;
        uint64_t p = static_cast<uint64_t>(SYM) | data << 5;  // sub 0
        return tag(team, sendRound, p) << 48 | p;
    }
#endif
    if (f.type == EXPLORED) {
        uint64_t p = static_cast<uint64_t>(EXPLORED) | (f.bits & ((1ull << 45) - 1)) << 3;
        return tag(team, sendRound, p) << 48 | p;
    }
    if (f.type == SYM) {
        uint64_t xy = static_cast<uint64_t>(f.x & 63) | static_cast<uint64_t>(f.y & 63) << 6 |
                      static_cast<uint64_t>(std::min(f.val, 255) & 255) << 12;
        uint64_t data = f.sub == 0 ? static_cast<uint64_t>(f.val & 7)
                      : f.sub == 1 ? (f.bits & ((1ull << 40) - 1))
                      : f.sub == 2 ? xy | static_cast<uint64_t>(f.round & 511) << 20
                                   : xy | static_cast<uint64_t>(f.aux & 4095) << 20 | static_cast<uint64_t>(f.round & 511) << 32;
        uint64_t p = static_cast<uint64_t>(SYM) | static_cast<uint64_t>(f.sub & 3) << 3 | data << 5;
        return tag(team, sendRound, p) << 48 | p;
    }
    uint64_t p = static_cast<uint64_t>(f.type & 7) | static_cast<uint64_t>(f.x & 63) << 3 |
                 static_cast<uint64_t>(f.y & 63) << 9 | static_cast<uint64_t>(std::min(f.val, 255) & 255) << 15 |
                 static_cast<uint64_t>(f.aux & 4095) << 23 | static_cast<uint64_t>(f.round & 511) << 35;
    return tag(team, sendRound, p) << 48 | p;
}

// A message is ours if its tag checks out for this round or the last one (a teammate with
// a lower id sent it earlier this round, a higher id last round). The 9-bit round is
// widened back using the current round.
inline bool decode(uint64_t m, char team, int round, Fact& f) {
    uint64_t p = m & ((1ull << 48) - 1);
    uint64_t t = m >> 48;
    if (t != tag(team, round, p) && t != tag(team, round - 1, p)) return false;
    f.type = static_cast<int>(p & 7);
    if (f.type == EXPLORED) {
        f.bits = p >> 3;
        return true;
    }
    if (f.type == SYM) {
        f.sub = static_cast<int>(p >> 3 & 3);
        uint64_t data = p >> 5;
        if (f.sub == 0) {
#if BC_MAPNET
            if (int ext = static_cast<int>(data >> 3 & 3)) {
                if (ext == 3) {
                    f.type = TRAPSET;
                    f.bits = data >> 5 & ((1ull << kTrapPids) - 1);
                    f.x = f.y = 0, f.round = round;
                    return true;
                }
                f.type = ext == 1 ? GATE : REGION;
                f.x = static_cast<int>(data >> 5 & 63);
                f.y = static_cast<int>(data >> 11 & 63);
                f.val = static_cast<int>(data >> 17 & 31);
                f.aux = static_cast<int>(data >> 22 & 4095);
                int r9 = static_cast<int>(data >> 34 & 511);
                f.round = round - ((round - r9) & 511);
                return true;
            }
#endif
            f.val = static_cast<int>(data & 7);
        } else if (f.sub == 1) {
            f.bits = data & ((1ull << 40) - 1);
        } else {
            f.x = static_cast<int>(data & 63);
            f.y = static_cast<int>(data >> 6 & 63);
            f.val = static_cast<int>(data >> 12 & 255);
            if (f.sub == 3) f.aux = static_cast<int>(data >> 20 & 4095);
            int r9 = static_cast<int>(data >> (f.sub == 3 ? 32 : 20) & 511);
            f.round = round - ((round - r9) & 511);
        }
        return true;
    }
    f.x = static_cast<int>(p >> 3 & 63);
    f.y = static_cast<int>(p >> 9 & 63);
    f.val = static_cast<int>(p >> 15 & 255);
    f.aux = static_cast<int>(p >> 23 & 4095);
    int r9 = static_cast<int>(p >> 35 & 511);
    f.round = round - ((round - r9) & 511);
    return f.type <= SYM;
}

// What this dragon knows from its teammates, newest per dragon id / per bed.
struct Knowledge {
    struct Entry { int cell, len, id, round; };
    std::vector<Entry> friends, enemies, hot, teams;
    Entry longest{-1, 0, -1, -1};
    int queenId = -1;     // BC_QUEEN: our queen's id; the LONGEST fact then names her while her facts are fresh
    int queenRound = -1;  // the newest round any fact of the queen carried (-1 never heard)

    static void upsert(std::vector<Entry>& v, Entry e, size_t cap) {
        for (Entry& x : v)
            if (x.id == e.id) {
                if (e.round >= x.round) x = e;
                return;
            }
        if (v.size() < cap) {
            v.push_back(e);
            return;
        }
        size_t oldest = 0;  // full: replace the stalest
        for (size_t i = 1; i < v.size(); i++)
            if (v[i].round < v[oldest].round) oldest = i;
        if (v[oldest].round < e.round) v[oldest] = e;
    }

    void prune(int round, int maxAge, int longestAge = -1) {
        if (longestAge < 0) longestAge = maxAge;
        auto stale = [&](Entry const& e) { return round - e.round > maxAge; };
        friends.erase(std::remove_if(friends.begin(), friends.end(), stale), friends.end());
        enemies.erase(std::remove_if(enemies.begin(), enemies.end(), stale), enemies.end());
        teams.erase(std::remove_if(teams.begin(), teams.end(), stale), teams.end());
        if (longest.cell >= 0 && round - longest.round > longestAge) longest = {-1, 0, -1, -1};
    }

    void noteLongest(Entry e) {
        if (queenId >= 0) {
            // The queen decides round 500 (rules 1.2.3), so she is the one the team feeds and guards, however short;
            // a teammate takes her place only after her facts went stale (prune) - she is dead or out of touch
            bool eq = e.id == queenId, lq = longest.cell >= 0 && longest.id == queenId;
            if (eq) queenRound = std::max(queenRound, e.round);
            if (eq && lq) {
                if (e.round >= longest.round) longest = e;
                return;
            }
            if (eq) {
                longest = e;
                return;
            }
            if (lq) return;
        }
        if (e.len > longest.len || (e.id == longest.id && e.round >= longest.round) || longest.cell < 0) longest = e;
    }
};

}  // namespace comms

}  // namespace bc
