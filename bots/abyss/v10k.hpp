#pragma once
// v10k: adapter between the search core and the king end-game library (king/*.hpp). INACTIVE in the final build:
// V10K_R0 = 501 is past the last round, so only a few constant-time checks per turn run. When enabled, the search
// core plays unchanged in the opening and the middle game; from the first crown event the king library decides the
// end game - who the king is, who feeds it and when, the king's safety rules, the FINISH merge - and the search
// executes it.
//
// Three phases per process:
//   OFF     r < V10K_R0: nothing runs (a few constant-time checks around the search).
//   LISTEN  r >= V10K_R0, not yet active: listen() runs after the search and the action (inbox, crown, statistics);
//           no rays, no World writes, no heap. The process activates (from the next turn) when it becomes a crown
//           candidate or king, or accepts a plausible king-library frame.
//   ACTIVE  pre() runs before the search (inbox, crown, hunger / sigma / mode, feeding; overrides this turn's Params
//           copy and World::know.longest), speak() after the action (rays: KING / SELF of the library + the core's facts).
// No search-core header is changed; main.cpp calls pre() / listen() / speak(). The adapter lives in a static buffer, built by
// placement new on first use (after a search): no global constructor runs before main.
#include <new>
#include <vector>

#include "io.hpp"
#include "policy.hpp"
#include "world.hpp"
#if __has_include("v10k_arm.hpp")
#include "v10k_arm.hpp"  // optional per-experiment switch overrides (absent in the final build)
#endif
#include "king/core.hpp"

// ---------------------------------------------------------------------------------------------------- switches
#ifndef V10K_ON
#define V10K_ON 1                 // 0: the adapter is never called (the search core plus dead code)
#endif
#ifndef V10K_MIN_TILES
#define V10K_MIN_TILES 1100        // the library never runs on maps of <= this many tiles (small maps measured worse
                                   // with it)
#endif
#ifndef V10K_R0
#define V10K_R0 501               // first round the library may run; 501 = always OFF (the final build: the queen
                                  // rules of 1.2.3 decide round 500)
#endif
#ifndef V10K_ACTIVATE
#define V10K_ACTIVATE 1           // 0: stay in LISTEN for good (diagnosis: silent-phase CPU and identity)
#endif
#ifndef V10K_FEED
#define V10K_FEED 1               // 0: crown, king rules and yield zone only, no FEED role and no execution
#endif
#ifndef V10K_FEED_FROM
#define V10K_FEED_FROM 300        // no FEED role before this round (the king still computes and broadcasts h)
#endif
#ifndef V10K_ARM
#define V10K_ARM 1                // feeding arm: 0 D5, 1 D5' (regional recycling), 2 both
#endif
#ifndef V10K_D5P_DIST
#define V10K_D5P_DIST 3           // FeedParams::d5pMaxDist: the king in view within 3 (999: anywhere inside D_ring).
                                  // With 999 most executions lay > 3 from the king, which then ate only a small share
                                  // of the drops
#endif
#ifndef V10K_D5P_DIST_SMALL
#define V10K_D5P_DIST_SMALL V10K_D5P_DIST  // ... on maps under V10K_D5P_SMALL_TILES tiles (999 below 2000 tiles, 4
                                           // above in the library's own tuning)
#endif
#ifndef V10K_D5P_SMALL_TILES
#define V10K_D5P_SMALL_TILES 2000
#endif
#ifndef V10K_EXEC_AGE
#define V10K_EXEC_AGE 3           // an execution needs the king in view or its claim at most this old (dead kings were fed)
#endif
#ifndef V10K_EXEC_THROTTLE
#define V10K_EXEC_THROTTLE 1      // with nF FEED-ready teammates heard (7 rounds) and U - uFloor < nF + 1 slots, a feeder
#endif                            // executes with probability (U - uFloor) / (nF + 1) (5 executions in one round seen)
#ifndef V10K_PATIENCE
#define V10K_PATIENCE 20          // CrownParams::claimPatience
#endif
#ifndef V10K_MINLEN
#define V10K_MINLEN 6             // CrownParams::minLen
#endif
#ifndef V10K_MINLEN_LATE
#define V10K_MINLEN_LATE 4        // CrownParams::minLenLate
#endif
#ifndef V10K_LATE_ROUND
#define V10K_LATE_ROUND 260       // CrownParams::lateRound
#endif
#ifndef V10K_AGE_TO_CLAIM
#define V10K_AGE_TO_CLAIM 20      // CrownParams::ageToClaim (library 60): the search core churns (reverse splits hand
                                  // the long tail to a new child), so on a portal map the longest dragons were mostly
                                  // < 60 rounds old and the first crown came near r493; the claim patience (20) still
                                  // follows
#endif
#ifndef V10K_UE_HEARD
#define V10K_UE_HEARD 0           // 1: U_E^lo also counts enemy ids heard of (the core's ENEMY facts); 0: seen only, as the
                                  // library's enemiesKnown(50) (heard ids of a churning enemy army exceed U and block
                                  // trigger T4)
#endif
#ifndef V10K_SEED
#define V10K_SEED -1              // CrownParams::seedFrom (-1 off): the locally longest >= 4 stops economic splits
#endif
#ifndef V10K_KING_RULES
#define V10K_KING_RULES 1         // the king's Params (kKingMin / ekMinLen 2, no sprint trades, reach, danger); 0: the core's own
#endif
#ifndef V10K_KING_REACH
#define V10K_KING_REACH 5         // the king's reachCap (min(Le, 5))
#endif
#ifndef V10K_KING_DANGER
#define V10K_KING_DANGER 3.0      // the king's kingDanger (x 1.5 while sigma = AHEAD)
#endif
#ifndef V10K_KING_VETO
#define V10K_KING_VETO 0          // the post-search root veto needs a policy.hpp hook: not implemented
#endif
#if V10K_KING_VETO
#error "V10K_KING_VETO needs a post-search root-veto hook in policy.hpp, which this build does not have"
#endif
#ifndef V10K_YIELD
#define V10K_YIELD 1              // the yield zone overrides (ekMinLen / ekYield / ekZone)
#endif
#ifndef V10K_ARMY_CONVERGE_NOSPLIT
#define V10K_ARMY_CONVERGE_NOSPLIT 1  // ARMY stops splitting once the king's mode is CONVERGE
#endif
#ifndef V10K_SONAR
#define V10K_SONAR 1              // 1: rays merged (library KING / SELF + the core's facts); 2: the core's facts first, the library
                                  // only SELF and 1 KING
#endif
#ifndef V10K_LIB_RAYS
#define V10K_LIB_RAYS 3           // at most this many library rays for a non-king when the core has facts to send
#endif
#ifndef V10K_PLAUSIBLE
#define V10K_PLAUSIBLE 1          // phantom KING filter
#endif
#ifndef V10K_QUARANTINE
#define V10K_QUARANTINE 10        // a claim failing only the length test waits in quarantine; a consistent second
                                  // frame of the same kid within this many rounds admits it (0: reject for good)
#endif
#ifndef V10K_REFRESH
#define V10K_REFRESH 3            // a non-king that sees the king it follows alive refreshes the claim to round r - this
#endif                            // (head, klen >= visible - this), so it forwards a fresh claim (0 off)
#ifndef V10K_HANDOVER
#define V10K_HANDOVER 1           // a process <= 3 rounds old hearing a stub claim or a fresh tombstone takes the handover
#endif                            // bypass (the library needs an older claim of that kid, which a new child lacks)
#ifndef V10K_STUB_RELAY
#define V10K_STUB_RELAY 5         // an abdicated stub keeps sending its stub claim this many rounds (its abdication ray
#endif                            // often ran along its own neck, away from the child)
#ifndef V10K_RECENT
#define V10K_RECENT 10            // the king followed: the longest claim at most this many rounds behind the newest claim
#endif                            // held (library kingViewOf: 20)
#ifndef V10K_SELF_KING
#define V10K_SELF_KING 1          // a candidate (and a HEIR / BACKUP longer than the claim it follows) is the core's king:
#endif                            // World::know.longest = itself, the king's Params
#ifndef V10K_REBUILD_KEEP
#define V10K_REBUILD_KEEP 1       // no lateNoSplit override while the core's survival rebuild holds (U <= 4, or isolated <= 8)
#endif
#ifndef V10K_RESERVE
#define V10K_RESERVE 0.002        // seconds (2 M points) less for the core's search on ACTIVE turns
#endif
#ifndef V10K_CPU_GUARD
#define V10K_CPU_GUARD 0.090      // speak skips ray prediction past this much of the turn
#endif
#ifndef V10K_FEED_FORAGE
#define V10K_FEED_FORAGE 0.4      // a FEED dragon's kFeedForage
#endif
#ifndef V10K_GROW_UHOLD
#define V10K_GROW_UHOLD 0         // 1: the king broadcasts h = 0 in GROW while U <= uHold
#endif
#ifndef V10K_V10FEED_FALLBACK
#define V10K_V10FEED_FALLBACK 1   // the core's own feeding runs while no live claim (> stub, 120 rounds) is held
#endif
#ifndef V10K_TRACE
#define V10K_TRACE 0              // a LOG line per ACTIVE turn (analysis builds only; costs 4000 points a byte)
#endif

