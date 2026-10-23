#pragma once
// King library, part 4: the hunger law, computed by the king's process every round and broadcast as KING.h. Default
// shape: track the back-loaded reference trajectory s(r) of top-ranked teams; the flat-rate shape (g_req = (K_goal -
// K) / (R_end - r) from GROW on, no r < 300 cap) is the alternative.
//
// Also the king-local statistics the law needs (KingStats): the length history for g_obs, the unit history (U_max,
// U_300, U_late), the team load trend, and the shrinkage estimate eta-hat of the feeding conversion.

#include "proto.hpp"
#include "sigma.hpp"

namespace bc::king {

struct HungerParams {
    int rEnd = 490;
    double gCap = 0.35;
    double convCap = 0.67;          // ceil(3/2)/3
    double regionShare = 0.2;
    int regionMin = 150;
    int m = 4, mBehind = 8;
    double d1 = 0.08, d2 = 0.20;    // delta thresholds for h = 1 / 2
    int hystRounds = 10;
    double loadDrop = 0.30;         // protection 1
    int uHoldMargin = 4;
    double c25On = 0.25, c25Off = 0.15;
    int contactUnits = 16;          // the contact guard only with U <= 16 (0.25 alone flags half of all games; wipes
                                    // before r300 happen with few units); the caller passes c25 = 0 when ISOLATED
                                    // (SEP views see enemy heads across the wall)
    int uFloor = 6, uFloorIsolated = 2, uFloorBehindLate = 3, uFloorContact = 12;
    int behindLate = 470;
    double stockConv = 0.20;        // K_feas: the king's growth per unit of non-king stock that top-ranked teams
                                    // realise (0.17 from r400, 0.24 from r300; calibrated on replays) - not eta-hat x
                                    // M_feed: eta-hat counts only the deaths in the king's view (a quarter of the
                                    // destroyed mass)
    bool flatShape = false;         // alternative: flat-rate target, no r < 300 cap
    bool behindFloor = true;        // sigma = BEHIND in GROW -> h >= 1 (a floor on top of the rules)
    bool zeroWhenAhead = true;      // h = 0 from delta while K >= K_ref(r + 20) (g_req = 0): a negative g_obs (noise,
                                    // a dropped tail) no longer triggers feeding a king that is ahead of its reference
};

// rho(r): the army at r relative to r300 below which the sustainability guard holds h <= 1. Calibrated on full-length
// replays of top-ranked teams: per-game median 0.95 / 0.71 / 0.34 / 0.20 at r350 / 400 / 450 / 490, q25 0.74 / 0.40 /
// 0.19 / 0.11; a first estimate of 0.84 / 0.56 / 0.43 / 0.22 was above both at r450.
inline double unitShape(double r) {
    static const double R[5] = {300, 350, 400, 450, 490};
    static const double S[5] = {1.0, 0.85, 0.55, 0.30, 0.15};
    if (r <= R[0]) return S[0];
    for (int i = 1; i < 5; i++)
        if (r <= R[i]) return S[i - 1] + (S[i] - S[i - 1]) * (r - R[i - 1]) / (R[i] - R[i - 1]);
    return S[4];
}

// ------------------------------------------------------------------------------------------------ king statistics
struct KingStats {
    static constexpr int kRing = 128;
    int lenRing[kRing];      // my length at round t (index t & 127)
    int lenRound[kRing];
    int uMax = 0, u300 = -1, uLate = 0;
    // eta-hat events per round (80-round window)
    int corpseEaten[kRing];  // pearls from own corpses the king ate in round t
    int ownDeadMass[kRing];  // length of own dragons that died near the king (D5: in its vision; D5': inside D_ring)
    int evRound[kRing];
    double loadAvg[kRing];   // team mean load code in round t (SELF heard in the last 16 rounds)
    int loadRound[kRing];

