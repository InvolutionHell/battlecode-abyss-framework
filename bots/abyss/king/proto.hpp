#pragma once
// King library, part 1: the sonar protocol of the king library (inactive in the final build).
//
// Header-only and self-contained (no World, no search code): the frame and its checksum, the per-round 4-round
// Feistel cipher, the eight message layouts, the local fact tables (the "blackboard") with merge and ageing, the
// ray-end prediction and the per-ray message choice. Every array is fixed-size; keep a Blackboard in static storage
// (the WASM stack is 64 KiB).
//
// Engine facts this relies on: rays are cast after the move; a lower id's ray reaches a higher id in the same round,
// a higher id's ray is read the next round, so a receiver tries the keys of rounds r and r - 1; the team letter is
// 'A' / 'B'; ids stay below 4096 (max seen 1907); maps are at most 64 x 64; a no-action turn sends no sonar.

#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace bc::king {

// ------------------------------------------------------------------------------------------------ constants
constexpr uint64_t kK1 = 0xA3C59AC2F1E4D78Bull;  // checksum key
constexpr uint64_t kK2 = 0x5B1D0E9F37C26A48ull;  // round-key seed
constexpr uint64_t kSalt[2] = {0x6E3D2C1B0A998877ull, 0xD41F2E3D4C5B6A79ull};  // team A, team B
constexpr uint64_t kGolden = 0x9E3779B97F4A7C15ull;
constexpr uint64_t kMask48 = 0xFFFFFFFFFFFFull;

enum MsgType : uint8_t {
    M_NONE = 0, M_KING = 1, M_EKING = 2, M_BED3 = 3, M_PORTAL = 4, M_SELF = 5, M_EXPL = 6, M_THREAT = 7, M_MAP = 8,
};

// TTLs.
constexpr int kKingNavTtl = 40;      // a claim steers navigation this long
constexpr int kKingBlockTtl = 120;   // a live claim blocks new claims this long (released early by a tombstone)
constexpr int kKingFwdTtl = 40;      // a claim is forwarded while at most this old (10 at first; in simulation 40 cut
                                     // coexisting kings)
constexpr int kTombFwdTtl = 20;      // a tombstone is forwarded while at most this old
constexpr int kChildKingTtl = 40;    // a new child gets the freshest claim up to this age
constexpr int kEKingTtl = 100;       // E_lo window
constexpr int kEKingFwdTtl = 10;
constexpr int kThreatTtl = 6;
constexpr int kThreatFwdTtl = 3;
constexpr int kSelfTtl = 7;          // claims (tgt) and (c4) competitors
constexpr int kSelfKeep = 16;        // kept for L-bar

// SELF.task values.
enum SelfTask : uint8_t { T_FORAGE = 0, T_HOLD = 1, T_EXPLORE = 2, T_FEED = 3, T_FACTORY = 4, T_HEIR = 5, T_HUNT = 6, T_NONE = 7 };
// KING.mode values.
enum Mode : uint8_t { MODE_OPEN = 0, MODE_GROW = 1, MODE_CONVERGE = 2, MODE_FINISH = 3 };
// KING.sigma values.
enum Sigma : uint8_t { SIG_UNKNOWN = 0, SIG_BEHIND = 1, SIG_EVEN = 2, SIG_AHEAD = 3 };
// MAP.flags bits; bit 0 is reserved and must be 0.
enum MapFlag : uint8_t {
    F_ISOLATED = 0x80, F_FAR = 0x40, F_CAPPED = 0x20, F_GATED = 0x10, F_ECO_F = 0x08, F_BARREN = 0x04, F_RICHSTART = 0x02,
};

