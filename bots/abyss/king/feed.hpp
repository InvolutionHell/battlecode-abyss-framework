#pragma once
// King library, part 5: feeding (the FEED and FACTORY roles, the CAPPED cull).
//
// Pure decision functions. The death itself is "no action": the dragon dies in place and ceil(L/2) pearls land on its
// body segments 0, 2, 4, ... (engine rule), so the head pearl lies on the dead head's cell. Handshake by id order (a
// higher id sees the lower ids' moves of the same round):
//   - feeder id > king id: the king has already moved this round; the feeder needs its head <= 2 steps from the king
//     head and in the king's front half-plane ((feeder - king) . facing >= 0); the king eats next round.
//   - feeder id < king id: the king moves after the feeder in the same round; the feeder's head must be 1-2 steps
//     straight ahead of the king head, so the king's next step lands on (or next to) the head pearl.
// Throttling is on the executing side: the king's busy bit, the 5-8 step waiting ring and the in-vision order (f).

#include "crown.hpp"
#include "hunger.hpp"
#include "proto.hpp"

namespace bc::king {

struct FeedParams {
    int lastRound = 497, lastRoundIsolated = 498;
    int kingMinLen = 6;        // (b)
    int backlogMax = 1;        // (c)
    int enemyNearKing = 4;     // (d)
    int ringLo = 5, ringHi = 8;
    int ladderStep = 15;       // CONVERGE ladder: 3, +15 -> 5, +30 -> 8, +45 -> 12, +60 -> all but HEIR
    int unlimitedFrom = 470;
    int etaSlack = 10;         // ETA <= R_left - 10
    int finish = 490;
    bool armD5 = true;         // D5: fixed-point feeding in front of the king
    bool armD5p = false;       // D5': regional recycling inside D_ring
    int d5pBigShare = 3;       // D5': no other own dragon >= K/3 within d5pNear of the king head
    int d5pNear = 8;
    int d5pMaxDist = 999;      // D5': die only with the king in view and at most this many steps away (in tests the
                               // king ate 54-77% of drops from executions within 3, 8-20% at 4-8)
    int factoryMinL = 6, factoryMaxL = 12, factoryDist = 12, factoryTipClear = 3;
    int cappedU = 62;
    bool loStandLoose = true;  // a lower-id feeder may stand anywhere strictly ahead within 2 (straight ahead 1-2
                               // only saw no lower-id execution in 18 bench scenarios)
};

// What a non-king process knows about the king it follows.
struct KingView {
    bool known = false;        // a live claim within the nav TTL (40 rounds)
    int kid = -1, klen = 0, h = 0, busy = 0, mode = MODE_OPEN, round = -1, sigma = SIG_UNKNOWN;
    int cx = 0, cy = 0;        // claimed head
    bool visible = false;      // the king's head is in my vision this turn
    int kx = 0, ky = 0, kdir = 0;  // seen head and facing (0 N, 1 E, 2 S, 3 W)
};

inline KingView kingViewOf(Blackboard const& bb, int myId) {
    KingView v;
    KingFact const* k = bb.currentKing(kKingNavTtl, myId);
    if (!k) return v;
    v.known = true;
    v.kid = k->m.kid; v.klen = k->m.klen; v.h = k->m.h; v.busy = k->m.busy; v.mode = k->m.mode;
    v.round = k->m.round; v.sigma = k->m.sigma; v.cx = k->m.x; v.cy = k->m.y;
    return v;
}

// Another feeder in my vision: its distance to the king head, its piQ (lower = higher pi(NONE)), its id, and
// whether it announced readiness (SELF task FEED with ready1) or I otherwise judge it ready.
struct FeederSeen {
    int dist = 99, piQ = 15, id = 0;
    bool ready = false;
};

struct FeedCtx {
    int r = 0, myId = 0, L = 2, U = 1, uFloor = 6;
    int hx = 0, hy = 0;
    int role = CR_NONE;        // CrownRole
    bool isolated = false;
    int dK = 999;              // BFS steps from my head to the king head (seen, else claimed); 999 unknown
    int convSince = -1;        // claim round of the first CONVERGE claim I heard (ladder origin), -1 none
    bool holderContested = true;  // I hold a water source: only feed when someone else claimed it too
    // vision around the king (valid when king visible)
    int backlog = 0;           // pearls within 3 cells of the king head
    bool enemyNearKing = false;// an enemy head within 4 cells of the king head
    bool enemyNearMe = false;  // an enemy head within 4 cells of my head
    int bigOwnNearKing = 0;    // other own dragons >= K/3 within 8 steps of the king head (D5')
    int myPiQ = 15;
    FeederSeen const* others = nullptr;
    int nOthers = 0;
};

// Lmax: GROW uses Lmax(h); CONVERGE uses max(Lmax(h), ladder) with the ladder counted from convSince; r >= 470
// (and the ladder's last rung) unlimited except HEIR.
inline int lmaxNow(FeedParams const& P, int h, int mode, int r, int convSince) {
    int l = lmaxOf(h);
    if (mode >= MODE_CONVERGE) {
        if (r >= P.unlimitedFrom) return 1 << 20;
        int since = convSince >= 0 ? r - convSince : 0;
        int rung = since / P.ladderStep;
        static const int ladder[4] = {3, 5, 8, 12};
        int lad = rung >= 4 ? (1 << 20) : ladder[rung];
        if (lad > l) l = lad;
    }
    return l;
}

// FEED role (eligibility to head for the king; departure is not rationed).
inline bool feedEligible(FeedParams const& P, FeedCtx const& c, KingView const& k) {
    if (!k.known || k.klen < P.kingMinLen) return false;
    if (k.mode < MODE_GROW || k.mode >= MODE_FINISH) return false;
    if (k.h < 1 && k.mode < MODE_CONVERGE) return false;
    if (c.role == CR_HEIR || c.role == CR_BACKUP || c.role == CR_KING) return false;
    int h = k.h < 1 ? 1 : k.h;
    int eff = k.mode >= MODE_CONVERGE && h < 2 ? 2 : h;
    if (c.L > lmaxNow(P, eff, k.mode, c.r, c.convSince)) return false;
    int ring = k.mode >= MODE_CONVERGE ? kRingAny : dRingOf(eff);
    if (c.dK > ring) return false;
    if (c.dK > (P.finish - c.r) - P.etaSlack) return false;
    if (c.U <= c.uFloor) return false;
    if (!c.holderContested) return false;
    return true;
}

// The king's busy bit: >= busyPearls pearls within 3 cells of its head, or >= n_feed(h) own heads within 3 cells.
// busyPearls 3 (calibrated on replays: the king gains 0.10 / 0.23 / 0.40 / 0.55 / 0.68 a round at a backlog of 0 / 1
// / 2 / 3-4 / 5+; a queue of 2-4 takes 0.40-0.55; a threshold of 2 stopped the queue at 1-2).
inline int busyBit(int pearlsNearHead, int ownHeadsNear, int h, int busyPearls = 3) {
    return (pearlsNearHead >= busyPearls || (h > 0 && ownHeadsNear >= nFeedOf(h))) ? 1 : 0;
}

inline bool feedOrderFirst(FeedCtx const& c) {
    for (int i = 0; i < c.nOthers; i++) {
        FeederSeen const& o = c.others[i];
        if (!o.ready) continue;
        if (o.dist < c.dK) return false;
        if (o.dist == c.dK && (o.piQ < c.myPiQ || (o.piQ == c.myPiQ && o.id < c.myId))) return false;
    }
    return true;
}

struct FeedDecision {
    bool die = false;          // output no action this turn
    bool ready = false;        // (a)-(e) hold, waiting for (f) or the stand: announce ready1
    bool retreat = false;      // stay in the 5-8 ring (busy, backlog, or not first)
    int why = 0;               // first failing condition: 1 a, 2 b-king, 3 b-stand, 4 c, 5 d, 6 e, 7 f, 8 busy
};

// D5 arm. geo: torus geometry; dist: Manhattan on the torus between my head and the seen king head.
inline FeedDecision feedDeathD5(FeedParams const& P, FeedCtx const& c, KingView const& k, Geo const& geo) {
    FeedDecision d;
    int last = c.isolated ? P.lastRoundIsolated : P.lastRound;
    // (a)
    bool modeOk = (k.mode >= MODE_GROW && k.h >= 1) || k.mode >= MODE_CONVERGE;
    if (!k.known || !modeOk || c.r > last) { d.why = 1; return d; }
    // (e)
    if (c.U <= c.uFloor) { d.why = 6; return d; }
    int h = k.h < 1 ? 1 : k.h;
    if (k.mode >= MODE_CONVERGE && h < 2) h = 2;
    if (c.L > lmaxNow(P, h, k.mode, c.r, c.convSince)) { d.why = 6; return d; }
    if (c.role == CR_HEIR || c.role == CR_BACKUP || c.role == CR_KING) { d.why = 6; return d; }
    // (b) the king's head in my vision, a real claim, klen >= 6
    if (!k.visible || k.klen < P.kingMinLen) { d.why = 2; d.retreat = k.busy; return d; }
    static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
    int rx = geo.dx(k.kx, c.hx), ry = geo.dy(k.ky, c.hy);  // king -> me
    int man = (rx < 0 ? -rx : rx) + (ry < 0 ? -ry : ry);
    bool stand;
    if (c.myId > k.kid) {
        stand = man >= 1 && man <= 2 && rx * DX[k.kdir] + ry * DY[k.kdir] >= 0;
    } else if (P.loStandLoose) {
        stand = man >= 1 && man <= 2 && rx * DX[k.kdir] + ry * DY[k.kdir] >= 1;
    } else {
        stand = false;
        for (int s = 1; s <= 2; s++)
            if (rx == DX[k.kdir] * s && ry == DY[k.kdir] * s) stand = true;
    }
    // (c), (d)
    bool cOk = c.backlog <= P.backlogMax;
    bool dOk = !c.enemyNearKing;
    if (!dOk) { d.why = 5; d.retreat = true; return d; }
    d.ready = true;
    if (k.busy) { d.why = 8; d.retreat = true; return d; }
    if (!cOk) { d.why = 4; d.retreat = true; return d; }
    if (!stand) { d.why = 3; return d; }
    // (f)
    if (!feedOrderFirst(c)) { d.why = 7; d.retreat = true; return d; }
    d.die = true;
    return d;
}

// D5' arm: die anywhere inside D_ring when no other own big dragon (>= K/3) is within 8 steps of the king head; the
// in-vision order (f) still throttles, and no enemy head may be within 4 of me (my pearls would feed them).
inline FeedDecision feedDeathD5p(FeedParams const& P, FeedCtx const& c, KingView const& k) {
    FeedDecision d;
    int last = c.isolated ? P.lastRoundIsolated : P.lastRound;
    bool modeOk = (k.mode >= MODE_GROW && k.h >= 1) || k.mode >= MODE_CONVERGE;
    if (!k.known || !modeOk || c.r > last) { d.why = 1; return d; }
    if (!feedEligible(P, c, k)) { d.why = 6; return d; }
    if (c.enemyNearMe) { d.why = 5; return d; }
    if (c.bigOwnNearKing > 0) { d.why = 2; return d; }
    if (P.d5pMaxDist < 999 && (!k.visible || c.dK > P.d5pMaxDist)) { d.why = 3; return d; }
    d.ready = true;
    if (!feedOrderFirst(c)) { d.why = 7; return d; }
    d.die = true;
    return d;
}

// Feeding factory. Returns the child length to split off (3 or 2), or 0.
struct FactoryCtx {
    int r = 0, myId = 0, L = 2;
    int role = CR_NONE;
    int dK = 999;
    bool splitAllowed = false;     // the unit quota rule (live UNIT_COUNT)
    // where the child's head (my tail tip) would be, relative to the king head, and the king's facing
    int tipRx = 0, tipRy = 0, kdir = 0;
};

inline int factorySplit(FeedParams const& P, FactoryCtx const& c, KingView const& k) {
    if (!k.known || k.klen < P.kingMinLen || k.h < 1 || k.busy) return 0;
    if (c.role == CR_KING || c.role == CR_HEIR || c.role == CR_BACKUP) return 0;
    if (c.L < P.factoryMinL || c.L > P.factoryMaxL || c.dK > P.factoryDist || !c.splitAllowed) return 0;
    if (splitmix64((static_cast<uint64_t>(c.myId) << 20) ^ static_cast<uint64_t>(c.r)) % 3 != 0) return 0;
    static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
    int man = (c.tipRx < 0 ? -c.tipRx : c.tipRx) + (c.tipRy < 0 ? -c.tipRy : c.tipRy);
    if (man < P.factoryTipClear) return 0;
    int fwd = c.tipRx * DX[c.kdir] + c.tipRy * DY[c.kdir];
    int lat = c.tipRx * DX[(c.kdir + 1) & 3] + c.tipRy * DY[(c.kdir + 1) & 3];
    bool corridor = fwd >= 1 && fwd <= 4 && lat >= -1 && lat <= 1;
    bool ring = man >= P.ringLo && man <= P.ringHi;
    if (!corridor && !ring) return 0;
    return c.L >= 6 ? 3 : 2;
}

// CAPPED: cull to free a unit slot at the pearl front (turnover), all of (i)-(vi).
struct CappedCtx {
    int r = 0, myId = 0, L = 2, U = 0, load = 0;
    int role = CR_NONE;
    bool bigNearBed = false;       // (iii) a non-king ally with L >= 5 within 4 steps, its head within 3 of a known bed
    bool smallestSmall = false;    // (iv) my (L, load, -id) is the minimum among visible allies with L <= 3
    bool enemyNear4 = false;       // (v)
    bool insideRing = false;       // inside D_ring of a feeding king: feed instead
};

inline bool cappedCull(FeedParams const& P, CappedCtx const& c) {
    if (c.U < P.cappedU) return false;
    if (c.L > 3 || c.role == CR_KING || c.role == CR_HEIR || c.role == CR_BACKUP) return false;
    if (!c.bigNearBed || !c.smallestSmall || c.enemyNear4 || c.insideRing) return false;
    return splitmix64((static_cast<uint64_t>(c.myId) << 20) ^ static_cast<uint64_t>(c.r) ^ 0xCAFEull) % 2 == 0;
}

// SELF.tgt for a FEED task: piQ4 | ready1 | 000.
inline int feedTgt(int piQ, bool ready) { return ((piQ & 15) << 4) | (ready ? 8 : 0); }
inline int piQOf(double piNone) {
    if (piNone <= 0) return 15;
    int q = static_cast<int>(-std::log2(piNone) + 0.5);
    return q < 0 ? 0 : (q > 15 ? 15 : q);
}

}  // namespace bc::king