    void init() {
        for (int i = 0; i < kRing; i++) {
            lenRing[i] = 0; lenRound[i] = -1000; corpseEaten[i] = 0; ownDeadMass[i] = 0; evRound[i] = -1000;
            loadAvg[i] = 0; loadRound[i] = -1000;
        }
        uMax = 0; u300 = -1; uLate = 0;
    }
    void noteLen(int r, int L) { lenRing[r & 127] = L; lenRound[r & 127] = r; }
    int lenAt(int r, int fallback) const { return lenRound[r & 127] == r ? lenRing[r & 127] : fallback; }
    void noteUnits(int r, int U) {
        if (U > uMax) uMax = U;
        if (r >= 300 && u300 < 0) u300 = U;
        if (r >= 300) { if (U > uLate) uLate = U; }
    }
    // U_late for E_cap: the maximum over r >= 300; before r300 the maximum so far.
    int uLateOr(int r) const { return r >= 300 && uLate > 0 ? uLate : uMax; }
    void evSlot(int r) {
        int s = r & 127;
        if (evRound[s] != r) { evRound[s] = r; corpseEaten[s] = 0; ownDeadMass[s] = 0; }
    }
    void noteCorpsePearl(int r, int n = 1) { evSlot(r); corpseEaten[r & 127] += n; }
    void noteOwnDeath(int r, int L) { evSlot(r); ownDeadMass[r & 127] += L; }
    // eta-hat = (prior * pseudo + corpse pearls eaten) / (pseudo + own mass died in the king's view), last 80 rounds.
    // Prior 0.30 / pseudo-mass 25 (calibrated on replays: top-ranked teams read 0.34 on this formula, 0.27 early ->
    // 0.43 late; a first estimate of 0.12 was the gain over all destroyed mass, a different quantity).
    double etaHat(int r, double prior = 0.30, double pseudo = 25) const {
        double eaten = 0, mass = 0;
        for (int t = r - 79; t <= r; t++) {
            int s = t & 127;
            if (t < 0 || evRound[s] != t) continue;
            eaten += corpseEaten[s];
            mass += ownDeadMass[s];
        }
        return (prior * pseudo + eaten) / (pseudo + mass);
    }
    void noteLoad(int r, double avg) { loadAvg[r & 127] = avg; loadRound[r & 127] = r; }
    // Relative change of the team's mean load over the last 25 rounds (negative = falling); 0 when unknown.
    double loadTrend(int r) const {
        int a = r & 127, b = (r - 25) & 127;
        if (loadRound[a] != r || loadRound[b] != r - 25 || loadAvg[b] <= 0) return 0;
        return (loadAvg[a] - loadAvg[b]) / loadAvg[b];
    }
};

// ------------------------------------------------------------------------------------------------ hunger law
struct HungerInput {
    int r = 0;
    int mode = MODE_OPEN;
    int K = 0;                 // king length now
    int K20 = 0;               // king length 20 rounds ago (process history)
    int kCrown = 0;            // length when crowned
    int U = 0;
    double mFeed = 0;          // (U - 1 - n_heir) * L-bar
    double etaHat = 0.12;
    int regionCells = 0;       // known walkable cells in the king's connected region
    double e500plus = 0;       // sigma
    int sigma = SIG_UNKNOWN;
    bool isolated = false;
    double capacity = 0;       // C-hat
    int u300 = -1;             // unit count at r300 (-1 before)
    double loadTrend = 0;      // KingStats::loadTrend
    double c25 = 0;
    int uEnemyLo = 0;
};

struct HungerOut {
    int h = 0;
    int hRaw = 0;              // from delta alone
    double kFeas = 0, kRegion = 0, kGoal = 0, kRef = 0, gReq = 0, gObs = 0, delta = 0;
    int uHold = 0, uFloor = 0;
    int nFeed = 0, lmax = 0, dRing = 0;
    // which rules bit (trace)
    bool capped300 = false, floor300 = false, hyst = false, protSustain = false, protContact = false,
         protBehindUnits = false, convergeFloor = false, stopFloor = false, behindFloor = false;
};

inline int nFeedOf(int h) { static const int t[4] = {0, 1, 2, 4}; return t[h & 3]; }
inline int lmaxOf(int h) { static const int t[4] = {0, 3, 5, 8}; return t[h & 3]; }
constexpr int kRingAny = 1000;  // D_ring for h = 3: any distance that still arrives in time (ETA <= R_left - 10)
inline int dRingOf(int h) { static const int t[4] = {0, 6, 12, kRingAny}; return t[h & 3]; }

struct Hunger {
    HungerParams P;
    int h = 0;
    int lastChange = -1000;
    bool contactHigh = false;

    void init() { P = HungerParams(); h = 0; lastChange = -1000; contactHigh = false; }

    double kRefAt(double r, double kCrown, double kGoal) const {
        double v = refShape(r) * kGoal;
        return v > kCrown ? v : kCrown;
    }