namespace bc::v10k {

namespace kg = bc::king;

enum Role : int { R_NONE = 0, R_KING = 1, R_CAND = 2, R_HEIR = 3, R_BACKUP = 4, R_FEED = 5, R_ARMY = 6 };

// What the library decided for this turn (POD; the default lets the core run as it is).
struct Plan {
    bool active = false;     // pre() ran (ACTIVE): the core's search budget is V10K_RESERVE shorter, speak() sends the rays
    bool over = false;       // Params / World are overridden this turn
    bool execute = false;    // a FEED execution: no action this turn
    int role = R_NONE;
    bool kingKnown = false;
    int yieldSteps = 0;
    bool ahead = false;      // king: sigma = AHEAD
    bool converge = false;   // the mode followed is CONVERGE or later
    bool seeding = false;    // crown seed
    bool noV10Feed = false;  // a live claim is held but no king followed (or V10K_V10FEED_FALLBACK 0): no core feeding
    bool selfKing = false;   // V10K_SELF_KING: this candidate / HEIR / BACKUP is the core's king (know.longest = itself)
    bool rebuild = false;    // the core's survival rebuild holds this turn (policy.hpp rebuild_): keep its splits
};

// The role's Params overrides on this process's copy for this turn.
inline void apply(Plan const& kp, int r, Params& p) {
    if (kp.role != R_NONE && p.ekFrom > r) p.ekFrom = r;  // the early-king machinery is what executes the roles
    // lateNoSplit is checked before trySplit's rebuild branch (policy.hpp): while the survival reproduction holds
    // (the base never blocked it: its lateNoSplit 400 = feedRound) the override would end it
    bool noSplit = !(V10K_REBUILD_KEEP && kp.rebuild);
    auto kingRules = [&](bool ahead) {
        p.kKingMin = 2;
        p.ekMinLen = 2;
        p.sprintTrades = 0;
        p.reachCap = V10K_KING_REACH;
        p.kingDanger = V10K_KING_DANGER * (ahead ? 1.5 : 1.0);
    };
    switch (kp.role) {
    case R_KING:
        p.feedStop = 0;
        if (noSplit) p.lateNoSplit = 0;
        if (V10K_KING_RULES) kingRules(kp.ahead);
        break;
    case R_CAND:
    case R_HEIR:
    case R_BACKUP:
        if (noSplit) p.lateNoSplit = 0;
        p.feedStop = 0;
        if (kp.selfKing) {
            if (V10K_KING_RULES) kingRules(false);
        } else if (kp.kingKnown && V10K_YIELD) {
            p.ekMinLen = 2;
            p.ekYield = kp.yieldSteps;
        }
        break;
    case R_FEED:
        p.finalFrom = r;           // the core's final push, used for the pull to the king only
        p.finalMaxLen = 99;
        p.feedDist = -1;           // the core never sacrifices by itself
        if (noSplit) p.lateNoSplit = 0;
        p.slayFrom = 501;
        p.fFrom = 501;
        p.kFeedForage = V10K_FEED_FORAGE;
        if (V10K_YIELD) {
            p.ekZone = 0;
            p.ekMinLen = 2;
            p.ekYield = kp.yieldSteps;
        }
        break;
    case R_ARMY:
        p.feedStop = 0;
        if (V10K_YIELD) {
            p.ekMinLen = 2;
            p.ekYield = kp.yieldSteps;
        }
        if (kp.converge && V10K_ARMY_CONVERGE_NOSPLIT && noSplit) p.lateNoSplit = 0;
        break;
    default:
        break;
    }
    if (kp.seeding && noSplit) p.lateNoSplit = 0;
    if (kp.noV10Feed) p.feedStop = 0;
}

// Ray-end prediction on the core's World (the library's Env contract).
struct Env {
    World const* w;
    std::vector<int> const* own;
    std::vector<int> const* child;
    char team;
    int W() const { return w->board.W; }
    int H() const { return w->board.H; }
    int step(int c, int d) const {
        int n = w->board.nb[c * 4 + d];
        if (n >= 0) return n;
        return w->board.side(c, d).kind == 2 ? -2 : -1;
    }
    int occ(int c, int& id, int& vl) const {
        for (int x : *own)
            if (x == c) return kg::OCC_SELF;
        for (int x : *child)
            if (x == c) {
                id = -1;
                vl = static_cast<int>(child->size());
                return kg::OCC_ALLY;
            }
        int o = w->occ[c];
        if (o < 0) return kg::OCC_EMPTY;
        id = o;
        vl = 0;
        bool friendly = false;
        for (Part const& p : w->t.parts)
            if (p.id == o) vl++, friendly = p.team == team;
        return friendly ? kg::OCC_ALLY : kg::OCC_ENEMY;
    }
    bool inWindow(int c) const { return w->visible(c); }
};

struct Adapter {
    kg::Blackboard bb;
    kg::Crown cr;
    kg::KingStats ks;
    kg::Hunger hu;
    kg::SigmaState sg;
    kg::ModeMachine mm;
    kg::FeedParams fp;
    kg::HungerOut ho;
    kg::SigmaOut so;
    kg::KingView kv;
    Params pOver;                 // this turn's Params copy (static storage, not the stack)
    std::vector<int> preBody;     // ACTIVE only: the body before the action (a split child's cells)

    static constexpr int kUFloor = 6;   // LibraryParams::uFloor
    static constexpr int kVis = 64;
    static constexpr int kIds = 128;
    static constexpr int kInbox = 256;

    bool started = false;
    int r = 0, myId = -1, birth = 0;
    char myTeam = 'A';
    int preL = 2, U = 1;
    int mode = kg::MODE_OPEN, busy = 0, convSince = -1, hOut = 0;
    int selfTask = kg::T_NONE, selfTgt = 0xFF;
    int allyIds[kVis], allyLens[kVis], allyHeadDist[kVis];
    int nAlly = 0;
    int enemyIds[kVis];
    int nEnemy = 0;
    bool thrNow = false;
    int thrRing[32], thrRound[32], eatRing[32], eatRound[32];
    int ekId[kIds], ekRound[kIds];    // enemy ids seen (U_E^lo = those within 50 rounds; V10K_UE_HEARD: heard of too)
    int akId[kIds], akRound[kIds];    // own ids seen or heard (SELF) (phantom KING filter: 120 rounds)
    struct Watch { int id, len, n; int cells[49]; };
    Watch watch[16];
    int nWatch = 0, watchRound = -1000;
    struct Drop { int cell, until; };
    Drop drops[64];
    int nDrops = 0;
    int ownEKingLen = 0, ownEKingRound = -1000;
    uint64_t inbox[kInbox];
    int rejKid[8];
    int nRej = 0;
    int nPhantom = 0;
    struct Quar { int kid, klen, round, heardAt; };  // V10K_QUARANTINE: claims waiting for a second frame
    Quar quar[8];
    int nQuar = 0, nQuarOk = 0;
    int lastWroteKid = -1;        // the library king last written into World::know.longest (-1: none / myself)
    int abdicatedAt = -1000;      // the round this process abdicated as a stub (V10K_STUB_RELAY)
    int convKid = -1;             // the king whose CONVERGE claim set convSince
    int nRefresh = 0, nStubSent = 0;
    Out* traceOut = nullptr;      // V10K_TRACE lines from receive()
    // trace (V10K_TRACE)
    int traceElig = 0, traceFeed = -1, traceWhyNot = 0, traceDK = -1, traceRole = R_NONE;
    int traceUE = 0, traceELo = 0, traceC25 = 0, traceSelfKing = 0;

