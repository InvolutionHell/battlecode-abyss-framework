#pragma once
// King library, part 3: the lead / lag estimate sigma (E_cap: the unit count is the maximum over r >= 300; an F-type
// economy without gated fountains uses C-hat, everything else 0.61 * U_late + 8.6).
//
// Pure functions plus a small state for the 20-round switching hysteresis. The reference shape s(r) is shared with
// the hunger law (hunger.hpp).

#include "proto.hpp"

namespace bc::king {

// s(r): king length / own final value in full-length replays of top-ranked teams: 0.15 up to r200, 0.25 r300, 0.35
// r350, 0.55 r400, 0.78 r450, 1.0 from r490; linear in between.
inline double refShape(double r) {
    static const double R[6] = {200, 300, 350, 400, 450, 490};
    static const double S[6] = {0.15, 0.25, 0.35, 0.55, 0.78, 1.0};
    if (r <= R[0]) return S[0];
    for (int i = 1; i < 6; i++)
        if (r <= R[i]) return S[i - 1] + (S[i] - S[i - 1]) * (r - R[i - 1]) / (R[i] - R[i - 1]);
    return 1.0;
}

// kappa50(r): median true / visible lower bound of the enemy king: 1.9 up to r400, 2.6 r450, 2.7 from r495.
inline double kappa50(double r) {
    if (r <= 400) return 1.9;
    if (r <= 450) return 1.9 + (2.6 - 1.9) * (r - 400) / 50.0;
    if (r <= 495) return 2.6 + (2.7 - 2.6) * (r - 450) / 45.0;
    return 2.7;
}

struct SigmaParams {
    double capSlope = 0.61, capBase = 8.6;
    double q75Ratio = 1.34;
    double gE = 0.17;
    double behindMargin = 2, aheadMargin = 6;
    int aheadOverElo = 4;
    int switchRounds = 20;
    double gPlanReqShare = 0.6;
    double stockConv = 0.20;     // K-hat500 mass term: stock conversion (hunger.hpp), not eta-hat x M_feed
};

struct SigmaInput {
    int r = 0;
    int K = 0;                 // king length
    int eLo = 0;               // E_lo
    bool isolated = false;
    bool ecoF = false, gated = false;
    double capacity = 0;       // C-hat
    int uLate = 0;             // max unit count over r >= 300 (before r300: the maximum so far)
    bool seenEnemyKing = false;
    double gObs = 0, gReq = 0; // from the hunger law
    double etaHat = 0.12;      // feeding conversion estimate
    double mFeed = 0;          // feedable mass
};

struct SigmaOut {
    double eCap = 0, q50 = 0, q75_500 = 0, eMid = 0, e500 = 0, e500plus = 0, k500 = 0;
    int sigma = SIG_UNKNOWN;   // after hysteresis
    int raw = SIG_UNKNOWN;     // before hysteresis
};

inline double eCapOf(SigmaParams const& P, SigmaInput const& in) {
    if (in.ecoF && !in.gated) {
        double c = in.capacity > 64 ? 64 : in.capacity;
        return P.capSlope * c + P.capBase;
    }
    return P.capSlope * in.uLate + P.capBase;
}

struct SigmaState {
    SigmaParams P;
    int sigma = SIG_UNKNOWN;
    int lastSwitch = -1000;

    void init() { P = SigmaParams(); sigma = SIG_UNKNOWN; lastSwitch = -1000; }

    SigmaOut update(SigmaInput const& in) {
        SigmaOut o;
        double r = in.r;
        o.eCap = eCapOf(P, in);
        o.q50 = refShape(r) * o.eCap;
        double q50_500 = refShape(500) * o.eCap;
        o.q75_500 = P.q75Ratio * q50_500;
        o.eMid = in.isolated ? o.q50 : (kappa50(r) * in.eLo > o.q50 ? kappa50(r) * in.eLo : o.q50);
        double lead = o.eMid - o.q50;
        o.e500 = q50_500 + (lead > 0 ? lead : 0);
        if (r >= 400) {
            double alt = o.eMid + P.gE * (500 - r);
            if (alt > o.e500) o.e500 = alt;
        }
        o.e500plus = in.seenEnemyKing ? o.e500 : (o.q75_500 > o.e500 ? o.q75_500 : o.e500);
        double gPlan = in.gObs > P.gPlanReqShare * in.gReq ? in.gObs : P.gPlanReqShare * in.gReq;
        double byRate = gPlan * (490 - r);
        if (byRate < 0) byRate = 0;
        double byMass = P.stockConv * in.mFeed;
        o.k500 = in.K + (byRate < byMass ? byRate : byMass);
        int raw = SIG_EVEN;
        if (o.k500 < o.e500 - P.behindMargin) raw = SIG_BEHIND;
        else if (o.k500 >= o.e500 + P.aheadMargin && in.K >= in.eLo + P.aheadOverElo) raw = SIG_AHEAD;
        o.raw = raw;
        if (sigma == SIG_UNKNOWN) {
            sigma = raw;
            lastSwitch = in.r;
        } else if (raw != sigma && in.r - lastSwitch >= P.switchRounds) {
            sigma = raw;
            lastSwitch = in.r;
        }
        o.sigma = sigma;
        return o;
    }
};

}  // namespace bc::king