    HungerOut update(HungerInput const& in) {
        HungerOut o;
        int r = in.r;
        bool behind = in.sigma == SIG_BEHIND;
        // contact hysteresis (protection 2)
        if (in.c25 >= P.c25On && in.U <= P.contactUnits) contactHigh = true;
        else if (in.c25 < P.c25Off || in.U > P.contactUnits) contactHigh = false;
        // floors
        int uFloor = P.uFloor;
        if (in.isolated) uFloor = P.uFloorIsolated;
        if (behind && r >= P.behindLate) uFloor = P.uFloorBehindLate;
        if (contactHigh && in.mode < MODE_CONVERGE) uFloor = P.uFloorContact;
        o.uFloor = uFloor;
        if (r < 300) {
            double v = 0.7 * in.capacity;
            if (v < 8) v = 8;
            if (v > 24) v = 24;
            o.uHold = static_cast<int>(v);
        } else {
            int base = in.u300 > 0 ? in.u300 : in.U;
            int v = static_cast<int>(unitShape(r) * base);
            o.uHold = v > uFloor ? v : uFloor;
        }
        if (behind) o.uHold -= 4;

        // targets
        o.kRegion = P.regionShare * (in.regionCells > P.regionMin ? in.regionCells : P.regionMin);
        double left = P.rEnd - r;
        if (left < 0) left = 0;
        double a = P.gCap * left, b = P.stockConv * in.mFeed, c = P.convCap * in.mFeed;
        double add = a < b ? a : b;
        if (c < add) add = c;
        o.kFeas = in.K + add;
        double m = behind ? P.mBehind : P.m;
        o.kGoal = o.kFeas < o.kRegion ? o.kFeas : o.kRegion;
        if (!in.isolated && in.e500plus + m < o.kGoal) o.kGoal = in.e500plus + m;
        double r20 = r + 20 < P.rEnd ? r + 20 : P.rEnd;
        if (P.flatShape) {
            double span = P.rEnd - r;
            o.gReq = span > 0 ? (o.kGoal - in.K) / span : 0;
            if (o.gReq < 0) o.gReq = 0;
            o.kRef = in.K + o.gReq * 20;
        } else {
            o.kRef = kRefAt(r20, in.kCrown, o.kGoal);
            o.gReq = (o.kRef - in.K) / 20.0;
            if (o.gReq < 0) o.gReq = 0;
        }
        o.gObs = (in.K - in.K20) / 20.0;
        o.delta = o.gReq - o.gObs;
        int hr = o.delta <= 0 ? 0 : (o.delta <= P.d1 ? 1 : (o.delta <= P.d2 ? 2 : 3));
        if (P.zeroWhenAhead && o.gReq <= 0) hr = 0;
        o.hRaw = hr;
        int hn = hr;

        if (in.mode == MODE_OPEN) {
            o.h = 0;
            o.nFeed = 0; o.lmax = 0; o.dRing = 0;
            if (h != 0) { h = 0; lastChange = r; }
            return o;
        }
        // lowest priority: the r < 300 cap / r >= 300 floor (and sigma = BEHIND in GROW -> h >= 1)
        if (!P.flatShape && r < 300 && hn > 1) { hn = 1; o.capped300 = true; }
        if (r >= 300 && in.mode == MODE_GROW && hn < 1) { hn = 1; o.floor300 = true; }
        if (P.behindFloor && behind && in.mode == MODE_GROW && hn < 1) { hn = 1; o.behindFloor = true; }
        // hysteresis: at most one change per 10 rounds unless the jump is >= 2
        if (hn != h) {
            int jump = hn > h ? hn - h : h - hn;
            if (jump < 2 && r - lastChange < P.hystRounds) { hn = h; o.hyst = true; }
        }
        bool conv = in.mode >= MODE_CONVERGE;
        // protections 2 and 3 (before CONVERGE)
        if (!conv && contactHigh && hn > 1) { hn = 1; o.protContact = true; }
        if (!conv && in.uEnemyLo > in.U && hn > 1) { hn = 1; o.protBehindUnits = true; }
        // protection 1 (sustainability), overridden by CONVERGE
        if (!conv && (in.loadTrend < -P.loadDrop || in.U <= o.uHold + P.uHoldMargin) && hn > 1) {
            hn = 1;
            o.protSustain = true;
        }
        if (conv && hn < 2) { hn = 2; o.convergeFloor = true; }
        // the floor beats everything: stop feeding
        if (in.U <= uFloor) { hn = 0; o.stopFloor = true; }

        if (hn != h) { h = hn; lastChange = r; }
        o.h = h;
        o.nFeed = nFeedOf(h);
        o.lmax = lmaxOf(h);
        o.dRing = dRingOf(h);
        return o;
    }
};

}  // namespace bc::king