    // ------------------------------------------------------------------------------------------------ setup
    void start(World const& w) {
        started = true;
        myId = w.init.id;
        myTeam = w.init.team;
        birth = w.firstRound;
        bb.init(w.board.W, w.board.H, myTeam, myId);
        bb.kingRays = 2;           // library default kingRays
        bb.kingRaysFresh = 10;     // library default kingRaysFresh
        cr.init(myId, w.firstRound, w.firstRound == 0);  // the birth round, not R0: (c0) counts the age from birth
        cr.P.claimPatience = V10K_PATIENCE;              // init resets P: parameters after it
        cr.P.minLen = V10K_MINLEN;
        cr.P.minLenLate = V10K_MINLEN_LATE;
        cr.P.lateRound = V10K_LATE_ROUND;
        cr.P.seedFrom = V10K_SEED;
        cr.P.ageToClaim = V10K_AGE_TO_CLAIM;
        ks.init();
        hu.init();
        sg.init();
        mm.init();
        fp = kg::FeedParams();
        fp.armD5 = V10K_ARM == 0 || V10K_ARM == 2;
        fp.armD5p = V10K_ARM == 1 || V10K_ARM == 2;
        fp.d5pMaxDist = w.board.NC < V10K_D5P_SMALL_TILES ? V10K_D5P_DIST_SMALL : V10K_D5P_DIST;
        for (int i = 0; i < 32; i++) thrRing[i] = eatRing[i] = 0, thrRound[i] = eatRound[i] = -1000;
        for (int i = 0; i < kIds; i++) ekId[i] = akId[i] = -1, ekRound[i] = akRound[i] = -100000;
        nWatch = nDrops = 0;
        nQuar = 0;
    }

    static void noteId(int* ids, int* rounds, int id, int round) {
        int slot = 0;
        for (int i = 0; i < kIds; i++) {
            if (ids[i] == id) {
                if (round > rounds[i]) rounds[i] = round;
                return;
            }
            if (rounds[i] < rounds[slot]) slot = i;
        }
        if (rounds[slot] < round) ids[slot] = id, rounds[slot] = round;
    }
    static int countIds(int const* rounds, int since) {
        int n = 0;
        for (int i = 0; i < kIds; i++) n += rounds[i] >= since;
        return n;
    }
    bool seenOwn(int id) const {
        for (int i = 0; i < kIds; i++)
            if (akId[i] == id) return r - akRound[i] <= kg::kKingBlockTtl;
        return false;
    }
    bool rejected(int kid) const {
        for (int i = 0; i < nRej; i++)
            if (rejKid[i] == kid) return true;
        return false;
    }

    // ------------------------------------------------------------------------------------------------ sensing
    // Vision (allies, enemies, threat ring), id rings, own enemy-king fact. No heap, no World writes.
    void sense(World const& w) {
        Turn const& t = w.t;
        Board const& b = w.board;
        r = t.round;
        preL = t.length;
        U = t.units;
        bb.beginTurn(r);
        nAlly = nEnemy = 0;
        thrNow = false;
        for (Part const& p : t.parts) {
            if (p.id == myId) continue;
            if (p.team != myTeam) {
                bool have = false;
                for (int i = 0; i < nEnemy && !have; i++) have = enemyIds[i] == p.id;
                if (!have && nEnemy < kVis) enemyIds[nEnemy++] = p.id;
                if (p.head) thrNow = true;
                continue;
            }
            int i = 0;
            while (i < nAlly && allyIds[i] != p.id) i++;
            if (i == nAlly) {
                if (nAlly >= kVis) continue;
                allyIds[nAlly] = p.id;
                allyLens[nAlly] = 0;
                allyHeadDist[nAlly] = -1;
                nAlly++;
            }
            allyLens[i]++;
            if (p.head) allyHeadDist[i] = b.cheb(w.head, b.id(p.x, p.y));
        }
        thrRing[r & 31] = thrNow;
        thrRound[r & 31] = r;
        for (int i = 0; i < nEnemy; i++) noteId(ekId, ekRound, enemyIds[i], r);
        if (V10K_UE_HEARD)
            for (auto const& e : w.know.enemies) noteId(ekId, ekRound, e.id, e.round);
        for (int i = 0; i < nAlly; i++) noteId(akId, akRound, allyIds[i], r);
        if (w.enemyKing.cell >= 0 && w.enemyKing.round == r && w.enemyKing.len >= 6) {
            kg::EKingMsg e;
            e.eid = w.enemyKing.id & 0xFFF;
            e.elen = w.enemyKing.len;
            e.x = b.X(w.enemyKing.cell);
            e.y = b.Y(w.enemyKing.cell);
            int nh = 0;
            for (World::Seen const& s : w.others)
                if (s.team != myTeam && b.cheb(s.head, w.enemyKing.cell) <= 4) nh++;
            e.nHead = std::min(7, nh);
            e.conf = 0;
            bb.noteOwnEKing(e);
            ownEKingLen = w.enemyKing.len;
            ownEKingRound = r;
        }
    }

    // E_lo: the blackboard, my own sightings, the core's enemy king (30 rounds) and longest enemy seen (100 rounds).
    int eLoNow(World const& w) const {
        int e = bb.eLo();
        if (r - ownEKingRound <= kg::kEKingTtl) e = std::max(e, ownEKingLen);
        if (w.enemyKing.cell >= 0 && r - w.enemyKing.round <= World::kKingAge) e = std::max(e, w.enemyKing.len);
        if (r - w.enemyLongestRound <= 100) e = std::max(e, w.enemyLongest);
        return e;
    }

    // ------------------------------------------------------------------------------------------------ inbox (search core)
    // frames (their tag checks out) never reach the library; of the rest only KING / SELF frames are passed on (v10k
    // never sends the other library types, so any that unseal are foreign). Returns true when a frame that wakes a
    // LISTEN process was accepted: a plausible KING (or tombstone), or a SELF of an own dragon known to me. translate
    // (ACTIVE): heard SELF and KING facts go into World::know.friends.
    bool receive(World& w, bool translate) {
        nRej = 0;
        int n = 0;
        for (uint64_t m : w.t.msgs) {
            if (m == 0 || n >= kInbox) continue;
            comms::Fact f;
            if (comms::decode(m, myTeam, r, f)) continue;  // the core's own traffic
            uint64_t p;
            int rs;
            if (!kg::unseal(bb.team, r, m, p, rs)) continue;
            int ty = kg::frameType(p);
            if (ty != kg::M_KING && ty != kg::M_SELF) continue;
            inbox[n++] = m;
        }
        if (!n) return false;
        kg::KingFact snap[kg::Blackboard::kKings];
        for (int i = 0; i < kg::Blackboard::kKings; i++) snap[i] = bb.kings[i];
        bool heardBefore = bb.heardAnyKing;
        int lastBefore = bb.lastKingHeardAt;
        bb.receive(inbox, n, enemyIds, nEnemy);
        bool wake = false, heardKing = false;
        bool young = r - birth <= 10;
        for (int i = 0; i < bb.nAccepted; i++)  // a SELF wakes me when its sender was known to me before this turn
            if (kg::frameType(bb.accepted[i]) == kg::M_SELF) {
                kg::SelfMsg m = kg::decSelf(kg::framePayload(bb.accepted[i]));
                if (m.id != myId && (young || seenOwn(m.id))) wake = true;
            }
        for (int i = 0; i < bb.nAccepted; i++)
            if (kg::frameType(bb.accepted[i]) == kg::M_SELF) {
                kg::SelfMsg m = kg::decSelf(kg::framePayload(bb.accepted[i]));
                if (m.id != myId) noteId(akId, akRound, m.id, r);
            }
        if (V10K_PLAUSIBLE) phantomFilter(w, snap);
        for (int i = 0; i < bb.nAccepted; i++) {
            uint64_t p = bb.accepted[i];
            uint64_t pl = kg::framePayload(p);
            if (kg::frameType(p) == kg::M_KING) {
                kg::KingMsg m = kg::decKing(pl);
                if (m.kid == myId || rejected(m.kid)) continue;
                heardKing = true;
                wake = true;
                if (translate && m.klen > 0)
                    comms::Knowledge::upsert(w.know.friends, {w.board.id(m.x, m.y), m.klen, m.kid & 4095, m.round}, 24);
            } else if (kg::frameType(p) == kg::M_SELF) {
                kg::SelfMsg m = kg::decSelf(pl);
                if (m.id == myId) continue;
                if (translate)
                    if (kg::SelfFact const* s = bb.selfOf(m.id, kg::kSelfKeep))
                        comms::Knowledge::upsert(w.know.friends, {w.board.id(m.x, m.y), m.len, m.id & 4095, s->round}, 24);
            }
        }
        bb.heardAnyKing = heardBefore || heardKing;
        if (!heardKing) bb.lastKingHeardAt = lastBefore;
        // A new child (<= 3 rounds) that hears a stub claim (an abdication under way) or a fresh tombstone takes the
        // crown's handover bypass: the library needs an older claim > stubMax of that kid (crown.hpp), which a child
        // born of the king's forced reverse split never held (it waited out the claim patience: 20-40 rounds).
        if (V10K_HANDOVER && r - birth <= 3)
            for (int i = 0; i < bb.nAccepted; i++) {
                if (kg::frameType(bb.accepted[i]) != kg::M_KING) continue;
                kg::KingMsg m = kg::decKing(kg::framePayload(bb.accepted[i]));
                if (m.kid == myId || rejected(m.kid)) continue;
                bool stub = m.klen > 0 && m.klen <= cr.P.stubMax && r - m.round <= cr.P.handoverWindow;
                bool tomb = m.klen == 0 && r - m.round <= 5;
                if (stub || tomb) cr.lastAbdicationHeardAt = r;
            }
        return wake;
    }