// ------------------------------------------------------------------------------------------------ primitives
inline uint64_t splitmix64(uint64_t x) {
    x += kGolden;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

inline int teamIndex(char team) { return (team == 'B' || team == 'b') ? 1 : 0; }

inline uint64_t roundKey(int team, int r) {
    return splitmix64(kK2 ^ kSalt[team & 1] ^ (static_cast<uint64_t>(r) * kGolden));
}

inline uint32_t feistelF(uint64_t k, int i, uint32_t x) {
    return static_cast<uint32_t>(splitmix64(k ^ (static_cast<uint64_t>(i) << 32) ^ x) >> 32);
}

inline uint64_t encrypt(uint64_t p, uint64_t k) {
    uint32_t L = static_cast<uint32_t>(p >> 32), R = static_cast<uint32_t>(p);
    for (int i = 0; i < 4; i++) {
        uint32_t nR = L ^ feistelF(k, i, R);
        L = R;
        R = nR;
    }
    return (static_cast<uint64_t>(L) << 32) | R;
}

inline uint64_t decrypt(uint64_t w, uint64_t k) {
    uint32_t L = static_cast<uint32_t>(w >> 32), R = static_cast<uint32_t>(w);
    for (int i = 3; i >= 0; i--) {
        uint32_t pL = R ^ feistelF(k, i, L);
        R = L;
        L = pL;
    }
    return (static_cast<uint64_t>(L) << 32) | R;
}

// Plain frame P = T(4) | payload(48) | C(12), C = splitmix64(K1 ^ SALT[team] ^ (P >> 12)) & 0xFFF.
inline uint64_t makeFrame(int team, int type, uint64_t payload) {
    uint64_t hi = (static_cast<uint64_t>(type & 15) << 48) | (payload & kMask48);
    uint64_t c = splitmix64(kK1 ^ kSalt[team & 1] ^ hi) & 0xFFF;
    return (hi << 12) | c;
}

inline bool frameOk(int team, uint64_t p) {
    int t = static_cast<int>(p >> 60);
    if (t < 1 || t > 8) return false;
    return (splitmix64(kK1 ^ kSalt[team & 1] ^ (p >> 12)) & 0xFFF) == (p & 0xFFF);
}

inline uint64_t seal(int team, int r, int type, uint64_t payload) {
    return encrypt(makeFrame(team, type, payload), roundKey(team, r));
}

// Tries the keys of rounds r and r - 1; on success returns the plain frame and the send round (r wins a double match).
inline bool unseal(int team, int r, uint64_t wire, uint64_t& plain, int& rSend) {
    for (int back = 0; back <= 1; back++) {
        int rs = r - back;
        if (rs < 0) break;
        uint64_t p = decrypt(wire, roundKey(team, rs));
        if (frameOk(team, p)) {
            plain = p;
            rSend = rs;
            return true;
        }
    }
    return false;
}

inline int frameType(uint64_t p) { return static_cast<int>(p >> 60); }
inline uint64_t framePayload(uint64_t p) { return (p >> 12) & kMask48; }

struct BitW {
    uint64_t v = 0;
    int n = 0;
    void put(uint64_t x, int bits) {
        v = (v << bits) | (x & ((1ull << bits) - 1));
        n += bits;
    }
};
struct BitR {
    uint64_t v;
    int left = 48;
    explicit BitR(uint64_t payload) : v(payload) {}
    uint32_t get(int bits) {
        left -= bits;
        return static_cast<uint32_t>((v >> left) & ((1ull << bits) - 1));
    }
};

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ------------------------------------------------------------------------------------------------ messages
struct KingMsg {  // T=1: round9 | kid12 | klen8 | x6 | y6 | sigma2 | h2 | busy1 | mode2
    int round = 0, kid = 0, klen = 0, x = 0, y = 0, sigma = 0, h = 0, busy = 0, mode = 0;
    bool tomb() const { return klen == 0; }
};
struct EKingMsg {  // T=2: round9 | eid12 | elen8 | x6 | y6 | dir2 | nHead3 | conf2
    int round = 0, eid = 0, elen = 0, x = 0, y = 0, dir = 0, nHead = 0, conf = 0;
};
struct BedEntry {  // x = y = 63 pads an empty slot
    int x = 63, y = 63, fount = 0, rateQ = 0;
    bool empty() const { return x == 63 && y == 63; }
};
struct Bed3Msg {  // T=3: 3 x (x6 | y6 | fount1 | rateQ3)
    BedEntry e[3];
};
struct PortalMsg {  // T=4: pid8 | ax6 | ay6 | aW1 | bx6 | by6 | bW1 | open4 | mirror1 | unexpl1 | zero8
    int pid = 0, ax = 0, ay = 0, aW = 0, bx = 0, by = 0, bW = 0, open = 0, mirror = 0, unexpl = 0;
};
struct SelfMsg {  // T=5: rnd3 | id12 | len7 | x6 | y6 | task3 | tgt8 | load2 | thr1
    int rnd3 = 0, id = 0, len = 0, x = 0, y = 0, task = T_NONE, tgt = 0xFF, load = 0, thr = 0;
};
struct ThreatMsg {  // T=7: round9 | x6 | y6 | nHead4 | maxLen8 | zero15
    int round = 0, x = 0, y = 0, nHead = 0, maxLen = 0;
};
struct MapMsg {  // T=8: round9 | flags8 | sym2 | esx6 | esy6 | capQ6 | nFount4 | zero7
    int round = 0, flags = 0, sym = 0, esx = 0, esy = 0, capQ = 0, nFount = 0;
};

inline uint64_t encKing(KingMsg const& m) {
    BitW w;
    w.put(m.round, 9); w.put(m.kid, 12); w.put(clampi(m.klen, 0, 255), 8); w.put(m.x, 6); w.put(m.y, 6);
    w.put(m.sigma, 2); w.put(m.h, 2); w.put(m.busy, 1); w.put(m.mode, 2);
    return w.v;
}
inline KingMsg decKing(uint64_t p) {
    BitR r(p);
    KingMsg m;
    m.round = r.get(9); m.kid = r.get(12); m.klen = r.get(8); m.x = r.get(6); m.y = r.get(6);
    m.sigma = r.get(2); m.h = r.get(2); m.busy = r.get(1); m.mode = r.get(2);
    return m;
}
inline uint64_t encEKing(EKingMsg const& m) {
    BitW w;
    w.put(m.round, 9); w.put(m.eid, 12); w.put(clampi(m.elen, 0, 255), 8); w.put(m.x, 6); w.put(m.y, 6);
    w.put(m.dir, 2); w.put(clampi(m.nHead, 0, 7), 3); w.put(m.conf, 2);
    return w.v;
}
inline EKingMsg decEKing(uint64_t p) {
    BitR r(p);
    EKingMsg m;
    m.round = r.get(9); m.eid = r.get(12); m.elen = r.get(8); m.x = r.get(6); m.y = r.get(6);
    m.dir = r.get(2); m.nHead = r.get(3); m.conf = r.get(2);
    return m;
}
inline uint64_t encBed3(Bed3Msg const& m) {
    BitW w;
    for (int i = 0; i < 3; i++) {
        w.put(m.e[i].x, 6); w.put(m.e[i].y, 6); w.put(m.e[i].fount, 1); w.put(clampi(m.e[i].rateQ, 0, 7), 3);
    }
    return w.v;
}
inline Bed3Msg decBed3(uint64_t p) {
    BitR r(p);
    Bed3Msg m;
    for (int i = 0; i < 3; i++) {
        m.e[i].x = r.get(6); m.e[i].y = r.get(6); m.e[i].fount = r.get(1); m.e[i].rateQ = r.get(3);
    }
    return m;
}
inline uint64_t encPortal(PortalMsg const& m) {
    BitW w;
    w.put(m.pid, 8); w.put(m.ax, 6); w.put(m.ay, 6); w.put(m.aW, 1); w.put(m.bx, 6); w.put(m.by, 6); w.put(m.bW, 1);
    w.put(m.open, 4); w.put(m.mirror, 1); w.put(m.unexpl, 1); w.put(0, 8);
    return w.v;
}
inline PortalMsg decPortal(uint64_t p, int* zero = nullptr) {
    BitR r(p);
    PortalMsg m;
    m.pid = r.get(8); m.ax = r.get(6); m.ay = r.get(6); m.aW = r.get(1); m.bx = r.get(6); m.by = r.get(6);
    m.bW = r.get(1); m.open = r.get(4); m.mirror = r.get(1); m.unexpl = r.get(1);
    int z = r.get(8);
    if (zero) *zero = z;
    return m;
}
inline uint64_t encSelf(SelfMsg const& m) {
    BitW w;
    w.put(m.rnd3, 3); w.put(m.id, 12); w.put(clampi(m.len, 0, 127), 7); w.put(m.x, 6); w.put(m.y, 6);
    w.put(m.task, 3); w.put(m.tgt, 8); w.put(m.load, 2); w.put(m.thr, 1);
    return w.v;
}
inline SelfMsg decSelf(uint64_t p) {
    BitR r(p);
    SelfMsg m;
    m.rnd3 = r.get(3); m.id = r.get(12); m.len = r.get(7); m.x = r.get(6); m.y = r.get(6);
    m.task = r.get(3); m.tgt = r.get(8); m.load = r.get(2); m.thr = r.get(1);
    return m;
}
inline uint64_t encThreat(ThreatMsg const& m) {
    BitW w;
    w.put(m.round, 9); w.put(m.x, 6); w.put(m.y, 6); w.put(clampi(m.nHead, 0, 15), 4); w.put(clampi(m.maxLen, 0, 255), 8);
    w.put(0, 15);
    return w.v;
}
inline ThreatMsg decThreat(uint64_t p, int* zero = nullptr) {
    BitR r(p);
    ThreatMsg m;
    m.round = r.get(9); m.x = r.get(6); m.y = r.get(6); m.nHead = r.get(4); m.maxLen = r.get(8);
    int z = r.get(15);
    if (zero) *zero = z;
    return m;
}
inline uint64_t encMap(MapMsg const& m) {
    BitW w;
    w.put(m.round, 9); w.put(m.flags & 0xFE, 8); w.put(m.sym, 2); w.put(m.esx, 6); w.put(m.esy, 6);
    w.put(clampi(m.capQ, 0, 63), 6); w.put(clampi(m.nFount, 0, 15), 4); w.put(0, 7);
    return w.v;
}
inline MapMsg decMap(uint64_t p, int* zero = nullptr) {
    BitR r(p);
    MapMsg m;
    m.round = r.get(9); m.flags = r.get(8); m.sym = r.get(2); m.esx = r.get(6); m.esy = r.get(6);
    m.capQ = r.get(6); m.nFount = r.get(4);
    int z = r.get(7);
    if (zero) *zero = z | (m.flags & 1);
    return m;
}

// rateQ3 = round(1.5 * log2(1 + pearls per 100 rounds)), capped at 7; and back.
inline int rateQ(double perHundred) {
    if (perHundred <= 0) return 0;
    int q = static_cast<int>(1.5 * std::log2(1.0 + perHundred) + 0.5);
    return q > 7 ? 7 : q;
}
inline double rateFromQ(int q) { return std::exp2((q & 7) / 1.5) - 1.0; }

// EXPL: 8 columns x 6 rows of sectors; sector i = sy * 8 + sx sits at payload bit 47 - i.
inline int sectorOf(int x, int y, int W, int H) { return (y * 6 / H) * 8 + (x * 8 / W); }
inline uint64_t sectorBit(int s) { return 1ull << (47 - s); }

// ------------------------------------------------------------------------------------------------ geometry
struct Geo {
    int W = 1, H = 1;
    int dx(int a, int b) const {  // signed torus delta b - a in x, in (-W/2, W/2]
        int d = ((b - a) % W + W) % W;
        return d > W / 2 ? d - W : d;
    }
    int dy(int a, int b) const {
        int d = ((b - a) % H + H) % H;
        return d > H / 2 ? d - H : d;
    }
    int man(int ax, int ay, int bx, int by) const { return std::abs(dx(ax, bx)) + std::abs(dy(ay, by)); }
    int cheb(int ax, int ay, int bx, int by) const {
        int a = std::abs(dx(ax, bx)), b = std::abs(dy(ay, by));
        return a > b ? a : b;
    }
};

// ------------------------------------------------------------------------------------------------ fact tables
struct KingFact {
    KingMsg m;
    bool valid = false;
    bool own = false;       // I originated it (I am or was this king)
    int heardAt = -1;       // last round any copy arrived
    int updatedAt = -1;     // last round a newer claim round arrived
    int freshSeenAt = -1;   // last round at which the newest claim was <= 3 rounds old
    int prevKlen = -1;      // klen of the previous claim of this kid (abdication detection)
};
struct EKingFact {
    EKingMsg m;             // elen = max seen, position = newest
    bool valid = false;
    int maxLenRound = -1;   // round of the max elen
    int firstRound = -1;    // first round this enemy was known (merge detection)
    int firstLen = 0;
};
struct BedFact {
    int x = 0, y = 0, fount = 0, rateQ = 0;
    bool valid = false, own = false;
    int round = -1;
};
struct PortalFact {
    PortalMsg m;
    bool valid = false, own = false;
    int round = -1;
};
struct SelfFact {
    SelfMsg m;
    bool valid = false;
    int round = -1;         // reconstructed from rnd3
};
struct ThreatFact {
    ThreatMsg m;
    bool valid = false;
};

struct RecvStats {
    int total = 0, accepted = 0, badFrame = 0, badSemantic = 0;
    int byType[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
};

// The per-process blackboard. One instance per dragon process, static storage.
struct Blackboard {
    static constexpr int kKings = 8, kEKings = 4, kBeds = 64, kPortals = 32, kSelves = 64, kThreats = 8;

    Geo g;
    int team = 0, myId = -1, r = 0;
    KingFact kings[kKings];
    EKingFact ekings[kEKings];
    BedFact beds[kBeds];
    PortalFact portals[kPortals];
    SelfFact selves[kSelves];
    ThreatFact threats[kThreats];
    uint64_t explored = 0;
    MapMsg map;
    bool mapValid = false;
    int mapRound = -1;
    bool heardAnyKing = false;  // crown (c0)
    int lastKingHeardAt = -1;   // any KING or tombstone arrival
    // SELF receipts per round (c25 and L-bar); ring of 32 rounds.
    int selfHeardRing[32] = {0};
    int selfThrRing[32] = {0};
    int ringRound[32] = {0};
    RecvStats stats;
    // Plain frames accepted by the last receive() (the adapter hands PORTAL / BED3 / MAP facts on to World).
    static constexpr int kAcceptLog = 32;
    uint64_t accepted[kAcceptLog];
    int nAccepted = 0;
    int staticRot = 0, portalRot = 0;
    int kingFwdTtl = kKingFwdTtl;   // forwarding age limit of a claim (simulator knob)
    int kingRays = 2;               // rays a non-king spends on the king fact per turn: the second one only while the
    int kingRaysFresh = 10;         //   claim is at most this old and 3 rays are useful (SELF keeps one)

    // Resets in place (no large temporary: the WASM stack is 64 KiB).
    void init(int W, int H, char teamLetter, int id) {
        g.W = W; g.H = H;
        team = teamIndex(teamLetter);
        myId = id;
        r = 0;
        for (auto& k : kings) k = KingFact();
        for (auto& e : ekings) e = EKingFact();
        for (auto& b : beds) b = BedFact();
        for (auto& p : portals) p = PortalFact();
        for (auto& s : selves) s = SelfFact();
        for (auto& t : threats) t = ThreatFact();
        explored = 0;
        map = MapMsg();
        mapValid = false;
        mapRound = -1;
        heardAnyKing = false;
        lastKingHeardAt = -1;
        for (int i = 0; i < 32; i++) { selfHeardRing[i] = 0; selfThrRing[i] = 0; ringRound[i] = -1000; }
        stats = RecvStats();
        nAccepted = 0;
        staticRot = portalRot = 0;
        kingFwdTtl = kKingFwdTtl;
        kingRays = 2;
        kingRaysFresh = 10;
    }

    void beginTurn(int round) {
        r = round;
        int s = r & 31;
        if (ringRound[s] != r) { ringRound[s] = r; selfHeardRing[s] = 0; selfThrRing[s] = 0; }
        for (auto& k : kings)
            if (k.valid && r - k.m.round <= 3) k.freshSeenAt = r;
    }

    // ---- KING table
    KingFact* kingSlot(int kid) {
        for (auto& k : kings) if (k.valid && k.m.kid == kid) return &k;
        return nullptr;
    }
    KingFact const* kingSlot(int kid) const {
        for (auto& k : kings) if (k.valid && k.m.kid == kid) return &k;
        return nullptr;
    }
    // Returns true when the table changed. Same kid: the newer round wins; on a tie a claim beats a tombstone
    // (a live king's refresh supersedes a false tombstone).
    bool mergeKing(KingMsg const& m, bool own) {
        KingFact* k = kingSlot(m.kid);
        if (k) {
            k->heardAt = r;
            bool newer = m.round > k->m.round || (m.round == k->m.round && k->m.klen == 0 && m.klen > 0);
            if (!newer) return false;
            if (k->m.klen > 0) k->prevKlen = k->m.klen;  // the last claimed length (a tombstone keeps it)
            k->m = m;
            k->own = own;
            k->updatedAt = r;
            if (r - m.round <= 3) k->freshSeenAt = r;
            return true;
        }
        // New kid: take a free slot, else evict the oldest claim round.
        KingFact* slot = nullptr;
        for (auto& s : kings) if (!s.valid) { slot = &s; break; }
        if (!slot) {
            slot = &kings[0];
            for (auto& s : kings) if (s.m.round < slot->m.round) slot = &s;
            if (slot->m.round > m.round) return false;
        }
        *slot = KingFact();
        slot->m = m;
        slot->valid = true;
        slot->own = own;
        slot->heardAt = r;
        slot->updatedAt = r;
        slot->freshSeenAt = (r - m.round <= 3) ? r : -1;
        return true;
    }
    // Live claim of kid for blocking: newest entry is a claim (not a tombstone) and at most kKingBlockTtl old.
    bool kingLive(KingFact const& k) const { return k.valid && k.m.klen > 0 && r - k.m.round <= kKingBlockTtl; }

    // Best live non-tombstone claim within maxAge, by (klen, -kid); excludes exceptKid.
    KingFact const* bestKing(int maxAge, int exceptKid = -1) const {
        KingFact const* best = nullptr;
        for (auto& k : kings) {
            if (!k.valid || k.m.klen == 0 || k.m.kid == exceptKid || r - k.m.round > maxAge) continue;
            if (!best || k.m.klen > best->m.klen || (k.m.klen == best->m.klen && k.m.kid < best->m.kid)) best = &k;
        }
        return best;
    }
    // The king to follow (navigation, forwarding, feeding): among claims within maxAge, keep those at most `recent`
    // rounds older than the newest claim (a dead king's claim stops updating), then the longest (klen, -kid).
    KingFact const* currentKing(int maxAge, int exceptKid = -1, int recent = 20) const {
        int newest = -1;
        for (auto& k : kings)
            if (k.valid && k.m.klen > 0 && k.m.kid != exceptKid && r - k.m.round <= maxAge && k.m.round > newest)
                newest = k.m.round;
        if (newest < 0) return nullptr;
        KingFact const* best = nullptr;
        for (auto& k : kings) {
            if (!k.valid || k.m.klen == 0 || k.m.kid == exceptKid || r - k.m.round > maxAge) continue;
            if (k.m.round < newest - recent) continue;
            if (!best || k.m.klen > best->m.klen || (k.m.klen == best->m.klen && k.m.kid < best->m.kid)) best = &k;
        }
        return best;
    }
    // Freshest entry (claim or tombstone) within maxAge, any kid.
    KingFact const* freshestKing(int maxAge, bool tombOnly = false) const {
        KingFact const* best = nullptr;
        for (auto& k : kings) {
            if (!k.valid || r - k.m.round > maxAge) continue;
            if (tombOnly && k.m.klen != 0) continue;
            if (!best || k.m.round > best->m.round) best = &k;
        }
        return best;
    }

    // ---- EKING table: elen max, position newest.
    void mergeEKing(EKingMsg const& m) {
        EKingFact* e = nullptr;
        for (auto& s : ekings) if (s.valid && s.m.eid == m.eid) { e = &s; break; }
        if (!e) {
            for (auto& s : ekings) if (!s.valid) { e = &s; break; }
            if (!e) {  // evict the smallest elen (ties: oldest)
                e = &ekings[0];
                for (auto& s : ekings)
                    if (s.m.elen < e->m.elen || (s.m.elen == e->m.elen && s.m.round < e->m.round)) e = &s;
                if (e->m.elen > m.elen && r - e->m.round <= kEKingTtl) return;
            }
            *e = EKingFact();
            e->valid = true;
            e->m = m;
            e->maxLenRound = m.round;
            e->firstRound = m.round;
            e->firstLen = m.elen;
            return;
        }
        int elen = e->m.elen;
        int mlr = e->maxLenRound;
        if (m.elen > elen || (m.elen == elen && m.round > mlr)) { elen = m.elen; mlr = m.round; }
        if (m.round >= e->m.round) e->m = m;
        e->m.elen = elen;
        e->maxLenRound = mlr;
        if (m.round < e->firstRound) { e->firstRound = m.round; e->firstLen = m.elen; }
    }
    // E_lo: max elen over enemy kings known within kEKingTtl rounds.
    int eLo() const {
        int best = 0;
        for (auto& e : ekings)
            if (e.valid && r - e.maxLenRound <= kEKingTtl && e.m.elen > best) best = e.m.elen;
        return best;
    }

    // ---- BED table: clusters closer than 3 (Chebyshev) are the same cluster.
    void mergeBed(int x, int y, int fount, int rq, bool own, int round) {
        BedFact* b = nullptr;
        for (auto& s : beds)
            if (s.valid && g.cheb(s.x, s.y, x, y) <= 2) { b = &s; break; }
        if (b) {
            b->fount |= fount;
            if (own || !b->own) {
                if (own || round >= b->round) { b->rateQ = rq; b->round = round; }
                b->own = b->own || own;
            }
            return;
        }
        for (auto& s : beds) if (!s.valid) { b = &s; break; }
        if (!b) {  // evict the lowest-rate relayed entry
            for (auto& s : beds) if (!s.own && (!b || s.rateQ < b->rateQ)) b = &s;
            if (!b || (b->rateQ > rq && !own)) return;
        }
        b->valid = true; b->x = x; b->y = y; b->fount = fount; b->rateQ = rq; b->own = own; b->round = round;
    }

    // ---- PORTAL table keyed by pid.
    void mergePortal(PortalMsg const& m, bool own, int round) {
        PortalFact* p = nullptr;
        for (auto& s : portals) if (s.valid && s.m.pid == m.pid) { p = &s; break; }
        if (p) {
            p->m.open |= m.open;
            p->m.mirror = p->m.mirror & m.mirror;
            p->m.unexpl = p->m.unexpl & m.unexpl;
            if (own) { p->m.ax = m.ax; p->m.ay = m.ay; p->m.aW = m.aW; p->m.bx = m.bx; p->m.by = m.by; p->m.bW = m.bW; }
            p->own = p->own || own;
            p->round = round > p->round ? round : p->round;
            return;
        }
        for (auto& s : portals) if (!s.valid) { p = &s; break; }
        if (!p) return;
        p->valid = true; p->m = m; p->own = own; p->round = round;
    }

    // ---- SELF table keyed by id.
    void mergeSelf(SelfMsg const& m, int round) {
        SelfFact* s = nullptr;
        for (auto& x : selves) if (x.valid && x.m.id == m.id) { s = &x; break; }
        if (s) {
            if (round >= s->round) { s->m = m; s->round = round; }
            return;
        }
        for (auto& x : selves) if (!x.valid || r - x.round > kSelfKeep) { s = &x; break; }
        if (!s) {
            s = &selves[0];
            for (auto& x : selves) if (x.round < s->round) s = &x;
        }
        s->valid = true; s->m = m; s->round = round;
    }
    SelfFact const* selfOf(int id, int maxAge) const {
        for (auto& x : selves) if (x.valid && x.m.id == id && r - x.round <= maxAge) return &x;
        return nullptr;
    }

    // ---- THREAT table keyed by cell.
    void mergeThreat(ThreatMsg const& m) {
        ThreatFact* t = nullptr;
        for (auto& s : threats) if (s.valid && s.m.x == m.x && s.m.y == m.y) { t = &s; break; }
        if (t) { if (m.round >= t->m.round) t->m = m; return; }
        for (auto& s : threats) if (!s.valid || r - s.m.round > kThreatTtl) { t = &s; break; }
        if (!t) {
            t = &threats[0];
            for (auto& s : threats) if (s.m.round < t->m.round) t = &s;
            if (t->m.round > m.round) return;
        }
        t->valid = true; t->m = m;
    }

    // ---- MAP: flags OR; sym / enemy spawn first consistent value; capQ / nFount newest.
    void mergeMap(MapMsg const& m) {
        if (!mapValid) { map = m; mapValid = true; mapRound = m.round; return; }
        map.flags |= (m.flags & 0xFE);
        if (map.sym == 0 && m.sym != 0) { map.sym = m.sym; map.esx = m.esx; map.esy = m.esy; }
        if (m.round >= mapRound) { map.capQ = m.capQ; map.nFount = m.nFount; mapRound = m.round; map.round = m.round; }
    }

    // ---- receiving. enemyIds: ids of enemy dragons in my vision this turn. Returns the number of accepted messages.
    int receive(uint64_t const* msgs, int n, int const* enemyIds, int nEnemy) {
        int acc = 0;
        nAccepted = 0;
        for (int i = 0; i < n; i++) {
            stats.total++;
            uint64_t p;
            int rs;
            if (msgs[i] == 0 || !unseal(team, r, msgs[i], p, rs)) { stats.badFrame++; continue; }
            if (!accept(p, rs, enemyIds, nEnemy)) { stats.badSemantic++; continue; }
            if (nAccepted < kAcceptLog) accepted[nAccepted++] = p;
            stats.accepted++;
            stats.byType[frameType(p)]++;
            acc++;
        }
        return acc;
    }

    bool isEnemyId(int id, int const* enemyIds, int nEnemy) const {
        for (int i = 0; i < nEnemy; i++) if (enemyIds[i] == id) return true;
        return false;
    }
    bool inMap(int x, int y) const { return x < g.W && y < g.H; }

    // Semantic checks and merge of one plain frame sent in round rs.
    bool accept(uint64_t p, int rs, int const* enemyIds, int nEnemy) {
        uint64_t pl = framePayload(p);
        switch (frameType(p)) {
        case M_KING: {
            KingMsg m = decKing(pl);
            if (m.round > rs || rs - m.round > kKingBlockTtl) return false;
            if (m.klen == 1) return false;
            if (!inMap(m.x, m.y)) return false;
            if (m.kid == 0xFFF || isEnemyId(m.kid, enemyIds, nEnemy)) return false;
            if (m.kid == myId) return true;  // my own old claim or a tombstone of me: nothing to learn
            mergeKing(m, false);
            heardAnyKing = true;
            lastKingHeardAt = r;
            return true;
        }
        case M_EKING: {
            EKingMsg m = decEKing(pl);
            if (m.round > rs || rs - m.round > kEKingTtl) return false;
            if (m.elen < 2 || !inMap(m.x, m.y) || m.conf == 3) return false;
            mergeEKing(m);
            return true;
        }
        case M_BED3: {
            Bed3Msg m = decBed3(pl);
            int got = 0;
            for (auto& e : m.e) {
                if (e.empty()) continue;
                if (!inMap(e.x, e.y)) return false;
                got++;
            }
            if (!got) return false;
            for (auto& e : m.e) if (!e.empty()) mergeBed(e.x, e.y, e.fount, e.rateQ, false, rs);
            return true;
        }
        case M_PORTAL: {
            int zero = 0;
            PortalMsg m = decPortal(pl, &zero);
            if (zero || !inMap(m.ax, m.ay) || !inMap(m.bx, m.by)) return false;
            mergePortal(m, false, rs);
            return true;
        }
        case M_SELF: {
            SelfMsg m = decSelf(pl);
            if (m.len < 2 || !inMap(m.x, m.y) || m.id == 0xFFF || isEnemyId(m.id, enemyIds, nEnemy)) return false;
            if (m.id == myId) return true;
            int round = rs - ((rs - m.rnd3) % 8 + 8) % 8;
            mergeSelf(m, round);
            int s = r & 31;
            selfHeardRing[s]++;
            selfThrRing[s] += m.thr;
            return true;
        }
        case M_EXPL:
            if (!pl) return false;
            explored |= pl;
            return true;
        case M_THREAT: {
            int zero = 0;
            ThreatMsg m = decThreat(pl, &zero);
            if (zero || m.round > rs || rs - m.round > kThreatTtl || !inMap(m.x, m.y) || m.nHead == 0) return false;
            mergeThreat(m);
            return true;
        }
        case M_MAP: {
            int zero = 0;
            MapMsg m = decMap(pl, &zero);
            if (zero || m.round > rs || !inMap(m.esx, m.esy)) return false;
            mergeMap(m);
            return true;
        }
        }
        return false;
    }

    // SELF receipts over the last `window` rounds (<= 32): heard and heard with thr = 1.
    void selfRates(int window, int& heard, int& thr) const {
        heard = thr = 0;
        for (int i = 0; i < 32; i++) {
            if (r - ringRound[i] < window && ringRound[i] <= r) { heard += selfHeardRing[i]; thr += selfThrRing[i]; }
        }
    }

    // ---- own facts (the World and the capacity estimate call these)
    void noteOwnBed(int x, int y, int fount, int rq) { mergeBed(x, y, fount, rq, true, r); }
    void noteOwnPortal(PortalMsg const& m) { mergePortal(m, true, r); }
    void noteOwnMap(MapMsg m) { m.round = r; mergeMap(m); }
    void noteOwnEKing(EKingMsg m) { m.round = r; mergeEKing(m); }
    void noteOwnThreat(ThreatMsg m) { m.round = r; mergeThreat(m); }
    void orExplored(uint64_t bits) { explored |= (bits & kMask48); }
};

// ------------------------------------------------------------------------------------------------ rays
enum RayEnd : uint8_t { RE_SELF = 0, RE_KELP = 1, RE_ALLY = 2, RE_ENEMY = 3, RE_OUT = 4, RE_NECK = 5 };

struct RayPred {
    RayEnd end = RE_KELP;
    int allyId = -1;
    int allyVisLen = 0;  // visible segments of the ally hit (<= 3 marks a likely new child)
    int dist = 0;        // steps to the stop
    int endX = 0, endY = 0;  // stop cell, or the first cell outside vision for RE_OUT
};

// Occupancy codes an Env reports.
enum Occ : uint8_t { OCC_EMPTY = 0, OCC_SELF = 1, OCC_ALLY = 2, OCC_ENEMY = 3 };

// Predicts where a ray cast from `head` (after this turn's move) in direction d stops. Env provides:
//   int  step(int cell, int d) const   next cell through wraps and paired portals; -1 kelp; -2 unknown (unpaired portal)
//   int  occ(int cell, int& id, int& visLen) const   Occ code at cast time: lower ids after their move, higher ids
//        before theirs, my own body after my move (my whole body, known anywhere)
//   bool inWindow(int cell) const       inside this turn's 7x7 vision
//   int  W() const, H() const
// Cells are y * W + x. Directions 0 N, 1 E, 2 S, 3 W.
template <class Env>
RayPred predictRay(Env const& env, int head, int d) {
    RayPred p;
    int W = env.W(), H = env.H();
    int cur = head;
    int limit = 2 * (W > H ? W : H) + 4;
    for (int k = 1; k <= limit; k++) {
        int nxt = env.step(cur, d);
        if (nxt == -1) {
            p.end = RE_KELP;
            p.dist = k - 1;
            p.endX = cur % W; p.endY = cur / W;
            return p;
        }
        if (nxt < 0) {
            p.end = RE_OUT;
            p.dist = k;
            p.endX = cur % W; p.endY = cur / W;
            return p;
        }
        int id = -1, vl = 0;
        int o = env.occ(nxt, id, vl);
        p.dist = k;
        p.endX = nxt % W; p.endY = nxt / W;
        if (o == OCC_SELF) { p.end = RE_SELF; return p; }
        if (!env.inWindow(nxt)) { p.end = RE_OUT; return p; }
        if (o == OCC_ALLY) { p.end = RE_ALLY; p.allyId = id; p.allyVisLen = vl; return p; }
        if (o == OCC_ENEMY) { p.end = RE_ENEMY; return p; }
        cur = nxt;
    }
    p.end = RE_OUT;
    return p;
}

// ------------------------------------------------------------------------------------------------ message choice
struct RayOut {
    int dir = -1;
    int type = M_NONE;
    uint64_t wire = 0;
};

// What this process wants to say about itself this turn (SELF), filled by the role layer.
struct SelfState {
    int task = T_NONE, tgt = 0xFF, load = 0, thr = 0;
};

struct PlanInput {
    int r = 0;
    int myId = 0, myLen = 2, hx = 0, hy = 0;  // after this turn's action
    bool amKing = false;
    KingMsg myKing;          // valid when amKing: this round's claim with the post-action length
    SelfState self;
};

// Assigns distinct messages to the useful rays (ALLY / OUT); SELF / KELP / ENEMY / NECK rays stay silent. Returns the
// number of rays written to out[] (at most 4).
inline int planRays(Blackboard& bb, RayPred const pred[4], PlanInput const& in, RayOut out[4]) {
    int team = bb.team, r = in.r;
    bool useful[4], used[4] = {false, false, false, false};
    int n = 0;
    for (int d = 0; d < 4; d++) useful[d] = pred[d].end == RE_ALLY || pred[d].end == RE_OUT;
    auto emit = [&](int d, int type, uint64_t payload) {
        used[d] = true;
        out[n].dir = d;
        out[n].type = type;
        out[n].wire = seal(team, r, type, payload);
        n++;
    };

    if (in.amKing) {
        uint64_t pl = encKing(in.myKing);
        for (int d = 0; d < 4; d++) if (useful[d]) emit(d, M_KING, pl);
        return n;
    }

    KingFact const* fwd = bb.currentKing(bb.kingFwdTtl, bb.myId);
    KingFact const* tomb = nullptr;
    if (!fwd) {
        tomb = bb.freshestKing(kTombFwdTtl, true);
        if (tomb && tomb->m.kid == bb.myId) tomb = nullptr;
    }
    KingFact const* childKing = bb.currentKing(kChildKingTtl, bb.myId);

    // 1. New children first: an ally we hold no SELF from, or one showing <= 3 segments.
    for (int d = 0; d < 4; d++) {
        if (!useful[d] || pred[d].end != RE_ALLY) continue;
        bool isNew = pred[d].allyVisLen <= 3 || !bb.selfOf(pred[d].allyId, kSelfTtl);
        if (!isNew) continue;
        if (childKing) emit(d, M_KING, encKing(childKing->m));
        else if (KingFact const* t = bb.freshestKing(kTombFwdTtl, true)) emit(d, M_KING, encKing(t->m));
        else if (bb.mapValid) emit(d, M_MAP, encMap(bb.map));
    }

    auto pickRay = [&](bool preferOut, bool preferAlly, int awayX, int awayY, bool useAway) {
        int best = -1, bestScore = -1000;
        for (int d = 0; d < 4; d++) {
            if (!useful[d] || used[d]) continue;
            int s = 0;
            if (preferOut && pred[d].end == RE_OUT) s += 10;
            if (preferAlly && pred[d].end == RE_ALLY) s += 10;
            if (useAway) {
                static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
                int ax = bb.g.dx(awayX, in.hx), ay = bb.g.dy(awayY, in.hy);  // king -> me
                int dot = ax * DX[d] + ay * DY[d];
                s += dot > 0 ? 3 : (dot == 0 ? 1 : 0);
            }
            s -= d;  // deterministic tie-break
            if (s > bestScore) { bestScore = s; best = d; }
        }
        return best;
    };

    // 2. The king fact (claim <= 10 rounds, else a tombstone <= 20), preferring OUT rays pointing away from the king;
    //    with kingRays = 2 a fresh claim takes a second ray.
    if (fwd || tomb) {
        KingFact const* k = fwd ? fwd : tomb;
        int sent = 0;
        for (int i = 0; i < n; i++) if (out[i].type == M_KING) sent++;
        int nUseful = 0;
        for (int d = 0; d < 4; d++) nUseful += useful[d];
        int want = (fwd && bb.kingRays > 1 && r - fwd->m.round <= bb.kingRaysFresh && nUseful >= 3) ? bb.kingRays : 1;
        if (want > nUseful - 1 && nUseful >= 3) want = nUseful - 1;  // SELF keeps one ray
        while (sent < want) {
            int d = pickRay(true, false, k->m.x, k->m.y, true);
            if (d < 0) break;
            emit(d, M_KING, encKing(k->m));
            sent++;
        }
    }
    // 3. Enemy king (<= 10 rounds) or threat (<= 3 rounds).
    {
        EKingFact const* e = nullptr;
        for (auto& x : bb.ekings)
            if (x.valid && r - x.m.round <= kEKingFwdTtl && (!e || x.m.round > e->m.round)) e = &x;
        ThreatFact const* t = nullptr;
        if (!e)
            for (auto& x : bb.threats)
                if (x.valid && r - x.m.round <= kThreatFwdTtl && (!t || x.m.round > t->m.round)) t = &x;
        if (e || t) {
            int d = pickRay(true, false, 0, 0, false);
            if (d >= 0) {
                if (e) {
                    EKingMsg m = e->m;
                    if (m.conf != 2 && m.round != r) m.conf = 2;  // relayed
                    emit(d, M_EKING, encEKing(m));
                } else {
                    emit(d, M_THREAT, encThreat(t->m));
                }
            }
        }
    }
    // 4. SELF, preferring an ALLY ray.
    {
        int d = pickRay(false, true, 0, 0, false);
        if (d >= 0) {
            SelfMsg m;
            m.rnd3 = r & 7; m.id = in.myId; m.len = in.myLen; m.x = in.hx; m.y = in.hy;
            m.task = in.self.task; m.tgt = in.self.tgt; m.load = in.self.load; m.thr = in.self.thr;
            emit(d, M_SELF, encSelf(m));
        }
    }
    // 5. Static rotation PORTAL -> BED3 -> MAP -> EXPL on the remaining rays (PORTAL first while r < 150).
    for (int d = 0; d < 4; d++) {
        if (!useful[d] || used[d]) continue;
        for (int tries = 0; tries < 4; tries++) {
            int kind = (r < 150 && tries == 0) ? 0 : (bb.staticRot++ & 3);
            if (kind == 0) {
                int np = 0;
                for (auto& p : bb.portals) if (p.valid) np++;
                if (!np) continue;
                int k = bb.portalRot++ % np, j = 0;
                for (auto& p : bb.portals) {
                    if (!p.valid) continue;
                    if (j++ == k) { emit(d, M_PORTAL, encPortal(p.m)); break; }
                }
                break;
            }
            if (kind == 1) {
                // the three clusters nearest to the ray's stop cell
                int idx[3] = {-1, -1, -1}, dist[3] = {1 << 20, 1 << 20, 1 << 20};
                for (int i = 0; i < Blackboard::kBeds; i++) {
                    auto& b = bb.beds[i];
                    if (!b.valid) continue;
                    int dd = bb.g.man(b.x, b.y, pred[d].endX, pred[d].endY);
                    for (int s = 0; s < 3; s++)
                        if (dd < dist[s]) {
                            for (int t = 2; t > s; t--) { dist[t] = dist[t - 1]; idx[t] = idx[t - 1]; }
                            dist[s] = dd; idx[s] = i;
                            break;
                        }
                }
                if (idx[0] < 0) continue;
                Bed3Msg m;
                for (int s = 0; s < 3; s++)
                    if (idx[s] >= 0) {
                        auto& b = bb.beds[idx[s]];
                        m.e[s].x = b.x; m.e[s].y = b.y; m.e[s].fount = b.fount; m.e[s].rateQ = b.rateQ;
                    }
                emit(d, M_BED3, encBed3(m));
                break;
            }
            if (kind == 2) {
                if (!bb.mapValid) continue;
                emit(d, M_MAP, encMap(bb.map));
                break;
            }
            if (kind == 3) {
                if (!bb.explored) continue;
                emit(d, M_EXPL, bb.explored);
                break;
            }
        }
    }
    return n;
}

}  // namespace bc::king
