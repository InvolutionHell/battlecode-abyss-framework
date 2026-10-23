#pragma once
// King library, part 2: the crown and the mode machine (inactive in the final build).
//
// Every process runs one Crown. Per turn: beginTurn() before choosing the action (reads the blackboard, detects a
// dead king and issues a tombstone, yields, claims, keeps HEIR / BACKUP), then afterAction() with the post-action
// length (records the length history for the time-aligned comparison, abdicates when a forced reverse split left a
// stub) and, when king, kingMsg() for the rays.
//
// No double yield: a king yields to a claim (tB, LB) only when (LB, -idB) beats both its own length at round tB and
// its current length. If two kings yielded to each other with non-decreasing lengths, A's claim at tA would beat B's
// current length >= B's claim at tB, which beats A's length at tB >= A's claim at tA: a contradiction. Lengths only
// fall by a split, and then the refreshed (shorter) claim releases the other side at once.

#include "proto.hpp"

namespace bc::king {

enum CrownRole : uint8_t { CR_NONE = 0, CR_KING = 1, CR_BACKUP = 2, CR_HEIR = 3 };

struct CrownParams {
    int minLen = 6;              // (c1)
    int minLenLate = 4;          // (c1) when r >= lateRound and no king was ever heard
    int lateRound = 260;
    int earliest = 150;          // (c2)
    int t1Rounds = 10;           // trigger T1 must hold this many consecutive rounds
    int t4Round = 220, t4RoundContact = 280;
    double c25High = 0.25;
    int contactUnits = 16;       // c25 only counts with U <= 16 (the caller passes c25 = 0 when ISOLATED)
    int stagger = 5;             // claims on trigger T4 alone wait hash(id) mod stagger rounds
    int ageToClaim = 60;         // (c0) (30 at first; in simulation 60 halved claims by dragons unaware of a king)
    int hysteresis = 3;          // a challenger needs >= incumbent + 3 ((c3): no live claim with klen >= L - 2)
    int yieldFresh = 40;         // claims at most this old make a king yield (3 at first; the time-aligned + now test
                                 // already rules out a double yield, and 40 cuts coexisting kings)
    int backupRounds = 20;       // BACKUP expires
    int deadSilence = 12;        // death rules (a) and (b)
    int deadFreshWindow = 20;    // rule (a): the claim was <= 3 rounds fresh within this many rounds before
    int deadNear = 6;            // rule (b): within this many steps of the last known king head
    bool ruleA = false, ruleB = false;  // silence-based tombstones: 15 false tombstones / game in simulation
    bool ruleW = true;           // witnessed death: the king's head was within 2 of mine last turn, now none of it
    int witnessDist = 2;         //   is visible (its neck would still be in my 7x7 window had it moved)
    int stubMax = 3;             // a king left with <= 3 segments (forced reverse split) abdicates as a stub
    int heirMin = 6;             // HEIR needs L >= max(6, K/3); feeding and HEIR need a claim >= 6
    int handoverWindow = 5;      // a claim right after hearing an abdication skips the stagger
    bool heardTrigger = true;    // once any KING was heard, (c2) needs only r >= earliest: the team keeps a king
    bool succession = true;      // HEIR / BACKUP succession: the claim of a king silent to me for successionSilence
    int successionSilence = 20;  //   rounds (and not in my vision) no longer blocks (c3) for its recent HEIR / BACKUP
    int successionMemory = 20;   //   (a false succession costs a few rounds of two kings: the shorter one yields)
    int seedFrom = 200;          // crown seed (see seed()); -1 off
    int seedMinLen = 4;
    int claimPatience = 20;      // (c0)-(c5) must hold this many consecutive rounds before a claim (gossip time to
                                 // bring news of an existing king); succession and handover claims skip it. In
                                 // closed-loop simulation: coexisting kings 2.25 -> 1.49, kingless 2.6% -> 5.4%
    int finishRound = 470;       // FINISH merge: from here a king yields to any live claim (<= 120 rounds) of a
    double finishEtaSlack = 5;   //   longer king it can still reach (1.3 x torus steps <= 500 - r - 5) and feeds it
    bool contactSuccession = false;  // also any dragon with L >= max(6, K/3) that had fresh contact (a claim <= 3
                                     // rounds old, or the king in view) within successionMemory rounds
};

struct CrownInput {
    int r = 0;
    int L = 2;                  // my length now (pre-action)
    int U = 1;                  // live unit count
    bool initialDragon = false; // alive at r0
    double capacity = 64;       // C-hat, the unit-capacity estimate (trigger T1); 64 when unknown
    bool isolated = false;      // ISOLATED (trigger T3; only when the offline agreement check passed)
    int eLo = 0;                // E_lo: blackboard ekings (100 rounds) and my own vision
    double c25 = 0;             // contact pressure
    int uEnemyLo = 0;           // U_E^lo
    bool executing = false;     // (c5): I execute a feeding death this turn
    int hx = 0, hy = 0;         // my head now
    // Visible allies this turn (ids and visible segment counts); a SELF length overrides a shorter visible count.
    int const* visAllyIds = nullptr;
    int const* visAllyLens = nullptr;
    int const* visAllyHeadDist = nullptr;  // Chebyshev distance of its head from mine, -1 when its head is not visible
    int nVisAlly = 0;
};

// Why a non-king did not claim this turn (trace / simulator).
enum ClaimWhy : uint8_t {
    W_KING = 0, W_C0 = 1, W_C1 = 2, W_TRIGGER = 3, W_STAGGER = 4, W_C3 = 5, W_C4 = 6, W_EXEC = 7, W_YIELDED = 8,
};

struct CrownEvents {  // what happened this turn (trace / simulator)
    bool claimed = false, yielded = false, abdicated = false, backupExpired = false, succession = false;
    bool finishMerge = false;   // this king yielded to a longer one at r >= 470 and turns feeder (not BACKUP)
    int yieldedTo = -1;
    int why = W_KING;           // ClaimWhy for a non-king
    int blockKid = -1;          // the claim that blocked (c3)
    int tombKid = -1;           // a tombstone issued this turn
    int tombRule = 0;           // 1 rule (a) silence after fresh, 2 rule (b) near the head without a sign, 3 witnessed
};

inline uint32_t crownHash(int id) { return static_cast<uint32_t>(splitmix64(0xC0FFEEull ^ static_cast<uint64_t>(id))); }

struct Crown {
    CrownParams P;
    CrownRole role = CR_NONE;
    int myId = -1, birthRound = 0;
    bool initial = false;
    int kingSince = -1, kCrown = 0;
    int backupSince = -1, yieldedTo = -1;
    int t1Run = 0, t4Since = -1;
    int nearRun = 0, nearKid = -1;
    // kings seen last turn (witness rule): id and head distance
    static constexpr int kSeen = 8;
    int seenKid[kSeen];
    int seenDist[kSeen];
    int nSeen = 0, seenRound = -1000;
    int heirOf = -1, lastHeirRound = -1000;  // succession memory
    int candSince = -1;                      // first round of the current streak meeting every claim condition
    // Claims I overrode by succession: a claim of that kid no newer than the mark neither blocks nor makes me yield.
    int supKid[4] = {-1, -1, -1, -1};
    int supRound[4] = {0, 0, 0, 0};
    int lastSeenKing[8] = {-1, -1, -1, -1, -1, -1, -1, -1};  // kid seen in view, and when (contact succession)
    int lastSeenKingAt[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int postLen[64];
    int postLenRound[64];
    int preLenNow = 2, rNow = 0;
    int lastAbdicationHeardAt = -1000;
    CrownEvents ev;
    // counters for trace / KPI
    int nClaims = 0, nYields = 0, nTombs = 0, nAbdications = 0;

    void init(int id, int birth, bool initialDragon) {
        P = CrownParams();
        role = CR_NONE;
        kingSince = backupSince = yieldedTo = t4Since = nearKid = -1;
        kCrown = t1Run = nearRun = 0;
        preLenNow = 2;
        rNow = 0;
        lastAbdicationHeardAt = -1000;
        abdicatePending = false;
        ev = CrownEvents();
        nClaims = nYields = nTombs = nAbdications = 0;
        nSeen = 0;
        seenRound = -1000;
        heirOf = -1;
        lastHeirRound = -1000;
        candSince = -1;
        for (int i = 0; i < 4; i++) { supKid[i] = -1; supRound[i] = 0; }
        for (int i = 0; i < 8; i++) { lastSeenKing[i] = -1; lastSeenKingAt[i] = 0; }
        myId = id;
        birthRound = birth;
        initial = initialDragon;
        for (int i = 0; i < 64; i++) { postLen[i] = 0; postLenRound[i] = -1000; }
    }

    bool amKing() const { return role == CR_KING; }
    // Crown seed: from seedFrom, with no live king known (no claim within the blocking TTL), the locally longest
    // dragon of at least seedMinLen keeps growing (no economic split) so that someone can reach the claim length (on
    // one portal map the team split ~60 times per 50 rounds all game and nobody stayed >= 6).
    bool seed(Blackboard const& bb, CrownInput const& in) const {
        if (P.seedFrom < 0 || role != CR_NONE || in.r < P.seedFrom || in.L < P.seedMinLen) return false;
        for (auto const& k : bb.kings)
            if (bb.kingLive(k) && k.m.kid != myId) return false;
        return localMax(bb, in);
    }
    // Serving out the claim patience: every claim condition holds (the executor should not halve it meanwhile).
    bool candidate() const { return role != CR_KING && candSince >= 0 && P.claimPatience > 0; }

    // My length at round t: the post-action length recorded then (what my claim said), else the current length.
    int lenAt(int t) const {
        if (t >= rNow) return preLenNow;
        int s = t & 63;
        if (postLenRound[s] == t) return postLen[s];
        return preLenNow;
    }

    static bool beats(int la, int ida, int lb, int idb) { return la > lb || (la == lb && ida < idb); }

    // (c4): (L, -id) is the maximum over visible allies and SELF entries at most 7 rounds old.
    bool localMax(Blackboard const& bb, CrownInput const& in) const {
        for (int i = 0; i < in.nVisAlly; i++) {
            int id = in.visAllyIds[i], len = in.visAllyLens ? in.visAllyLens[i] : 0;
            if (SelfFact const* s = bb.selfOf(id, kSelfTtl)) len = len > s->m.len ? len : s->m.len;
            if (id != myId && beats(len, id, in.L, myId)) return false;
        }
        for (auto& s : bb.selves) {
            if (!s.valid || bb.r - s.round > kSelfTtl || s.m.id == myId) continue;
            if (beats(s.m.len, s.m.id, in.L, myId)) return false;
        }
        return true;
    }

    bool visibleAlly(CrownInput const& in, int id) const {
        for (int i = 0; i < in.nVisAlly; i++) if (in.visAllyIds[i] == id) return true;
        return false;
    }

    int visibleHeadDist(CrownInput const& in, int id) const {
        for (int i = 0; i < in.nVisAlly; i++)
            if (in.visAllyIds[i] == id) return in.visAllyHeadDist ? in.visAllyHeadDist[i] : -1;
        return -2;  // not visible at all
    }

    bool superseded(KingFact const& k) const {
        for (int i = 0; i < 4; i++)
            if (supKid[i] == k.m.kid && k.m.round <= supRound[i]) return true;
        return false;
    }
    void markSuperseded(KingFact const& k) {
        int slot = 0;
        for (int i = 0; i < 4; i++) {
            if (supKid[i] == k.m.kid || supKid[i] < 0) { slot = i; break; }
            if (supRound[i] < supRound[slot]) slot = i;
        }
        supKid[slot] = k.m.kid;
        supRound[slot] = k.m.round;
    }
    int seenAt(int kid) const {
        for (int i = 0; i < 8; i++) if (lastSeenKing[i] == kid) return lastSeenKingAt[i];
        return -1000;
    }
    void noteSeen(int kid, int r) {
        int slot = 0;
        for (int i = 0; i < 8; i++) {
            if (lastSeenKing[i] == kid || lastSeenKing[i] < 0) { slot = i; break; }
            if (lastSeenKingAt[i] < lastSeenKingAt[slot]) slot = i;
        }
        lastSeenKing[slot] = kid;
        lastSeenKingAt[slot] = r;
    }

    // Succession: I was this king's HEIR / BACKUP recently (or, with contactSuccession, had fresh contact with it and
    // am HEIR-long), its claim has not been updated for successionSilence rounds and it is not in my vision.
    bool successor(KingFact const& k, CrownInput const& in) const {
        if (in.r - k.updatedAt < P.successionSilence) return false;
        if (visibleHeadDist(in, k.m.kid) != -2) return false;
        if (k.m.kid == heirOf && in.r - lastHeirRound <= P.successionMemory) return true;
        if (!P.contactSuccession) return false;
        int need = k.m.klen / 3 > P.heirMin ? k.m.klen / 3 : P.heirMin;
        if (in.L < need) return false;
        bool contact = k.freshSeenAt >= in.r - P.successionMemory || seenAt(k.m.kid) >= in.r - P.successionMemory;
        return contact;
    }

    // Death detection for kings I follow (not myself). Issues at most one tombstone per turn.
    void detectDeath(Blackboard& bb, CrownInput const& in) {
        int r = in.r;
        // Witnessed death: a claimed king whose head was within witnessDist of my head last turn has no segment in my
        // window now. Had it moved, its neck (its old head cell) would still be in view.
        if (P.ruleW) {
            bool fired = false;
            if (seenRound == r - 1)
                for (int i = 0; i < nSeen && !fired; i++) {
                    if (seenDist[i] < 0 || seenDist[i] > P.witnessDist) continue;
                    if (visibleHeadDist(in, seenKid[i]) != -2) continue;
                    KingFact* k = bb.kingSlot(seenKid[i]);
                    if (k && k->m.klen > 0) { issueTomb(bb, *k, 3); fired = true; }
                }
            nSeen = 0;
            for (auto& k : bb.kings) {
                if (!k.valid || k.m.klen == 0 || k.m.kid == myId || nSeen >= kSeen) continue;
                int dist = visibleHeadDist(in, k.m.kid);
                if (dist == -2) continue;
                noteSeen(k.m.kid, r);
                seenKid[nSeen] = k.m.kid;
                seenDist[nSeen] = dist;
                nSeen++;
            }
            seenRound = r;
            if (fired) return;
        }
        if (!P.ruleA && !P.ruleB) return;
        // Rule (a): a claim that was <= 3 rounds fresh within the last deadFreshWindow rounds, no newer claim for
        // deadSilence rounds, and the king not in my vision.
        for (auto& k : bb.kings) {
            if (!P.ruleA) break;
            if (!k.valid || k.m.klen == 0 || k.m.kid == myId) continue;
            if (k.freshSeenAt < r - P.deadFreshWindow || k.freshSeenAt < 0) continue;
            if (r - k.updatedAt < P.deadSilence) continue;
            if (visibleAlly(in, k.m.kid)) continue;
            issueTomb(bb, k, 1);
            return;
        }
        // Rule (b): I stay within deadNear steps of the last known head of the king I follow, and for deadSilence
        // rounds I neither see it nor hold a claim of it at most 3 rounds old.
        KingFact const* f = P.ruleB ? bb.bestKing(kKingBlockTtl, myId) : nullptr;
        if (!f) { nearRun = 0; nearKid = -1; return; }
        if (f->m.kid != nearKid) { nearKid = f->m.kid; nearRun = 0; }
        bool near = bb.g.man(in.hx, in.hy, f->m.x, f->m.y) <= P.deadNear;
        bool sign = visibleAlly(in, f->m.kid) || r - f->m.round <= 3;
        if (!near || sign) { nearRun = 0; return; }
        if (++nearRun >= P.deadSilence) {
            KingFact* k = bb.kingSlot(f->m.kid);
            if (k) issueTomb(bb, *k, 2);
            nearRun = 0;
        }
    }

    void issueTomb(Blackboard& bb, KingFact const& k, int rule) {
        KingMsg t = k.m;
        t.klen = 0;
        t.round = bb.r;
        t.busy = 0;
        int kid = t.kid;
        bb.mergeKing(t, true);
        ev.tombKid = kid;
        ev.tombRule = rule;
        nTombs++;
    }

    // Trigger (c2); sets stag = true when only trigger T4 holds.
    bool heardKing = false;
    bool trigger(CrownInput const& in, bool& onlyT4) {
        onlyT4 = false;
        if (in.r < P.earliest) return false;
        double cap = in.capacity;
        double thr = 0.8 * cap;
        if (thr < 12) thr = 12;
        if (thr > 56) thr = 56;
        bool t1 = t1Run >= P.t1Rounds;
        bool t2 = in.eLo >= (10 > in.L + 4 ? 10 : in.L + 4);
        bool t3 = in.isolated;
        int t4r = (in.c25 >= P.c25High && in.U <= P.contactUnits) ? P.t4RoundContact : P.t4Round;
        bool t4 = in.r >= t4r && in.uEnemyLo <= in.U;
        if (t1 || t2 || t3) return true;
        if (P.heardTrigger && heardKing) return true;
        if (t4) { onlyT4 = true; return true; }
        return false;
    }

    void beginTurn(Blackboard& bb, CrownInput const& in) {
        ev = CrownEvents();
        if (abdicatePending) {  // last turn's claim went out with the stub's length; now a BACKUP
            abdicatePending = false;
            role = CR_BACKUP;
            backupSince = rNow;
            yieldedTo = -1;
        }
        rNow = in.r;
        preLenNow = in.L;
        int r = in.r;
        // Trigger T1 run length (U saturation).
        {
            double thr = 0.8 * in.capacity;
            if (thr < 12) thr = 12;
            if (thr > 56) thr = 56;
            t1Run = in.U >= thr ? t1Run + 1 : 0;
        }
        // Abdications heard (a claim that fell to a stub): a handover is under way.
        for (auto& k : bb.kings)
            if (k.valid && k.m.klen > 0 && k.m.klen <= P.stubMax && k.prevKlen > P.stubMax &&
                r - k.m.round <= P.handoverWindow)
                lastAbdicationHeardAt = r;

        if (role == CR_KING) {
            // Yield to the best fresh claim that beats me at its round and now.
            bool finish = r >= P.finishRound;
            KingFact const* to = nullptr;
            for (auto& k : bb.kings) {
                int maxAge = finish ? kKingBlockTtl : P.yieldFresh;
                if (!k.valid || k.m.klen == 0 || k.m.kid == myId || r - k.m.round > maxAge) continue;
                if (superseded(k)) continue;
                if (!beats(k.m.klen, k.m.kid, lenAt(k.m.round), myId)) continue;
                if (!beats(k.m.klen, k.m.kid, in.L, myId)) continue;
                if (finish && r - k.m.round > P.yieldFresh &&
                    1.3 * bb.g.man(in.hx, in.hy, k.m.x, k.m.y) > 500 - r - P.finishEtaSlack)
                    continue;  // a stale claim only counts when that king is still reachable
                if (!to || beats(k.m.klen, k.m.kid, to->m.klen, to->m.kid)) to = &k;
            }
            if (to) {
                role = finish ? CR_NONE : CR_BACKUP;  // FINISH merge: the shorter king feeds the longer one
                backupSince = r;
                yieldedTo = to->m.kid;
                ev.yielded = true;
                ev.finishMerge = finish;
                ev.yieldedTo = yieldedTo;
                ev.why = W_YIELDED;
                nYields++;
            }
            return;
        }

        detectDeath(bb, in);

        if (role == CR_BACKUP && r - backupSince >= P.backupRounds) {
            role = CR_NONE;
            ev.backupExpired = true;
        }

        // Claim check (c0)-(c5).
        bool c0 = initial || bb.heardAnyKing || r - birthRound >= P.ageToClaim;
        int minLen = P.minLen;
        if (r >= P.lateRound && !bb.heardAnyKing) minLen = P.minLenLate;
        bool c1 = in.L >= minLen;
        bool onlyT4 = false;
        heardKing = bb.heardAnyKing;
        bool c2 = trigger(in, onlyT4);
        if (!(c2 && onlyT4)) t4Since = -1;
        ev.why = !c0 ? W_C0 : (!c1 ? W_C1 : (!c2 ? W_TRIGGER : (in.executing ? W_EXEC : W_KING)));
        if (!(c0 && c1 && c2 && !in.executing)) candSince = -1;
        if (c0 && c1 && c2 && !in.executing) {
            bool wait = false;
            if (onlyT4 && r - lastAbdicationHeardAt > P.handoverWindow) {
                if (t4Since < 0) t4Since = r;
                wait = r < t4Since + static_cast<int>(crownHash(myId) % static_cast<uint32_t>(P.stagger));
            }
            if (wait) ev.why = W_STAGGER;
            if (!wait) {
                bool c3 = true;
                KingFact const* succeeded[Blackboard::kKings];
                int nSucc = 0;
                for (auto& k : bb.kings) {
                    if (!bb.kingLive(k) || k.m.kid == myId) continue;
                    if (k.m.klen < in.L - (P.hysteresis - 1)) continue;
                    if (superseded(k)) continue;
                    if (P.succession && successor(k, in)) { succeeded[nSucc++] = &k; continue; }
                    c3 = false;
                    ev.blockKid = k.m.kid;
                    break;
                }
                bool c4 = c3 && localMax(bb, in);
                ev.why = !c3 ? W_C3 : (!c4 ? W_C4 : W_KING);
                bool patient = true;
                if (c3 && c4) {
                    if (candSince < 0) candSince = r;
                    // no wait when the throne is known to be empty: a succession, a handover, or a fresh tombstone
                    // (<= 10 rounds) of a king whose claim I held
                    bool freshTomb = false;
                    for (auto& k : bb.kings)
                        if (k.valid && k.m.klen == 0 && r - k.m.round <= 10 && k.prevKlen >= P.heirMin) freshTomb = true;
                    bool bypass = nSucc > 0 || r - lastAbdicationHeardAt <= P.handoverWindow || freshTomb;
                    patient = bypass || r - candSince >= P.claimPatience;
                    if (!patient) ev.why = W_STAGGER;
                } else {
                    candSince = -1;
                }
                if (c3 && c4 && patient) {
                    for (int i = 0; i < nSucc; i++) markSuperseded(*succeeded[i]);
                    ev.succession = nSucc > 0;
                    role = CR_KING;
                    kingSince = r;
                    kCrown = in.L;
                    yieldedTo = -1;
                    ev.claimed = true;
                    nClaims++;
                    return;
                }
            }
        }

        // HEIR: not king / backup, a live king known (nav TTL), L >= max(6, K/3), locally the longest apart from it.
        if (role == CR_NONE || role == CR_HEIR) {
            role = CR_NONE;
            KingFact const* k = bb.bestKing(kKingNavTtl, myId);
            if (k && k->m.klen >= P.heirMin) {
                int need = k->m.klen / 3 > P.heirMin ? k->m.klen / 3 : P.heirMin;
                if (in.L >= need && localMax(bb, in)) {
                    role = CR_HEIR;
                    heirOf = k->m.kid;
                    lastHeirRound = r;
                }
            }
        }
        if (role == CR_BACKUP && yieldedTo >= 0) {
            heirOf = yieldedTo;
            lastHeirRound = r;
        }
    }

    // After choosing the action: record the post-action length (my claim this round) and abdicate as a stub.
    void afterAction(int postL) {
        int s = rNow & 63;
        postLen[s] = postL;
        postLenRound[s] = rNow;
        if (role == CR_KING && postL <= P.stubMax) {
            // The claim of this round still goes out with the stub's length (it releases (c3) everywhere and tells a
            // new child that a handover is under way); from the next round this process is a BACKUP.
            ev.abdicated = true;
            nAbdications++;
            abdicatePending = true;
        }
    }
    bool abdicatePending = false;

    KingMsg kingMsg(int postL, int hx, int hy, int sigma, int h, int busy, int mode) const {
        KingMsg m;
        m.round = rNow;
        m.kid = myId;
        m.klen = postL < 2 ? 2 : (postL > 255 ? 255 : postL);
        m.x = hx; m.y = hy;
        m.sigma = sigma & 3; m.h = h & 3; m.busy = busy & 1; m.mode = mode & 3;
        return m;
    }
};

// ------------------------------------------------------------------------------------------------ mode machine
struct ModeParams {
    int finishRound = 490;
    int rFeed = 400;             // backstop
    int rFeedMergeMin = 350;     // merge detection moves r_feed to max(350, r + 10)
    int mergeLead = 10;
    double mu = 0.35;            // king mouth throughput (pearls / round)
    int slack = 10;
    int behindConverge = 440;
    int fallbackKingless = 40;   // non-king: no live claim this old -> local fallback (20 at first, but the FEED rule
                                 // lets a claim <= 40 rounds old start a feeder: a trace saw a third of all non-king
                                 // turns holding a 21-40-round claim and barred from feeding by a 20-round fallback)
    int fallbackOpenUntil = 220;
    bool needBased = true;       // r_formula counts only the pearls the king still needs (K_goal - K), not the whole
                                 // army's drops (with a 40-unit army the latter puts CONVERGE right after the crown)
};

struct ModeInput {
    int r = 0;
    bool haveKing = false;       // a king exists in my view (I am king, or a claim within fallbackKingless)
    int kingMode = MODE_OPEN;    // mode carried by the claim I follow (non-kings)
    bool amKing = false;
    double eta80 = 0;            // king only: 80th percentile BFS steps of heard teammates to the king head
    double mDrop = 0;            // king only: pearls the reachable non-king non-heir dragons would drop
    double kNeed = -1;           // king only: K_goal - K (last round's goal), for needBased; < 0 unknown
    bool mergeDetected = false;  // same enemy king +5 visible length in 25 rounds, or E_mid >= K + 6
    int sigma = SIG_UNKNOWN;
};

struct ModeMachine {
    ModeParams P;
    int mode = MODE_OPEN;
    int rConv = 10000;           // king: the CONVERGE round once fixed
    int rFeed = 400;
    bool kinglessFallback = false;