    // Phantom filter: a claim merged this turn whose kid was never seen as ours (vision or SELF, 120 rounds) is
    // dropped when it grew by more than (rounds + 2) since the last claim of that kid; a tombstone of a kid I never
    // knew is dropped. A claim whose klen exceeds the longest own dragon I know of by more than 6 waits in quarantine
    // (V10K_QUARANTINE) and is admitted when a later frame of the same kid agrees (|dklen| <= |dround| + 2) within
    // the window: the king sends no SELF, so far dragons knew only short teammates, and without the quarantine every
    // real king was rejected in a test. A random frame repeating a 12-bit kid with a consistent length is ~(5e-5)^2.
    // A process in its first 10 rounds (a child hearing its parent's relay) skips the length test.
    bool quarantined(int kid, int klen, int round) {
        for (int i = 0; i < nQuar; i++) {
            Quar const& q = quar[i];
            if (q.kid != kid || q.heardAt >= r || r - q.heardAt > V10K_QUARANTINE) continue;
            int dr = round > q.round ? round - q.round : q.round - round;
            int dl = klen > q.klen ? klen - q.klen : q.klen - klen;
            if (dl <= dr + 2) {
                quar[i] = quar[--nQuar];
                return true;
            }
        }
        int slot = -1;
        for (int i = 0; i < nQuar && slot < 0; i++)
            if (quar[i].kid == kid || r - quar[i].heardAt > V10K_QUARANTINE) slot = i;
        if (slot < 0 && nQuar < 8) slot = nQuar++;
        if (slot < 0) {
            slot = 0;
            for (int i = 1; i < 8; i++)
                if (quar[i].heardAt < quar[slot].heardAt) slot = i;
        }
        quar[slot] = {kid, klen, round, r};
        return false;
    }

    void phantomFilter(World const& w, kg::KingFact const* snap) {
        int known = preL;
        for (int i = 0; i < nAlly; i++) known = std::max(known, allyLens[i]);
        for (auto const& s : bb.selves)
            if (s.valid && r - s.round <= kg::kSelfKeep) known = std::max(known, s.m.len);
        if (w.know.longest.cell >= 0 && r - w.know.longest.round <= kg::kKingNavTtl) known = std::max(known, w.know.longest.len);
        for (int i = 0; i < kg::Blackboard::kKings; i++)
            if (snap[i].valid && snap[i].m.klen > 0 && r - snap[i].m.round <= kg::kKingBlockTtl)
                known = std::max(known, snap[i].m.klen);
        bool young = r - birth <= 10;
        for (int i = 0; i < kg::Blackboard::kKings; i++) {
            kg::KingFact const& k = bb.kings[i];
            kg::KingFact const& s = snap[i];
            if (!k.valid || k.own) continue;
            if (s.valid == k.valid && s.m.kid == k.m.kid && s.m.round == k.m.round && s.m.klen == k.m.klen) continue;
            int kid = k.m.kid;
            kg::KingFact const* old = nullptr;
            for (int j = 0; j < kg::Blackboard::kKings; j++)
                if (snap[j].valid && snap[j].m.kid == kid) old = &snap[j];
            bool seen = seenOwn(kid);
            bool bad = false, held = false, admitted = false;
            if (k.m.klen == 0) {
                bad = !old && !seen;
            } else {
                // growth past (rounds + 2) since the last claim of that kid, or klen past the longest own dragon I know
                // of + 6 (a refreshed claim keeps a stale klen at a newer round, so the real one can fail the first)
                bool grew = old && old->m.klen > 0 && k.m.klen - old->m.klen > (k.m.round - old->m.round) + 2;
                if (!seen && (grew || (!young && k.m.klen > known + 6))) {
                    if (V10K_QUARANTINE > 0 && quarantined(kid, k.m.klen, k.m.round)) {
                        nQuarOk++;
                        admitted = true;
                    } else {
                        bad = true;
                        held = V10K_QUARANTINE > 0;
                    }
                }
            }
            if (V10K_TRACE && traceOut && (bad || admitted)) {
                char b3[112];
                std::snprintf(b3, sizeof b3, "LOG kp %d %d %d %d %d %d %d %d\n", r, kid, k.m.klen, known,
                              old ? old->m.klen : -1, k.m.round, bad ? (held ? 2 : 1) : 0, nQuarOk);
                traceOut->buf += b3;
            }
            if (!bad) continue;
            bb.kings[i] = snap[i];
            if (nRej < 8) rejKid[nRej++] = kid;
            nPhantom++;
        }
    }

    void crownInput(World const& w, kg::CrownInput& in) const {
        in.r = r;
        in.L = preL;
        in.U = U;
        in.initialDragon = birth == 0;
        in.capacity = 64;   // the core has no capacity estimate: trigger T1 = U >= 51 for 10 rounds, sigma uses 0.61 U_late +
                            // 8.6
        in.isolated = false;
        in.eLo = eLoNow(w);
        if (cr.amKing()) {
            int heard, thr;
            bb.selfRates(25, heard, thr);
            in.c25 = heard ? static_cast<double>(thr) / heard : 0.0;
        } else {
            int thrN = 0, thrK = 0;
            for (int i = 0; i < 32; i++)
                if (thrRound[i] > r - 25 && thrRound[i] <= r) thrN++, thrK += thrRing[i];
            in.c25 = thrN ? static_cast<double>(thrK) / thrN : 0.0;
        }
        in.uEnemyLo = countIds(ekRound, r - 50);
        in.hx = w.board.X(w.head);
        in.hy = w.board.Y(w.head);
        in.visAllyIds = allyIds;
        in.visAllyLens = allyLens;
        in.visAllyHeadDist = allyHeadDist;
        in.nVisAlly = nAlly;
    }

    static int postLenOf(Choice const& c, int L) {
        if (c.split) return std::max(2, L - c.n);
        if (!c.sprint.empty() || c.dest < 0) return L;
        return L + (c.eat ? 1 : 0);
    }
    void noteEat(Choice const& c) {
        if (!c.split && c.sprint.empty() && c.dest >= 0 && c.eat) {
            eatRing[r & 31] = 1;
            eatRound[r & 31] = r;
        }
    }

    // ------------------------------------------------------------------------------------------------ LISTEN
    // After the search and the action; returns true when the process activates from the next turn.
    bool listen(World& w, Choice const& c, Out& out) {
        if (!started) start(w);
        traceOut = &out;
        sense(w);
        bool wake = receive(w, false);
        kg::CrownInput in;
        crownInput(w, in);
        cr.beginTurn(bb, in);
        ks.noteLen(r, preL);
        ks.noteUnits(r, U);
        noteEat(c);
        cr.afterAction(postLenOf(c, preL));
        // the crown seed (V10K_SEED >= 0) also activates: with the whole team in LISTEN nobody else would ever seed
        bool seedNow = V10K_SEED >= 0 && cr.seed(bb, in);
        bool act = V10K_ACTIVATE && (cr.candidate() || cr.amKing() || wake || seedNow);
        if (V10K_TRACE) {  // analysis builds only (the bytes cost points)
            char buf[128];
            std::snprintf(buf, sizeof buf, "LOG kl %d %d %d %d %d %d %d %d %d\n", r, preL, U, static_cast<int>(cr.ev.why),
                          cr.candSince, in.eLo, in.uEnemyLo, static_cast<int>(100 * in.c25), act ? 1 : 0);
            out.buf += buf;
        }
        return act;
    }

    // ------------------------------------------------------------------------------------------------ ACTIVE: pre
    void pre(World& w, Params const& mp, Plan& kp, Out& out) {
        if (!started) start(w);
        traceOut = &out;
        kp.active = true;
        sense(w);
        preBody = w.body;
        receive(w, true);
        refreshSeen(w);
        // the core's survival rebuild this turn (the same expression as policy.hpp): its splits stay allowed
        kp.rebuild = r < std::min(kRebuildStop, BC_V9_KING ? 500 - mp.kWindow : mp.feedRound) &&
                     (U <= kSurvivalUnits || (w.isolated() && U <= kIsolatedUnits));
        kg::CrownInput in;
        crownInput(w, in);
        cr.beginTurn(bb, in);
        traceUE = in.uEnemyLo;
        traceELo = in.eLo;
        traceC25 = static_cast<int>(100 * in.c25);
        kp.seeding = cr.seed(bb, in);
        ks.noteLen(r, preL);
        ks.noteUnits(r, U);
        traceElig = 0;
        traceFeed = -1;
        traceWhyNot = 0;
        traceDK = -1;
        if (cr.amKing()) {
            watchDeaths(w);
            kingDecide(w, in);
            kv = kg::KingView();
            kp.role = R_KING;
            kp.over = true;
            kp.ahead = so.sigma == kg::SIG_AHEAD;
            kp.converge = mode >= kg::MODE_CONVERGE;
            w.know.longest = {w.head, preL, myId & 4095, r};
            lastWroteKid = -1;
        } else {
            nWatch = 0;
            subjectDecide(w, mp, in, kp);
        }
        traceRole = kp.role;
        traceSelfKing = kp.selfKing ? 1 : 0;
    }

    // In testing the whole team held one claim and it expired together although teammates saw the king alive. A
    // non-king that sees the king it follows (the claim to follow within the blocking TTL, > stubMax) with more than
    // stubMax segments in view refreshes that claim to round r - V10K_REFRESH (its seen head; klen at least the
    // segments in view) and so forwards a fresh claim. The lag lets the king's own newer claims (h, busy, mode) still
    // win the merge; a dead king is never seen, so it is never refreshed.
    void refreshSeen(World const& w) {
        if (!V10K_REFRESH || cr.amKing()) return;
        kg::KingFact const* f = bb.currentKing(kg::kKingBlockTtl, myId, V10K_RECENT);
        if (!f || f->m.klen <= cr.P.stubMax) return;
        int rr = r - V10K_REFRESH;
        if (f->m.round >= rr) return;
        for (World::Seen const& s : w.others) {
            if (s.team != myTeam || (s.id & 0xFFF) != f->m.kid || s.visible <= cr.P.stubMax) continue;
            kg::KingMsg m = f->m;
            m.round = rr;
            m.klen = std::max(m.klen, s.visible);
            m.x = w.board.X(s.head);
            m.y = w.board.Y(s.head);
            bb.mergeKing(m, false);
            nRefresh++;
            return;
        }
    }

    // An ally whose head was within 2 of mine last turn and shows no segment now died there; its drops lie on its
    // segments 0, 2, 4, ... (eta-hat of the hunger law).
    void watchDeaths(World const& w) {
        if (watchRound == r - 1) {
            for (int i = 0; i < nWatch; i++) {
                Watch const& x = watch[i];
                bool vis = false;
                for (int j = 0; j < nAlly && !vis; j++) vis = allyIds[j] == x.id;
                if (vis) continue;
                ks.noteOwnDeath(r, x.len);
                for (int k = 0; k < x.n; k += 2) addDrop(x.cells[k], r + 30);
            }
        }
        int m = 0;
        for (int i = 0; i < nDrops; i++)
            if (drops[i].until >= r) drops[m++] = drops[i];
        nDrops = m;
        nWatch = 0;
        watchRound = r;
        for (int i = 0; i < nAlly && nWatch < 16; i++) {
            if (allyHeadDist[i] < 0 || allyHeadDist[i] > 2) continue;
            Watch& x = watch[nWatch++];
            x.id = allyIds[i];
            x.len = allyLens[i];
            x.n = 0;
            for (Part const& p : w.t.parts)
                if (p.id == x.id && x.n < 49) x.cells[x.n++] = w.board.id(p.x, p.y);
        }
    }
    void addDrop(int cell, int until) {
        if (nDrops < 64) {
            drops[nDrops++] = {cell, until};
            return;
        }
        int o = 0;
        for (int i = 1; i < 64; i++)
            if (drops[i].until < drops[o].until) o = i;
        drops[o] = {cell, until};
    }

    void kingDecide(World const& w, kg::CrownInput const& in) {
        int K = preL;
        double sumLen = 0, sumLoad = 0;
        int nSelf = 0, nHeir = 0;
        double eta[kg::Blackboard::kSelves];
        int lens[kg::Blackboard::kSelves];
        int ne = 0;
        for (auto const& s : bb.selves) {
            if (!s.valid || r - s.round > kg::kSelfKeep) continue;
            if (s.m.task == kg::T_HEIR) {
                nHeir++;
                continue;
            }
            nSelf++;
            sumLen += s.m.len;
            sumLoad += s.m.load;
            eta[ne] = 1.3 * bb.g.man(in.hx, in.hy, s.m.x, s.m.y);
            lens[ne] = s.m.len;
            ne++;
        }
        double lBar = nSelf ? sumLen / nSelf : 3.0;
        double mFeed = std::max(0, U - 1 - nHeir) * lBar;
        if (nSelf) ks.noteLoad(r, sumLoad / nSelf);
        double eta80 = 0, mDrop = 0;
        if (ne) {
            double e[kg::Blackboard::kSelves];
            for (int i = 0; i < ne; i++) e[i] = eta[i];
            std::sort(e, e + ne);
            eta80 = e[std::min(ne - 1, static_cast<int>(0.8 * ne))];
            double left = 490 - r, scale = std::max(1.0, static_cast<double>(U - 1 - nHeir) / nSelf);
            for (int i = 0; i < ne; i++)
                if (eta[i] <= left) mDrop += (lens[i] + 1) / 2;
            mDrop *= scale;
        }
        kg::SigmaInput si;
        si.r = r;
        si.K = K;
        si.eLo = in.eLo;
        si.isolated = false;
        si.ecoF = bb.mapValid && (bb.map.flags & kg::F_ECO_F);
        si.gated = bb.mapValid && (bb.map.flags & kg::F_GATED);
        si.capacity = in.capacity;
        si.uLate = ks.uLateOr(r);
        si.seenEnemyKing = in.eLo >= 8;
        si.gObs = (K - ks.lenAt(r - 20, K)) / 20.0;
        si.gReq = ho.gReq;
        si.etaHat = ks.etaHat(r);
        si.mFeed = mFeed;
        so = sg.update(si);
        bool merge = so.eMid >= K + 6;
        for (auto const& e : bb.ekings)
            if (e.valid && e.m.round - e.firstRound <= 25 && e.m.elen - e.firstLen >= 5) merge = true;
        kg::ModeInput mi;
        mi.r = r;
        mi.amKing = true;
        mi.eta80 = eta80;
        mi.mDrop = mDrop;
        mi.kNeed = ho.kGoal > 0 ? std::max(0.0, ho.kGoal - K) : 0.0;
        mi.mergeDetected = merge;
        mi.sigma = so.sigma;
        mode = mm.update(mi);
        kg::HungerInput hi;
        hi.r = r;
        hi.mode = mode;
        hi.K = K;
        hi.K20 = ks.lenAt(r - 20, K);
        hi.kCrown = cr.kCrown;
        hi.U = U;
        hi.mFeed = mFeed;
        hi.etaHat = si.etaHat;
        hi.regionCells = w.tilesSeen;
        hi.e500plus = so.e500plus;
        hi.sigma = so.sigma;
        hi.isolated = false;
        hi.capacity = in.capacity;
        hi.u300 = ks.u300;
        hi.loadTrend = ks.loadTrend(r);
        hi.c25 = in.c25;
        hi.uEnemyLo = in.uEnemyLo;
        ho = hu.update(hi);
        hOut = ho.h;
        if (V10K_GROW_UHOLD && mode == kg::MODE_GROW && U <= ho.uHold) hOut = 0;
        int pearls = 0, heads = 0;
        for (int dy = -3; dy <= 3; dy++)
            for (int dx = -3; dx <= 3; dx++) {
                int c = w.board.id(in.hx + dx, in.hy + dy);
                if (w.visible(c) && w.seenPearl[c]) pearls++;
            }
        for (int i = 0; i < nAlly; i++)
            if (allyHeadDist[i] >= 0 && allyHeadDist[i] <= 3) heads++;
        busy = kg::busyBit(pearls, heads, hOut);
        selfTask = kg::T_NONE;
        selfTgt = 0xFF;
    }