    void init() {
        P = ModeParams();
        mode = MODE_OPEN;
        rConv = 10000;
        rFeed = P.rFeed;
        kinglessFallback = false;
    }

    // r_formula: the first round with 490 - r <= ETA80 + M_drop / mu + 10.
    bool formulaDue(int r, double eta80, double mDrop) const {
        return P.finishRound - r <= eta80 + mDrop / P.mu + P.slack;
    }

    // The king keeps a forward-only mode; a non-king follows the claim it holds (<= 20 rounds) and falls back to
    // OPEN without one (kinglessFallback from r220: forage biased towards the last known king head, no feeding).
    int update(ModeInput const& in) {
        kinglessFallback = false;
        if (in.r >= P.finishRound) { mode = MODE_FINISH; return mode; }
        if (in.amKing) {
            int m = mode < MODE_GROW ? MODE_GROW : mode;
            if (in.mergeDetected) {
                int rf = in.r + P.mergeLead;
                if (rf < P.rFeedMergeMin) rf = P.rFeedMergeMin;
                if (rf < rFeed) rFeed = rf;
            }
            double mass = in.mDrop;
            if (P.needBased && in.kNeed >= 0 && in.kNeed < mass) mass = in.kNeed;
            bool conv = in.r >= rFeed || formulaDue(in.r, in.eta80, mass) ||
                        (in.sigma == SIG_BEHIND && in.r >= P.behindConverge);
            if (conv && m < MODE_CONVERGE) { m = MODE_CONVERGE; rConv = in.r; }
            mode = m;
            return mode;
        }
        if (in.haveKing) {
            mode = in.kingMode < MODE_GROW ? MODE_GROW : in.kingMode;
            return mode;
        }
        mode = MODE_OPEN;
        kinglessFallback = in.r >= P.fallbackOpenUntil;
        return mode;
    }
};

}  // namespace bc::king