    // feed.hpp kingViewOf with the claims kept V10K_RECENT (not 20) rounds behind the newest one: a dead king's claim
    // stops updating, and the library keeps the longest claim of that window, so after a forced reverse (the child is
    // 2 shorter) or a king's death the dead king was followed (fed, yielded to) for up to 35 rounds (about a fifth of
    // the non-king turns in one test; tombstones need a witness and are not forwarded while another king is).
    kg::KingView kingView() const {
        kg::KingView v;
        kg::KingFact const* k = bb.currentKing(kg::kKingNavTtl, myId, V10K_RECENT);
        if (!k) return v;
        v.known = true;
        v.kid = k->m.kid;
        v.klen = k->m.klen;
        v.h = k->m.h;
        v.busy = k->m.busy;
        v.mode = k->m.mode;
        v.round = k->m.round;
        v.sigma = k->m.sigma;
        v.cx = k->m.x;
        v.cy = k->m.y;
        return v;
    }

    void subjectDecide(World& w, Params const& mp, kg::CrownInput const& in, Plan& kp) {
        Board const& b = w.board;
        kv = kingView();
        // A stub claim (a king's abdication, klen <= stubMax) releases (c3); it is nobody to follow, yield to or feed.
        if (kv.known && kv.klen <= cr.P.stubMax) kv = kg::KingView();
        if (kv.known) {
            for (World::Seen const& s : w.others)
                if ((s.id & 0xFFF) == kv.kid && s.team == myTeam) {
                    kv.visible = true;
                    kv.kx = b.X(s.head);
                    kv.ky = b.Y(s.head);
                    kv.kdir = s.headDir;
                }
        }
        // the CONVERGE ladder counts from the first CONVERGE claim of the king followed now (a new king restarts it)
        if (!kv.known || kv.kid != convKid) convSince = -1;
        if (kv.known && kv.mode >= kg::MODE_CONVERGE && convSince < 0) {
            convSince = kv.round;
            convKid = kv.kid;
        }
        kg::ModeInput mi;
        mi.r = r;
        mi.haveKing = kv.known && r - kv.round <= mm.P.fallbackKingless;
        mi.kingMode = kv.mode;
        mode = mm.update(mi);
        bool heir = cr.role == kg::CR_HEIR, backup = cr.role == kg::CR_BACKUP, cand = cr.candidate();
        kp.yieldSteps = fp.armD5p ? 2 : (V10K_FEED ? 4 : 3);
        if (mode >= kg::MODE_CONVERGE && kp.yieldSteps < 4) kp.yieldSteps = 4;
        kp.kingKnown = kv.known;
        kp.converge = mode >= kg::MODE_CONVERGE;
        kp.role = cand ? R_CAND : heir ? R_HEIR : backup ? R_BACKUP : kv.known ? R_ARMY : R_NONE;
        // A candidate is longer than every claim blocking it; a HEIR / BACKUP can outgrow a stale claim (in a test
        // the team's longest, 22, followed a king dead 18 rounds, the core thought that king was 23 and kept its guard
        // off). Such a dragon is the core's king: World::know.longest = itself and the king's Params.
        kp.selfKing = V10K_SELF_KING &&
                      (cand || ((heir || backup) && preL >= 4 && (!kv.known || preL >= kv.klen)));
        selfTask = heir || backup ? kg::T_HEIR : kg::T_FORAGE;
        selfTgt = 0xFF;
        traceWhyNot = !V10K_FEED ? 1 : (!kv.known ? 2 : (mode < kg::MODE_GROW ? 3 : (r < V10K_FEED_FROM ? 9 : 0)));
        if (V10K_FEED && kv.known && mode >= kg::MODE_GROW && r >= V10K_FEED_FROM) feedDecide(w, in, kp);
        kp.over = kp.role != R_NONE || kp.seeding;
        // the core's own feeding (the fallback for "still kingless at r >= 400") only while no live claim is held at all:
        // a 40-round navigation gap is not kinglessness (in a test many core sacrifices happened 18-36 steps from a
        // live king)
        if (!kv.known) {
            bool live = false;
            for (auto const& k : bb.kings)
                if (bb.kingLive(k) && k.m.klen > cr.P.stubMax && k.m.kid != myId) live = true;
            if (!V10K_V10FEED_FALLBACK || live) kp.over = kp.noV10Feed = true;
        }
        // World::know.longest: the core's king. Stamped with the claim's round (not r: a dead king's claim must age out of
        // the core too), and written only while the library follows that king; afterwards the core's own view is rebuilt.
        if (kp.selfKing) {
            w.know.longest = {w.head, preL, myId & 4095, r};
            lastWroteKid = -1;
        } else if (kv.known && kp.role != R_NONE) {
            int kc = kv.visible ? b.id(kv.kx, kv.ky) : b.id(kv.cx, kv.cy);
            w.know.longest = {kc, std::max(kv.klen, preL + 1), kv.kid & 4095, kv.round};
            lastWroteKid = kv.kid & 4095;
        } else if (lastWroteKid >= 0) {
            if (w.know.longest.cell < 0 || w.know.longest.id == lastWroteKid) rebuildLongest(w, mp);
            lastWroteKid = -1;
        }
    }

    // the core's own LONGEST view without the library king written last turn: myself, the teammates in view and the
    // facts heard (World::hear's noteLongest calls, within longestMaxAge).
    void rebuildLongest(World& w, Params const& mp) {
        comms::Knowledge& K = w.know;
        K.longest = {-1, 0, -1, -1};
        K.noteLongest({w.head, preL, myId & 4095, r});
        for (World::Seen const& s : w.others)
            if (s.team == myTeam) K.noteLongest({s.head, s.visible, s.id & 4095, r});
        for (auto const& e : K.friends)
            if (r - e.round <= mp.longestMaxAge && e.id != lastWroteKid) K.noteLongest(e);
    }

    // FEED role and the D5 / D5' arms, with the core's World.
    void feedDecide(World const& w, kg::CrownInput const& in, Plan& kp) {
        Board const& b = w.board;
        int kx = kv.visible ? kv.kx : kv.cx, ky = kv.visible ? kv.ky : kv.cy;
        kg::FeedCtx c;
        c.r = r;
        c.myId = myId;
        c.L = preL;
        c.U = U;
        c.uFloor = kUFloor;
        c.hx = in.hx;
        c.hy = in.hy;
        c.role = cr.role;
        c.dK = bb.g.man(in.hx, in.hy, kx, ky);
        c.convSince = convSince;
        if (kv.visible) {
            int kc = b.id(kx, ky);
            for (int dy = -3; dy <= 3; dy++)
                for (int dx = -3; dx <= 3; dx++) {
                    int t = b.id(kx + dx, ky + dy);
                    if (w.visible(t) && w.seenPearl[t]) c.backlog++;
                }
            int bigNeed = std::max(2, kv.klen / 3);
            for (World::Seen const& s : w.others) {
                if (s.team != myTeam) {
                    if (b.cheb(s.head, kc) <= 4) c.enemyNearKing = true;
                    if (b.cheb(s.head, w.head) <= 4) c.enemyNearMe = true;
                } else if (s.id != kv.kid && s.visible >= bigNeed && bb.g.man(b.X(s.head), b.Y(s.head), kx, ky) <= 8) {
                    c.bigOwnNearKing++;
                }
            }
        } else {
            for (World::Seen const& s : w.others)
                if (s.team != myTeam && b.cheb(s.head, w.head) <= 4) c.enemyNearMe = true;
        }
        c.myPiQ = 15;  // the core has no policy net for this: the in-vision order (f) goes by distance, then id
        kg::FeederSeen others[kVis];
        int no = 0;
        for (int i = 0; i < nAlly; i++) {
            kg::SelfFact const* s = bb.selfOf(allyIds[i], kg::kSelfTtl);
            if (!s || s->m.task != kg::T_FEED) continue;
            kg::FeederSeen& f = others[no++];
            f.id = allyIds[i];
            f.dist = bb.g.man(s->m.x, s->m.y, kx, ky);
            f.piQ = (s->m.tgt >> 4) & 15;
            f.ready = (s->m.tgt & 8) != 0;
        }
        c.others = others;
        c.nOthers = no;
        traceDK = c.dK;
        if (!kg::feedEligible(fp, c, kv)) {
            int h = kv.h < 1 ? 1 : kv.h;
            if (kv.mode >= kg::MODE_CONVERGE && h < 2) h = 2;
            traceWhyNot = (c.role == kg::CR_HEIR || c.role == kg::CR_BACKUP) ? 4
                        : (c.L > kg::lmaxNow(fp, h, kv.mode, c.r, c.convSince)) ? 5
                        : (c.U <= c.uFloor) ? 7
                        : (kv.h < 1 && kv.mode < kg::MODE_CONVERGE) ? 8 : 6;
            return;
        }
        traceElig = 1;
        kg::FeedDecision d;
        if (fp.armD5 && fp.armD5p) {
            d = kg::feedDeathD5(fp, c, kv, bb.g);
            if (!d.die && !d.retreat) {
                kg::FeedDecision d2 = kg::feedDeathD5p(fp, c, kv);
                if (d2.die) d = d2;
            }
        } else {
            d = fp.armD5p ? kg::feedDeathD5p(fp, c, kv) : kg::feedDeathD5(fp, c, kv, bb.g);
        }
        traceFeed = d.die ? 99 : d.why + (d.ready ? 10 : 0);
        kp.role = R_FEED;
        selfTask = kg::T_FEED;
        selfTgt = kg::feedTgt(c.myPiQ, d.ready);
        // the last check before a no-action turn
        bool ok = V10K_FEED && r <= 497 && r >= V10K_FEED_FROM && cr.role != kg::CR_KING && cr.role != kg::CR_HEIR &&
                  cr.role != kg::CR_BACKUP && preL >= 2 && U > kUFloor;
        // the king alive: in view, or its claim fresh (without this, dead kings up to 30 rounds old were fed)
        bool alive = kv.visible || r - kv.round <= V10K_EXEC_AGE;
        // U floor across simultaneous executions: U is the round-start count and every feeder sees the same one (in
        // tests U fell below the floor, and up to 5 executions happened in one round). nF FEED-ready teammates heard
        // in the last 7 rounds compete for U - uFloor slots; with too few slots each executes with that share.
        bool slot = true;
        if (V10K_EXEC_THROTTLE) {
            int nF = 0;
            for (auto const& s : bb.selves)
                if (s.valid && r - s.round <= kg::kSelfTtl && s.m.id != (myId & 0xFFF) && s.m.task == kg::T_FEED &&
                    (s.m.tgt & 8))
                    nF++;
            int head = U - kUFloor;
            if (nF + 1 > head)
                slot = head > 0 &&
                       kg::splitmix64((static_cast<uint64_t>(myId) << 20) ^ static_cast<uint64_t>(r) ^ 0xFEEDull) %
                                   static_cast<uint64_t>(nF + 1) <
                           static_cast<uint64_t>(head);
        }
        if (d.die && ok && alive && slot) kp.execute = true;
        else if (d.die) traceFeed = !ok ? 98 : (!alive ? 97 : 96);
    }

    // ------------------------------------------------------------------------------------------------ ACTIVE: speak
    void speak(World& w, Choice const& c, Params const& P, Policy const& pol, Out& out, double turnStart) {
        w.crossedPortal = false;  // main.cpp's sonar block did this
        Board const& b = w.board;
        bool headKnown = c.split || (c.sprint.empty() && c.dest >= 0);
        int postL = postLenOf(c, preL);
        noteEat(c);
        if (cr.amKing() && headKnown && !c.split && c.eat)
            for (int i = 0; i < nDrops; i++)
                if (drops[i].cell == c.dest) ks.noteCorpsePearl(r);
        cr.afterAction(postL);
        if (cr.ev.abdicated) abdicatedAt = r;
#if BC_FALLBACK95
        // BC_FALLBACK95: the guard's own clock read (no new read) also reports the 95M line; past it, no rays (the
        // guard at 90M of the turn already skips them)
        double fbNow = 0;
        if (!headKnown || (fbNow = clockNow()) - turnStart > V10K_CPU_GUARD) {  // an unknown landing, or no time: no rays
            if (headKnown && fb95::past(fbNow)) fb95::mark(out, "speak");
            trace(out, postL, 0);
            return;
        }
#else
        if (!headKnown || clockNow() - turnStart > V10K_CPU_GUARD) {  // an unknown landing, or no time: no rays
            trace(out, postL, 0);
            return;
        }
#endif
        int newHead = c.split ? w.head : c.dest;
        std::vector<int> own, child;
        if (c.split) {
            for (size_t i = 0; i < preBody.size(); i++) (static_cast<int>(i) < postL ? own : child).push_back(preBody[i]);
        } else {
            own = w.body;
            if (own.empty() || own[0] != newHead) own.insert(own.begin(), newHead);
        }
        Env env{&w, &own, &child, myTeam};
        int neck = own.size() > 1 ? own[1] : -1;
        kg::RayPred pred[4];
        for (int d = 0; d < 4; d++) {
            if (neck >= 0 && b.nb[newHead * 4 + d] == neck) {
                pred[d].end = kg::RE_NECK;
                continue;
            }
            pred[d] = kg::predictRay(env, newHead, d);
        }
        kg::PlanInput pi;
        pi.r = r;
        pi.myId = myId;
        pi.myLen = postL;
        pi.hx = b.X(newHead);
        pi.hy = b.Y(newHead);
        pi.amKing = cr.amKing();
        pi.myKing = cr.kingMsg(postL, pi.hx, pi.hy, so.sigma, hOut, busy, mode);
        int eats = 0;
        for (int i = 0; i < 32; i++)
            if (eatRound[i] > r - 16 && eatRound[i] <= r) eats += eatRing[i];
        pi.self.task = selfTask;
        pi.self.tgt = selfTgt;
        pi.self.load = eats == 0 ? 0 : (eats <= 2 ? 1 : (eats <= 5 ? 2 : 3));
        pi.self.thr = thrNow ? 1 : 0;
        kg::RayOut ro[4];
        // planRays gives EKING / THREAT a ray before SELF; v10k drops those types, and with them the SELF that ray
        // displaced (a third to two thirds of turns). Hide them from the planner for this call.
        bool ekValid[kg::Blackboard::kEKings], thValid[kg::Blackboard::kThreats];
        for (int i = 0; i < kg::Blackboard::kEKings; i++) ekValid[i] = bb.ekings[i].valid, bb.ekings[i].valid = false;
        for (int i = 0; i < kg::Blackboard::kThreats; i++) thValid[i] = bb.threats[i].valid, bb.threats[i].valid = false;
        int n = kg::planRays(bb, pred, pi, ro);
        for (int i = 0; i < kg::Blackboard::kEKings; i++) bb.ekings[i].valid = ekValid[i];
        for (int i = 0; i < kg::Blackboard::kThreats; i++) bb.threats[i].valid = thValid[i];
        // A non-king forwards the king it follows (V10K_RECENT), not the library's longest claim of the last 20
        // rounds: after a forced reverse the dead parent's claim (2 longer than its child's) was forwarded instead of
        // the live successor's, so far dragons never heard the successor (in a test a dead king was followed 35
        // rounds on). With a king followed, every KING the planner chose is a claim (tombstones go out only without
        // one).
        if (!cr.amKing() && kv.known && V10K_RECENT < 20)
            if (kg::KingFact const* f = bb.kingSlot(kv.kid))
                if (f->m.klen > cr.P.stubMax) {
                    uint64_t wire = kg::seal(bb.team, r, kg::M_KING, kg::encKing(f->m));
                    for (int i = 0; i < n; i++)
                        if (ro[i].type == kg::M_KING) ro[i].wire = wire;
                }
        bool useful[4], used[4] = {false, false, false, false};
        int nUseful = 0;
        for (int d = 0; d < 4; d++) {
            useful[d] = pred[d].end == kg::RE_ALLY || pred[d].end == kg::RE_OUT;
            nUseful += useful[d];
        }
        int nLib = 0;
        if (cr.amKing()) {  // the king: every useful ray carries its claim; no core facts
            for (int i = 0; i < n; i++) {
                out.sonar(ro[i].dir, ro[i].wire);
                used[ro[i].dir] = true;
                nLib++;
            }
            trace(out, postL, nLib);
            return;
        }
        // the core's facts this turn (not SELF / LONGEST: the library's SELF and KING replace them)
        std::vector<comms::Fact> facts;
        if (V10K_SONAR && P.sonarOn && r >= P.sonarFrom) {
            TeamPlan const& tp = pol.team();
            int lead = (!tp.members.empty() && tp.leaderId == (myId & 4095)) ? static_cast<int>(tp.members.size()) : 1;
            for (comms::Fact const& f : w.tell(P, lead))
                if (f.type != comms::SELF && f.type != comms::LONGEST) facts.push_back(f);
        }
        int cap = 4;
        if (!facts.empty()) cap = std::min(V10K_LIB_RAYS, std::max(1, nUseful - 1));
        int maxKing = 4;
        if (V10K_SONAR == 2) {
            maxKing = 1;
            if (!facts.empty()) cap = std::min(cap, 2);
        }
        // An abdicated stub keeps announcing its stub claim for V10K_STUB_RELAY rounds, ALLY rays first (up to 2): its
        // abdication ray often ran along its own neck, so its child (the long tail) never heard the handover and
        // waited 20-40 rounds to claim, or held the old claim and stayed blocked by (c3).
        if (V10K_STUB_RELAY > 0 && r - abdicatedAt >= 1 && r - abdicatedAt <= V10K_STUB_RELAY && postL <= cr.P.stubMax) {
            uint64_t wire =
                kg::seal(bb.team, r, kg::M_KING, kg::encKing(cr.kingMsg(postL, pi.hx, pi.hy, so.sigma, 0, 0, mode)));
            int sent = 0;
            for (int pass = 0; pass < 2; pass++)
                for (int d = 0; d < 4 && sent < 2 && nLib < cap; d++) {
                    if (!useful[d] || used[d] || (pred[d].end == kg::RE_ALLY) != (pass == 0)) continue;
                    out.sonar(d, wire);
                    used[d] = true;
                    nLib++;
                    sent++;
                    nStubSent++;
                }
        }
        // the library's rays of a non-king, KING and SELF only: a new child's KING (1) > the first forwarded KING >
        // SELF > the second forwarded KING > other children's KINGs
        int kind[4];
        for (int i = 0; i < n; i++) {
            kind[i] = -1;
            if (ro[i].type == kg::M_SELF) {
                kind[i] = 2;
            } else if (ro[i].type == kg::M_KING) {
                kg::RayPred const& p = pred[ro[i].dir];
                bool child = p.end == kg::RE_ALLY && (p.allyVisLen <= 3 || !bb.selfOf(p.allyId, kg::kSelfTtl));
                kind[i] = child ? 0 : 1;
            }
        }
        int pick[8], np = 0;
        auto nth = [&](int k, int which) {
            int seen = 0;
            for (int i = 0; i < n; i++)
                if (kind[i] == k && seen++ == which) return i;
            return -1;
        };
        int a;
        if ((a = nth(0, 0)) >= 0) pick[np++] = a;
        if ((a = nth(1, 0)) >= 0) pick[np++] = a;
        if ((a = nth(2, 0)) >= 0) pick[np++] = a;
        if ((a = nth(1, 1)) >= 0) pick[np++] = a;
        for (int j = 1; j < 4; j++)
            if ((a = nth(0, j)) >= 0) pick[np++] = a;
        int kings = 0, selfSent = 0;
        for (int j = 0; j < np && nLib < cap; j++) {
            kg::RayOut const& o = ro[pick[j]];
            if (used[o.dir]) continue;
            if (o.type == kg::M_KING && kings >= maxKing) continue;
            kings += o.type == kg::M_KING;
            selfSent += o.type == kg::M_SELF;
            out.sonar(o.dir, o.wire);
            used[o.dir] = true;
            nLib++;
        }
        if (V10K_TRACE) {  // rays: useful, planned, SELF planned / sent, KING planned, library rays sent, stub rays
            int selfIn = 0, kingIn = 0;
            for (int i = 0; i < n; i++) selfIn += ro[i].type == kg::M_SELF, kingIn += ro[i].type == kg::M_KING;
            char b2[112];
            std::snprintf(b2, sizeof b2, "LOG kd %d %d %d 0 %d %d %d %d %d\n", r, nUseful, n, selfIn, selfSent, kingIn,
                          nLib, nStubSent);
            out.buf += b2;
        }
        // the core's facts on the remaining useful rays (never towards an enemy, ourselves, the neck or kelp: the core's frames are
        // plain text with a MAC, and an enemy hit reads our positions)
        size_t fi = 0;
        for (int k = 0; k < 4 && fi < facts.size(); k++) {
            int d = (k + r) & 3;
            if (!useful[d] || used[d]) continue;
            out.sonar(d, comms::encode(facts[fi++], myTeam, r));
            used[d] = true;
        }
        trace(out, postL, nLib);
    }

    void trace(Out& out, int postL, int nLib) {
        if (!V10K_TRACE) return;
        char buf[200];
        std::snprintf(buf, sizeof buf,
                      "LOG kt %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n", r,
                      traceRole, static_cast<int>(cr.role), postL, traceElig, traceFeed, traceWhyNot, traceDK, mode,
                      kv.known ? kv.kid : (cr.amKing() ? myId : -1), kv.known ? kv.klen : (cr.amKing() ? postL : 0),
                      cr.amKing() ? hOut : kv.h, nLib, nPhantom, static_cast<int>(cr.ev.why), cr.candSince, U, traceUE,
                      traceELo, traceC25, kv.known ? r - kv.round : -1, kv.visible ? 1 : 0, traceSelfKing, nRefresh,
                      nQuarOk);
        out.buf += buf;
    }
    void traceExec(Out& out) {
        if (!V10K_TRACE) return;
        char buf[128];
        std::snprintf(buf, sizeof buf, "LOG kx %d %d %d %d %d %d %d %d %d %d\n", r, preL, traceDK, kv.kid, kv.klen, U,
                      r - kv.round, kv.visible ? 1 : 0, kv.h, kv.mode);
        out.buf += buf;
    }
};

// ---------------------------------------------------------------------------------------------------- entry points
static bool g_active = false;   // zero-initialised: no constructor before main
static Adapter* g_adapter = nullptr;
alignas(16) static unsigned char g_store[sizeof(Adapter)];

inline Adapter& K() {
    if (!g_adapter) g_adapter = ::new (static_cast<void*>(g_store)) Adapter();
    return *g_adapter;
}
// Maps up to 64 x 64 and ids below 4096 (the protocol's fields); otherwise the adapter stays OFF.
inline bool supported(World const& w) {
    return w.board.W <= 64 && w.board.H <= 64 && w.init.id >= 0 && w.init.id < 4096;
}

// ACTIVE, before the search: the library's turn; P points at this turn's overridden copy when the role needs one.
__attribute__((noinline)) inline void pre(World& w, Params const& mapParams, Plan& kp, Params const*& P, Out& out) {
    Adapter& a = K();
    a.pre(w, mapParams, kp, out);
    if (kp.over) {
        a.pOver = mapParams;
        apply(kp, w.t.round, a.pOver);
        P = &a.pOver;
    }
}
// LISTEN, after the search and the action (the core's own sonar already written).
__attribute__((noinline)) inline void listen(World& w, Choice const& c, Out& out) {
    if (!supported(w)) return;
    if (K().listen(w, c, out)) g_active = true;
}
// ACTIVE, after the action: rays.
__attribute__((noinline)) inline void speak(World& w, Choice const& c, Params const& P, Policy const& pol, Out& out,
                                            double turnStart) {
    K().speak(w, c, P, pol, out, turnStart);
}
// ACTIVE, an execution turn (no action, no rays).
__attribute__((noinline)) inline void executing(Out& out) { K().traceExec(out); }
#if V10K_TRACE
// Analysis builds: the core's own feeding, reverse, sprint-trade, trapped and split actions from R0 (LISTEN and ACTIVE).
__attribute__((noinline)) inline void traceV10(Out& out, int round, int L, Choice const& c, Plan const& kp) {
    if (round < V10K_R0 || !(c.split || c.why == "feed" || c.why == "reverse" || c.why == "sprint-trade" || c.why == "trapped"))
        return;
    char buf[128];
    std::snprintf(buf, sizeof buf, "LOG vw %d %s%s %d %d %d %d %d\n", round, c.split ? "S:" : "", c.why.c_str(), L,
                  kp.active ? 1 : 0, kp.role, c.n, g_active ? 1 : 0);
    out.buf += buf;
}
#endif

}  // namespace bc::v10k
