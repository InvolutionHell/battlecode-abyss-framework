#pragma once

#include "nav.hpp"
#include "team.hpp"
#include "world.hpp"
#if BC_FIGHT
#include "fight.hpp"
#include "qsk.hpp"
#endif
#if BC_RL_PRIOR
#include "rl_net.hpp"
#endif
#if BC_R4NET
#include "r4net.hpp"
#endif
#if BC_R6
#include "r6.hpp"
#endif
#if BC_R6 >= 2
#include "r6net.hpp"
#define BC_R4_WEFF (r6w_)  // BC_R6 (inactive, headers not shipped): the prior weight of this turn's net (BC_R6_W with a class net, else BC_R4_W)
#else
#define BC_R4_WEFF BC_R4_W
#endif
#if BC_T64_FIELD
#define BC_R4_WEFF_NOW t64w  // BC_T64_FIELD (inactive): this turn's weight - BC_T64_W in the field of a fast bed, else BC_R4_WEFF (t64w is set where the move prior is applied)
#else
#define BC_R4_WEFF_NOW BC_R4_WEFF
#endif
#if BC_R5_GATE
#define BC_R5_GATEOK(p) r5Gate(p)
#else
#define BC_R5_GATEOK(p) ((p) >= BC_R4_SPRINTP)
#endif

namespace bc {

struct Choice {
    bool split = false;
    int dir = N;
    int n = 0;
    int dest = -1;
    bool eat = false;
    double score = -1e18;
    std::string why;
    // search bookkeeping
    std::string sprint;      // multi-step MOVE (a sprint trade), empty for one step
    std::vector<int> flee;   // queen flee: the tiles of the multi-step MOVE in `sprint` (the body follows them)
    std::vector<char> fleeEat;  // ... and whether each step eats
    int fleePaid = 0;        // ... segments paid (steps beyond the free ceil(L/4))
    bool terminal = false;   // trade or vetoed: nothing to search past it
    double stepTerm = 0;     // what this step alone earns (eat bonus minus danger)
    int newL = 0;
};

// Roles are decided locally every turn from shared facts, so teammates agree
// without talking:
//   grower  - a starting dragon, or anyone long. Farms, buds off short children, avoids fights.
//   swarm   - everyone else. Escorts visible growers, forms squads on long enemies:
//             the closest swarm dragon trades heads, the next (squadSize-1) wait a few
//             tiles back and eat the ceil(L/2) pearls the dead enemy drops.
class Policy {
  public:
    int depthReached() const { return depthReached_; }
    bool v9Loud() const {
        return (BC_V9_KING && v9King_ && w_.t.round >= p_.kStreamFrom - 80) || (BC_V9_EKING && kingMe_ && v9KingNow_);
    }
    std::string v9Info() const {
        auto const& K = w_.know.longest;
        return "king=" + std::to_string(v9King_) + " feeder=" + std::to_string(v9Feeder_) + " K=" +
               std::to_string(K.id) + ":" + std::to_string(K.len) + "@" + std::to_string(w_.t.round - K.round) +
               " way=" + std::to_string(v9Way_) + " " + qdbg_;
    }
    std::string qdbg_;  // debug builds (BC_DEBUG): the queen's root moves
    int qskTier_ = 0;
    int idleBedCell_ = -1;  // BC_ECON_BED idle-to-bed: the bed an idle short dragon heads for this turn, -1 none
#if BC_HARVEST
    struct HRoom { int entry, dir, land; std::vector<int> tiles; };
    std::vector<HRoom> hRooms_;  // BC_HARVEST: this turn's harvest rooms
    int harvestEntry_ = -1, harvestDir_ = -1, harvestLand_ = -1;  // ... the entry this dragon heads for, -1 none
    int harvestIn_ = -1;  // ... the room its head is in (an index into hRooms_), -1 none
#endif
#if BC_EXPAND
    int expandCell_ = -1;   // opening expansion: the bed (or unseen tile) an idle dragon heads for to contest a core, -1 none
#endif
#if BC_EXPAND_OPENING
    int eoMirror_ = -1;     // open-mode MIRROR_TARGET: the band (a mirrored bed) this dragon heads for, -1 none
    int eoHunter_ = -1;     // open-mode ANTIHUNT: the hunter's index in enemies_, -1 none
#endif
    bool lock_ = false, desperate_ = false;  // queen modes (LOCK / DESPERATE)
    // LOCK's no-trade rule: everybody (BC_QUEEN_LOCKALL) or only the escorts (the queen never trades anyway)
    bool lockNoTrade() const { return lock_ && (BC_QUEEN_LOCKALL || escortCell_ >= 0); }
    int escortCell_ = -1;                     // queen escort: the shield tile, -1 none
    std::vector<int> homeScore_;              // queen home map: per tile min(enemy dist, 30) - min(friend dist, 30), the queen's
    int growHead_ = -1;
#if BC_QUEEN_GROWUP
    bool growMe_ = false;  // queen growth mode: I am our queen and she grows (World::qGrow)
#endif
    int guardQh_ = -1, guardQL_ = 0;
#if BC_QUEEN_GROWFIRST
    // BC_QUEEN_GROWFIRST: I am our queen, before kGrowFirstUntil and shorter than kGrowFirstLen
    bool queenGrows() const { return BC_QUEEN && queenMe_ && w_.t.round < kGrowFirstUntil && L_ < kGrowFirstLen; }
#endif
#if BC_QUEEN_BOXGUARD & 1
    int boxExit_ = -1;  // BC_QUEEN_BOXGUARD: our queen's last free side (she in view with exactly one), -1 none: never ours, even trapped
#if BC_ECON_TRACE
    bool boxSacrifice_ = false;  // BC_QUEEN_BOXGUARD trace: this turn's shared-exit fallback skipped her last exit
#endif
    void planBoxGuard() {
        boxExit_ = -1;
#if BC_ECON_TRACE
        boxSacrifice_ = false;
#endif
        if (!BC_QUEEN || queenMe_) return;
        Board const& b = w_.board;
        int qh = -1;
        for (World::Seen const& s : w_.others)
            if (!isEnemy(s) && s.id < 2) qh = s.head;
        if (qh < 0) return;
        int free = 0, last = -1;
        for (int d = 0; d < 4; d++) {
            int const n = b.nb[qh * 4 + d];
            if (n >= 0 && w_.occ[n] < 0) free++, last = n;
        }
        if (free == 1) boxExit_ = last;
#if BC_ECON_TRACE
        if (boxExit_ >= 0 && b.cheb(w_.head, boxExit_) <= 1) econTrace_ += " qb";
#endif
    }
#endif
#if BC_QUEEN_YIELD
    int yieldQh_ = -1;  // BC_QUEEN_YIELD: our queen's head when I must yield to her (my head within Chebyshev 1, her free sides <= 2), else -1
    void planYield() {
        yieldQh_ = -1;
        if (!BC_QUEEN || queenMe_) return;
        Board const& b = w_.board;
        int qh = -1;
        for (World::Seen const& s : w_.others)
            if (!isEnemy(s) && s.id < 2) qh = s.head;
        if (qh < 0 || b.cheb(w_.head, qh) > 1) return;
        int free = 0;
        for (int d = 0; d < 4; d++) {
            int const n = b.nb[qh * 4 + d];
            if (n >= 0 && w_.occ[n] < 0) free++;
        }
        if (free > 2) return;
        yieldQh_ = qh;
#if BC_ECON_TRACE
        econTrace_ += " qy" + std::to_string(free);
#endif
    }
#endif
    int kinQh_ = -1;  // kin avoidance: our queen's head in view
    TeamPlan const& team() const { return team_; }
    bool isQueen() const { return BC_QUEEN && queenMe_; }
#if BC_LONGEST_RESERVE
    void setReserve(bool r) { reserveMe_ = r; }  // BC_LONGEST_RESERVE (set by main.cpp from World::longestReserve)
#endif
#if BC_LONGEST_RESERVE >= 3
    int reserveFeedHead_ = -1;  // BC_LONGEST_RESERVE 3: the reserve's head this dragon leaves its food to (-1: not a feeder)
    // BC_LONGEST_RESERVE 3: from kReserveFrom a non-queen, non-reserve dragon is a feeder when the reserve (the fresh LONGEST
    // fact, another id; its head in view if seen) is within kFeedNear steps and fewer than two of our other heads (seen, or
    // heard within 10 rounds) are nearer to it
    void planReserveFeed() {
        reserveFeedHead_ = -1;
        if (w_.t.round < kReserveFrom || reserveMe_ || (BC_QUEEN && queenMe_) || myDist_.empty()) return;
        auto const& K = w_.know.longest;
        int const me = w_.init.id & 4095;
        if (K.cell < 0 || K.id == me || w_.t.round - K.round > kReserveAge || K.id < 2) return;
        Board const& b = w_.board;
        int head = K.cell;
        for (World::Seen const& s : w_.others)
            if (!isEnemy(s) && (s.id & 4095) == K.id) head = s.head;
        int const mine = myDist_[head];
        if (mine > kFeedNear) return;
        int nearer = 0;
        auto closer = [&](int c) { return c != w_.head && b.cheb(c, head) < b.cheb(w_.head, head); };
        for (World::Seen const& s : w_.others)
            if (!isEnemy(s) && (s.id & 4095) != K.id && closer(s.head)) nearer++;
        for (auto const& f : w_.know.friends)
            if (w_.t.round - f.round <= 10 && f.cell >= 0 && f.id != me && f.id != K.id && closer(f.cell)) nearer++;
        if (nearer < 2) reserveFeedHead_ = head;
    }
#endif
#if BC_QUEEN_FEED350
    int queenFeedHead_ = -1;  // late queen feeding: our queen's head this dragon leaves its food to (-1: not a feeder)
#if BC_QUEEN_FEED350 >= 2
    // late queen feeding: I am our queen, from kQFeedFrom and shorter than kQFeedLen
    bool queenFeeds() const { return BC_QUEEN && queenMe_ && w_.t.round >= kQFeedFrom && L_ < kQFeedLen; }
#endif
    // late queen feeding: from kQFeedFrom a non-queen is a feeder when our queen (in view, else her fact within kQFeedAge
    // rounds) is shorter than kQFeedLen, within kQFeedNear steps of it, and fewer than two of our other heads (seen, or heard
    // within kQFeedAge) are nearer to her
    void planQueenFeed() {
        queenFeedHead_ = -1;
        if (w_.t.round < kQFeedFrom || (BC_QUEEN && queenMe_) || myDist_.empty() || !queenAlive()) return;
        int const me = w_.init.id & 4095;
        int head = -1, len = 0, qid = -1;
        for (World::Seen const& s : w_.others)
            if (!isEnemy(s) && s.id < 2) head = s.head, len = s.visible, qid = s.id;
        for (auto const& f : w_.know.friends)
            if (f.id < 2 && f.cell >= 0 && w_.t.round - f.round <= kQFeedAge && (qid < 0 || f.id == qid)) {
                if (head < 0) head = f.cell, qid = f.id;
                len = std::max(len, f.len);  // her own word on her length (in view she may be cut by the window)
            }
        if (head < 0 || len >= kQFeedLen || myDist_[head] > kQFeedNear) return;
        Board const& b = w_.board;
        int nearer = 0;
        auto closer = [&](int c) { return c != w_.head && b.cheb(c, head) < b.cheb(w_.head, head); };
        for (World::Seen const& s : w_.others)
            if (!isEnemy(s) && s.id >= 2 && closer(s.head)) nearer++;
        for (auto const& f : w_.know.friends)
            if (w_.t.round - f.round <= kQFeedAge && f.cell >= 0 && f.id >= 2 && f.id != me && closer(f.cell)) nearer++;
        if (nearer < 2) queenFeedHead_ = head;
#if BC_ECON_TRACE
        if (queenFeedHead_ >= 0) econTrace_ += " qf";
#endif
    }
#endif
#if BC_UNIT_SLOT
    // BC_UNIT_SLOT: a non-queen's split cap leaves the last unit slot to the queen
    int unitCap(int cap) const { return BC_QUEEN && queenMe_ ? cap : std::min(cap, w_.init.unitLimit - 1); }
#endif
    std::string const& econTrace() const { return econTrace_; }
    std::string econTrace_;  // BC_ECON_TRACE: this turn's economy trace tags

    Policy(World& w, Params const& p, double deadline) : w_(w), p_(p), deadline_(deadline) {
        gammaPow_.resize(kPowSize);
        gammaPow_[0] = 1.0;
        for (int i = 1; i < kPowSize; i++) gammaPow_[i] = gammaPow_[i - 1] * p_.gamma;
        farPow_.resize(kPowSize);
        farPow_[0] = 1.0;
        for (int i = 1; i < kPowSize; i++) farPow_[i] = farPow_[i - 1] * p_.gammaFar;
        if (BC_SCAV) w_.scavOn = p_.wScav > 0;  // the DROPS facts go out only while the scavenger is on
        if (BC_SCAV) {
            scavPow_.resize(64);
            scavPow_[0] = 1.0;
            for (int i = 1; i < 64; i++) scavPow_[i] = scavPow_[i - 1] * 0.85;
        }
    }

#if BC_FALLBACK95
    // BC_FALLBACK95: the cheapest legal action - one step to a free neighbour (N, E, S, W; main.cpp's legality rule: no
    // wall, no other dragon, not our own body), eating a pearl seen there now; with no free neighbour, straight on (a MOVE
    // that may die, never no action)
    Choice fb95Move() const {
        Board const& b = w_.board;
        Choice c;
        c.why = "fb95";
        for (int d = 0; d < 4; d++) {
            int n = b.nb[w_.head * 4 + d];
            if (n < 0 || w_.occ[n] >= 0 || std::find(w_.body.begin(), w_.body.end(), n) != w_.body.end()) continue;
            c.dir = d;
            c.dest = n;
            c.eat = w_.seenRound[n] == w_.t.round && w_.seenPearl[n];
            return c;
        }
        c.dir = w_.t.dir;
        c.dest = -1;  // main.cpp: an unknown landing (the next turn's view shows it)
        return c;
    }
#endif

    Choice decide() {
        Board const& b = w_.board;
        L_ = w_.t.length;
        if (BC_ECON_TRACE) econTrace_.clear();
        // Survival reproduction stops once the feeding phase starts: feeding thins the army, and rebuild splits then shaved the
        // longest dragon over and over. The few-units no-trade rule still holds to the end (tradeOk).
        // BC_QUEEN_QKILL: before anything else, any of ours but the queen takes the enemy queen when it can reach her head this
        // turn, and guards our queen from a striker that moves after us.
        // Queen modes (witnessed only): LOCK - their queen dead, ours alive: no hunting, no trades, guards; DESPERATE - ours
        // dead, theirs alive or unknown: everybody hunts.
        lock_ = BC_QUEEN && BC_QUEEN_MODES && w_.eq.dead && !w_.ourQueenDead;
        desperate_ = BC_QUEEN && BC_QUEEN_MODES && w_.ourQueenDead && !w_.eq.dead;
        if (BC_QUEEN && BC_QUEEN_QKILL && w_.init.id >= 2) {
            Choice k;
            if (queenKill(k) || queenGuard(k)) return k;
            if (BC_QUEEN_HUNT && !lock_ && queenPursue(k)) return k;
        }
        rebuild_ = w_.t.round < std::min(kRebuildStop, BC_V9_KING ? 500 - p_.kWindow : p_.feedRound) &&
                   (w_.t.units <= kSurvivalUnits || (w_.isolated() && w_.t.units <= kIsolatedUnits));
        computeTactics();
        grower_ = isGrower(w_.init.id, L_);
        // Dragon plans, guard: from guardFrom the longest dragon we know of (ourselves included) is the king.
        kingMe_ = BC_KINGGUARD && w_.t.round >= p_.guardFrom && L_ >= 4 &&
                  (w_.know.longest.cell < 0 || w_.know.longest.id == (w_.init.id & 4095) || L_ >= w_.know.longest.len);
        // BC_V9_KING / BC_V9_EKING: whether we are the king (the late king and the early king share the rule)
        v9KingNow_ = (BC_V9_KING || BC_V9_EKING) && v9IsKing();
        // queen rule: round 500 goes to the longer queen first, so the queen is the king from round 0 and nobody else is
        queenMe_ = BC_QUEEN && w_.init.id >= 0 && w_.init.id < 2;  // ids 0 and 1 are the two queens, one per team
        if (BC_QUEEN && BC_QUEEN_KING) kingMe_ = v9KingNow_ = queenMe_;
        if (BC_V9_EKING && v9KingNow_ && w_.t.round >= p_.ekFrom && L_ >= std::max(4, p_.ekMinLen)) {
            kingMe_ = true;
            if (p_.ekGamma > 0) {  // the king looks further for its food
                gammaPow_[0] = 1.0;
                for (int i = 1; i < kPowSize; i++) gammaPow_[i] = gammaPow_[i - 1] * p_.ekGamma;
            }
        }
        kingHead_ = -1;
        growHead_ = -1;
        guardQh_ = -1;
        guardQL_ = 0;
        kinQh_ = -1;
        if (BC_QUEEN && BC_QUEEN_KINAVOID && !queenMe_)
            for (World::Seen const& s : w_.others)
                if (!isEnemy(s) && s.id < 2) kinQh_ = s.head;
        if (BC_QUEEN && (BC_QUEEN_ROOMGUARD || BC_QUEEN_BREATHE > 0) && !queenMe_)  // queen room guard: our queen in view, for the friend discipline
            for (World::Seen const& s : w_.others)
                if (!isEnemy(s) && s.id < 2) guardQh_ = s.head, guardQL_ = s.visible;
#if BC_QUEEN_BOXGUARD & 1
        planBoxGuard();
#endif
#if BC_QUEEN_YIELD
        planYield();
#endif
        if (BC_QUEEN && BC_QUEEN_GROW && !queenMe_)  // queen growth mode (BC_QUEEN_GROW, off): our queen in view while she is short - her food is hers
            for (World::Seen const& s : w_.others)
                if (!isEnemy(s) && s.id < 2 && s.visible < kGrowTo) growHead_ = s.head;
#if BC_QUEEN_GROWUP
        planGrowUp();
#endif
        kingZone_ = -1;
        if (BC_V9_EKING && (p_.ekYield > 0 || p_.ekZone > 0) && !v9KingNow_ && (w_.t.round >= p_.ekFrom || (BC_QUEEN && BC_QUEEN_ZONE))) {
            // the others leave the king's food to it (scoreFrom: pearls within ekYield of its head)
            auto const& K = w_.know.longest;
            for (World::Seen const& s : w_.others)
                if (!isEnemy(s) && (BC_QUEEN && BC_QUEEN_ZONE ? s.id < 2 : (s.id & 4095) == K.id && s.visible >= p_.ekMinLen))
                    kingHead_ = s.head;  // queen: her food is hers whatever her length
            if (p_.ekZone > 0) kingZone_ = kingHead_;
            if (p_.ekYield <= 0) kingHead_ = -1;
        }
        feedHead_ = -1;
        feedPull_ = -1;
        // A fresh child's setup (map load, pearl re-simulation) can eat most of its
        // first turn late in the game: then play a lean one-step turn.
#if BC_FALLBACK95
#if BC_T13_BURN
        if (w_.t.round % 10 == 3) fb95::burn("policy");
#endif
        {   // BC_FALLBACK95: the same clock read decides the 95M fallback (no new read before the search)
            double now = clockNow();
            lean_ = now > deadline_ - p_.leanMargin;
            if (fb95::past(now)) {
                if (!fb95::hit) fb95::hit = "policy";
                return fb95Move();
            }
        }
#else
        lean_ = clockNow() > deadline_ - p_.leanMargin;
#endif
#if BC_RL_PRIOR
        // Opening prior (BC_RL_PRIOR, rl_net.hpp): the network gives a move prior; it runs before the search, so the search's
        // clock accounts for it.
        rlOn_ = rlSplit_ = false;
#if BC_R4NET
        // Move-prior net (BC_R4NET, r4net.hpp): every dragon but the queen takes its move prior from this net (the queen keeps the
        // opening prior). With BC_R4_SPLIT_ON this net also gives the split trigger and the opening net is not built in those
        // processes; without it the opening net still runs for the split.
        r4On_ = r4SprOn_ = r4CullOn_ = false;
        bool const useR1 = queenMe_ || !BC_R4_SPLIT_ON;
#else
        bool const useR1 = true;
#endif
        if (useR1 && !lean_ && w_.t.round < p_.rlUntil && (p_.wPrior > 0 || p_.rlSplitP <= 1.0)) {
            static RLNet net;
            net.run(w_.init, w_.t);
            for (int k = 0; k < 9; k++) rlProb_[k] = net.prob[k];
            rlOn_ = true;
            rlSplit_ = net.prob[8] >= p_.rlSplitP;
        }
#if BC_R4NET
        if (!queenMe_ && !lean_) {
#if BC_R6
            static R6Select r6s;
            r6s.update(w_);
            r6c_ = r6s.cls;
            if (BC_ECON_TRACE && r6s.judgedAt == w_.t.round)  // BC_R6 trace (inactive): class, tiles seen, kelp / deg3 / portal (x1000 / x1000 / x100), the squared distances x10
                econTrace_ += " r6=" + std::to_string(r6s.cls) + "," + std::to_string(r6s.seen) + "," + std::to_string(static_cast<int>(std::lround(r6s.f[1] * 1000))) + "," +
                              std::to_string(static_cast<int>(std::lround(r6s.f[2] * 1000))) + "," + std::to_string(static_cast<int>(std::lround(std::expm1(r6s.f[3]) * 100))) + "," + std::to_string(static_cast<int>(std::lround(r6s.f[4] * 1000))) + "," + std::to_string(static_cast<int>(std::lround(r6s.f[5] * 1000))) + "," +
                              std::to_string(static_cast<int>(std::lround(std::min(r6s.dA, 999.0) * 10))) + "," + std::to_string(static_cast<int>(std::lround(std::min(r6s.dB, 999.0) * 10)));
#endif
            bool calm = true;  // BC_R4_CALM 1: the move prior only while no enemy part is in view (foraging / positioning, never a fight)
            if (BC_R4_CALM >= 1)
                for (Part const& pt : w_.t.parts)
                    if (pt.team != w_.init.team) {
                        calm = false;
                        break;
                    }
            bool const moveWin = calm && w_.t.round >= BC_R4_FROM && w_.t.round < BC_R4_UNTIL;
#if BC_R5_RLUNTIL > 0
            bool const splitWin = BC_R4_SPLIT_ON && w_.t.round < static_cast<double>(BC_R5_RLUNTIL);  // BC_R5_RLUNTIL: the net's split trigger window to this round (default: p_.rlUntil = 200)
#else
            bool const splitWin = BC_R4_SPLIT_ON && w_.t.round < p_.rlUntil;  // the split trigger of the opening-net slot, from the move-prior net (natural split rates)
#endif
            bool const sprintWin = BC_R4_SPRINT && w_.t.round >= BC_R4_FROM && w_.t.round < BC_R4_UNTIL;
            bool const cullWin = BC_R4_CULL && w_.t.round >= BC_R4_CULLFROM && L_ >= 2 && L_ <= BC_R4_CULLMAXLEN;
#if BC_R4_NOISE
            // control setting (BC_R4_NOISE 1, with BC_R4_SPLIT_ON 0 and no sprint; off): a deterministic pseudo-random move prior in
            // place of the net - measures what the net's knowledge adds over breaking the search's ties by anything but the first
            // index (the opening net still gives the split trigger below rlUntil)
            if (moveWin) {
                uint32_t h = static_cast<uint32_t>(w_.t.round) * 2654435761u ^ static_cast<uint32_t>(w_.init.id) * 40503u ^ static_cast<uint32_t>(w_.head) * 2246822519u;
                h ^= h >> 15;
                h *= 2246822519u;
                h ^= h >> 13;
                for (int rel = 0; rel < 4; rel++) r4Move_[rel] = 0.1f + static_cast<float>((h >> (8 * rel)) & 255) / 255.0f;
                r4On_ = true;
            }
            if (false) {
                static F1Net net4;
                (void)net4;
#else
            if (moveWin || splitWin || sprintWin || cullWin) {
#if BC_R6 >= 2
                R6Out net4;  // the F1Net members read below, filled from this dragon's class net (BC_R6, inactive) or the default move-prior net
                r6Run(net4);
#else
                static F1Net net4;
                net4.run(w_.init, w_.t);
#endif
                if (moveWin) {
                    for (int rel = 0; rel < 4; rel++) r4Move_[rel] = net4.moveProb(rel);
                    r4On_ = true;
                }
                if (splitWin) rlSplit_ = net4.splitProb() >= BC_R4_SPLITP;
                if (sprintWin) {  // P(more than one step | the first step goes to rel): the demonstrations' free-step habit
                    for (int rel = 0; rel < 4; rel++) {
                        double const mv = net4.moveProb(rel);
                        r4Spr_[rel] = mv > 1e-6 ? (net4.prob[F1Net::MOVE2 + rel] + net4.prob[F1Net::MOVE3 + rel]) / mv : 0.0;
                    }
                    for (int k = 0; k < 4; k++) r4S2_[k] = net4.step2[k];
                    r4SprOn_ = true;
                }
                if (cullWin) {
                    r4None_ = net4.prob[F1Net::NONE];
                    r4CullOn_ = true;
                }
#endif
            }
        }
#endif
#endif
        buildBase();
        buildTargets();
        if (!lean_) buildFriendFields();
        else noFriendFields();
        planTeam();
#if BC_MN_VALUE
        planValue();
#endif
#if BC_OPEN_EXPAND
        planOpenExpand();
#endif
#if BC_LONGEST_RESERVE >= 3
        planReserveFeed();
#endif
#if BC_QUEEN_FEED350
        planQueenFeed();
#endif
#if BC_ECON_TRACE && BC_MN_FACTS
        // BC_MAPNET_EVIDENCE trace: the PROBING timeouts of this turn - "tI x.y.d" into a TRAP, "tL x.y.d" lapsed
        for (int k : w_.mn.inferredNow) econTrace_ += " tI" + std::to_string(w_.board.X(k / 4)) + "." + std::to_string(w_.board.Y(k / 4)) + "." + std::to_string(k & 3);
        for (int k : w_.mn.lapsedNow) econTrace_ += " tL" + std::to_string(w_.board.X(k / 4)) + "." + std::to_string(w_.board.Y(k / 4)) + "." + std::to_string(k & 3);
#endif
#if BC_EXPAND_OPENING
        planOpenMode();
#endif
#if BC_HARVEST
        planHarvest();
        {
            Choice h;
            if (harvestAct(h)) return h;  // BC_HARVEST: inside a harvest room, or at its entry
        }
#endif
        planPortals();
        buildSquadTargets();
        buildTips();

        // End-game feeding (see Params::feedRound).
        // BC_HEAT: no sacrifice where the enemy holds the sector - they would eat the drops (in a tested game our army fed
        // itself from 64 units down to 8 after round 349 while the enemy ate more than twice as many of the drops as we did).
        int hs = w_.heatOf(w_.head);
        bool enemyHeld = BC_HEAT && w_.enemyHeat[hs] >= World::kHeatOn && w_.enemyHeat[hs] > w_.friendHeat[hs];
        auto sacrifice = [&]() {
            // Step into our own neck: we die here and the pearls land by its head.
            Choice c;
            c.dir = w_.t.dir ^ 2;
            c.dest = b.nb[w_.head * 4 + c.dir];
            c.why = "feed";
            c.score = 1e9;
            return c;
        };
#if BC_QUEEN_GROWUP && BC_GROWUP_FEED
        // queen growth feed: a short teammate next to our asking queen (growHead_) dies there - its pearls land by her head -
        // one at a time (her drops eaten first), never with an enemy head within 2 of her; no pull towards her (her blockers are
        // teammates)
        if (growFeed()) return sacrifice();
#endif
#if BC_R4NET
        if (BC_R4_CULL && r4CullOn_ && r4None_ >= BC_R4_CULLP && !(BC_QUEEN && queenMe_) && w_.t.units > BC_R4_CULLUNITS) {
            // BC_R4_CULL (off): where the net would give no action (the default suicide) next to a long teammate of ours (our queen
            // in view, ids 0 / 1): feed her, one dragon at a time (her surroundings hold at most BC_R4_CULLBACKLOG pearls), never
            // with an enemy head within 2 of her
            int qh = -1;
            for (World::Seen const& s : w_.others)
                if (!isEnemy(s) && s.id < 2 && b.cheb(w_.head, s.head) <= BC_R4_CULLNEAR) qh = s.head;
            if (qh >= 0 && backlog(qh) <= BC_R4_CULLBACKLOG && !enemyNear(qh, 2)) {
                if (BC_ECON_TRACE) econTrace_ += " r4cull";
                return sacrifice();
            }
        }
#endif
        bool kingDone = false;
        v9King_ = v9Feeder_ = false;
        if (BC_V9_KING) kingHead_ = -1, kingZone_ = -1;
        v9Way_ = -1;
        finalFeeder_ = false;
#if BC_FULL
        if (w_.t.round >= p_.finalFrom && w_.t.round < p_.feedStop && !lean_ && !myDist_.empty() && !(BC_QUEEN && queenMe_)) {
            // Final push (Params::finalFrom): the king keeps every segment; every other dragon of finalMaxLen or less walks to it
            // and feeds it, throttled by the drops around its head.
            int me = w_.init.id & 4095;
            auto const& K = w_.know.longest;
            bool fresh = K.cell >= 0 && w_.t.round - K.round <= p_.kLongestAge;
            bool other = fresh && K.id != me && (K.len > L_ || (K.len == L_ && K.id < me));
            if (!other) {
                kingDone = true;
                kingMe_ = true;
            } else if (L_ <= p_.finalMaxLen && w_.t.units >= 2) {
                kingDone = true;
                finalFeeder_ = true;
                grower_ = false;
                hunts_.clear();
                escortOf_ = -1;
                int kh = -1;
                for (World::Seen const& s : w_.others)
                    if (!isEnemy(s) && (s.id & 4095) == K.id) kh = s.head;
                buildGoalField(kh >= 0 ? kh : K.cell);
                if (kh >= 0) {
                    feedHead_ = kh;
                    if (distToHead(myDist_, kh) <= p_.feedDist && L_ >= 2 && backlog(kh) <= p_.finalBacklog &&
                        !enemyNear(kh, 2) && w_.t.units > p_.finalKeepUnits)
                        return sacrifice();
                } else {
                    feedPull_ = K.cell;
                }
            }
        }
#endif
        goalDist_.clear();
#if BC_V9_KING
        {
            // BC_V9_KING late king (Params::kWindow; off): replaces the older feeding blocks.
            kingDone = true;
            int me = w_.init.id & 4095;
            auto const& K = w_.know.longest;
            bool fresh = K.cell >= 0 && w_.t.round - K.round <= p_.kLongestAge;
            bool other = fresh && K.id != me && (K.len > L_ || (K.len == L_ && K.id < me));
            v9King_ = !other && L_ >= p_.kKingMin;  // a short dragon never claims the crown
            // Saturated army (Params::kCapUnits): at the unit limit extra length makes no new dragons, so it goes to
            // the king from kCapFrom on.
            bool saturated = w_.t.units >= p_.kCapUnits && w_.t.round >= p_.kCapFrom;
            if (v9King_) {
                if ((w_.t.round >= 500 - p_.kWindow - p_.kNoSplit || saturated) && L_ >= 4) kingMe_ = true;
            } else if (w_.t.round < p_.feedStop) {
                // Only the king is fed. The stream: the king in view within kStreamRange, from its kQueue (kCapQueue when saturated)
                // nearest teammates. The final window: by the way to the king, far dragons first, medium ones too; only then do dragons
                // walk to where the king was heard of.
                int kh = -1;
                if (other)
                    for (World::Seen const& s : w_.others)
                        if (!isEnemy(s) && (s.id & 4095) == K.id) kh = s.head;
                bool streaming = w_.t.round >= p_.kStreamFrom || saturated;
                if (kh >= 0 && streaming) kingHead_ = kh;  // its drops are the king's
                if (kh >= 0 && (w_.t.round >= 500 - p_.kWindow - p_.kNoSplit || saturated)) kingZone_ = kh;
                int goal = -1;
                if (other && K.len >= L_ + p_.feedMargin) {
                    buildGoalField(kh >= 0 ? kh : K.cell);
                    int way = kh >= 0 ? distToHead(myDist_, kh) : INF;
                    if (way == INF && goalDist_[w_.head] < INF)
                        way = goalDist_[w_.head] + (kh >= 0 ? 0 : w_.t.round - K.round);
                    v9Way_ = way;
                    int maxLen = saturated ? p_.kCapMaxLen : p_.kStreamMaxLen;
                    if (way < INF && w_.t.round + p_.kWalk * way >= 500 - p_.kWindow)
                        goal = kh >= 0 ? kh : -2;
                    else if (kh >= 0 && streaming && L_ <= maxLen && way <= p_.kStreamRange &&
                             rankAt(kh, way, true) < (saturated ? p_.kCapQueue : p_.kQueue))
                        goal = kh;
                }
                if (goal != -1 && !enemyHeld) {
                    v9Feeder_ = true;
                    grower_ = false;
                    hunts_.clear();
                    escortOf_ = -1;
                    if (goal >= 0) {
                        feedHead_ = goal;
                        if (distToHead(myDist_, goal) <= p_.feedDist && L_ >= 2 && backlog(goal) <= p_.kBacklog &&
                            !enemyNear(goal, 2))
                            return sacrifice();
                    } else {
                        feedPull_ = K.cell;  // heard of, out of sight: walk to where it was
                    }
                } else {
                    goalDist_.clear();
                }
            }
        }
#endif
        if (BC_KING && p_.kingOn && !kingDone && w_.t.round >= p_.feedRound && w_.t.round < p_.feedStop && !enemyHeld &&
            !(BC_QUEEN && queenMe_)) {  // queen: the queen never feeds
            // King feeding: one target for the whole team, the longest teammate heard of; medium dragons feed too while it is not
            // kingMargin longer than the longest enemy seen (in a replay sample many round-500 losses had more total length than the
            // enemy but a much shorter longest dragon: only dragons of feedMaxLen or less fed, and most of our length was in dragons
            // of 10-40).
            auto const& K = w_.know.longest;
            bool fresh = K.cell >= 0 && K.id != (w_.init.id & 4095) && w_.t.round - K.round <= p_.longestMaxAge;
            bool behind = fresh && K.len < w_.enemyLongest + p_.kingMargin;
            if (fresh && K.len >= L_ + p_.feedMargin && (L_ <= p_.feedMaxLen || behind)) {
                kingDone = true;
                grower_ = false;
                hunts_.clear();
                escortOf_ = -1;
                int kh = -1;
                for (World::Seen const& s : w_.others)
                    if (!isEnemy(s) && (s.id & 4095) == K.id) kh = s.head;
                if (kh >= 0) {
                    feedHead_ = kh;
                    if (distToHead(myDist_, kh) <= p_.feedDist && L_ >= 2) return sacrifice();
                } else {
                    feedPull_ = K.cell;
                }
            } else if (!fresh || K.len < L_ + p_.feedMargin) {
                kingDone = true;
                if (L_ >= p_.feedMargin) grower_ = true;  // nobody known is clearly longer: we are the king
            }
        }
        // One-king rule (Params::oneKingFrom; the older BC_KING block is turned off by kingOn = 0): from oneKingFrom one king -
        // the LONGEST facts' dragon - is fed by every teammate that sees it, whatever its length, from 1 shorter (an as-long one
        // with a higher id feeds too). In replays, teams that fed a single king won clearly more of their full-length games than
        // teams that ended with two kings.
        bool const oneKing = p_.oneKingFrom > 0 && w_.t.round >= p_.oneKingFrom;
        int const me = w_.init.id & 4095;
        if (!kingDone && !(BC_QUEEN && queenMe_) && w_.t.round >= p_.feedRound && w_.t.round < p_.feedStop && (L_ <= p_.feedMaxLen || oneKing) &&
            !enemyHeld) {
            int bestLen = oneKing ? L_ - 1 : L_ + p_.feedMargin - 1;
            // BC_V9_KFEED: only the king (the LONGEST facts' dragon) is fed, not the longest teammate in view (otherwise dragons were
            // fed to scattered local kings of ~10 in rounds 300-350, and the round-500 losses had more total length but a shorter
            // longest), and a feeder waits while its drops lie.
            int kingId = (BC_V9_KFEED || oneKing) && w_.know.longest.cell >= 0 ? w_.know.longest.id : -1;
            // queen feeding (BC_QUEEN_FEED, off): the queen, whatever her length and ours, while the team believes her alive
            bool const toQueen = BC_QUEEN && BC_QUEEN_FEED && queenAlive();
            if (toQueen) kingId = w_.know.queenId >= 0 ? w_.know.queenId : -3;  // -3: not known yet (only by sight)
            if ((oneKing || toQueen) && kingId == me) kingId = -2;  // we are the king: nobody to feed
            for (World::Seen const& s : w_.others)
                if (!isEnemy(s) && (toQueen ? s.id < 2 :
                                    s.visible > bestLen && (kingId == -1 || (s.id & 4095) == kingId) &&
                                    !(oneKing && s.visible == L_ && (s.id & 4095) > me))) {
                    bestLen = s.visible;
                    feedHead_ = s.head;
                }
            if (feedHead_ >= 0) {
                grower_ = false;
                hunts_.clear();
                escortOf_ = -1;
                int d = distToHead(myDist_, feedHead_);
                bool ready = !BC_V9_KFEED || (backlog(feedHead_) <= p_.kBacklog && !enemyNear(feedHead_, 2));
                if (d <= p_.feedDist && L_ >= 2 && ready) return sacrifice();
            } else if (p_.feedRemote && w_.know.longest.cell >= 0 && w_.know.longest.id != (w_.init.id & 4095) &&
                       (w_.know.longest.len > bestLen || (toQueen && w_.know.longest.id == kingId)) &&
                       w_.t.round - w_.know.longest.round <= p_.longestMaxAge) {
                // The longest teammate is out of sight but heard of: walk towards it (the sacrifice itself still waits until its head is
                // in view).
                feedPull_ = w_.know.longest.cell;
                grower_ = false;
            } else if (L_ >= p_.feedMargin) {
                grower_ = true;  // nobody visible is longer: we are the one being fed
            }
        }

        // BC_HEAT: a free swarm dragon heads for the nearest sector nobody holds.
        heatAnchors_.clear();
        heatSpread_ = 0;
        if (BC_HEAT && !grower_ && hunts_.empty() && escortOf_ < 0 && feedHead_ < 0 && feedPull_ < 0 && !lean_) {
            for (int s = 0; s < World::kHeat; s++) {
                if (s == hs || w_.friendHeat[s] >= World::kHeatOn || w_.enemyHeat[s] >= World::kHeatOn) continue;
                int sx = s % World::kHeatX, sy = s / World::kHeatX;
                int x0 = sx * b.W / World::kHeatX, x1 = (sx + 1) * b.W / World::kHeatX;
                int y0 = sy * b.H / World::kHeatY, y1 = (sy + 1) * b.H / World::kHeatY;
                for (int q = 1; q <= 3; q += 2)          // four points inside the sector
                    for (int r = 1; r <= 3; r += 2) {
                        // Only land the team has explored: an unexplored sector is not empty, just unknown (at the start it is the enemy's half;
                        // without this rule everyone walked to the midline to trade heads while the enemy farmed).
                        int a = b.id(x0 + (x1 - x0) * q / 4, y0 + (y1 - y0) * r / 4);
                        if (w_.sectorExplored(a)) heatAnchors_.push_back(a);
                    }
            }
            heatSpread_ = p_.wHeatSpread;
        }

        planScavengers();
        if (BC_QUEEN && BC_QUEEN_ESCORT) planEscort();
        if (BC_QUEEN && BC_QUEEN_HUNT) planRegicide();
        else planSlay();
        planFinish();
        planScout();
        if (BC_ECON_BED) planIdleBed();
#if BC_BED_KEEP
        planKeep();
#endif
#if BC_OPEN_RISK
        planFastBed();
#endif
#if BC_OPEN_EXPAND
        if (expandTarget_ >= 0 && idleBedCell_ >= 0 && homeBed(idleBedCell_)) idleBedCell_ = -1;  // opening expansion: an expander leaves the start tower's beds
#endif
#if BC_EXPAND
        planExpand();
#endif

        // Depth 1 is the plain one-step evaluation; then deepen while the CPU budget lasts.
        std::vector<Choice> first(4);
        std::vector<std::vector<int>> firstBody(4);
        std::vector<int> none;
        for (int d = 0; d < 4; d++) first[d] = scoreFrom(w_.body, L_, d, 0, none, &firstBody[d]);
        std::vector<double> total(4);
        for (int d = 0; d < 4; d++) total[d] = first[d].score;
        depthReached_ = 1;
        for (int depth = 2; depth <= p_.maxDepth && !lean_ && !timeUp(); depth++) {
            std::vector<double> t(4, -1e18);
            bool complete = true;
            for (int d = 0; d < 4 && complete; d++) {
                Choice const& c = first[d];
                if (c.dest < 0) continue;
                if (c.terminal) { t[d] = c.score; continue; }
                std::vector<int> eaten;
                if (c.eat) eaten.push_back(c.dest);
                double rest = searchFrom(firstBody[d], c.newL, depth - 1, 1, eaten);
                if (timedOut_) complete = false;
                t[d] = rest <= -1e17 ? -900.0 : c.stepTerm + rest;
            }
            if (!complete) break;
            total = t;
            depthReached_ = depth;
        }
#if BC_RL_PRIOR
        // RL move prior: log p(relative direction) on the search value; with rlBand > 0 only near-best moves get it.
#if BC_R4NET
        bool const r1Move = !r4On_;   // the move-prior net replaces the opening net's move prior for the non-queen dragons
#else
        bool const r1Move = true;
#endif
        if (rlOn_ && p_.wPrior > 0 && r1Move) {
            double top = -1e18;
            for (int d = 0; d < 4; d++)
                if (first[d].dest >= 0) top = std::max(top, total[d]);
            for (int d = 0; d < 4; d++) {
                if (p_.rlBand > 0 && total[d] < top - p_.rlBand) continue;
                int rel = (d - w_.t.dir) & 3;
                total[d] += p_.wPrior * std::log(rlProb_[rel] + rlProb_[4 + rel] + 1e-3);
            }
        }
#if BC_R4NET
        // Move-prior net: log p(first step | the dragon moves) over the legal moves, weight BC_R4_W; only where the net is
        // decided (its top legal move holds BC_R4_CONF of the legal mass) and, with BC_R4_BAND > 0, only among moves within that
        // band of the best.
        if (r4On_ && BC_R4_W > 0) {
            double top = -1e18, mass = 0, mx = 0;
            for (int d = 0; d < 4; d++)
                if (first[d].dest >= 0) {
                    int const rel = (d - w_.t.dir) & 3;
                    top = std::max(top, total[d]);
                    mass += r4Move_[rel];
                    mx = std::max<double>(mx, r4Move_[rel]);
                }
            if (mass > 1e-9 && mx >= BC_R4_CONF * mass) {
                int before = -1, after = -1;
                double bv = -1e18, av = -1e18, t0[4];
#if BC_T64_FIELD
                bool const t64on = t64Field();
                double const t64w = t64on ? static_cast<double>(BC_T64_W) : static_cast<double>(BC_R4_WEFF);
#endif
#if BC_R7_KINDW_ON
                // BC_R7_KINDW_ON (off): the weight of this turn by the kind of the search's best step before the prior (a pre-pass; the
                // loop below is unchanged)
                double r7w = BC_R4_WEFF_NOW;
                {
                    int pb = -1;
                    double pv = -1e18;
                    for (int d = 0; d < 4; d++)
                        if (first[d].dest >= 0 && total[d] > pv) pv = total[d], pb = d;
                    if (pb >= 0) r7w *= r7KindMult(first[pb].why);
                }
#endif
#if BC_R7_TOPB_ON
                // BC_R7_TOPB_ON (off): the net's top legal first step (ties: the first index)
                int r7top = -1;
                {
                    double pm = -1;
                    for (int d = 0; d < 4; d++)
                        if (first[d].dest >= 0 && r4Move_[(d - w_.t.dir) & 3] > pm) pm = r4Move_[(d - w_.t.dir) & 3], r7top = d;
                }
#endif
                for (int d = 0; d < 4; d++) {
                    t0[d] = total[d];
                    if (first[d].dest < 0) continue;
                    if (total[d] > bv) bv = total[d], before = d;
                    if (BC_R4_BAND > 0 && total[d] < top - BC_R4_BAND) continue;
                    int const rel = (d - w_.t.dir) & 3;
#if BC_R7_KINDW_ON
                    total[d] += r7w * std::log(r4Move_[rel] / mass + 1e-3);
#else
                    total[d] += BC_R4_WEFF_NOW * std::log(r4Move_[rel] / mass + 1e-3);
#endif
#if BC_R7_TOPB_ON
                    if (d == r7top) total[d] += BC_R7_TOPB;
#endif
#if BC_T64_FIELD
                    if (t64on && BC_T64_EATB > 0 && first[d].eat) total[d] += BC_T64_EATB;
#endif
                }
                for (int d = 0; d < 4; d++)
                    if (first[d].dest >= 0 && total[d] > av) av = total[d], after = d;
                if (BC_ECON_TRACE)  // r4= the best step stayed, r4!<n> it changed and the search value given up is n / 100
                    econTrace_ += before == after ? std::string(" r4=") : " r4!" + std::to_string(static_cast<int>(std::lround((t0[before] - t0[after]) * 100)));
#if BC_T64_FIELD
                if (BC_ECON_TRACE && t64on) econTrace_ += " t64f";  // BC_T64_FIELD trace (off): this turn the dragon was in the field (the weight above is BC_T64_W)
#endif
            }
        }
#endif
#endif
        Choice best;
#if BC_FIGHT
        // Local fight search around the nearest enemy head (fight.hpp; its weights are 0 in the final parameters): our step, its
        // reply, our next step, under the game's rules; its value (in lengths) is added to each root move, and an even or losing
        // head-on trade chosen by the lookahead is dropped (fightTradeMin > -10).
        if (!lean_ && (p_.wFight > 0 || p_.fightTradeMin > -10)) {
            World::Seen const* foe = nullptr;
            int fd = INF;
            for (World::Seen const& s : w_.others)
                if (isEnemy(s) && b.cheb(s.head, w_.head) < fd) {
                    fd = b.cheb(s.head, w_.head);
                    foe = &s;
                }
            if (foe && fd <= p_.fightRadius && w_.body.size() >= 2) {
                FightSearch fs{b, w_.occ, foe->id, foe->head, foe->headDir, foe->visible, w_.body, w_.bodyKnown(),
                               p_.fightParanoia};
                for (int d = 0; d < 4; d++) {
                    if (d == (w_.t.dir ^ 2)) continue;
                    if (p_.wFight > 0) total[d] += p_.wFight * fs.rootStep(d, p_.fightPlies);
                    if (p_.fightTradeMin > -10 && first[d].why == "trade" && foe->visible - L_ < p_.fightTradeMin)
                        total[d] = -600.0;
                }
            }
        }
#endif
        for (int d = 0; d < 4; d++) {
            if (first[d].dest < 0) continue;
            Choice c = first[d];
            c.score = total[d];
            if (c.score > best.score) best = c;
        }
#ifdef BC_DEBUG
        if (queenMe_) {  // debug builds: the queen's root moves (first-step score, search total, why)
            qdbg_.clear();
            for (int d = 0; d < 4; d++)
                qdbg_ += std::string(1, DCH[d]) + (first[d].dest < 0 ? ":x " : ":" + std::to_string(static_cast<int>(first[d].stepTerm)) + "/" +
                                                   std::to_string(static_cast<int>(total[d])) + first[d].why + " ");
            qdbg_ += "lean=" + std::to_string(lean_) + " en=" + std::to_string(enemies_.size()) + " tips=" + std::to_string(tips_.size());
        }
#endif
        if (best.dest < 0) {
            // Nothing legal: first try a tile we only gave up as a teammate's last exit,
            // otherwise any well-formed move beats the default suicide.
            best.dir = w_.t.dir;
            best.dest = b.nb[w_.head * 4 + best.dir];
            best.why = "trapped";
            // Doomed: die alone. Prefer an enemy head (trade), then an unpaired portal (its landing is unknown, not certain death:
            // children born in a pocket whose only exit is a portal they never saw the other end of otherwise died there over and
            // over), then kelp / our own body / a teammate's body (only we die); a teammate's head would take it with us.
            int bestRank = 99;
            for (int d = 0; d < 4; d++) {
                int n = b.nb[w_.head * 4 + d];
#if BC_QUEEN_BOXGUARD & 1
                if (n >= 0 && n == boxExit_) continue;  // BC_QUEEN_BOXGUARD: dying alone means anywhere but her last way out (any other side ranks first)
#endif
                int hid = n >= 0 ? w_.occHeadId[n] : -1;
                bool enemyHead = false, friendHead = false;
                for (World::Seen const& s2 : w_.others)
                    if (s2.id == hid) (isEnemy(s2) ? enemyHead : friendHead) = true;
                bool portal = BC_TRAPPED_PORTAL && n < 0 && b.side(w_.head, d).kind == 2;
                int rank = enemyHead ? 0 : portal ? 1 : friendHead ? 9 : 5;
                if (BC_QUEEN && queenMe_) rank = portal ? 0 : enemyHead ? 1 : friendHead ? 9 : 5;  // queen: the portal may not kill her
                if (rank < bestRank) {
                    bestRank = rank;
                    best.dir = d;
                    best.dest = n;
                }
            }
            for (int d = 0; d < 4; d++) {
                int n = b.nb[w_.head * 4 + d];
                if (n < 0 || w_.occ[n] >= 0 || base_[n] < INF || ownCell(n)) continue;
#if BC_QUEEN_BOXGUARD & 1
                if (n == boxExit_) {  // BC_QUEEN_BOXGUARD: never our queen's last way out, even trapped - we die alone instead
#if BC_ECON_TRACE
                    boxSacrifice_ = true;
#endif
                    continue;
                }
#endif
                best.dir = d;
                best.dest = n;
                best.why = "shared-exit";
                break;
            }
#if (BC_QUEEN_BOXGUARD & 1) && BC_ECON_TRACE
            if (boxSacrifice_ && best.why == "trapped") econTrace_ += " qbS";  // BC_QUEEN_BOXGUARD trace: a teammate dies for her last exit
#endif
            if (best.why == "trapped") {
#ifdef BC_DEBUG
            for (int d = 0; d < 4; d++) {
                int n = b.nb[w_.head * 4 + d];
                char const* r = n < 0 ? "kelp" : n == b.nb[w_.head * 4 + (w_.t.dir ^ 2)] ? "neck"
                              : w_.occ[n] >= 0 ? "dragon" : base_[n] >= INF ? "exit" : "own";
                best.why += std::string(" ") + DCH[d] + ":" + r;
                if (n >= 0 && w_.occ[n] >= 0) best.why += std::to_string(w_.occ[n]);
            }
#endif
            }
        }

        // Queen flee (BC_QUEEN_FLEE): the short hunters that ram the queen move 1 step a turn (2-3 by paying segments); she has
        // ceil(L/4) free steps. When a head is inside her danger circle, a 2-step move whose end tile scores better (first-step
        // terms: danger, safe room, food) than every single step is taken.
        // Queen Safety Kernel (BC_QUEEN_QSK, qsk.hpp): picks her landing by hard tiers; the search values order a tier's 1-step
        // moves.
        if (BC_QUEEN && BC_QUEEN_QSK && queenMe_) {
            double tot[4];
            bool legal1[4];
            for (int d = 0; d < 4; d++) tot[d] = total[d], legal1[d] = first[d].dest >= 0;
            Qsk q(w_, L_);
#if BC_QUEEN_BREATHE
            {   // BC_QUEEN_BREATHE: her free sides now (other bodies, tails included, and her own body block): boxed turns in a row
                int fs = 0;
                for (int d = 0; d < 4; d++) {
                    int n = w_.board.nb[w_.head * 4 + d];
                    if (n >= 0 && w_.occ[n] < 0 && std::find(w_.body.begin(), w_.body.end(), n) == w_.body.end()) fs++;
                }
                w_.qTight = fs <= 1 ? w_.qTight + 1 : 0;
                q.tightTurns = w_.qTight;
            }
#endif
#if BC_QUEEN_GROWUP
            {   // BC_QUEEN_GROWUP (off): her free sides now (counted as for BREATHE) before kGrowCheck; the latch
                int fs = 0;
                for (int d = 0; d < 4; d++) {
                    int n = w_.board.nb[w_.head * 4 + d];
                    if (n >= 0 && w_.occ[n] < 0 && std::find(w_.body.begin(), w_.body.end(), n) == w_.body.end()) fs++;
                }
                w_.noteQueenTurn(fs);
                growMe_ = w_.qGrow;
            }
#endif
#if BC_EXPAND_OPENING && BC_EO_QUEEN_MOTHER
            q.grow = (BC_QUEEN_GROW && L_ < kGrowTo) || w_.openMode;  // open mode (BC_EXPAND_OPENING, off): the mother eats first
#else
            q.grow = BC_QUEEN_GROW && L_ < kGrowTo;
#endif
#if BC_QUEEN_GROWUP
            q.grow = q.grow || growArm();  // BC_QUEEN_GROWUP (off): the growth arm while she asks
#endif
            q.slackOn = BC_QUEEN_SLACK;
#if BC_QUEEN_FEED350 >= 2
            q.feed350 = queenFeeds();
#endif
#if BC_QUEEN_GROWFIRST
            q.feed350 = q.feed350 || queenGrows();  // BC_QUEEN_GROWFIRST: the same QSK food preferences before r200
#endif
            if (BC_QUEEN_HOME) {
                buildHome();
                q.home = &homeScore_;
            }
#if BC_FALLBACK95
            // BC_FALLBACK95: QSK's limit (search deadline + 10M <= 86.5M of the metered turn) never passes the hard line; the clamp
            // keeps it so if the budgets change
            QskOut o = q.run(tot, legal1, std::min(deadline_ + kQskBudget, fb95::hard() - 0.003));
#else
            QskOut o = q.run(tot, legal1, deadline_ + kQskBudget);
#endif
            qskTier_ = o.ok ? o.tier : 6;
#ifdef BC_DEBUG
            qdbg_ += " QSK T" + std::to_string(qskTier_) + " m" + std::to_string(o.m) + " ms" + std::to_string(o.mStrict) +
                     " room" + std::to_string(o.room) + " ex" + std::to_string(o.exits) + " st" + std::to_string(o.steps) +
                     (o.hold ? " hold" : "") +
#if BC_QUEEN_EXITFIX
                     (o.xcls >= 0 ? " xc" + std::to_string(o.xcls) : std::string()) +
#endif
                     " path=" + o.path + " src" + std::to_string(q.basicSrc.size()) + " eh" +
                     std::to_string(q.enemyHeads.size()) + " Lk" + std::to_string(q.longestKnown);
            for (int d = 0; d < 4; d++) {
                int n = w_.board.nb[w_.head * 4 + d];
                qdbg_ += std::string(" ") + DCH[d] + "=" + (n < 0 ? "x" : std::to_string(q.mOf(q.mB, n)));
            }
#endif
            if (o.ok) {
                Choice c;
                c.why = "qsk-t" + std::to_string(o.tier);
                c.score = o.m;
                if (o.hold) {
                    c.split = true;
                    c.n = 2;
                    c.why += "-hold";
                    return c;
                }
                c.dir = o.dir;
                if (o.steps == 1) {
                    // The search's own move when it is the tier's pick: the rest of the search's logic (the bud, ...) may still follow
                    if (o.dir == best.dir && best.dest == o.dest && o.tier <= 2) goto qskDone;
                    c.dest = o.dest;
                    c.eat = o.eats[0];
                    return c;
                }
                c.dest = -1;
                c.sprint = o.path;
                c.flee = o.cells;
                c.fleeEat = o.eats;
                c.fleePaid = o.paid;
                return c;
            }
        }
    qskDone:
        if (BC_QUEEN && BC_QUEEN_SAFE && BC_QUEEN_FLEE && !BC_QUEEN_QSK && queenMe_ && !lean_ && !timeUp() && queenThreatened()) {
            double single = -1e18;
            for (int d = 0; d < 4; d++)
                if (first[d].dest >= 0) single = std::max(single, first[d].score);
            Choice f;
            if (queenFlee(f, single)) return f;
        }

        // Portal probe: at the entry tile, step into the unpaired portal (landing unknown).
        if (prober_ && w_.head == probeEntry_ && best.why != "trade" && best.why != "defend" && !best.eat &&
            best.score < 50 && !(BC_PORTAL2 && L_ > 1 && probeDir_ == (w_.t.dir ^ 2))) {
            Choice pr;
            pr.dir = probeDir_;
            pr.dest = -1;  // landing tile unknown until next turn's vision
            pr.why = "probe";
            return pr;
        }

        if (p_.sprintTrades && best.why != "trade" && best.why != "defend" && !lean_) {
            std::string path;
            if (sprintTrade(path)) {
                Choice s;
                s.dir = dirFromChar(path[0]);
                s.dest = -1;  // we die with the target: no body to track
                s.sprint = path;
                s.why = "sprint-trade";
                return s;
            }
        }

        Choice split;
        if (BC_ECON_DIP && dipDone(split)) return split;  // BC_ECON_DIP: the lane is empty - leave by the reverse split
        if (trySplit(split, best)) return split;
        // Boxed in: splitting keeps the front half still this turn while the rear half (a new dragon that moves right away)
        // walks off and frees room.
        // Dragon plans, guard: the king reverses only when (nearly) boxed in: every reverse leaves a 2-long stub, and a long king
        // in a crowd of our own can otherwise shed a third of its length that way in two rounds.
        bool revReserve = BC_V9_KING || (BC_V9_EKING && w_.t.round >= p_.ekFrom);
        int revCap = w_.init.unitLimit - (revReserve && !v9King_ && !v9KingNow_ ? p_.kReserve : 0);
#if BC_UNIT_SLOT
        revCap = unitCap(revCap);
#endif
        bool protectLong = BC_FULL && w_.t.round >= std::min(p_.finalFrom, p_.lateRevFrom) && L_ >= p_.finalRevLen;  // BC_FULL
#if BC_LONGEST_RESERVE
        protectLong |= reserveMe_;  // BC_LONGEST_RESERVE: the reserve reverses only when (nearly) boxed in, like the king
#endif
        // queen: she reverses only with no legal move left (her stub lives; the lookahead's doom is often just an unknown map:
        // in a test a long queen reversed in her start corridor and the stub died within a few rounds)
        bool queenHolds = BC_QUEEN && queenMe_ && best.why != "trapped";
        if (best.score < (kingMe_ || protectLong ? -996.0 : -500.0) && L_ >= 4 && w_.t.units < revCap && !queenHolds) {
            // Reverse: keep a 2-long stub here, the child (our old tail, facing out) leaves with the rest. This is how strong teams
            // farm dead-end fountains.
            split.split = true;
            split.n = L_ - 2;
            split.why = "reverse";
            return split;
        }
        if (BC_FREESPRINT) {  // BC_FREESPRINT (off): the search's step heads for food - run on along the way on the free steps
            Choice fs;
            if (freeSprint(fs, best)) return fs;
        }
#if BC_R4NET
        if (BC_ECON_TRACE && BC_R4_SPRINT && r4SprOn_ && L_ >= 5 && best.dir >= 0)  // the net's P(multi-step | this first step) on the turns that have free steps
            econTrace_ += " r4p" + std::to_string(static_cast<int>(std::lround(r4Spr_[(best.dir - w_.t.dir) & 3] * 100))) + best.why.substr(0, 3);
        if (BC_R4_SPRINT && r4SprOn_ && !BC_FREESPRINT && best.dir >= 0 && BC_R5_GATEOK(r4Spr_[(best.dir - w_.t.dir) & 3])) {
            Choice fs;
            if (BC_R4_SPRINT & 2) {  // BC_R4_SPRINT bit 2: the free steps wherever the imitation net predicts more than one step (in the training
                                     // data the demonstrated dragons of length >= 5 moved several steps on about half of their moves; ours almost never did)
                r4SprWhy_ = 0;
                if (r4Sprint(fs, best)) {
                    if (BC_ECON_TRACE) econTrace_ += " r4s" + std::to_string(fs.sprint.size());
                    return fs;
                }
                if (BC_ECON_TRACE) econTrace_ += " r4x" + std::to_string(r4SprWhy_);
            } else if (freeSprint(fs, best)) {  // BC_R4_SPRINT bit 1: the free steps toward food where the net would multi-step (free steps toward food on every turn
                                                // did not pay)
                if (BC_ECON_TRACE) econTrace_ += " r4s" + std::to_string(fs.sprint.size());
                return fs;
            }
        }
#endif
#if BC_SPRINT_TARGET & 1
        if (!BC_FREESPRINT) {  // BC_SPRINT_TARGET bit 1 (off): the free steps toward a contested fresh pearl only
            Choice fs;
            if (freeSprint(fs, best, true)) {
#if BC_ECON_TRACE
                econTrace_ += " sg" + std::to_string(fs.sprint.size());
#endif
                return fs;
            }
        }
#endif
        return best;
    }

  private:
    struct Target {
        int cell;
        double belief;
        int countdown;  // rounds until the bed spawns, -1 none
        double explore;
        bool frontier = false;  // never seen, in a sector the team has not explored
    };
    enum class Job { None, Attack, Support };
    struct Hunt {
        World::Seen const* enemy;
        double gain;     // length we expect to come out ahead by
        Job job;
        bool threat;     // it is closing in on one of our growers
    };

    static constexpr int kPowSize = 256;
    std::vector<double> gammaPow_;
    double gp(int k) const { return k < 0 ? 1.0 : k >= kPowSize ? 0.0 : gammaPow_[k]; }
    std::vector<double> farPow_;
    double fp(int k) const { return k < 0 ? 1.0 : k >= kPowSize ? 0.0 : farPow_[k]; }
    static double bedWaitPow(int n) {
        static double tab[65];
        static bool init = false;
        if (!init) {
            tab[0] = 1.0;
            for (int i = 1; i < 65; i++) tab[i] = tab[i - 1] * kBedWait;
            init = true;
        }
        return tab[std::min(n, 64)];
    }
    struct Hot { int cell; double rate; };
    std::vector<Hot> hot_;

    World& w_;
    Params const& p_;
    double deadline_ = 1e9;         // seconds on the turn clock
    bool timedOut_ = false;
    int depthReached_ = 0;
    std::vector<int> blkScratch_;
    std::vector<double> belief_;    // per tile, for lookahead pearl pickup
    std::vector<int> countdown_;

    bool lean_ = false;
    bool rlSplit_ = false;        // opening prior (BC_RL_PRIOR): the network would split here (rlSplitP)
    bool rlOn_ = false;           // opening prior: the network ran this turn; rlProb_ holds its action probabilities
    float rlProb_[9] = {};
#if BC_R4NET
    bool r4On_ = false;           // move-prior net: ran this turn (every dragon but the queen); r4Move_ holds its move probabilities
    float r4Move_[4] = {};
    bool r4SprOn_ = false;        // move-prior net: r4Spr_[rel] = P(a multi-step move | the first step goes to rel) of this turn (BC_R4_SPRINT)
    double r4Spr_[4] = {};
    int r4SprWhy_ = 0;            // move-prior net trace: why r4Sprint refused (1 free steps < 2, 2 split / no dest / sprint / eats, 3 the step's why, 4 body unknown, 5 portal edge, 6 no legal second step, 7 landing margin, 8 room, 9 the queen's ring / last exit, 10 the clock)
    double r4S2_[4] = {};         // move-prior net: P(the second step goes to rel2 of the first step | a multi-step move) (the net's auxiliary head)
    bool r4CullOn_ = false;       // move-prior net: r4None_ = the net's P(no action: the default suicide, a cull) this turn (BC_R4_CULL, off)
    double r4None_ = 0;
#if BC_R6 >= 2
    struct R6Out {
        float prob[16] = {}, step2[4] = {};
        template <class N> void copy(N const& n) {
            for (int k = 0; k < 16; k++) prob[k] = n.prob[k];
            for (int k = 0; k < 4; k++) step2[k] = n.step2[k];
        }
        float moveProb(int rel) const { return prob[rel] + prob[4 + rel] + prob[8 + rel]; }
        float splitProb() const { return prob[12] + prob[13] + prob[14]; }
    };
    // BC_R6 (inactive): the net of this turn: the class net (built on the first turn the dragon plays with its class, if the turn clock has BC_R6_BUILD_SLACK left), else the default move-prior net.
    void r6Run(R6Out& o) {
        static R6NetA* netA = nullptr;
        static R6NetB* netB = nullptr;
        static F1Net* net0 = nullptr;
        bool const slack = deadline_ - clockNow() > BC_R6_BUILD_SLACK;
        int const c = r6c_ && ((BC_R6_MASK >> (r6c_ - 1)) & 1) ? r6c_ : 0;  // BC_R6_MASK: bit 0 = class A on, bit 1 = class B on
        r6w_ = c != 0 && BC_R6_W >= 0 ? BC_R6_W : BC_R4_W;
        if (c == 1 && !netA && slack) netA = new R6NetA;
        if (c == 2 && !netB && slack) netB = new R6NetB;
        if (c == 1 && netA) {
            netA->run(w_.init, w_.t);
            o.copy(*netA);
            return;
        }
        if (c == 2 && netB) {
            netB->run(w_.init, w_.t);
            o.copy(*netB);
            return;
        }
        if (!net0) net0 = new F1Net;
        net0->run(w_.init, w_.t);
        o.copy(*net0);
    }
#endif
#if BC_R6 >= 2
    double r6w_ = BC_R4_W;         // BC_R6 (inactive): the root-prior weight of this turn (BC_R4_WEFF)
#endif
#if BC_R6
    int r6c_ = 0;                 // BC_R6 (inactive): this process's map class this turn (0 none / 1 A / 2 B; a static selector: the Policy object is rebuilt every turn)
#endif
#endif

    void noFriendFields() {
        Board const& b = w_.board;
        friendDist_.assign(b.NC, INF);
        friendId_.assign(b.NC, -1);
        enemyDist_.assign(b.NC, INF);
        friends_.clear();
        enemies_.clear();
        std::vector<int> blk = ownBlk();
        nav_.bfs(b, w_.head, blk, p_.horizon);
        myDist_ = nav_.dist;
    }

    bool timeUp() {
        if (!timedOut_ && clockNow() >= deadline_) timedOut_ = true;
        return timedOut_;
    }
    Nav nav_;
    int L_ = 2;
    bool grower_ = false;
    std::vector<int> base_;       // other dragons' segments = INF, plus own stray segments
    std::vector<Target> targets_;
    double farExplore_ = 0;       // BC_FAR_EXPLORE: long-range pull to the nearest frontier tile this turn
    std::vector<int> heatAnchors_;  // BC_HEAT: tiles inside sectors nobody holds
    double heatSpread_ = 0;
    bool kingMe_ = false;            // dragon plans, guard: I am the king (our longest)
    bool queenMe_ = false;           // BC_QUEEN: I am our queen (id 0 team A / id 1 team B), the king from round 0
    // queen: the queen's yield to the opening rule - a few dragons alive and she is long: she buds a 2-3 child (front stays queen)
    bool queenBud() const {
        return BC_QUEEN && BC_QUEEN_SPLIT && queenMe_ && w_.t.units <= kQueenBudUnits && L_ >= kQueenBudLen && w_.t.round < p_.feedRound;
    }
    // queen: enemy tail tips. A split puts a new head on the tip that moves this very round (in a test game a dragon split
    // next to the queen and its 2-long child took her). The queen counts a tip as a head of reach 1 to 2; tipDist_[c]: steps
    // from the nearest tip over tiles free of dragons (<= 2, INF beyond), empty unless queen.
    // A tip is a visible enemy segment no other segment of its dragon points to (a chain cut by the view edge counts too).
    std::vector<int> tipDist_, tips_;
    void buildTips() {
        tipDist_.clear();
        tips_.clear();
        if (!(BC_QUEEN && BC_QUEEN_SAFE && queenMe_)) return;
        Board const& b = w_.board;
        auto const& P = w_.t.parts;
        for (Part const& a : P) {
            if (a.head || a.team == w_.init.team || a.id == w_.init.id) continue;
            int c = b.id(a.x, a.y), seg = 0;
            bool pointed = false;
            for (Part const& o : P) {
                if (o.id != a.id) continue;
                seg++;
                if (o.head) continue;
                if (b.nb[b.id(o.x, o.y) * 4 + o.facing] == c) pointed = true;
            }
            if (!pointed && seg >= 2) tips_.push_back(c);  // a split needs 4 segments; 2 visible may be the end of more
        }
        if (tips_.empty()) return;
        tipDist_.assign(b.NC, INF);
        std::vector<int> q = tips_;
        for (int c : q) tipDist_[c] = 0;
        for (size_t qi = 0; qi < q.size(); qi++) {
            int c = q[qi];
            if (tipDist_[c] >= 2) continue;
            for (int d = 0; d < 4; d++) {
                int n = b.nb[c * 4 + d];
                if (n < 0 || tipDist_[n] != INF || w_.occ[n] >= 0 || ownCell(n)) continue;
                tipDist_[n] = tipDist_[c] + 1;
                q.push_back(n);
            }
        }
    }
    // queen: an enemy could reach the queen's head if she stands still this turn (a bud keeps her in place)
    bool queenThreatened() const {
        Board const& b = w_.board;
        for (EnemyField const& e : enemies_)
            if (distToHead(e.dist, w_.head) <= queenReachOf(e.visible) + 1) return true;
        for (World::Seen const& s : w_.others)
            if (isEnemy(s) && b.cheb(s.head, w_.head) <= 2) return true;  // no field (lean turn, far rivals cut)
        for (int t : tips_)
            if (b.cheb(t, w_.head) <= 3) return true;
        return false;
    }
    // Queen flee: the best 2- or 3-step MOVE for the queen (free steps: 2 from 5 long, 3 from 9; at 3-4 long she pays a
    // segment for the second step, at kQueenPaidCost). Hunters of 3-4 pay for 2-3 steps, so a 9+ long queen needs 3.
    bool queenFlee(Choice& out, double single) {
        int free = sprintFree(L_);
        int paid = free >= 2 ? 0 : 1;
        if (L_ - paid < 2) return false;
        int maxSteps = std::max(2, std::min(free, kQueenFleeSteps));
        fleeBest_ = single + kQueenFleeMargin;
        fleeFound_ = false;
        std::vector<int> eaten, cells;
        std::vector<char> eats;
        std::string path;
        fleeDfs(w_.body, L_, 0, maxSteps, paid, 0.0, eaten, cells, eats, path, out);
        return fleeFound_;
    }
    double fleeBest_ = 0;
    bool fleeFound_ = false;
    void fleeDfs(std::vector<int> const& body, int L, int depth, int maxSteps, int paid, double eatSum, std::vector<int>& eaten,
                 std::vector<int>& cells, std::vector<char>& eats, std::string& path, Choice& out) {
        std::vector<int> nb;
        for (int d = 0; d < 4 && !timeUp(); d++) {
            Choice c = scoreFrom(body, L, d, 0, eaten, &nb);
            if (c.dest < 0) continue;
            bool last = depth + 1 >= maxSteps;
            cells.push_back(c.dest);
            eats.push_back(static_cast<char>(c.eat));
            path.push_back(DCH[d]);
            if (depth >= 1 && c.score > -500.0) {  // a 2+ step end tile, not vetoed (cramped / dead end) nor a trade
                double v = c.score + eatSum - kQueenPaidCost * paid;
                if (v > fleeBest_) {
                    fleeBest_ = v;
                    fleeFound_ = true;
                    out = Choice{};
                    out.dir = cells.empty() ? d : dirFromChar(path[0]);
                    out.dest = -1;
                    out.sprint = path;
                    out.flee = cells;
                    out.fleeEat = eats;
                    out.fleePaid = paid;
                    out.score = v;
                    out.why = "queen-flee";
                }
            }
            // walk on through tiles we would not stop on (cramped is fine in passing; a trade or a dead end is not)
            if (!last && (!c.terminal || c.why == "cramped")) {
                if (c.eat) eaten.push_back(c.dest);
                std::vector<int> next = nb;
                fleeDfs(next, c.newL, depth + 1, maxSteps, paid, eatSum + (c.eat ? p_.eatBonus : 0.0), eaten, cells, eats, path, out);
                if (c.eat) eaten.pop_back();
            }
            cells.pop_back();
            eats.pop_back();
            path.pop_back();
        }
    }
    // ---- BC_QUEEN_QKILL: QKILL and QGUARD -------------------------------------------------------------------------------
    // BFS from `from` over tiles that pass(c), at most maxD steps; dist / par / dirTo per tile (dist INF unreached).
    template <class Pass>
    void bfsFrom(int from, int maxD, Pass pass, std::vector<int>& dist, std::vector<int>& par, std::vector<int>& dirTo) const {
        Board const& b = w_.board;
        dist.assign(b.NC, INF);
        par.assign(b.NC, -1);
        dirTo.assign(b.NC, -1);
        std::vector<int> q{from};
        dist[from] = 0;
        for (size_t i = 0; i < q.size(); i++) {
            int c = q[i];
            if (dist[c] >= maxD) continue;
            for (int d = 0; d < 4; d++) {
                int n = b.nb[c * 4 + d];
                if (n < 0 || dist[n] != INF || !pass(n)) continue;
                dist[n] = dist[c] + 1;
                par[n] = c;
                dirTo[n] = d;
                q.push_back(n);
            }
        }
    }
    std::string pathTo(int from, int to, std::vector<int> const& par, std::vector<int> const& dirTo,
                       std::vector<int>* cells = nullptr) const {
        std::string rev;
        std::vector<int> cs;
        for (int c = to; c != from && c >= 0; c = par[c]) rev += DCH[dirTo[c]], cs.push_back(c);
        if (cells) cells->assign(cs.rbegin(), cs.rend());
        return std::string(rev.rbegin(), rev.rend());
    }
    // Our queen's head in view (a teammate with id < 2), -1 none.
    int ourQueenHead() const {
        for (World::Seen const& s : w_.others)
            if (!isEnemy(s) && s.id < 2) return s.head;
        return -1;
    }
    // A sprint (or step) onto the head `target`: free tiles only, not through our queen's surroundings (Chebyshev 1).
    bool strikePath(int target, int qh, std::string& path) {
        Board const& b = w_.board;
        int maxSteps = sprintMaxSteps(L_);
        std::vector<int> dist, par, dirTo;
        auto pass = [&](int c) {
            if (w_.occ[c] >= 0 || ownCell(c)) return false;
            return qh < 0 || b.cheb(c, qh) > 1;
        };
        bfsFrom(w_.head, maxSteps - 1, pass, dist, par, dirTo);
        int bestD = INF, bestPre = -1, bestDir = -1;
        for (int d = 0; d < 4; d++) {
            int pre = -1;
            for (int k = 0; k < 4; k++) {
                int n = b.nb[target * 4 + k];
                if (n >= 0 && b.nb[n * 4 + d] == target) pre = n;
            }
            if (b.nb[w_.head * 4 + d] == target && 1 < bestD) bestD = 1, bestPre = w_.head, bestDir = d;
            if (pre < 0 || dist[pre] == INF || pre == w_.head) continue;
            if (dist[pre] + 1 < bestD) bestD = dist[pre] + 1, bestPre = pre, bestDir = d;
        }
        if (bestPre < 0 || bestD > maxSteps) return false;
        path = (bestPre == w_.head ? std::string() : pathTo(w_.head, bestPre, par, dirTo)) + DCH[bestDir];
        return true;
    }
    // QKILL: the enemy queen (an enemy with id < 2) in reach this turn, by any of ours but our queen - paid steps too, no
    // trade gates (her death decides round 500).
    bool queenKill(Choice& out) {
        if (L_ < 2) return false;
        int qh = ourQueenHead();
        for (World::Seen const& s : w_.others) {
            if (!isEnemy(s) || s.id >= 2) continue;
            std::string path;
            if (!strikePath(s.head, qh, path)) continue;
            out = Choice{};
            out.dir = dirFromChar(path[0]);
            out.dest = -1;  // we die with her
            out.sprint = path;
            out.why = "qkill";
            out.score = 1e9;
            return true;
        }
        return false;
    }
    // QGUARD: our queen in view and an enemy head that moves after us (a larger id) able to reach her head this round: trade
    // heads with it, else put our head on its shortest way to her (reachable on our free steps).
    bool queenGuard(Choice& out) {
        Board const& b = w_.board;
        int qh = ourQueenHead();
        if (qh < 0 || L_ < 2) return false;
        for (World::Seen const& t : w_.others) {
            if (!isEnemy(t) || t.id <= w_.init.id) continue;
            int le = std::max(t.visible, 4);  // reach 3 at least (strikers may sprint up to 4)
            if (b.cheb(t.head, w_.head) >= 3)   // its body may run on past my view: as long as the longest enemy known
                le = std::max({le, w_.enemyLongest, 6});
            int reach = sprintMaxSteps(le);
            std::vector<int> dT, pT, xT;
            auto passT = [&](int c) { return c == qh || (w_.occ[c] < 0 && !ownCell(c)); };
            bfsFrom(t.head, reach, passT, dT, pT, xT);
            if (dT[qh] > reach) continue;
            std::string path;
            if (strikePath(t.head, qh, path)) {
                out = Choice{};
                out.dir = dirFromChar(path[0]);
                out.dest = -1;
                out.sprint = path;
                out.why = "qguard-trade";
                out.score = 1e9;
                return true;
            }
            // block: a tile on its shortest way to her that we reach on free steps, nearest to it
            std::vector<int> dQ, pQ, xQ;
            bfsFrom(qh, reach, passT, dQ, pQ, xQ);
            std::vector<int> dM, pM, xM;
            int free = sprintFree(L_);
            bfsFrom(w_.head, free, [&](int c) { return w_.occ[c] < 0 && !ownCell(c) && b.cheb(c, qh) > 0; }, dM, pM, xM);
            int best = -1;
            for (int c = 0; c < b.NC; c++) {
                if (dM[c] == INF || dM[c] == 0 || dT[c] == INF || dQ[c] == INF || c == qh) continue;
                if (dT[c] + dQ[c] != dT[qh]) continue;
                if (best < 0 || dT[c] < dT[best]) best = c;
            }
            if (best < 0) continue;
            std::vector<int> cells;
            std::string p = pathTo(w_.head, best, pM, xM, &cells);
            out = Choice{};
            out.dir = dirFromChar(p[0]);
            out.why = "qguard-block";
            out.score = 1e8;
            if (p.size() == 1) {
                out.dest = best;
                out.eat = w_.seenRound[best] == w_.t.round && w_.seenPearl[best];
            } else {
                out.dest = -1;
                out.sprint = p;
                out.flee = cells;
                for (int c : cells) out.fleeEat.push_back(static_cast<char>(w_.seenRound[c] == w_.t.round && w_.seenPearl[c]));
                out.fleePaid = 0;
            }
            return true;
        }
        return false;
    }
#if BC_T64_FIELD
    // BC_T64_FIELD (off): is this dragon within BC_T64_R steps (along the walkable graph) of a known-fast bed - board.bed == 1 and 0 < gapMax <= BC_T64_GAP - and, with BC_T64_FASTEST, of one of the fastest class its board knows
    // (gap <= BC_T64_TOL x the smallest known gap on the whole board)? The breadth-first walk is bounded (at most (2R + 1)^2 tiles); the whole-board scan runs only when a candidate bed was found.
    bool t64Field() const {
        Board const& b = w_.board;
        std::vector<int> q{w_.head}, dq{0};
        int near = INF;
        for (size_t i = 0; i < q.size(); i++) {
            int const c = q[i], dc = dq[i];
            if (b.bed[c] == 1 && b.gapMax[c] > 0 && b.gapMax[c] <= BC_T64_GAP) near = std::min<int>(near, b.gapMax[c]);
            if (dc >= BC_T64_R) continue;
            for (int d = 0; d < 4; d++) {
                int const n = b.nb[c * 4 + d];
                if (n < 0 || std::find(q.begin(), q.end(), n) != q.end()) continue;
                q.push_back(n);
                dq.push_back(dc + 1);
            }
        }
        if (near == INF) return false;
        if (!BC_T64_FASTEST) return true;
        int fastest = INF;
        for (int c = 0; c < b.NC; c++)
            if (b.bed[c] == 1 && b.gapMax[c] > 0) fastest = std::min<int>(fastest, b.gapMax[c]);
        return near <= fastest * BC_T64_TOL;
    }
#endif

    // ---- BC_ECON_BED: spawn clock (our coverage of the beds at spawn time is where the food gap is) ---------------------
    // (e) spread of the idle: a short dragon that has not eaten for 30 rounds takes the nearest known bed that is not in an
    // enemy-held sector and that no visible teammate is nearer to (ties: the lower id)
#if BC_OPEN_RISK
    // BC_OPEN_RISK (off): the opening's small non-queens contest food like strong teams (BC_OPEN_RISK's bits)
    bool openRisk(int bit) const {
        return (BC_OPEN_RISK & bit) && w_.t.round < kOpenRiskUntil && L_ <= kOpenRiskMaxLen && !(BC_QUEEN && queenMe_);
    }
#endif
#if BC_BED_KEEP
    int keepCell_ = -1;  // BC_BED_KEEP (off): the fast bed this dragon keeps this turn, -1 none
    // a bed's refill gap, estimated: the largest countdown seen on it or on its mirror (mirror tiles share one countdown), else its learned gap
    int keepGap(int c) const {
        Board const& b = w_.board;
        int g = w_.maxCd[c];
        if (b.sym > 0) {
            int const mc = b.mirror(c);
            if (mc != c) g = std::max<int>(g, w_.maxCd[mc]);
        }
        if (g <= 0 && b.gapMax[c] > 0) g = b.gapMax[c];
        return g;
    }
    bool keepFast(int c) const {
        if (w_.board.bed[c] != 1) return false;
        int const g = keepGap(c);
        return g > 0 && g <= kKeepGap;
    }
#if BC_TIER2
    bool keepTier(int c, int gap) const {  // BC_TIER2 (off): a bed of gap at most `gap`
        if (w_.board.bed[c] != 1) return false;
        int const g = keepGap(c);
        return g > 0 && g <= gap;
    }
#endif
#if BC_BED_KEEP & 2
    bool keepDue_ = true;  // BC_BED_KEEP bit 2 (off): the kept bed pulls this turn
#endif
    // BC_BED_KEEP (off): the nearest fast bed within kKeepSteps whose cluster (its fast beds within kKeepR) has more beds
    // than the teammates in view that are at least as near to it (ties: the lower id) - a non-queen, before kKeepUntil, not
    // feeding
    void planKeep() {
        keepCell_ = -1;
        if ((BC_QUEEN && queenMe_) || myDist_.empty() || w_.t.round >= kKeepUntil) return;
        if ((BC_V9_KING && v9Feeder_) || finalFeeder_ || feedHead_ >= 0 || feedPull_ >= 0) return;
        Board const& b = w_.board;
        int bestD = INF;
        for (int c = 0; c < b.NC; c++) {
            int const d = myDist_[c];
            if (d > kKeepSteps || d >= bestD || !keepFast(c)) continue;
            int beds = 0;
            for (int dy = -kKeepR; dy <= kKeepR; dy++)
                for (int dx = -kKeepR; dx <= kKeepR; dx++)
                    if (keepFast(b.id(b.X(c) + dx, b.Y(c) + dy))) beds++;
            int mates = 0;
            for (FriendField const& f : friends_) {
                int const fd = f.dist[c];
                if (fd < d || (fd == d && f.id < w_.init.id)) mates++;
            }
            if (mates >= beds) continue;
            bestD = d, keepCell_ = c;
        }
#if BC_TIER2
        if (keepCell_ < 0) {  // BC_TIER2 (off): the field full or empty - the nearest second-tier bed with room
            for (int c = 0; c < b.NC; c++) {
                int const d = myDist_[c];
                if (d > kTier2Steps || d >= bestD || !keepTier(c, kTier2Gap)) continue;
                int beds = 0;
                for (int dy = -kKeepR; dy <= kKeepR; dy++)
                    for (int dx = -kKeepR; dx <= kKeepR; dx++)
                        if (keepTier(b.id(b.X(c) + dx, b.Y(c) + dy), kTier2Gap)) beds++;
                int mates = 0;
                for (FriendField const& f : friends_) {
                    int const fd = f.dist[c];
                    if (fd < d || (fd == d && f.id < w_.init.id)) mates++;
                }
                if (mates >= beds) continue;
                bestD = d, keepCell_ = c;
            }
            if (keepCell_ >= 0 && econTraceOn()) econTrace_ += " tier2";
        }
#endif
#if BC_BED_KEEP & 2
        keepDue_ = true;
        if (keepCell_ >= 0) {  // BC_BED_KEEP bit 2 (off): due when a pearl is on it or its countdown is at most its distance + kKeepSlack
            int cd = w_.next[keepCell_];
            if (b.sym > 0 && cd < 0) cd = w_.next[b.mirror(keepCell_)];
            keepDue_ = w_.seenPearl[keepCell_] || cd < 0 || cd <= myDist_[keepCell_] + kKeepSlack;
            if (!keepDue_ && econTraceOn()) econTrace_ += " keep-early";
        }
#endif
        if (keepCell_ >= 0 && econTraceOn()) econTrace_ += " keep";
    }
#endif
#if BC_OPEN_RISK
    int fastBedCell_ = -1;  // BC_OPEN_RISK bit 8 (off): the fast bed this dragon parks at this turn, -1 none
    // a bed's refill gap, estimated: the largest countdown seen on it or on its mirror (mirror tiles share one countdown); 0 unknown
    int bedGap(int c) const {
        Board const& b = w_.board;
        int g = w_.maxCd[c];
        if (b.sym > 0) {
            int const mc = b.mirror(c);
            if (mc != c) g = std::max<int>(g, w_.maxCd[mc]);
        }
        return g;
    }
    // BC_OPEN_RISK bit 8 (off): the known bed (seen, or by the map's symmetry) of the best estimated rate / distance with a
    // gap of at most kOpenRiskFastGap that no teammate in view is nearer to (ties: the lower id)
    void planFastBed() {
        fastBedCell_ = -1;
        if (!openRisk(8) || myDist_.empty()) return;
        Board const& b = w_.board;
        double best = 0.0;
        for (int c = 0; c < b.NC; c++) {
            if (b.bed[c] != 1 || myDist_[c] == INF) continue;
            int const g = bedGap(c);
            if (g <= 0 || g > kOpenRiskFastGap) continue;
            double const v = 1.0 / (g * (myDist_[c] + 1.0));
            if (v <= best) continue;
            bool nearer = false;
            for (FriendField const& f : friends_) {
                int const fd = f.dist[c];
                if (fd < myDist_[c] || (fd == myDist_[c] && f.id < w_.init.id)) { nearer = true; break; }
            }
            if (nearer) continue;
            best = v, fastBedCell_ = c;
        }
        if (fastBedCell_ >= 0 && econTraceOn()) econTrace_ += " fast-bed";
    }
#endif
    void planIdleBed() {
        idleBedCell_ = -1;
#if BC_OPEN_RISK
        if (openRisk(2)) return;  // BC_OPEN_RISK bit 2 (off): no waiting at a bed
#endif
        Board const& b = w_.board;
        if (lean_ || myDist_.empty() || (BC_QUEEN && queenMe_) || L_ > 3 || w_.t.round >= kBedUntil ||
            w_.t.round - std::max(w_.lastEatRound, w_.firstRound) < kIdleRounds)
            return;
        int best = -1, bestD = INF;
        for (int c = 0; c < b.NC; c++) {
            if (b.bed[c] != 1 || w_.maxCd[c] <= 0 || w_.maxCd[c] > kBedMaxGap || myDist_[c] == INF || myDist_[c] >= bestD) continue;
            int hs = w_.heatOf(c);
            if (BC_HEAT && w_.enemyHeat[hs] >= World::kHeatOn && w_.enemyHeat[hs] > w_.friendHeat[hs]) continue;
            bool nearer = false;
            for (FriendField const& f : friends_) {
                int fd = f.dist[c];
                if (fd < myDist_[c] || (fd == myDist_[c] && f.id < w_.init.id)) { nearer = true; break; }
            }
            if (nearer) continue;
            best = c, bestD = myDist_[c];
        }
        idleBedCell_ = best;
        if (best >= 0 && econTraceOn()) econTrace_ += " idle-bed";
    }
#if BC_EXPAND
    // BC_EXPAND (off): an idle non-queen dragon (no meal for kExpandIdle rounds, at most kExpandMaxLen long, no idle bed of
    // BC_ECON_BED) outside a bed core (no known bed within kExpandHeld steps) heads for the nearest known bed - seen or
    // mirrored, any refill gap, in enemy heat too - that none of ours holds (no teammate nearer, none in view within
    // kExpandHeld), else for the nearest never-seen tile: on some maps our dragons farmed the home room to the end while the
    // central beds fed the enemy. A dragon waiting in a core stays (without that test the waiters were pulled off their
    // beds and fewer spawns fell nearer to us).
    void planExpand() {
        expandCell_ = -1;
        Board const& b = w_.board;
        if (lean_ || myDist_.empty() || (BC_QUEEN && queenMe_) || L_ > kExpandMaxLen || idleBedCell_ >= 0 ||
            w_.t.round - std::max(w_.lastEatRound, w_.firstRound) < kExpandIdle)
            return;
        int best = -1, bestD = INF, unseen = -1, unseenD = INF;
        for (int c = 0; c < b.NC; c++) {
            int dc = myDist_[c];
            if (dc == INF) continue;
            if (w_.seenRound[c] < 0 && !w_.mirrored[c]) {
                if (dc > 0 && dc < unseenD) unseen = c, unseenD = dc;
                continue;
            }
            if (b.bed[c] != 1) continue;
            if (dc <= kExpandHeld) return;  // in a core already: stay
            if (dc >= bestD) continue;
            bool held = false;
            for (World::Seen const& s : w_.others)
                if (!isEnemy(s) && b.cheb(s.head, c) <= kExpandHeld) held = true;
            for (FriendField const& f : friends_)
                if (f.dist[c] < dc || (f.dist[c] == dc && f.id < w_.init.id)) held = true;
            if (held) continue;
            best = c, bestD = dc;
        }
        expandCell_ = best >= 0 ? best : unseen;
        if (expandCell_ >= 0 && econTraceOn()) econTrace_ += best >= 0 ? " expand-bed" : " expand-unseen";
    }
#endif
#if BC_EXPAND_OPENING
    // BC_EXPAND_OPENING (open mode, off): the mode, judged each turn in each process - a team of <= kEoMaxUnits with no known
    // bed in reach (seen or mirrored, by this dragon's BFS) from r10 or nothing eaten from r12, or from r40 at most half the
    // longest enemy known; it ends at kEoExitUnits units, r150, or kEoExitEats eaten with kEoExitBeds beds in reach. Then its
    // parts: the queen's gate (applied from her next turn), the explorer's band, the hunter to flee
    int eoBeds_ = 0;  // known beds in reach this turn (tests / trace)
    // "in reach" = over known tiles (seen or mirrored) and the portals this dragon may take: myDist_ also crosses never-seen
    // ground (otherwise a band across the walls looked in reach and the explorer chased it for dozens of rounds)
    std::vector<int> eoKnown_, eoPar_, eoDir_;
    std::vector<char> eoLocal_;  // MIRROR_TARGET: the tiles this dragon reaches over known open edges, no portal (its own region)
    void planOpenMode() {
        Board const& b = w_.board;
        int const r = w_.t.round;
        eoMirror_ = eoHunter_ = -1;
        eoBeds_ = 0;
        if (w_.t.units >= kEoExitUnits || r >= kEoUntil) {  // out of the opening: no mode, no search
            w_.openMode = false;
#if BC_EO_QUEEN_MOTHER
            if (BC_QUEEN && queenMe_ && w_.motherGate) {
                w_.motherGate = false;
                w_.rebuildGated();
            }
#endif
            return;
        }
        // the known-tile search only where its answer is read (no fixed cost each opening turn): the bed clause of the entry (no
        // other clause holds yet), the exit after kEoExitEats meals, and the band of a non-queen in the mode
        bool searched = false;
        auto search = [&]() {
            if (searched) return;
            searched = true;
            bfsFrom(w_.head, b.NC, [&](int c) { return w_.seenRound[c] >= 0 || w_.mirrored[c]; }, eoKnown_, eoPar_, eoDir_);
            for (int c = 0; c < b.NC; c++)
                if (eoKnown_[c] < INF && b.bed[c] == 1 && (w_.seenRound[c] >= 0 || w_.mirrored[c])) eoBeds_++;
        };
        int const eL = std::max(w_.enemyKing.len, r - w_.enemyLongestRound <= 100 ? w_.enemyLongest : 0);
        bool const few = w_.t.units <= kEoMaxUnits;
        bool const enterOther = (few && r >= kEoEatCheck && w_.eatCount == 0) || (r >= kEoLenCheck && eL > 0 && 2 * L_ <= eL);
        bool const bedClause = few && r >= kEoBedCheck;
        if (!enterOther && bedClause) search();
        bool const enter = enterOther || (bedClause && eoBeds_ == 0);
        if (enter && w_.eatCount >= kEoExitEats) search();
        bool const leave = w_.t.units >= kEoExitUnits || r >= kEoUntil || (w_.eatCount >= kEoExitEats && eoBeds_ >= kEoExitBeds);
        w_.openMode = enter && !leave;
#if BC_ECON_TRACE
        if (w_.openMode) econTrace_ += " eo";  // open-mode trace tag (trace builds only)
#endif
#if BC_EO_QUEEN_MOTHER
        if (BC_QUEEN && queenMe_ && w_.motherGate != w_.openMode) {
            w_.motherGate = w_.openMode;
            w_.rebuildGated();
        }
#endif
        if (!w_.openMode || (BC_QUEEN && queenMe_)) return;
#if BC_EO_MIRROR_TARGET
        if (b.sym > 0) {  // the mirrored bed reached through a portal with the most mirrored beds around it, then the nearest
            search();
            // its own region (known open edges, no portal) is not a band: only bands through seen portal ends count (a band in its
            // own start strip otherwise held the explorer there for most of the opening)
            eoLocal_.assign(b.NC, 0);
            std::vector<int> q{w_.head};
            eoLocal_[w_.head] = 1;
            for (size_t i = 0; i < q.size(); i++)  // the reach's own adjacency (nb) without its portal steps: a band differs only by those
                for (int d = 0; d < 4; d++) {
                    int c = q[i];
                    int n = b.nb[c * 4 + d];
                    if (n < 0 || b.side(c, d).kind == 2) continue;
                    if (eoLocal_[n] || (w_.seenRound[n] < 0 && !w_.mirrored[n])) continue;
                    eoLocal_[n] = 1;
                    q.push_back(n);
                }
            int best = -1, bestN = 0, bestD = INF;
            for (int c = 0; c < b.NC; c++) {
                if (eoKnown_[c] >= INF || eoLocal_[c] || b.bed[c] != 1 || !w_.mirrored[c]) continue;
                int n = 0;
                for (int dy = -kEoBandR; dy <= kEoBandR; dy++)
                    for (int dx = -kEoBandR; dx <= kEoBandR; dx++) {
                        int m = b.id(b.X(c) + dx, b.Y(c) + dy);
                        n += b.bed[m] == 1 && w_.mirrored[m];
                    }
                if (n > bestN || (n == bestN && eoKnown_[c] < bestD)) best = c, bestN = n, bestD = eoKnown_[c];
            }
            eoMirror_ = best;
        }
#endif
#if BC_EO_ANTIHUNT
        if (w_.t.units <= kEoHuntUnits) {
            int bestC = INF;
            for (size_t i = 0; i < enemies_.size(); i++) {
                int ch = b.cheb(enemies_[i].head, w_.head);
                if (enemies_[i].visible >= kEoHuntLen && ch <= kEoHuntR && ch < bestC) eoHunter_ = static_cast<int>(i), bestC = ch;
            }
        }
#endif
    }
#endif
#if BC_QUEEN_GROWUP
    // BC_QUEEN_GROWUP (off): she grows once latched (World::noteQueenTurn); until kGrowUntil teammates leave her food to her
    // while she is short - mode 1 once they heard her ask lately, mode 2 always
    void planGrowUp() {
        growMe_ = BC_QUEEN && queenMe_ && w_.qGrow;
        if (BC_QUEEN && !queenMe_ && w_.t.round >= kGrowCheck && w_.t.round < kGrowUntil &&
            (BC_QUEEN_GROWUP >= 2 || w_.t.round - w_.qGrowHeard <= kGrowHeardAge))
            for (World::Seen const& s : w_.others)
                if (!isEnemy(s) && s.id < 2 && s.visible < kGrowUpTo) growHead_ = s.head;
    }
#if BC_GROWUP_FEED
    bool growFeed() const {
        return growHead_ >= 0 && !(BC_QUEEN && queenMe_) && !myDist_.empty() && L_ >= 2 && L_ <= kGrowFeedMaxLen &&
               w_.t.units > kGrowFeedKeep && distToHead(myDist_, growHead_) <= p_.feedDist && backlog(growHead_) == 0 &&
               !enemyNear(growHead_, 2);
    }
#endif
    // her QSK growth arm (the nearest pearl first while no threat is within her reach + 3): BC_QUEEN_GROW's, or while
    // she asks
    bool growArm() const { return (BC_QUEEN_GROW && L_ < kGrowTo) || w_.qGrowAsk(); }
    // a step's eat weight, as the growth arm's: a teammate leaves her food (within kGrowYield of her head) to her; she
    // eats at x2.5 (< 5 long) / x1.5 while she asks
    double growEat(int dest, int L, double eatW) const {
        if (BC_QUEEN_GROW) return eatW;  // BC_QUEEN_GROW's growth arm already weighs it
        if (growHead_ >= 0 && w_.board.cheb(dest, growHead_) <= kGrowYield) eatW = 0.0;
        if (queenMe_ && w_.qGrowAsk()) eatW *= L < 5 ? 2.5 : 1.5;
        return eatW;
    }
#endif
#if BC_HARVEST
    // BC_HARVEST: the room harvest. A harvest room: the landing of a paired portal side (taken raw: a gate may wall it)
    // flooded over open edges through known tiles - closed (no open edge onto a never-seen tile), 2..kHarvestRoomMax tiles,
    // the portal's outside tile not in it, a bed refilling within kHarvestGap (the largest countdown seen, else the mirrored
    // gap)
    void findHarvestRooms() {
        Board const& b = w_.board;
        hRooms_.clear();
        auto known = [&](int c) { return w_.seenRound[c] >= 0 || w_.mirrored[c]; };
        for (int c = 0; c < b.NC; c++) {
            if (!b.portalSide[c] || !known(c)) continue;
            for (int d = 0; d < 4; d++) {
                Edge const& e = b.side(c, d);
                if (e.kind != 2 || e.partnerOrient < 0) continue;
                int land = b.stepRaw(c, d);
                if (land < 0 || !known(land)) continue;
                std::vector<int> tiles{land};
                bool ok = true;
                for (size_t i = 0; i < tiles.size() && ok; i++)
                    for (int k = 0; k < 4 && ok; k++) {
                        if (b.side(tiles[i], k).kind != 0) continue;
                        int m = b.id(b.X(tiles[i]) + DX[k], b.Y(tiles[i]) + DY[k]);
                        if (std::find(tiles.begin(), tiles.end(), m) != tiles.end()) continue;
                        if (!known(m) || m == c || static_cast<int>(tiles.size()) >= kHarvestRoomMax) ok = false;
                        else tiles.push_back(m);
                    }
                if (!ok || tiles.size() < 2) continue;
                bool fast = false;
                for (int t : tiles)
                    if (b.bed[t] == 1) {
                        int g = w_.cdObs[t] > 0 ? w_.maxCd[t] : b.gapMax[t];
                        if (g > 0 && g <= kHarvestGap) fast = true;
                    }
                if (fast) hRooms_.push_back({c, d, land, tiles});
            }
        }
    }
    static bool inTiles(std::vector<int> const& v, int c) { return std::find(v.begin(), v.end(), c) != v.end(); }
    // the checks before a harvester goes in: the landing known (seen or mirrored), no known 1-tile room behind the door
    // (World's DEAD test: no open edge), this door face not a DEAD / TRAP gate. Not the TRAPSET's portal-id mask: on some
    // maps a harvest room's door and a trap room's door are the two faces of one portal edge (one id), so the mask would wall
    // the harvest room too (in paired test games the harvest-room entries almost vanished while the trap entries stayed)
    bool harvestDoorOk(int c, int d, int land) const {
        if (w_.seenRound[land] < 0 && !w_.mirrored[land]) return false;
        if (w_.openEdges(land) == 0) return false;
#if BC_MAPNET
        int const gs = w_.portalState(c, d);
        if (gs == comms::G_DEAD || gs == comms::G_TRAP) return false;
#endif
        return true;
    }
    // the rooms; the one the head is in; or, for an idle non-queen of 2-3 (no meal for kHarvestIdle rounds) with room for two more
    // units, in the expand mode or under half the unit limit, the nearest free entry (no teammate's head in the room or by its entry)
    void planHarvest() {
        harvestEntry_ = harvestDir_ = harvestLand_ = harvestIn_ = -1;
        if (BC_QUEEN && queenMe_) return;
        int const limit = w_.init.unitLimit;
        bool mode = false;
#if BC_EXPAND_OPENING
        mode = w_.openMode;
#endif
        bool const eligible = L_ >= 2 && L_ <= 3 && w_.t.units <= limit - 2 && (mode || w_.t.units < limit / 2) &&
                              w_.t.round - std::max(w_.lastEatRound, w_.firstRound) >= kHarvestIdle;
        if (!eligible && w_.harvestRound < 0) return;
        findHarvestRooms();
        for (size_t i = 0; i < hRooms_.size(); i++)
            if (inTiles(hRooms_[i].tiles, w_.head)) harvestIn_ = static_cast<int>(i);
        if (harvestIn_ >= 0 || !eligible || myDist_.empty()) return;
        Board const& b = w_.board;
        int bestD = INF;
        for (HRoom const& r : hRooms_) {
            int dc = myDist_[r.entry];
            if (dc >= bestD || w_.occ[r.land] >= 0 || !harvestDoorOk(r.entry, r.dir, r.land)) continue;
            bool taken = false;
            for (World::Seen const& s : w_.others)
                if (!isEnemy(s) && (inTiles(r.tiles, s.head) || b.cheb(s.head, r.entry) <= 1)) taken = true;
            if (taken) continue;
            bestD = dc, harvestEntry_ = r.entry, harvestDir_ = r.dir, harvestLand_ = r.land;
        }
    }
    // the routine: inside a room, split off the rear outside the room (>= 2 segments, the parent keeping >= 2) as soon as it can; else eat
    // or move inside; trapped at 4+, split 2 once more. At the entry, the step through the portal
    bool harvestAct(Choice& out) {
        Board const& b = w_.board;
#if BC_UNIT_SLOT
        bool const roomForUnit = w_.t.units < unitCap(w_.init.unitLimit);
#else
        bool const roomForUnit = w_.t.units < w_.init.unitLimit;
#endif
        if (harvestIn_ >= 0) {
            std::vector<int> const& room = hRooms_[harvestIn_].tiles;
            int k = 0;
            for (int i = static_cast<int>(w_.body.size()) - 1; i >= 0 && !inTiles(room, w_.body[i]); i--) k++;
            int best = -1, bestD = -1;
            bool bestEat = false;
            for (int d = 0; d < 4; d++) {
                int n = b.nb[w_.head * 4 + d];
                if (n < 0 || w_.occ[n] >= 0 || std::find(w_.body.begin(), w_.body.end(), n) != w_.body.end()) continue;
                bool eat = w_.visible(n) && w_.seenPearl[n];
                if (best < 0 || (eat && !bestEat)) best = n, bestD = d, bestEat = eat;
            }
            // the late split: eat while it can - each eat keeps the tail outside - and split L - 2 once the next step eats nothing
            // (or it is trapped), while the tail is still outside: the child (its head the old tail, outside; its body through the
            // portal) takes the eaten mass, the parent's last 2 segments stay. Mode 2 also splits in chunks while 3+ rear segments
            // are outside - all but one, so the parent keeps a tail outside and can split again (a pattern seen in strong teams'
            // replays)
            if (BC_HARVEST >= 2 && roomForUnit && w_.bodyKnown() && k >= 3 && L_ - (k - 1) >= 2) {
                out.split = true, out.n = k - 1, out.why = "harvest-chunk";
                return true;
            }
            if (roomForUnit && w_.bodyKnown() && k >= 1 && L_ - 2 >= 2 && (!bestEat || best < 0)) {
                out.split = true, out.n = L_ - 2, out.why = "harvest-split";
                return true;
            }
            if (best >= 0) {
                out.dir = bestD, out.dest = best, out.eat = bestEat, out.why = bestEat ? "harvest-eat" : "harvest-move";
                return true;
            }
            if (roomForUnit && w_.bodyKnown() && L_ >= 4) {
                out.split = true, out.n = 2, out.why = "harvest-last";
                return true;
            }
#if BC_ECON_TRACE
            econTrace_ += " harvest-end";  // the parent's death: the harvest's cost, not a suicide
#endif
            return false;
        }
        // in only onto a pearl: the eat keeps the tail outside, so the rear can split out (a step that eats nothing pulls it in)
        if (harvestEntry_ >= 0 && w_.head == harvestEntry_ && w_.occ[harvestLand_] < 0 && !enemyNear(harvestLand_, 1) &&
            w_.visible(harvestLand_) && w_.seenPearl[harvestLand_] && !(L_ > 1 && harvestDir_ == (w_.t.dir ^ 2))) {
            out.dir = harvestDir_, out.dest = harvestLand_, out.eat = w_.visible(harvestLand_) && w_.seenPearl[harvestLand_];
            out.why = "harvest-in";
            w_.harvestRound = w_.t.round;
            return true;
        }
        return false;
    }
#endif
    // ---- BC_ECON_DIP: dead-end / fountain harvest -----------------------------------------------------------------------
    bool econTraceOn() const { return BC_ECON_TRACE != 0; }
    // a pearl we may eat in the next k turns on tile c: one in view now, belief >= 0.5, or a bed spawning within k
    bool pearlSoon(int c, int k) const {
        return belief_[c] >= 0.5 || (countdown_[c] >= 0 && countdown_[c] <= k);
    }
    // Pearls ahead of `from` inside the dead-end tree entered at tile n (we came from `from`): cells reachable over deadEnd
    // tiles without passing `from` or a body, at most 12 cells; returns the count, depth of the farthest food in `depthOut`.
    int laneFood(int n, int from, std::vector<int> const* ownBody, int* depthOut = nullptr, int* cellsOut = nullptr) const {
        Board const& b = w_.board;
        int food = 0, depthFar = 0, cells = 0;
        std::vector<int> q{n}, dep{1}, par{from};
        for (size_t i = 0; i < q.size() && cells < 12; i++) {
            int c = q[i];
            cells++;
            if (pearlSoon(c, 3)) food++, depthFar = std::max(depthFar, dep[i]);
            for (int d = 0; d < 4; d++) {
                int m = b.nb[c * 4 + d];
                if (m < 0 || m == par[i] || !b.deadEnd[m] || w_.occ[m] >= 0) continue;
                bool seen = false;
                for (int x : q) seen |= x == m;
                if (ownBody)
                    for (int x : *ownBody) seen |= x == m;
                if (seen) continue;
                q.push_back(m), dep.push_back(dep[i] + 1), par.push_back(c);
            }
        }
        if (depthOut) *depthOut = depthFar;
        if (cellsOut) *cellsOut = cells;
        return food;
    }
    int laneAheadFood(int n, int from, std::vector<int> const& body) const { return laneFood(n, from, &body); }
    // The entry gate (id >= 2, not the king, units <= limit - 2, not rebuilding / tiny army, enough food, a clear lane, no
    // enemy reaching the mouth within the dip's length, our side of the map, no lower-id teammate at the mouth).
    std::vector<int> dipCache_;  // per turn: mouth * 4096 ... -> 0 unknown, 1 ok, 2 no (keyed by mouth, lane tile)
    std::vector<std::pair<long, char>> dipMemo_;
    int dipMemoRound_ = -1;
    bool dipOk(int mouth, int n, int Lnew) {
        if (w_.init.id < 2 || kingMe_ || (BC_QUEEN && queenMe_) || rebuild_ || w_.t.units < kDipMinUnits ||
            w_.t.units > w_.init.unitLimit - 2 || !w_.bodyKnown()) {
            if (econTraceOn()) econTrace_ += " dip-no6";
            return false;
        }
        if (dipMemoRound_ != w_.t.round) dipMemo_.clear(), dipMemoRound_ = w_.t.round;
        long key = static_cast<long>(mouth) * 100000L + n * 8L + std::min(Lnew, 7);
        for (auto const& e : dipMemo_)
            if (e.first == key) return e.second != 0;
        dipWhy_ = 0;
        bool ok = dipGate(mouth, n, Lnew);
        if (econTraceOn() && !ok) econTrace_ += " dip-no" + std::to_string(dipWhy_);
        dipMemo_.push_back({key, static_cast<char>(ok)});
        return ok;
    }
    int dipWhy_ = 0;
    bool dipGate(int mouth, int n, int Lnew) {
        Board const& b = w_.board;
        int depthFar = 0, cells = 0;
        int E = laneFood(n, mouth, &w_.body, &depthFar, &cells);
        if (E < std::max(kDipMinFood, 4 - Lnew)) return dipWhy_ = 1, false;
        // lane clear: no other dragon's head within Chebyshev 3 of the mouth, none inside the lane
        for (World::Seen const& s : w_.others) {
            if (b.cheb(s.head, mouth) <= kDipMouthR) {
                if (isEnemy(s) || s.id < w_.init.id) return dipWhy_ = 2, false;  // an enemy at the mouth; a lower id takes it first
            }
        }
        // enemies reaching the mouth within the dip: ceil(steps / free steps) must exceed depth + slack (bodies are walls)
        std::vector<int> dist, par, dirTo;
        bfsFrom(mouth, 16, [&](int c) { return w_.occ[c] < 0 || c == mouth; }, dist, par, dirTo);
        int turnsAllowed = depthFar + kDipReachSlack;
        for (World::Seen const& s : w_.others) {
            if (!isEnemy(s)) continue;
            int le = std::max(std::max(s.visible, 4), (b.cheb(s.head, w_.head) >= 3) ? w_.enemyLongest : 0);
            int F = sprintFree(le);
            int d = dist[s.head];
            if (d == INF) d = distToHead(dist, s.head);  // a head cut off by bodies: its neighbours' distance
            if (d != INF && (d + F - 1) / F <= turnsAllowed) return dipWhy_ = 3, false;
            if (d == INF && b.cheb(s.head, mouth) <= 6) return dipWhy_ = 3, false;
        }
        for (auto const& e : w_.know.enemies) {  // remembered heads (1-3 rounds old, out of sight)
            int age = w_.t.round - e.round;
            if (age < 1 || age > 3 || w_.visible(e.cell)) continue;
            int le = std::max(e.len, 4), F = sprintFree(le);
            int d = dist[e.cell];
            if (d == INF) d = TeamPlan::torus(b, mouth, e.cell);
            if ((std::max(0, d - age * F) + F - 1) / F <= turnsAllowed) return dipWhy_ = 4, false;
        }
        // our side: the mouth nearer to our heads than to the enemy's (home score > 0)
        if (homeScoreRound_ != w_.t.round) buildHome(), homeScoreRound_ = w_.t.round;
        if (homeScore_.empty() || homeScore_[mouth] <= 0) return dipWhy_ = 5, false;
        return true;
    }
    int homeScoreRound_ = -1;
    // BC_ECON_DIP (c): the dragon sits at a lane's tip (all 4 moves illegal in the simulation), is long enough to split and
    // its tail cell has a free neighbour: the reverse split leaves a stub
    bool dipStubOk(std::vector<int> const& body, int L) const {
        Board const& b = w_.board;
        if (L < 4 || body.empty() || (BC_QUEEN && queenMe_) || w_.init.id < 2 || w_.t.units > w_.init.unitLimit - 2) return false;
        if (!b.deadEnd[body.front()]) return false;
        int tail = body.back();
        for (int d = 0; d < 4; d++) {
            int n = b.nb[tail * 4 + d];
            if (n < 0 || w_.occ[n] >= 0) continue;
            bool own = false;
            for (int x : body) own |= x == n;
            if (!own) return true;
        }
        return false;
    }
    // BC_ECON_DIP (d): in a lane (head on a dead-end tile), L >= 4, nothing ahead worth eating: SPLIT L - 2 (the child leaves
    // by our old tail; the 2-long stub stays). Exempt from splitEnemyDist and the room checks except the child's room.
    bool dipDone(Choice& out) {
        Board const& b = w_.board;
#if BC_LONGEST_RESERVE
        if (reserveMe_) return false;  // BC_LONGEST_RESERVE: no dip reverse for the reserve
#endif
        if (L_ < 4 || (BC_QUEEN && queenMe_) || w_.init.id < 2 || kingMe_ || !w_.bodyKnown() || !b.deadEnd[w_.head] ||
            w_.t.units >= w_.init.unitLimit || !BC_DEADEND)
            return false;
#if BC_UNIT_SLOT
        if (w_.t.units >= unitCap(w_.init.unitLimit)) return false;
#endif
        for (int d = 0; d < 4; d++) {  // the tile ahead: the head's facing
            int n = b.nb[w_.head * 4 + d];
            if (n < 0 || n == b.nb[w_.head * 4 + (w_.t.dir ^ 2)] || !b.deadEnd[n] || w_.occ[n] >= 0) continue;
            if (laneFood(n, w_.head, &w_.body) > 0) return false;  // food ahead: keep going
        }
        // no bed spawning within 2 in front either (a pearl on the tile ahead or next to it)
        for (int d = 0; d < 4; d++) {
            int n = b.nb[w_.head * 4 + d];
            if (n >= 0 && countdown_[n] >= 0 && countdown_[n] <= 2 && w_.occ[n] < 0) return false;
        }
        int n = L_ - 2;
        // the child (our tail end, acting later this round) needs room to leave
        int childNeed = std::min(static_cast<int>(p_.spaceFactor * n) + p_.spaceMargin, b.NC / 2);
        std::vector<int> blk2 = base_;
        for (size_t i = 0; i + 1 < w_.body.size(); i++) blk2[w_.body[i]] = INF;
        int tail = w_.body.back(), room = 0;
        for (int q = 0; q < 4 && room < childNeed; q++) {
            int m = b.nb[tail * 4 + q];
            if (m >= 0 && blk2[m] <= 1) room = std::max(room, nav_.flood(b, m, blk2, childNeed + 1) + 1);
        }
        if (room < childNeed) return false;
        out = Choice{};
        out.split = true;
        out.n = n;
        out.why = "dip-rev";
        if (econTraceOn()) econTrace_ += " dip-rev L" + std::to_string(L_);
        return true;
    }
    // ---- queen hunt (BC_QUEEN_HUNT): pursuit and regicide; escort (BC_QUEEN_ESCORT, off); the home map ----------------
    // Free-step pursuit: their queen in view, out of strike reach: a multi-step move (free steps only, L >= 5) that brings
    // our head at least 2 steps nearer her (approach on free steps, strike when in reach).
    bool queenPursue(Choice& out) {
        Board const& b = w_.board;
        int free = sprintFree(L_);
        if (L_ < BC_HUNT_PURSUE_MINLEN || free < 2) return false;
        int eh = -1;
        for (World::Seen const& s : w_.others)
            if (isEnemy(s) && s.id < 2) eh = s.head;
        if (eh < 0) return false;
        int qh = ourQueenHead();
        std::vector<int> dH, pH, xH, dM, pM, xM;
        auto passH = [&](int c) { return w_.occ[c] < 0 && !ownCell(c); };
        bfsFrom(eh, 64, passH, dH, pH, xH);
        int cur = INF;
        for (int d = 0; d < 4; d++) {
            int n = b.nb[w_.head * 4 + d];
            if (n >= 0 && dH[n] < INF) cur = std::min(cur, dH[n] + 1);
        }
        if (cur == INF) return false;
        bfsFrom(w_.head, free, [&](int c) { return passH(c) && (qh < 0 || b.cheb(c, qh) > 1); }, dM, pM, xM);
        int best = -1;
        for (int c = 0; c < b.NC; c++)
            if (dM[c] != INF && dM[c] >= 1 && dH[c] != INF && (best < 0 || dH[c] < dH[best] || (dH[c] == dH[best] && dM[c] < dM[best])))
                best = c;
        if (best < 0 || dH[best] > cur - 2 || dM[best] < 2) return false;
        std::vector<int> cells;
        std::string p = pathTo(w_.head, best, pM, xM, &cells);
        out = Choice{};
        out.dir = dirFromChar(p[0]);
        out.dest = -1;
        out.sprint = p;
        out.flee = cells;
        for (int c : cells) out.fleeEat.push_back(static_cast<char>(w_.seenRound[c] == w_.t.round && w_.seenPearl[c]));
        out.fleePaid = 0;
        out.why = "pursue-q";
        out.score = 1e7;
        return true;
    }
    // Regicide: the k = clamp(units / 8, 1, 3) of ours (DESPERATE: max(3, units / 3)) with the shortest ETA (steps / free
    // steps a turn) to the enemy queen's last known head (<= 10 rounds) go for her; the same rule everywhere, so the hunters
    // emerge without talking. Her head in view becomes a squad target (QKILL strikes).
    void planRegicide() {
        slayCell_ = -1;
        auto const& E = w_.eq;
        if (lean_ || queenMe_ || lock_ || myDist_.empty() || E.cell < 0 || E.dead || w_.t.round - E.round > BC_REGICIDE_AGE) return;
        Board const& b = w_.board;
        auto stepsTo = [&](std::vector<int> const& field, int from) {
            int d = field[E.cell];
            if (d == INF) d = distToHead(field, E.cell);
            return d == INF ? TeamPlan::torus(b, from, E.cell) + 4 : d;
        };
        auto eta = [&](int d, int len) { return static_cast<double>(d) / std::max(1, sprintFree(len)); };
        double mine = eta(stepsTo(myDist_, w_.head), L_);
        int rank = 0, me = w_.init.id & 4095;
        for (FriendField const& f : friends_) {
            int fl = 2;
            for (World::Seen const& s : w_.others)
                if (s.id == f.id) fl = s.visible;
            if (f.id < 2) continue;  // never our queen
            double e = eta(stepsTo(f.dist, f.head), fl);
            if (e < mine || (e == mine && f.id < w_.init.id)) rank++;
        }
        for (auto const& fr : w_.know.friends) {
            int age = w_.t.round - fr.round;
            if (fr.id == me || fr.id < 2 || age > 2 || w_.visible(fr.cell)) continue;
            if (eta(TeamPlan::torus(b, fr.cell, E.cell) + age, fr.len) < mine) rank++;
        }
        int k = desperate_ ? std::max(3, w_.t.units / 3) : std::max(1, std::min(3, w_.t.units / BC_REGICIDE_PER));
        if (rank >= k) return;
        slayCell_ = E.cell;
        feedHead_ = feedPull_ = -1;
        for (World::Seen const& s : w_.others)
            if (isEnemy(s) && s.id < 2 && !huntAt(s.head)) hunts_.push_back({&s, static_cast<double>(s.visible), Job::Attack, false});
    }
    // Escort (emergent): our queen in view and an enemy head (in view or heard <= 3 rounds) within 8 of her; the nearest
    // two of ours within 5 of her (LOCK: four within 10) take the tile on that head's shortest way to her, 2 steps from her.
    void planEscort() {
        escortCell_ = -1;
        if (queenMe_ || L_ < 2) return;
        Board const& b = w_.board;
        int qh = ourQueenHead();
        if (qh < 0) return;
        int reachMe = lock_ ? 10 : 5, cap = lock_ ? 4 : 2;
        int mine = b.cheb(w_.head, qh);
        if (mine > reachMe) return;
        int eh = -1, ed = 99;
        for (World::Seen const& s : w_.others)
            if (isEnemy(s) && b.cheb(s.head, qh) < ed) ed = b.cheb(s.head, qh), eh = s.head;
        for (auto const& e : w_.know.enemies)
            if (w_.t.round - e.round <= 3 && b.cheb(e.cell, qh) < ed) ed = b.cheb(e.cell, qh), eh = e.cell;
        if (eh < 0 || ed > 8) return;
        int rank = 0;
        for (World::Seen const& s : w_.others)
            if (!isEnemy(s) && s.id >= 2 && (b.cheb(s.head, qh) < mine || (b.cheb(s.head, qh) == mine && s.id < w_.init.id))) rank++;
        if (rank >= cap) return;
        std::vector<int> dQ, pQ, xQ, dE, pE, xE;
        auto pass = [&](int c) { return w_.occ[c] < 0 || c == qh || c == eh; };
        bfsFrom(qh, 16, pass, dQ, pQ, xQ);
        bfsFrom(eh, 16, pass, dE, pE, xE);
        if (dE[qh] == INF) return;
        int best = -1;
        for (int c = 0; c < b.NC; c++) {
            if (dQ[c] == INF || dE[c] == INF || c == qh || c == eh || w_.occ[c] >= 0) continue;
            if (dQ[c] + dE[c] != dE[qh] || dQ[c] > 2) continue;
            if (best < 0 || dQ[c] > dQ[best] || (dQ[c] == dQ[best] && b.cheb(c, w_.head) < b.cheb(best, w_.head))) best = c;
        }
        escortCell_ = best;
    }
    // Home (the queen): our side's interior - tiles far from the enemy heads we know of and near ours (geography only).
    void buildHome() {
        Board const& b = w_.board;
        auto field = [&](std::vector<int> const& src) {
            std::vector<int> d(b.NC, INF);
            std::vector<int> q;
            for (int c : src)
                if (d[c] == INF) d[c] = 0, q.push_back(c);
            for (size_t i = 0; i < q.size(); i++)
                for (int k = 0; k < 4; k++) {
                    int n = b.nb[q[i] * 4 + k];
                    if (n >= 0 && d[n] == INF) d[n] = d[q[i]] + 1, q.push_back(n);
                }
            return d;
        };
        std::vector<int> es, fs;
        for (World::Seen const& s : w_.others) (isEnemy(s) ? es : fs).push_back(s.head);
        for (auto const& e : w_.know.enemies)
            if (w_.t.round - e.round <= 30) es.push_back(e.cell);
        if (w_.eq.cell >= 0 && !w_.eq.dead && w_.t.round - w_.eq.round <= 30) es.push_back(w_.eq.cell);
        for (auto const& f : w_.know.friends)
            if (w_.t.round - f.round <= 10 && f.id != (w_.init.id & 4095)) fs.push_back(f.cell);
        std::vector<int> dE = field(es), dF = field(fs);
        homeScore_.assign(b.NC, 0);
        for (int c = 0; c < b.NC; c++)
            homeScore_[c] = std::min(dE[c] == INF ? 30 : dE[c], 30) - std::min(dF[c] == INF ? 30 : dF[c], 30);
    }
    // ---- free-sprint foraging (rules: ceil(L/4) steps a turn cost nothing) -------------------------------------------------
    // When the search's chosen step is the first step of a shortest way to the nearest food (a pearl seen in the last 2
    // rounds, or a bed spawning by the time we get there), carry on along that way for up to ceil(L/4) steps in all, never
    // paying: every step onto a visible tile no dragon holds and our body has left (one tail cell per step that eats
    // nothing), not back onto the path, no portal edge; stop on the first pearl; the landing must keep m >= 1 against the
    // enemy heads (the QSK margins) and leave room >= L + 2.
#if BC_SPRINT_TARGET & 1
    bool freeSprint(Choice& out, Choice const& best, bool contested = false) {
#else
    bool freeSprint(Choice& out, Choice const& best) {
#endif
        Board const& b = w_.board;
        int F = sprintFree(L_);
        if (BC_ECON_B0 && BC_QUEEN && queenMe_) return false;  // BC_ECON_B0: the queen's multi-steps come from QSK only
        if (F < 2 || lean_ || best.split || best.dest < 0 || !best.sprint.empty() || best.eat) return false;
        static char const* const kOk[] = {"forage", "scout", "disperse", "detach", "team-target"};
        bool okWhy = false;
        for (char const* w : kOk) okWhy |= best.why == w;
        if (!okWhy) return false;  // never on a trade, a feed, an escort, a probe, a pocket exit or a cramped / trapped step
        if (static_cast<int>(w_.body.size()) < L_ || w_.body.empty()) return false;  // her tail must be known
        if (b.side(w_.head, best.dir).kind == 2) return false;
        std::vector<int> fd(b.NC, INF), q;
        for (int c = 0; c < b.NC; c++) {
            bool pearl = w_.seenPearl[c] && w_.seenRound[c] >= w_.t.round - 2;
            bool soon = !countdown_.empty() && countdown_[c] >= 0 && countdown_[c] <= F + 1;
#if BC_SPRINT_TARGET & 1
            if (contested) {  // BC_SPRINT_TARGET bit 1 (off): only a fresh pearl in view an enemy head contests
                soon = false;
                pearl = pearl && w_.seenRound[c] == w_.t.round && !enemyDist_.empty() && enemyDist_[c] < INF &&
                        !myDist_.empty() && enemyDist_[c] <= myDist_[c] + kGrabSlack;
            }
#endif
            if ((pearl || soon) && w_.occ[c] < 0) fd[c] = 0, q.push_back(c);
        }
        for (size_t i = 0; i < q.size(); i++)
            for (int d = 0; d < 4; d++) {
                int n = b.nb[q[i] * 4 + d];
                if (n < 0 || fd[n] != INF || w_.occ[n] >= 0) continue;
                fd[n] = fd[q[i]] + 1;
                q.push_back(n);
            }
        int at = best.dest;
        if (fd[at] == INF || fd[at] == 0) return false;  // no food ahead, or the first step eats already
        int hd = INF;
        for (int d = 0; d < 4; d++) {
            int n = b.nb[w_.head * 4 + d];
            if (n >= 0 && fd[n] < INF) hd = std::min(hd, fd[n] + 1);
        }
        if (fd[at] + 1 != hd) return false;  // the search's step is not on a shortest way to the nearest food
        std::vector<int> cells{at};
        std::vector<char> eats{0};
        std::string path(1, DCH[best.dir]);
        int eatsSoFar = 0;
        for (int s = 2; s <= F && fd[at] > 0; s++) {
            int vacated = (s - 1) - eatsSoFar;
            int occupiedUpTo = static_cast<int>(w_.body.size()) - 1 - vacated;
            int nxt = -1, nd = -1;
            for (int d = 0; d < 4 && nxt < 0; d++) {
                int n = b.nb[at * 4 + d];
                if (n < 0 || fd[n] != fd[at] - 1 || !w_.visible(n) || w_.occ[n] >= 0 || b.side(at, d).kind == 2) continue;
                if (n == w_.head || std::find(cells.begin(), cells.end(), n) != cells.end()) continue;
                bool own = false;
                for (int i = 0; i <= occupiedUpTo && i < static_cast<int>(w_.body.size()); i++) own |= w_.body[i] == n;
                if (own) continue;
                nxt = n, nd = d;
            }
            if (nxt < 0) break;
            bool eat = w_.seenRound[nxt] == w_.t.round && w_.seenPearl[nxt];
            cells.push_back(nxt);
            eats.push_back(static_cast<char>(eat));
            path.push_back(DCH[nd]);
            eatsSoFar += eat;
            at = nxt;
            if (eat) break;
        }
        if (cells.size() < 2) return false;
        Qsk qs(w_, L_);
        qs.threats();
        if (qs.mOf(qs.mB, at) < 1) return false;  // the landing must stay out of every known enemy reach
        std::vector<int> nb2(cells.rbegin(), cells.rend());
        int keep = static_cast<int>(w_.body.size()) - (static_cast<int>(cells.size()) - eatsSoFar);
        for (int i = 0; i < keep; i++) nb2.push_back(w_.body[i]);
        if (qs.room(at, nb2, L_ + eatsSoFar + 2) < L_ + eatsSoFar + 2) return false;
        out = Choice{};
        out.dir = best.dir;
        out.dest = -1;
        out.sprint = path;
        out.flee = cells;
        out.fleeEat = eats;
        out.fleePaid = 0;
        out.why = "free-sprint";
        out.score = best.score;
        return true;
    }
#if BC_R4NET
#if BC_R5_GATE
    // BC_R5_GATE 1 (off): the free steps are tried with probability min(1, BC_R5_KAPPA x P) (P = the net's P(multi-step |
    // the first step)) instead of P >= BC_R4_SPRINTP. A calibrated net makes the sprint rate of the turns with free steps the
    // demonstrations' own (and 1.5 x P asks for more where the landing checks refuse many); the random number is a hash of
    // (round, id, head), the same on every run of the same game.
    bool r5Gate(double p) const {
        uint32_t h = static_cast<uint32_t>(w_.t.round) * 2654435761u ^ static_cast<uint32_t>(w_.init.id) * 40503u ^ static_cast<uint32_t>(w_.head) * 2246822519u;
        h ^= h >> 15;
        h *= 2246822519u;
        h ^= h >> 13;
        return static_cast<double>(h & 0xFFFFFF) < std::min(1.0, BC_R5_KAPPA * p) * 16777216.0;
    }
#endif
#if BC_R7_KINDW_ON
    // BC_R7_KINDW_ON 1 (off): the prior weight multiplier of the kind of a step (class 0 positioning, 1 tactical, 2 travel / team / queen; BC_R7_KW0 / KW1 / KW2)
    static double r7KindMult(std::string const& why) {
        static char const* const kPos[] = {"forage", "scout", "disperse", "detach", "idle-bed", "expand", "leave-pocket", "probe", "eat", "harvest", "harvest-in", "harvest-last", "harvest-chunk", "harvest-split", "reverse", "dip-rev"};
        static char const* const kTac[] = {"hunt", "slay", "finish", "trade", "sprint-trade", "support", "escort", "escort-q", "pursue-q", "qkill", "qguard-trade", "qguard-block"};
        for (char const* w : kPos)
            if (why == w) return BC_R7_KW0;
        for (char const* w : kTac)
            if (why == w) return BC_R7_KW1;
        return BC_R7_KW2;
    }
#endif
#if BC_R4_LAND
    // BC_R4_LAND 1: the landing rule of a non-queen (she is the one that needs QSK's margins), O(enemy heads): every known
    // enemy head - in view, or seen 1-3 rounds ago - lies farther (Chebyshev, a lower bound of its path distance) from the
    // landing than its FREE reach + 1 (ceil(L / 4) free steps; a head whose tail the window cuts counts as at least visible
    // + 1 and 6 long; a head seen `age` rounds ago has moved `age` more times). Replaces QSK's threat map and the clock guard
    // (code 10) in r4Sprint.
    bool r4LandOk(int at) const {
        Board const& b = w_.board;
        for (World::Seen const& s : w_.others)
            if (isEnemy(s)) {
                int const le = std::max(s.visible + 1, 6);
                if (b.cheb(s.head, at) <= (le + 3) / 4 + BC_R5_LANDM) return false;
            }
        for (auto const& e : w_.know.enemies) {
            int const age = w_.t.round - e.round;
            if (age < 1 || age > 3 || w_.visible(e.cell)) continue;
            int const le = std::max(e.len, 4);
            if (b.cheb(e.cell, at) <= (le + 3) / 4 * (age + 1) + BC_R5_LANDM) return false;
        }
        return true;
    }
#endif
    // BC_R4_SPRINT bit 2: the free steps strong teams spend every turn they have two or more. After the search's single step
    // (a plain forage / scout / disperse / detach / team-target step that eats nothing) carry on for up to
    // min(ceil(L / 4), BC_R4_MAXSTEPS) steps, each to the legal cell (forward / right / left of the previous step, visible,
    // no dragon on it, our own tail has left it, no portal edge) where the net's second-step head is highest (a pearl first
    // among near ties); stop on the first pearl. freeSprint's landing checks: the margin against every known enemy reach >= 1
    // and room >= L + 2.
    bool r4Sprint(Choice& out, Choice const& best) {
        Board const& b = w_.board;
        int const F = std::min(sprintFree(L_), BC_R4_MAXSTEPS);
        if (BC_QUEEN && queenMe_) return false;
        bool const broad = (BC_R4_SPRINT & 4) != 0;  // bit 4: also a step that eats, and the pocket exits / expansions / bed waits / the finish (in test games our long dragons are mostly in those modes)
        if (F < 2) return r4SprWhy_ = 1, false;
        if (lean_ || best.split || best.dest < 0 || !best.sprint.empty() || (best.eat && !broad)) return r4SprWhy_ = 2, false;
        static char const* const kOk[] = {"forage", "scout", "disperse", "detach", "team-target"};
        static char const* const kBroad[] = {"leave-pocket", "expand", "idle-bed", "finish", "eat"};
        bool okWhy = false;
        for (char const* w : kOk) okWhy |= best.why == w;
        if (broad)
            for (char const* w : kBroad) okWhy |= best.why == w;
#if BC_R4_SPRINT & 8
        {  // BC_R4_SPRINT bit 8 (off): the travel kinds too (in a traced game about a quarter of the refusals were to-feed / to-probe or cramped steps)
            static char const* const kTravel[] = {"to-feed", "to-feed-heard", "to-probe", "cramped"};
            for (char const* w : kTravel) okWhy |= best.why == w;
        }
#endif
#if BC_R5_KINDS & 1
        okWhy |= best.why == "slay";  // BC_R5_KINDS bit 1 (off): the squad's slay step (a step toward a long enemy; the most common kind refusal in testing); the landing checks still apply
#endif
        if (!okWhy) return r4SprWhy_ = 3, false;
        if (static_cast<int>(w_.body.size()) < L_ || w_.body.empty()) return r4SprWhy_ = 4, false;
        if (b.side(w_.head, best.dir).kind == 2) return r4SprWhy_ = 5, false;
        int at = best.dest, dprev = best.dir, eatsSoFar = best.eat ? 1 : 0;
        std::vector<int> cells{at};
        std::vector<char> eats{static_cast<char>(best.eat)};
        std::string path(1, DCH[best.dir]);
        for (int s = 2; s <= F; s++) {
            int const vacated = (s - 1) - eatsSoFar;
            int const occupiedUpTo = static_cast<int>(w_.body.size()) - 1 - vacated;
            int nxt = -1, nd = -1;
            double bp = -1e9;
            for (int rel2 : {0, 1, 3}) {  // back is the neck
                int const d = (dprev + rel2) & 3;
                int const n = b.nb[at * 4 + d];
                if (n < 0 || !w_.visible(n) || w_.occ[n] >= 0 || b.side(at, d).kind == 2) continue;
                if (n == w_.head || std::find(cells.begin(), cells.end(), n) != cells.end()) continue;
                bool own = false;
                for (int i = 0; i <= occupiedUpTo && i < static_cast<int>(w_.body.size()); i++) own |= w_.body[i] == n;
                if (own) continue;
                double pr = (s == 2 ? r4S2_[rel2] : (rel2 == 0 ? 1.0 : 0.0)) + (w_.seenRound[n] == w_.t.round && w_.seenPearl[n] ? 0.25 : 0.0);
                if (pr > bp) bp = pr, nxt = n, nd = d;
            }
            if (nxt < 0) break;
            bool const eat = w_.seenRound[nxt] == w_.t.round && w_.seenPearl[nxt];
            cells.push_back(nxt);
            eats.push_back(static_cast<char>(eat));
            path.push_back(DCH[nd]);
            eatsSoFar += eat;
            at = nxt;
            dprev = nd;
            if (eat) break;
        }
        if (cells.size() < 2) return r4SprWhy_ = 6, false;
        // free steps on every turn cost the queen's end-game survival in testing: no cell of the path within BC_R4_QRING of our queen's head (BREATHE's bubble is 1) and never her last exit
        for (int c : cells) {
            if (guardQh_ >= 0 && b.cheb(c, guardQh_) <= BC_R4_QRING) return r4SprWhy_ = 9, false;
#if BC_QUEEN_BOXGUARD & 1
            if (c == boxExit_) return r4SprWhy_ = 9, false;
#endif
        }
        // the landing checks run after the search. QSK's threat map (up to ~10M points: in testing an unguarded variant hit 87.4M
        // in a game where the baseline peaked at 81.3M; the hard line is 95M) is needed only where an enemy is known - a part in
        // view, or a head seen 1-3 rounds ago - and then only while the clock is BC_R4_SLACK short of the search deadline; with
        // no enemy known the landing margin is trivially safe and only the room check (a bounded BFS) runs
        bool enemyKnown = false;
        for (Part const& pt : w_.t.parts)
            if (pt.team != w_.init.team) {
                enemyKnown = true;
                break;
            }
        if (!enemyKnown)
            for (auto const& e : w_.know.enemies) {
                int const age = w_.t.round - e.round;
                if (age >= 1 && age <= 3 && !w_.visible(e.cell)) {
                    enemyKnown = true;
                    break;
                }
            }
        Qsk qs(w_, L_);
#if BC_R4_LAND
        if (enemyKnown && !r4LandOk(at)) return r4SprWhy_ = 7, false;  // BC_R4_LAND 1: the cheap rule, no threat map, no clock guard
#else
        if (enemyKnown) {
            if (clockNow() > deadline_ - BC_R4_SLACK) return r4SprWhy_ = 10, false;
            qs.threats();
            if (qs.mOf(qs.mB, at) < 1) return r4SprWhy_ = 7, false;  // the landing must stay out of every known enemy reach
        }
#endif
        std::vector<int> nb2(cells.rbegin(), cells.rend());
        int const keep = static_cast<int>(w_.body.size()) - (static_cast<int>(cells.size()) - eatsSoFar);
        for (int i = 0; i < keep; i++) nb2.push_back(w_.body[i]);
        if (qs.room(at, nb2, L_ + eatsSoFar + BC_R5_ROOM) < L_ + eatsSoFar + BC_R5_ROOM) return r4SprWhy_ = 8, false;
        out = Choice{};
        out.dir = best.dir;
        out.dest = -1;
        out.sprint = path;
        out.flee = cells;
        out.fleeEat = eats;
        out.fleePaid = 0;
        out.why = "r4-sprint";
        out.score = best.score;
        return true;
    }
#endif
    // queen: is our queen still alive as far as the team knows (never heard of counts as alive; her facts stop when she dies)
    bool queenAlive() const {
        return w_.know.queenRound < 0 || w_.t.round - w_.know.queenRound <= kQueenGone;
    }
    int slayCell_ = -1;              // dragon plans, slay: last known head of the enemy king I hunt, -1 none
    struct Scav { int cell; double w; int ring; };  // ring 0: go to the drops; > 0: stand by this far off
    std::vector<Scav> scav_;
    std::vector<double> scavPow_;
    double sp(int m) const { return m < 0 ? 1.0 : m >= static_cast<int>(scavPow_.size()) ? 0.0 : scavPow_[m]; }
    std::vector<int> friendDist_; // min distance from any visible teammate head
    std::vector<int> friendId_;
    std::vector<int> enemyDist_;
    struct FriendField { int id; bool grower; int head; std::vector<int> dist; };
    std::vector<FriendField> friends_;
    struct EnemyField { int id; int head; int visible; std::vector<int> dist; };
    std::vector<EnemyField> enemies_;  // the nearest visible enemy heads, with their BFS fields
    std::vector<int> myDist_;     // from our head as it is now
    std::vector<Hunt> hunts_;
    int escortOf_ = -1;           // head tile of the grower we escort, -1 none
    int feedHead_ = -1;           // head of the longer teammate we are walking to, -1 none
    int feedPull_ = -1;           // heard-of longest teammate out of sight, -1 none
    TeamPlan team_;               // my local team and its target assignment (team.hpp)
    int leaderCell_ = -1;
    std::vector<int> remoteFriendDist_;  // torus Manhattan distance to the nearest heard teammate

    // BC_V9_KING / BC_V9_EKING: the king rule (the longest teammate heard of in kLongestAge rounds, never under kKingMin).
    bool v9IsKing() const {
        int me = w_.init.id & 4095;
        auto const& K = w_.know.longest;
        bool fresh = K.cell >= 0 && w_.t.round - K.round <= p_.kLongestAge;
        bool other = fresh && K.id != me && (K.len > L_ || (K.len == L_ && K.id < me));
        return !other && L_ >= p_.kKingMin;
    }
    // King module state and helpers (BC_V9_KING / BC_V9_EKING).
    bool v9King_ = false;         // no teammate known to be longer: we are the king
    bool v9KingNow_ = false;      // the king rule (v9IsKing) this turn, for the late king (BC_V9_KING) and the early king (BC_V9_EKING)
    bool v9Feeder_ = false;       // leaving to feed the king
    bool finalFeeder_ = false;    // final push: walking to the king to feed it
    int v9Way_ = -1;              // debug: the way to the king this turn
    int kingHead_ = -1;           // the king's head in view (not us), from kStreamFrom: pearls around it are its
    int kingZone_ = -1;           // the king's head in view while it plays the guard: others keep 3 tiles off
    int finishCell_ = -1;         // BC_V9_FINISH: the enemy head I hunt (seen or heard of), -1 none
    std::vector<int> goalDist_;   // steps from the feeding goal on the known map, empty when none
    // Distance field from `goal` on the known map (kelp and unpaired portals block, dragons do not: they move).
    void buildGoalField(int goal) {
        Board const& b = w_.board;
        std::vector<int> open(b.NC, 0);
        Nav nav;
        nav.bfs(b, goal, open, b.NC);
        goalDist_ = nav.dist;
    }
    // Pearls in view within 3 of a head: drops the king has yet to eat.
    int backlog(int head) const {
        Board const& b = w_.board;
        int n = 0;
        for (int dy = -3; dy <= 3; dy++)
            for (int dx = -3; dx <= 3; dx++) {
                int c = b.id(b.X(head) + dx, b.Y(head) + dy);
                if (w_.visible(c) && w_.seenPearl[c]) n++;
            }
        return n;
    }
    int kingYield() const { return BC_V9_KING ? 2 : p_.ekYield; }
    bool enemyNear(int cell, int r) const {
        for (World::Seen const& s : w_.others)
            if (isEnemy(s) && w_.board.cheb(s.head, cell) <= r) return true;
        return false;
    }

    // How many teammates are nearer to `cell` than we are (`mine` steps): those in view by their BFS fields
    // (head: to a tile next to it), those heard of in the last 2 rounds by torus distance plus their age.
    int rankAt(int cell, int mine, bool head) const {
        Board const& b = w_.board;
        int me = w_.init.id & 4095;
        int rank = 0;
        for (FriendField const& f : friends_) {
            if (f.head == cell) continue;
            int fd = head ? distToHead(f.dist, cell) : f.dist[cell];
            if (fd < mine || (fd == mine && f.id < w_.init.id)) rank++;
        }
        for (auto const& fr : w_.know.friends) {
            int age = w_.t.round - fr.round;
            if (fr.id == me || age > 2 || w_.visible(fr.cell)) continue;
            if (TeamPlan::torus(b, fr.cell, cell) + age < mine) rank++;
        }
        return rank;
    }

    bool ownCell(int c) const {
        for (int x : w_.body)
            if (x == c) return true;
        for (int x : w_.ownExtra)
            if (x == c) return true;
        return false;
    }

    bool isEnemy(World::Seen const& s) const { return s.team != w_.init.team; }
    // Everyone expands until the army is big (or late); only then do the starting dragons and long ones switch to growing
    // for the length tiebreak.
    bool isGrower(int id, int len) const {
        bool growPhase = w_.t.units >= p_.growerMinUnits || w_.t.round >= p_.growRound;
        if (p_.tacticsOn && vipId_ >= 0 && id == vipId_) return true;  // tactics library: the local VIP, any round
        return growPhase && (w_.isInitial(id) || len >= p_.promoteLen);
    }

    // ---- tactics library: read the situation, set this turn's tactic weights ----------
    int crowd_ = 0;               // own heads within crowdR
    int vipId_ = -1;              // longest teammate in view (me included) if >= vipMinLen
    bool vip_ = false;
    double dispW_ = 0;            // dispersal weight this turn (0: not crowded)
    std::vector<float> crowdField_;

    void computeTactics() {
        Board const& b = w_.board;
        crowd_ = 0;
        vipId_ = -1;
        vip_ = false;
        dispW_ = 0;
        if (!p_.tacticsOn) return;
        int bestLen = L_ >= p_.vipMinLen ? L_ : -1, bestId = w_.init.id;
        int longer = 0;
        for (World::Seen const& s : w_.others) {
            if (isEnemy(s)) continue;
            if (b.cheb(s.head, w_.head) <= p_.crowdR) crowd_++;
            if (s.visible > L_) longer++;
            if (s.visible >= p_.vipMinLen && (s.visible > bestLen || (s.visible == bestLen && s.id < bestId))) {
                bestLen = s.visible;
                bestId = s.id;
            }
        }
        if (bestLen >= p_.vipMinLen) vipId_ = bestId;
        vip_ = vipId_ == w_.init.id;
        if (crowd_ < p_.crowdStart) return;
        crowdField_.assign(b.NC, 0.0f);
        int R = p_.crowdR;
        for (World::Seen const& s : w_.others) {
            if (isEnemy(s)) continue;
            int hx = b.X(s.head), hy = b.Y(s.head);
            for (int dy = -R; dy <= R; dy++)
                for (int dx = -R; dx <= R; dx++)
                    crowdField_[b.id(hx + dx, hy + dy)] += static_cast<float>(R + 1 - std::max(std::abs(dx), std::abs(dy))) / (R + 1);
        }
        dispW_ = p_.wDisperse * std::min(4, crowd_ - p_.crowdStart + 1) * (vip_ ? p_.vipDisperse : 1.0) *
                 (1.0 + 0.15 * std::min(longer, 5));
    }
    // Head-on trades are one-for-one: worth it while our army is not tiny and we do
    // not give up much more length than we take.
    bool tradeOk(int myLen, int enemyLen) const {
        if (BC_QUEEN && queenMe_) return false;          // queen: the queen never trades (her death decides round 500)
        if (lockNoTrade()) return false;                 // queen LOCK mode: their queen is dead, ours lives - keep everybody
        if (w_.t.units <= kSurvivalUnits) return false;  // fixed survival policy: never with few units
        return w_.t.units >= p_.tradeMinUnits && myLen <= enemyLen + p_.tradeSlack;
    }
    bool rebuild_ = false;        // fixed survival policy: few units or isolated -> reproduce
    bool survival() const { return w_.t.units <= p_.survivalUnits; }

    // Whoever has more heads around a trade site eats most of the pearls the two dead dragons drop (in ladder replays the
    // side with more heads ate 2.5-4.5x more). Counts heads near `site`, leaving out ourselves and the enemy we would trade
    // with (`skip`, -1 none).
    bool localOk(int site, int skip) const {
        Board const& b = w_.board;
        int mine = 0, theirs = 0;
        for (World::Seen const& s : w_.others) {
            if (s.id == skip || b.cheb(s.head, site) > p_.localRadius) continue;
            (isEnemy(s) ? theirs : mine)++;
        }
        return mine >= theirs;
    }
    // A voluntary trade needs teammates at the site to eat the drops, at least as many as the enemy has there.
    bool supportOk(int site, int skip) const {
        Board const& b = w_.board;
        int mine = 0, theirs = 0;
        for (World::Seen const& s : w_.others) {
            if (s.id == skip || b.cheb(s.head, site) > p_.supportRadius) continue;
            (isEnemy(s) ? theirs : mine)++;
        }
        return mine >= p_.tradeSupport && mine >= theirs;
    }
    // Sprint rule: a dragon of length L takes its first ceil(L/4) steps free and pays a segment for each step after them,
    // keeping at least 2 (an under-paid sprint kills it): at most ceil(L/4) + L - 2 steps.
    static int sprintFree(int L) { return (L + 3) / 4; }
    static int sprintMaxSteps(int L) { return L < 2 ? 1 : sprintFree(L) + L - 2; }
    // How far an enemy head can move in one turn: its free steps always, paid ones up to reachCap (with BC_QUEEN_SAFE: two
    // tiers; equal to the old min(length - 1, reachCap) below 13 visible segments).
    int reachOf(int visible) const {
        if (!(BC_QUEEN && BC_QUEEN_SAFE)) return std::min(std::max(visible - 1, 1), p_.reachCap);  // the older reach model (before free sprint steps)
        int Le = std::max(visible, 2);
        return std::min(sprintMaxSteps(Le), std::max(p_.reachCap, sprintFree(Le)));
    }
    // queen: what the queen fears - any enemy will pay segments for her (her death decides round 500), up to kQueenReach steps
    int queenReachOf(int visible) const {
        int Le = std::max(visible, kQueenAssumeLen);  // a head at her view's edge shows 1-2 segments of a longer dragon
        return std::min(sprintMaxSteps(Le), std::max(kQueenReach, sprintFree(Le)));
    }
    bool midGame() const { return w_.t.round >= p_.earlyEnd; }
    bool lateGame() const { return w_.t.round >= p_.midEnd; }

    std::vector<int> ownBlk() const {
        std::vector<int> blk = base_;
        markBody(blk, w_.body, L_);
        return blk;
    }

    // Distance from a precomputed field to any tile next to `head` (the head itself is blocked).
    int distToHead(std::vector<int> const& dist, int head) const {
        Board const& b = w_.board;
        int best = INF;
        for (int k = 0; k < 4; k++) {
            int n = b.nb[head * 4 + k];
            if (n >= 0) best = std::min(best, dist[n]);
        }
        return best;
    }

    void buildBase() {
        Board const& b = w_.board;
        base_.assign(b.NC, 0);
        for (int c = 0; c < b.NC; c++)
            if (w_.occ[c] >= 0) base_[c] = INF;
        for (int c : w_.ownExtra) base_[c] = INF;
#if BC_TRAPPOCKET
        if (L_ >= 2)
            for (int c = 0; c < b.NC; c++)
                if (trapPocket(c)) base_[c] = INF;
#endif

        // Never take a teammate's last way out: with dozens of dragons in kelp
        // corridors that is how most of our own deaths happened.
        std::vector<int> blk = base_;
        markBody(blk, w_.body, L_);
        bool king = (BC_V9_KING && v9IsKing() && w_.t.round >= 500 - p_.kWindow - p_.kNoSplit - 100) ||
                    (BC_V9_EKING && kingMe_);
        for (World::Seen const& s : w_.others) {
            if (isEnemy(s) || king) continue;  // king: the king does not give way (long boxed-in kings died that way)
#if BC_OPEN_RISK
            if (openRisk(4) && s.id != w_.queenId()) continue;  // BC_OPEN_RISK bit 4 (off): a teammate's last way out is ours to take (never the queen's)
#endif
            int exits = 0, exit = -1;
            for (int d = 0; d < 4; d++) {
                int n = b.nb[s.head * 4 + d];
                if (n >= 0 && blk[n] <= 1) exits++, exit = n;
            }
            if (exits == 1) base_[exit] = INF;
        }
    }

#if BC_TRAPPOCKET
    // BC_TRAPPOCKET (off): a known tile with exactly one open (non-portal) side and portal sides that all land in known 1-tile rooms or are DEAD / TRAP
    bool trapPocket(int c) const {
        Board const& b = w_.board;
        if (w_.seenRound[c] < 0 && !w_.mirrored[c]) return false;
        int open = 0, traps = 0;
        for (int d = 0; d < 4; d++) {
            Edge const& e = b.side(c, d);
            if (e.kind == 0) {
                open++;
                continue;
            }
            if (e.kind != 2) continue;
            bool trap = false;
            int const land = b.stepRaw(c, d);
            if (land >= 0 && (w_.seenRound[land] >= 0 || w_.mirrored[land]) && w_.openEdges(land) == 0) trap = true;
#if BC_MAPNET
            int const gs = w_.portalState(c, d);
            if (gs == comms::G_DEAD || gs == comms::G_TRAP) trap = true;
#endif
            if (!trap) return false;  // a portal on to somewhere: a way out
            traps++;
        }
        return open == 1 && traps >= 1;
    }
#endif

#if BC_MN_VALUE
    int valueTarget_ = -1;      // BC_MAPNET_VALUE: the bed room (its anchor) this dragon is pulled to, -1 none
    double valueWeight_ = 0.0;  // ... kWValue x min(beds, 6) / 6
    // BC_MAPNET_VALUE: the best known bed room for a non-queen - reachable, not dangerous, none of ours there (by the fact's
    // held bit while fresh, and by the heads we see / heard within kValueHeldR of its anchor): beds x the far discount of its
    // distance
    void planValue() {
        valueTarget_ = -1;
        valueWeight_ = 0.0;
        if ((BC_QUEEN && queenMe_) || myDist_.empty()) return;
        Board const& b = w_.board;
        int const r = w_.t.round, me = w_.init.id & 4095;
#if BC_ECON_TRACE
        // trace counts for tuning kWValue: sent this turn, heard and relayed since the last count, and an arrival at last turn's
        // area ("vA1": it still has 2+ known beds and none of ours there but us, "vA0": not)
        if (w_.valueRound_ == r) econTrace_ += " vS";
        if (w_.valueHeardN) econTrace_ += " vH" + std::to_string(w_.valueHeardN);
        if (w_.valueRelayedN) econTrace_ += " vR" + std::to_string(w_.valueRelayedN);
        w_.valueHeardN = w_.valueRelayedN = 0;
#endif
        if (w_.valueLastTarget >= 0 && b.cheb(w_.head, w_.valueLastTarget) <= kValueHeldR) {
#if BC_ECON_TRACE
            bool mate = false;
            for (World::Seen const& s : w_.others)
                mate |= !isEnemy(s) && w_.heatOf(s.head) == w_.heatOf(w_.valueLastTarget);
            World::ValueRoom const* v = w_.valueRoom(w_.valueLastTarget);
            econTrace_ += (!mate && v && v->beds >= kValueMinBeds) ? " vA1" : " vA0";
#endif
            w_.valueLastTarget = -1;
        }
        double best = 0.0;
        for (World::ValueRoom const& v : w_.valueRooms) {
            int const d = myDist_[v.anchor];
            if (d >= INF || d < 2 || w_.danger(v.anchor) > 0.5) continue;
            if (v.held && r - v.round <= kValueHeldAge) continue;
            bool held = false;
            int const sec = w_.heatOf(v.anchor);
            for (World::Seen const& s : w_.others)
                if (!isEnemy(s) && (b.cheb(s.head, v.anchor) <= kValueHeldR || w_.heatOf(s.head) == sec)) held = true;
            for (auto const& f : w_.know.friends)
                if (r - f.round <= 10 && f.cell >= 0 && f.id != me && (b.cheb(f.cell, v.anchor) <= kValueHeldR || w_.heatOf(f.cell) == sec))
                    held = true;
            if (w_.heatOf(w_.head) == sec) continue;  // already there
            if (held) continue;
            double const sc = std::min(v.beds, 6) / 6.0 * fp(d);
            if (sc > best) best = sc, valueTarget_ = v.anchor;
        }
        if (valueTarget_ >= 0) valueWeight_ = kWValue * std::min(w_.valueRoom(valueTarget_)->beds, 6) / 6.0;
        if (valueTarget_ >= 0) w_.valueLastTarget = valueTarget_;
#if BC_ECON_TRACE
        econTrace_ += " vr" + std::to_string(w_.valueRooms.size()) +
                      (valueTarget_ >= 0 ? "@" + std::to_string(b.X(valueTarget_)) + "," + std::to_string(b.Y(valueTarget_)) : std::string());
#endif
    }
#endif

#if BC_OPEN_EXPAND
    int expandTarget_ = -1;  // BC_OPEN_EXPAND: the bed (or the unseen centre) this non-queen heads for in the opening, -1 none
    // ... and while it has one, the start tower's beds (within kOpenExpandHome of our first head) hold it no more: no wait for their
    // countdowns, no idle bed or VALUE area there (the pearls in sight still count)
    bool homeBed(int c) const { return w_.board.cheb(c, w_.firstHead) <= kOpenExpandHome; }
#if BC_OPEN_EXPAND_GATE
    // BC_OPEN_EXPAND_GATE: on when an enabled condition holds; decided once (bit 1 waits for its round)
    bool expandGateOn() {
        if (w_.expandGate >= 0) return w_.expandGate == 1;
        Board const& b = w_.board;
        bool on = false, decided = true;
        if (BC_OPEN_EXPAND_GATE & 4) {
            bool const small = b.W * b.H <= kGateMaxTiles;
            on |= w_.startUnits > 0 ? small && w_.startUnits <= kGateMaxStarts : small && (BC_OPEN_EXPAND_GATE & 8) != 0;
        }
        if (BC_OPEN_EXPAND_GATE & 16) on |= b.W * b.H <= kGateMaxTiles;  // the small map alone, whatever the start (UNIT_COUNT at r0 counts
                                                                         // the children of mates that split before this turn, so it can exceed the number of starters)
        if (BC_OPEN_EXPAND_GATE & 1) {
            if (w_.t.round < kGateBedRound || myDist_.empty()) {
                decided = false;
            } else {
                int n = 0;
                for (int c = 0; c < b.NC; c++)
                    if (b.bed[c] == 1 && (w_.seenRound[c] >= 0 || w_.mirrored[c]) && myDist_[c] <= kGateBedSteps) n++;
                on |= n <= kGateMaxBeds;
            }
        }
        if (on || decided) w_.expandGate = on ? 1 : 0;
#if BC_ECON_TRACE
        if (w_.expandGate >= 0) econTrace_ += w_.expandGate ? " xg1" : " xg0";
#endif
        return on;
    }
#endif
    double expandFar() const { return std::max(40.0, static_cast<double>(w_.board.W + w_.board.H)); }  // the pull's (and the choice's) fade
    // BC_OPEN_EXPAND: the known bed area with the most free beds - a known bed (seen or mirrored), reachable, not dangerous,
    // away from home (our first head) and the enemy's start (its mirror) by more than kOpenExpandHome, no other head of ours
    // nor an enemy head within kOpenExpandHeld; its beds = such beds within kOpenExpandBand - by beds x the pull's linear
    // fade; none: the map's centre while unseen. The escort (with 3+ of us known, the non-queen nearest our queen; ties by
    // id) and the queen keep their rules.
    void planOpenExpand() {
        expandTarget_ = -1;
        Board const& b = w_.board;
        if (w_.firstHead < 0) w_.firstHead = w_.head;
        int const r = w_.t.round, me = w_.init.id & 4095;
        if ((BC_QUEEN && queenMe_) || myDist_.empty() || r > kOpenExpandUntil) return;
#if BC_OPEN_EXPAND_GATE
        if (!expandGateOn()) return;
#endif
        std::vector<std::pair<int, int>> mates, foes;  // (id, head)
        for (World::Seen const& s : w_.others) (isEnemy(s) ? foes : mates).push_back({s.id, s.head});
        auto has = [](std::vector<std::pair<int, int>> const& v, int id) {
            for (auto const& m : v)
                if (m.first == id) return true;
            return false;
        };
        for (auto const& f : w_.know.friends)
            if (r - f.round <= kOpenExpandAge && f.cell >= 0 && f.id != me && !has(mates, f.id)) mates.push_back({f.id, f.cell});
        for (auto const& e : w_.know.enemies)
            if (r - e.round <= kOpenExpandAge && e.cell >= 0 && !has(foes, e.id)) foes.push_back({e.id, e.cell});
        int qh = -1;
        for (auto const& m : mates)
            if (m.first == w_.queenId()) qh = m.second;
        if (qh >= 0 && mates.size() >= 2) {
            int const md = b.cheb(w_.head, qh);
            bool escort = true;
            for (auto const& m : mates)
                if (m.first != w_.queenId() && (b.cheb(m.second, qh) < md || (b.cheb(m.second, qh) == md && m.first < me))) escort = false;
            if (escort) {
#if BC_ECON_TRACE
                econTrace_ += " xpE";
#endif
                return;
            }
        }
        int const theirs = b.sym > 0 ? b.mirror(w_.firstHead) : -1;
        auto freeBed = [&](int c) {
            if (b.bed[c] != 1 || (w_.seenRound[c] < 0 && !w_.mirrored[c])) return false;
            int cd = w_.next[c];
            if (cd < 0 && w_.seenRound[c] < 0 && b.sym > 0) cd = w_.next[b.mirror(c)];  // mirror tiles share one countdown
            if (cd > kOpenExpandSoon) return false;
            if (b.cheb(c, w_.firstHead) <= kOpenExpandHome || (theirs >= 0 && b.cheb(c, theirs) <= kOpenExpandHome)) return false;
            for (auto const& m : mates)
                if (b.cheb(c, m.second) <= kOpenExpandHeld) return false;
            for (auto const& f : foes)
                if (b.cheb(c, f.second) <= kOpenExpandHeld) return false;
            return true;
        };
        std::vector<char> ok(b.NC, 0);
        std::vector<double> wt(b.NC, 0.0);  // a free bed's weight: 1 refilling within kOpenExpandSoon, 1/2 countdown unknown
        for (int c = 0; c < b.NC; c++) {
            ok[c] = freeBed(c);
            if (ok[c]) wt[c] = (w_.next[c] >= 0 || (w_.seenRound[c] < 0 && b.sym > 0 && w_.next[b.mirror(c)] >= 0)) ? 1.0 : 0.5;
        }
        double best = 0.0;
        for (int c = 0; c < b.NC; c++) {
            if (!ok[c] || myDist_[c] >= INF || w_.danger(c) > 0.5) continue;
            double const fade = 1.0 - myDist_[c] / expandFar();
            if (fade <= 0.0) continue;
            double n = 0.0;
            for (int dy = -kOpenExpandBand; dy <= kOpenExpandBand; dy++)
                for (int dx = -kOpenExpandBand; dx <= kOpenExpandBand; dx++) {
                    int const x = b.X(c) + dx, y = b.Y(c) + dy;
                    if (x >= 0 && y >= 0 && x < b.W && y < b.H) n += wt[b.id(x, y)];
                }
            double const sc = std::min(n, static_cast<double>(kOpenExpandCap)) * fade;
            if (sc > best) best = sc, expandTarget_ = c;
        }
        if (expandTarget_ < 0) {
            int const c0 = b.id(b.W / 2, b.H / 2);
            if (w_.seenRound[c0] < 0 && myDist_[c0] < INF) expandTarget_ = c0;
        }
#if BC_MN_VALUE
        if (expandTarget_ >= 0 && valueTarget_ >= 0 && homeBed(valueTarget_)) valueTarget_ = -1;
#endif
#if BC_ECON_TRACE
        if (expandTarget_ >= 0)
            econTrace_ += " xp" + std::string(ok[expandTarget_] ? "B" : "C") + std::to_string(b.X(expandTarget_)) + "," + std::to_string(b.Y(expandTarget_)) +
                          "d" + std::to_string(myDist_[expandTarget_]);
#endif
    }
#endif

    void buildTargets() {
        Board const& b = w_.board;
        targets_.clear();
        belief_.assign(b.NC, 0.0);
        countdown_.assign(b.NC, -1);
        double unseen = w_.unseenPearlProb(p_);  // pearls likely waiting on never-seen tiles
        double unseenBed = w_.unseenBedProb(p_);  // ... on a never-seen tile whose mirror is a bed
        double fade = std::max(0.0, 1.0 - static_cast<double>(w_.t.round) / p_.exploreUntil);
        double sectorBonus = p_.wExploreSector * fade;
        farExplore_ = BC_FAR_EXPLORE ? p_.wExploreFar * fade : 0.0;
        for (int c = 0; c < b.NC; c++) {
            double bel = w_.belief(c, p_);
            int cd = (b.bed[c] == 1 && w_.next[c] >= 0 && w_.next[c] < p_.horizon) ? w_.next[c] : -1;
            if (BC_ECON_BED && cd < 0 && b.bed[c] == 1 && w_.t.round < kBedUntil) {
                // BC_ECON_BED (b) countdown completion: mirror tiles share one countdown (see the game rules), so a bed we watch tells
                // when its twin spawns; else the refill-gap estimate from its last spawn (0.75 x the largest countdown seen)
                if (b.sym > 0) {
                    int mc = b.mirror(c);
                    if (mc != c && w_.next[mc] >= 0 && w_.next[mc] < p_.horizon) {
                        cd = w_.next[mc];
                        if (econTraceOn() && cd <= 12) econTrace_ += " bed-cd-mirror";
                    }
                }
                if (cd < 0 && w_.lastSpawn[c] >= 0 && w_.maxCd[c] > 0 && w_.maxCd[c] <= kBedMaxGap) {
                    int est = w_.lastSpawn[c] + static_cast<int>(0.75 * w_.maxCd[c] + 0.5) - w_.t.round;
                    if (est > -3 && est < p_.horizon) cd = std::max(0, est);
                    if (econTraceOn() && cd >= 0 && cd <= 12) econTrace_ += " bed-cd-est";
                }
            }
            belief_[c] = bel;
            countdown_[c] = cd;
            double ex = 0;
            bool frontier = false;
            if (w_.seenRound[c] < 0) {
                // bed status known only through the map's symmetry (World::copyToMirror) or a fountain heard of
                double pearl = !BC_MIRROR ? unseen : b.bed[c] == 1 ? unseenBed : b.bed[c] == 0 ? 0.0 : unseen;
                frontier = !w_.sectorExplored(c);
                ex = p_.exploreValue + pearl + (frontier ? sectorBonus : 0.0);
#if BC_MN_EXPLORE
                if (w_.danger(c) > BC_MN_EXPLORE_TH) ex = 0, frontier = false;  // BC_MAPNET: no exploring into a known death trap
#endif
            }
            if (bel > 0.02 || cd >= 0 || ex > 0) targets_.push_back({c, bel, cd, ex, frontier});
        }
        // teammates heard over sonar but out of sight (vision covers the rest).
        remoteFriendDist_.assign(b.NC, INF);
#if BC_FORAGE_PATH
        // BC_FORAGE_PATH: path steps from the heard teammate over the known map (paired portals in), kForagePathCap at most, + its fact's age
        std::vector<int> fd(b.NC, INF), fq;
        for (auto const& f : w_.know.friends) {
            if (w_.t.round - f.round > p_.remoteMaxAge || w_.visible(f.cell) || f.cell < 0) continue;
            std::fill(fd.begin(), fd.end(), INF);
            fq.assign(1, f.cell);
            fd[f.cell] = 0;
            for (size_t i = 0; i < fq.size(); i++) {
                if (fd[fq[i]] >= kForagePathCap) continue;
                for (int d = 0; d < 4; d++) {
                    int n = b.nb[fq[i] * 4 + d];
                    if (n < 0 || fd[n] != INF || (w_.seenRound[n] < 0 && !w_.mirrored[n])) continue;
                    fd[n] = fd[fq[i]] + 1;
                    fq.push_back(n);
                }
            }
            for (Target const& t : targets_)
                if (fd[t.cell] < INF) remoteFriendDist_[t.cell] = std::min(remoteFriendDist_[t.cell], fd[t.cell] + (w_.t.round - f.round));
        }
#else
        for (auto const& f : w_.know.friends) {
            if (w_.t.round - f.round > p_.remoteMaxAge || w_.visible(f.cell)) continue;
            int fx = b.X(f.cell), fy = b.Y(f.cell);
            for (Target const& t : targets_) {
                int dx = std::abs(b.X(t.cell) - fx), dy = std::abs(b.Y(t.cell) - fy);
                int d = std::min(dx, b.W - dx) + std::min(dy, b.H - dy) + (w_.t.round - f.round);
                remoteFriendDist_[t.cell] = std::min(remoteFriendDist_[t.cell], d);
            }
        }
#endif
        hot_.clear();
        for (int c = 0; c < b.NC; c++) {
            if (b.bed[c] != 1 || b.gapMax[c] <= 0) continue;
            double rate = 2.0 / (b.gapMin[c] + b.gapMax[c]);
            if (rate >= p_.hotRate) hot_.push_back({c, rate});
        }
    }

    // The local team (team.hpp) splits nearby pearls and fountains among its members. Runs after the distance fields:
    // assignments use my BFS and my teammates' BFS fields.
    void planTeam() {
        Board const& b = w_.board;
        team_ = TeamPlan{};
        leaderCell_ = -1;
        if (p_.teamOn) {
            std::vector<int> cand;
            for (Target const& t : targets_)
                if ((t.belief >= 0.5 || (t.countdown >= 0 && t.countdown <= 3)) &&
                    b.cheb(t.cell, w_.head) <= p_.teamRadius + 3)
                    cand.push_back(t.cell);
            for (Hot const& h : hot_)
                if (b.cheb(h.cell, w_.head) <= p_.teamRadius + 3) cand.push_back(h.cell);
            std::sort(cand.begin(), cand.end(), [&](int x, int y) {
                int dx = TeamPlan::torus(b, x, w_.head), dy = TeamPlan::torus(b, y, w_.head);
                return dx != dy ? dx < dy : x < y;
            });
            cand.erase(std::unique(cand.begin(), cand.end()), cand.end());
            if (cand.size() > 24) cand.resize(24);
            if (!p_.assignOn) cand.clear();  // membership (and TEAM facts) only
            int me = w_.init.id & 4095;
            team_.build(w_, p_, cand, [&](int id, int cell, int t) {
                if (id == me) return myDist_.empty() ? INF : myDist_[t];
                for (FriendField const& f : friends_)
                    if ((f.id & 4095) == id) return f.dist[t];
                return TeamPlan::torus(b, cell, t) + 2;  // heard, not in view: rough
            });
            leaderCell_ = team_.members.front().cell;
        }
    }

    void buildFriendFields() {
        Board const& b = w_.board;
        friendDist_.assign(b.NC, INF);
        friendId_.assign(b.NC, -1);
        enemyDist_.assign(b.NC, INF);
        friends_.clear();
        enemies_.clear();
        std::vector<int> blk = ownBlk();
        // Only the nearest heads matter; this bounds CPU when 60+ dragons crowd the vision.
        std::vector<World::Seen const*> near;
        for (World::Seen const& s : w_.others) near.push_back(&s);
        std::sort(near.begin(), near.end(), [&](World::Seen const* x, World::Seen const* y) {
            return b.cheb(x->head, w_.head) < b.cheb(y->head, w_.head);
        });
        if (static_cast<int>(near.size()) > p_.maxRivalFields) near.resize(p_.maxRivalFields);
        for (World::Seen const* sp : near) {
            World::Seen const& s = *sp;
            nav_.bfs(b, s.head, blk, p_.horizon);
            if (isEnemy(s)) {
                for (int c = 0; c < b.NC; c++) enemyDist_[c] = std::min(enemyDist_[c], nav_.dist[c]);
                enemies_.push_back({s.id, s.head, s.visible, nav_.dist});
                continue;
            }
            for (int c = 0; c < b.NC; c++) {
                int d = nav_.dist[c];
                if (d < friendDist_[c] || (d == friendDist_[c] && d != INF && s.id < friendId_[c])) {
                    friendDist_[c] = d;
                    friendId_[c] = s.id;
                }
            }
            friends_.push_back({s.id, isGrower(s.id, s.visible), s.head, nav_.dist});
        }
        nav_.bfs(b, w_.head, blk, p_.horizon);
        myDist_ = nav_.dist;
    }

    // How many visible swarm teammates are closer than us to `head` (ties by id).
    int swarmRank(int head, int mine) const {
        int rank = 0;
        for (FriendField const& f : friends_) {
            if (f.grower) continue;
            int d = distToHead(f.dist, head);
            if (d < mine || (d == mine && d != INF && f.id < w_.init.id)) rank++;
        }
        return rank;
    }

    void buildSquadTargets() {
        Board const& b = w_.board;
        hunts_.clear();
        escortOf_ = -1;
        if (grower_ || w_.t.units <= kSurvivalUnits) return;  // survival first: no hunts with few units
        if (BC_QUEEN && queenMe_) return;                      // queen: the queen neither hunts nor defends by head-on

        // Enemy heads near one of our visible growers are threats whatever their length.
        for (World::Seen const& e : w_.others) {
            if (!isEnemy(e)) continue;
            bool threat = false;
            double protect = 0;
            for (FriendField const& f : friends_)
                if (f.grower && b.cheb(f.head, e.head) <= p_.escortRadius) {
                    threat = true;
                    for (World::Seen const& s : w_.others)
                        if (s.id == f.id) protect = std::max<double>(protect, s.visible);
                }
            double gain = threat ? std::max<double>(protect, e.visible) - L_ : e.visible - L_;
            bool worth = threat ? e.visible >= 2 : tradeOk(L_, e.visible);
            if (!worth) continue;
            int mine = distToHead(myDist_, e.head);
            if (mine == INF) continue;
            int rank = swarmRank(e.head, mine);
            Job job = rank == 0 ? Job::Attack : rank < p_.squadSize ? Job::Support : Job::None;
            if (job != Job::None) hunts_.push_back({&e, gain, job, threat});
        }

        // Idle escort: stay near the closest visible grower if we are among its first escorts.
        if (hunts_.empty() && midGame()) {
            int bestD = INF;
            for (FriendField const& f : friends_) {
                if (!f.grower) continue;
                int d = distToHead(myDist_, f.head);
                if (d == INF || d >= bestD) continue;
                if (swarmRank(f.head, d) < p_.escortCount) {
                    bestD = d;
                    escortOf_ = f.head;
                }
            }
        }
    }

    // Scavenger (BC_SCAV). Tasks are decided by each dragon from what it knows, the same rule everywhere so teammates agree
    // without talking: a drop cluster takes its ceil(count/2) nearest free dragons (visible teammates by their BFS fields,
    // heard ones by torus distance plus age); the cluster I was already on counts 2 steps nearer (keep a task rather than
    // hand it back and forth).
    void planScavengers() {
        scav_.clear();
        if (!BC_SCAV || p_.wScav <= 0 || lean_ || grower_ || feedHead_ >= 0 || feedPull_ >= 0 || myDist_.empty()) {
            if (BC_SCAV) w_.scavCell = -1;
            return;
        }
        for (Hunt const& h : hunts_)
            if (h.job == Job::Attack) return;  // committed to a trade of our own
        Board const& b = w_.board;
        int me = w_.init.id & 4095;
        auto rankAt = [&](int cell, int mine, bool head) {
            int rank = 0;
            for (FriendField const& f : friends_) {
                if (f.head == cell) continue;
                int fd = head ? distToHead(f.dist, cell) : f.dist[cell];
                if (fd < mine || (fd == mine && f.id < w_.init.id)) rank++;
            }
            for (auto const& fr : w_.know.friends) {
                int age = w_.t.round - fr.round;
                if (fr.id == me || age > 2 || w_.visible(fr.cell)) continue;
                if (TeamPlan::torus(b, fr.cell, cell) + age < mine) rank++;
            }
            return rank;
        };
        int keep = -1;
        for (World::Drop const& d : w_.drops) {
            int mine = myDist_[d.cell];
            if (mine == INF) mine = distToHead(myDist_, d.cell);  // someone on the tile
            if (mine == INF || mine > p_.scavRange) continue;
            if (d.cell == w_.scavCell) mine = std::max(0, mine - 2);
            if (rankAt(d.cell, mine, false) >= (d.count + 1) / 2) continue;
            if (enemyDist_[d.cell] + p_.scavSlack < mine) continue;  // their scavengers win this race
            scav_.push_back({d.cell, static_cast<double>(std::min(d.count, 4)), 0});
            if (keep < 0) keep = d.cell;
        }
        // Stand by where a trade may come: a teammate's head within 2 of an enemy head. Most trades are started by the enemy,
        // and the side with a head nearer the drops eats them (in replays we ate most of the drops we were nearer to and only a
        // quarter of the rest).
        for (FriendField const& f : friends_) {
            bool threat = false;
            for (World::Seen const& e : w_.others)
                if (isEnemy(e) && b.cheb(e.head, f.head) <= 2) threat = true;
            if (!threat) continue;
            int mine = distToHead(myDist_, f.head);
            if (mine == INF || mine > 6) continue;
            if (f.head == w_.scavCell) mine = std::max(0, mine - 2);
            if (rankAt(f.head, mine, true) >= 1) continue;
            scav_.push_back({f.head, 0.5, 2});
            if (keep < 0) keep = f.head;
        }
        w_.scavCell = keep;
    }

    // Engage (BC_ENGAGE, Params::engB0...): the value of a head-on trade at `site` for the side that moves in (length la; the
    // victim has lv): lv - la + (2p - 1) * drops. usAttack: we move in (our heads are the attacker's side), else an enemy
    // moves onto us. skipId: the enemy taking part (left out of the counts).
    double tradeValue(int la, int lv, int site, bool usAttack, int skipId) const {
        Board const& b = w_.board;
        bool occupied = w_.occ[site] >= 0;
        int n3 = 0, n5 = 0;
        for (World::Seen const& s : w_.others) {
            if (s.id == skipId) continue;
            int c = b.cheb(s.head, site);
            if (c > 5) continue;
            int sign = (usAttack ? !isEnemy(s) : isEnemy(s)) ? 1 : -1;
            n5 += sign;
            if (c <= 3) n3 += sign;
        }
        int nearUs = 8, nearThem = 8;
        for (FriendField const& f : friends_)
            nearUs = std::min(nearUs, occupied ? distToHead(f.dist, site) : f.dist[site]);
        for (EnemyField const& e : enemies_)
            if (e.id != skipId) nearThem = std::min(nearThem, occupied ? distToHead(e.dist, site) : e.dist[site]);
        int lead = std::max(-4, std::min(4, usAttack ? nearThem - nearUs : nearUs - nearThem));
        double z = p_.engB0 + p_.engB3 * n3 + p_.engB5 * n5 + p_.engBL * lead;
        double p = 1.0 / (1.0 + std::exp(-z));
        int drops = (la + 1) / 2 + (lv + 1) / 2;
        return (lv - la) + (2.0 * p - 1.0) * drops;
    }

    // Dragon plans, slay (BC_SLAY): the enemy's longest dragon (World::enemyKing) decides round-500 games; a
    // head-on trade of one of our short dragons for it swings its whole length. Its slayHunters nearest short
    // dragons (visible teammates by BFS, heard ones by torus distance plus age) go for it, the same rule
    // everywhere, so teammates agree without talking; in view it becomes their squad target.
    void planSlay() {
        slayCell_ = -1;
        auto const& E = w_.enemyKing;
        if (!BC_SLAY || lean_ || kingMe_ || grower_ || myDist_.empty() || w_.t.units <= kSurvivalUnits ||
            L_ > p_.slayMaxLen || w_.t.round < p_.slayFrom || E.cell < 0 || E.len < p_.slayMinLen ||
            w_.t.round - E.round > World::kKingAge)
            return;
        // Feeding our king comes first unless theirs threatens the round-500 count (it is not clearly shorter than ours): then
        // its hunters stop walking to feed (otherwise an enemy king could grow from round ~320 while every small dragon of ours
        // walked to feed ours).
        bool threat = w_.know.longest.cell < 0 || E.len + 3 >= w_.know.longest.len;
        if ((feedHead_ >= 0 || feedPull_ >= 0) && !threat) return;
        Board const& b = w_.board;
        auto dist = [&](std::vector<int> const& field, int from) {
            int d = field[E.cell];
            if (d == INF) d = distToHead(field, E.cell);
            return d == INF ? TeamPlan::torus(b, from, E.cell) + 4 : d;
        };
        int mine = dist(myDist_, w_.head), rank = 0, me = w_.init.id & 4095;
        for (FriendField const& f : friends_) {
            int fl = 0;
            for (World::Seen const& s : w_.others)
                if (s.id == f.id) fl = s.visible;
            if (fl > p_.slayMaxLen) continue;
            int fd = dist(f.dist, f.head);
            if (fd < mine || (fd == mine && f.id < w_.init.id)) rank++;
        }
        for (auto const& fr : w_.know.friends) {
            int age = w_.t.round - fr.round;
            if (fr.id == me || age > 2 || w_.visible(fr.cell) || fr.len > p_.slayMaxLen) continue;
            if (TeamPlan::torus(b, fr.cell, E.cell) + age < mine) rank++;
        }
        // Slay with a big army: more hunters (units / slayPer), and they all go in (one attacker with the others standing off
        // never caught a fleeing king in the open).
        int hunters = std::max(p_.slayHunters, w_.t.units / std::max(1, p_.slayPer));
        if (rank >= hunters) return;
        slayCell_ = E.cell;
        feedHead_ = feedPull_ = -1;  // a hunter does not walk to feed
        v9Feeder_ = false;
        for (World::Seen const& s : w_.others)
            if (isEnemy(s) && (s.id & 4095) == E.id && !huntAt(s.head))
                hunts_.push_back({&s, static_cast<double>(s.visible - L_), rank < p_.slayAttack ? Job::Attack : Job::Support,
                                  false});
    }

    // Scouts (BC_V9_SCOUT, Params::scoutUntil): the nearest unexplored sector no known teammate is nearer to; its centre's
    // distance field on the known map gives the pull a gradient from afar.
    int scoutCell_ = -1;
    std::vector<int> scoutDist_;
    void planScout() {
        scoutCell_ = -1;
        if (!BC_V9_SCOUT || lean_ || w_.t.round >= p_.scoutUntil || w_.t.round - w_.lastEatRound < p_.scoutHungry ||
            grower_ || kingMe_ || feedHead_ >= 0 ||
            feedPull_ >= 0 || slayCell_ >= 0 || finishCell_ >= 0 || prober_ || leaving_ || myDist_.empty())
            return;
        for (Hunt const& h : hunts_)
            if (h.job == Job::Attack) return;
        Board const& b = w_.board;
        int me = w_.init.id & 4095;
        int best = INF, bestCell = -1;
        int nsec = comms::kSectorsX * comms::kSectorsY;
        for (int s = 0; s < nsec; s++) {
            if (w_.teamSectors >> s & 1ull) continue;
            int sx = s % comms::kSectorsX, sy = s / comms::kSectorsX;
            int cx = (2 * sx + 1) * b.W / (2 * comms::kSectorsX), cy = (2 * sy + 1) * b.H / (2 * comms::kSectorsY);
            int c = b.id(cx, cy);
            int mine = TeamPlan::torus(b, w_.head, c);
            if (mine >= best) continue;
            bool nearer = false;
            for (World::Seen const& o : w_.others)
                if (!isEnemy(o) && TeamPlan::torus(b, o.head, c) < mine) nearer = true;
            for (auto const& f : w_.know.friends) {
                if (nearer) break;
                int age = w_.t.round - f.round;
                if (f.id == me || age > 3 || w_.visible(f.cell)) continue;
                if (TeamPlan::torus(b, f.cell, c) + age < mine) nearer = true;
            }
            if (nearer) continue;
            best = mine;
            bestCell = c;
        }
        if (bestCell < 0) return;
        scoutCell_ = bestCell;
        std::vector<int> open(b.NC, 0);
        Nav nav;
        nav.bfs(b, bestCell, open, b.NC);
        scoutDist_ = nav.dist;
    }

    // Finish (BC_V9_FINISH, Params::fFrom): the nearest enemy head known, for a free short dragon of a big late army.
    void planFinish() {
        finishCell_ = -1;
        if (!BC_V9_FINISH || lean_ || w_.t.round < p_.fFrom || w_.t.units < p_.fUnits || L_ > p_.fMaxLen || grower_ ||
            kingMe_ || v9King_ || v9Feeder_ || feedHead_ >= 0 || feedPull_ >= 0 || slayCell_ >= 0 || myDist_.empty())
            return;
        for (Hunt const& h : hunts_)
            if (h.job == Job::Attack) return;  // already on one in view
        Board const& b = w_.board;
        int best = INF;
        for (auto const& e : w_.know.enemies) {
            if (w_.t.round - e.round > p_.fAge) continue;
            int d = myDist_[e.cell];
            if (d == INF) d = distToHead(myDist_, e.cell);
            if (d == INF) d = TeamPlan::torus(b, w_.head, e.cell) + 4;
            d += w_.t.round - e.round;
            if (d < best) best = d, finishCell_ = e.cell;
        }
    }

    Hunt const* huntAt(int headCell) const {
        for (Hunt const& h : hunts_)
            if (h.enemy->head == headCell) return &h;
        return nullptr;
    }

    bool nextToEnemyHead(int cell, int& enemyLen, bool& isHunt) const {
        Board const& b = w_.board;
        enemyLen = 0;
        isHunt = false;
        bool found = false;
        for (World::Seen const& s : w_.others) {
            if (!isEnemy(s)) continue;
            for (int d = 0; d < 4; d++)
                if (b.nb[s.head * 4 + d] == cell) {
                    found = true;
                    enemyLen = std::max(enemyLen, s.visible);
                    if (huntAt(s.head)) isHunt = true;
                }
        }
        return found;
    }

    // One step from a simulated state: `body` (head first, true length L) after k of
    // our own moves this turn-plan, with `eaten` pearls already taken along the way.
    // k = 0 is the real move; deeper k is lookahead with other dragons held still.
#if BC_BED_KEEP & 1
#define BC_KEEP_FREE (keepCell_ < 0)  // BC_BED_KEEP (off): no far pull while keeping a fast bed
#else
#define BC_KEEP_FREE true
#endif
    Choice scoreFrom(std::vector<int> const& body, int L, int d, int k, std::vector<int> const& eaten,
                     std::vector<int>* nextBody = nullptr) {
        Board const& b = w_.board;
        Choice c;
        c.dir = d;
        int head = body.front();
        int dest = b.nb[head * 4 + d];
        if (dest < 0) return c;  // kelp or unknown portal
        for (int x : body)
            if (x == dest) return c;  // own segment, tail included
        if (w_.occ[dest] < 0 && base_[dest] >= INF) return c;  // own stray segment or a teammate's last exit

        if (w_.occ[dest] >= 0) {
            if (k > 0) return c;  // lookahead treats other dragons as walls
            if ((BC_QUEEN && queenMe_) || lockNoTrade()) return c;  // queen: no head-on for the queen, ever (LOCK: for nobody)
            // Stepping onto an enemy head kills both. Swarm does it on squad targets;
            // anyone does it when the trade is clearly good.
            int hid = w_.occHeadId[dest];
            for (World::Seen const& s : w_.others) {
                if (s.id != hid || !isEnemy(s)) continue;
                Hunt const* h = huntAt(s.head);
                double gain = s.visible - L;
                if (BC_ENGAGE) {
                    // Engage, attack: when the trade is worth engageAttack to us (the fixed survival policy still forbids it with few
                    // units); a threat to a grower is fought anyway
                    double v = tradeValue(L, s.visible, dest, true, s.id);
                    bool defend = h && h->threat && !grower_;
                    // Fight filter: no even or losing swaps when fightTradeMin > -10 (strong teams trade less and pick their trades)
                    bool worthIt = defend || !BC_FIGHT || p_.fightTradeMin <= -10 || s.visible - L >= p_.fightTradeMin;
                    if (!grower_ && w_.t.units > kSurvivalUnits && worthIt && (defend || v >= p_.engageAttack)) {
                        c.dest = dest;
                        c.score = (defend ? 100.0 : 50.0) + v;
                        c.why = defend ? "defend" : "trade";
                        c.terminal = true;
                    }
                    continue;
                }
                if (h && !grower_) {
                    c.dest = dest;
                    c.score = 100.0 + h->gain;
                    c.why = h->threat ? "defend" : "trade";
                    c.terminal = true;
                } else if (tradeOk(L, s.visible) && !grower_ && supportOk(dest, s.id)) {
                    c.dest = dest;
                    c.score = 50.0 + gain;
                    c.why = "trade";
                    c.terminal = true;
                }
            }
            return c;
        }

        c.dest = dest;
        bool taken = std::find(eaten.begin(), eaten.end(), dest) != eaten.end();
        if (k == 0) c.eat = w_.seenRound[dest] == w_.t.round && w_.seenPearl[dest];
        else c.eat = !taken && (belief_[dest] >= 0.5 || (countdown_[dest] >= 0 && countdown_[dest] <= k));
        int newL = L + (c.eat ? 1 : 0);

        std::vector<int> local;
        std::vector<int>& nb2 = nextBody ? *nextBody : local;
        nb2.clear();
        nb2.reserve(body.size() + 1);
        nb2.push_back(dest);
        nb2.insert(nb2.end(), body.begin(), body.end());
        if (!c.eat && static_cast<int>(nb2.size()) > newL) nb2.pop_back();
        c.newL = newL;

        std::vector<int>& blk = blkScratch_;
        blk = base_;
        markBody(blk, nb2, newL);

        int need = std::min(static_cast<int>(p_.spaceFactor * newL) + p_.spaceMargin, b.NC / 2);
        if (kingMe_ && !(BC_QUEEN && BC_QUEEN_SAFE && queenMe_)) need = std::min(need, p_.kingSpace);  // dragon plans, guard: our own crowd is not a trap
        int space = nav_.flood(b, dest, blk, need + 1);
        // Portal probe (BC_PORTAL2): the probe entry sits beside the unpaired portal it will take, so its dead end is no trap
        // (otherwise the space check vetoed the entry and starving probers paced beside it)
        if ((BC_V9_ESCAPE || BC_PORTAL2) && escaping_ && dest == probeEntry_) space = std::max(space, need);
#if BC_HARVEST
        if (harvestEntry_ >= 0 && dest == harvestEntry_) space = std::max(space, need);  // BC_HARVEST: the entry needs no room (without this, harvesters hovered 3-7 tiles off it)
#endif
        // Reverse by splitting: a dragon of 4+ can split off everything but a 2-long stub;
        // the child's head is our tail and faces out. So a dead end (a fountain corridor,
        // a pocket) is fine as long as the tail side has room; it costs the 2-long stub.
        bool viaReverse = false;
        // BC_V9_TAIL: at round 0 a starter sees only ~4 of its segments, so it could never count on a reverse split and gave up
        // every move into a corridor (on a map with long starters in narrow corridors they reverse-split on round 0 and their
        // stubs died within a few rounds); with the tail not tracked yet it assumes the reverse split's child can leave the way
        // the body came.
        bool tailUnknown = BC_V9_TAIL && p_.tailOn && static_cast<int>(body.size()) < newL && w_.t.round - w_.firstRound < newL &&
                           (!p_.tailStarters || w_.firstRound == 0);
        bool const noRev = BC_QUEEN && BC_QUEEN_SAFE && queenMe_;  // queen: the queen does not plan on a reverse (it costs her body)
        if (space < need && newL >= 4 && tailUnknown && !noRev) viaReverse = true;
        // BC_ECON_DIP (a): a step that eats makes newL = L + 1 > body.size(), which cut every 'eat on the way into a lane' from
        // the reverse (the tail stays where it is, so the child's room is the same): compare with L
        int const revBody = BC_ECON_DIP ? L : newL;
        if (space < need && newL >= 4 && static_cast<int>(body.size()) == revBody && !noRev) {
            int childNeed = std::min(static_cast<int>(p_.spaceFactor * (newL - 2)) + p_.spaceMargin, b.NC / 2);
            std::vector<int> blk2 = base_;
            for (size_t i = 0; i + 1 < body.size(); i++) blk2[body[i]] = INF;
            int tail = body.back();
            int room = 0;
            for (int q = 0; q < 4 && room < childNeed; q++) {
                int n = b.nb[tail * 4 + q];
                if (n >= 0 && blk2[n] <= 1) room = std::max(room, nav_.flood(b, n, blk2, childNeed + 1) + 1);
            }
            if (room >= childNeed) viaReverse = true;
        }

        nav_.bfs(b, dest, blk, p_.horizon);
        std::vector<int> const& dist = nav_.dist;

        double value = 0;
        int frontierM = INF;
        for (Target const& t : targets_) {
            if (t.cell == dest) continue;
#if BC_BED_KEEP & 1
            if (keepCell_ >= 0 && b.cheb(t.cell, keepCell_) > kKeepR) continue;  // BC_BED_KEEP (off): only the kept bed's surroundings count
#endif
            int dd = dist[t.cell];
            if (dd == INF) continue;
            if (k > 0 && std::find(eaten.begin(), eaten.end(), t.cell) != eaten.end()) continue;
            int m = dd + 1 + k;  // moves from now, counting the ones already planned
            if (t.frontier && m < frontierM) frontierM = m;
            double v = t.belief * gp(m);
#if BC_OPEN_EXPAND
            if (t.countdown >= 0 && t.belief < 0.5 && expandTarget_ >= 0 && homeBed(t.cell)) {
            } else
#endif
            if (t.countdown >= 0 && t.belief < 0.5) {
                if (BC_ECON_BED && w_.t.round < kBedUntil
#if BC_OPEN_RISK
                    && !openRisk(2)  // BC_OPEN_RISK bit 2 (off): a bed counts from its spawn, no wait value
#endif
                ) {
                    // BC_ECON_BED (a): waiting for a spawn costs bedWait per turn, not gamma (a bed 6 turns off at distance 2 was worth
                    // 0.074, now ~0.26): dragons go to the bed early and wait next to it
                    int wait = std::max(0, t.countdown + 1 - m);
                    v = std::max(v, 0.9 * gp(m) * bedWaitPow(wait));
                } else {
                    v = std::max(v, 0.9 * gp(std::max(m, t.countdown + 1)));
                }
            }
            if (rebuild_ && L_ < 4) v *= kRebuildPearlBoost;  // too short to reproduce: food first
            double raw = v;
#if BC_OPEN_RISK
            if (v > 0 && openRisk(1)) {
                if (enemyDist_[t.cell] < m) v *= p_.enemyCloserFactor;  // BC_OPEN_RISK bit 1 (off): no teammate yields (the enemy's lead still counts)
            } else
#endif
            if (v > 0) {
                int fd = friendDist_[t.cell];
                if (fd < m || (fd == m && friendId_[t.cell] < w_.init.id)) v = 0;
                else if (enemyDist_[t.cell] < m) v *= p_.enemyCloserFactor;
                if (remoteFriendDist_[t.cell] + p_.remoteSlack < m) v *= p_.remoteYield;
                // The team's assignment overrides the closer-friend rule both ways.
                if (p_.teamOn && team_.owner[t.cell] >= 0)
                    v = t.cell == team_.myTarget ? raw : raw * p_.teamYield;
            }
            v += t.explore * gp(m);
            if ((BC_V9_KING && v9Feeder_) || finalFeeder_) v *= p_.kFeedForage;  // a leaving feeder does not forage much
            if ((BC_V9_KING || BC_V9_EKING) && kingHead_ >= 0 && b.cheb(t.cell, kingHead_) <= kingYield()) v = 0;
            value += v;
        }
        // BC_FAR_EXPLORE: the per-tile exploration value fades with gamma (0.7: nothing left 10 steps out), so a dragon whose
        // surroundings are all seen had no pull at all (in a test game a starting dragon paced back and forth for 40 rounds
        // while fountains 19-37 steps away were never found). The nearest frontier tile pulls from afar.
        if (BC_KEEP_FREE && frontierM < INF) value += farExplore_ * fp(frontierM);
#if BC_MN_VALUE
        if (BC_KEEP_FREE && valueTarget_ >= 0) {  // BC_MAPNET_VALUE: the best known bed room none of ours holds
            int dd = dist[valueTarget_];
            if (dd == INF) dd = distToHead(dist, valueTarget_);
            if (dd != INF) value += valueWeight_ * fp(dd + k);
        }
#endif
        if (BC_KEEP_FREE && heatSpread_ > 0) {
            int best = INF;
            for (int a : heatAnchors_) best = std::min(best, dist[a]);
            if (best < INF) value += heatSpread_ * fp(best + 1 + k);
        }

        // Long-range pull toward fountains nobody visible is closer to.
        for (Hot const& h : hot_) {
#if BC_BED_KEEP & 1
            if (keepCell_ >= 0 && b.cheb(h.cell, keepCell_) > kKeepR) continue;  // BC_BED_KEEP (off)
#endif
            int dd = dist[h.cell];
            if (dd == INF) {
                // beyond the BFS horizon: no pull (keeps CPU bounded)
                continue;
            }
            int m = dd + 1 + k;
            // A fountain cluster feeds several dragons; yield only to a teammate already on it
            // (yielding to anyone merely closer left clusters unfarmed: everybody deferred).
#if BC_OPEN_RISK
            if (!openRisk(1))  // BC_OPEN_RISK bit 1 (off): no yield to a teammate on the fountain
#endif
            if (friendDist_[h.cell] < m && friendDist_[h.cell] <= p_.hotYield) continue;
            // BC_MIRROR: a fountain known only as the mirror image of one we saw is usually on the enemy's side: raiding it is worth
            // wMirrorHot of a fountain we watched.
            value += p_.wHot * (BC_MIRROR && w_.mirrored[h.cell] ? p_.wMirrorHot : 1.0) * h.rate * fp(m);
        }

        std::string why;
#if BC_OPEN_EXPAND
        if (BC_KEEP_FREE && expandTarget_ >= 0) {  // BC_OPEN_EXPAND: the opening's bed area (or the unseen centre)
            int dd = dist[expandTarget_];
            if (dd == INF) dd = distToHead(dist, expandTarget_);
            if (dd != INF) value += kWOpenExpand * std::max(0.0, expandFar() - (dd + k));
            if (why.empty()) why = "expand47";
        }
#endif
#if BC_EXPAND_OPENING
        if (BC_KEEP_FREE && eoMirror_ >= 0) {  // open mode MIRROR_TARGET (off): the band with the most mirrored beds in reach
            int dd = dist[eoMirror_];
            if (dd == INF) dd = distToHead(dist, eoMirror_);
            if (dd != INF) value += kWEoMirror * fp(dd + k);
            if (why.empty()) why = "eo-mirror";
        }
        if (eoHunter_ >= 0 && k == 0) {  // open mode ANTIHUNT (off): away from the hunter, never into a dead end
            value += kWEoHunt * std::min(enemies_[eoHunter_].dist[dest], 8);
            if (b.deadEnd[dest]) value -= kEoDeadPen;
            if (why.empty()) why = "eo-flee";
        }
#endif
#if BC_HARVEST
        if (BC_KEEP_FREE && harvestEntry_ >= 0) {  // BC_HARVEST: the harvest room's entry
            int dd = dist[harvestEntry_];
            if (dd == INF) dd = distToHead(dist, harvestEntry_);
            if (dd != INF) value += kWHarvest * std::max(0.0, 1.0 - (dd + k) / 40.0);  // a linear fade: with the far discount a weight of 2.0 lost
            if (why.empty()) why = "harvest";                                         // to the start strip's pulls for 80 rounds in testing
        }
#endif
        if (BC_KEEP_FREE && idleBedCell_ >= 0) {  // BC_ECON_BED (e): an idle short dragon heads for a bed nobody of ours is nearer to
            int dd = dist[idleBedCell_];
            if (dd == INF) dd = distToHead(dist, idleBedCell_);
            if (dd != INF) value += kWIdleBed * fp(dd + k);
            if (why.empty()) why = "idle-bed";
        }
#if BC_OPEN_RISK
        if (fastBedCell_ >= 0) {  // BC_OPEN_RISK bit 8 (off): park at the opening's fast bed
            int dd = dist[fastBedCell_];
            if (dd == INF) dd = distToHead(dist, fastBedCell_);
            if (dd != INF) value += kWOpenRiskFast * fp(dd + k);
            if (why.empty()) why = "fast-bed";
        }
#endif
#if BC_EXPAND
        if (BC_KEEP_FREE && expandCell_ >= 0) {  // BC_EXPAND (off): an idle dragon contests a bed core none of ours holds
            int dd = dist[expandCell_];
            if (dd == INF) dd = distToHead(dist, expandCell_);
            if (dd != INF) value += kWExpand * fp(dd + k);
            if (why.empty()) why = "expand";
        }
#endif
#if BC_BED_KEEP & 1
        if (keepCell_ >= 0
#if BC_BED_KEEP & 2
            && keepDue_  // BC_BED_KEEP bit 2 (off): on time, not early
#endif
        ) {  // BC_BED_KEEP (off): hold the fast bed
            int dd = dist[keepCell_];
            if (dd == INF) dd = distToHead(dist, keepCell_);
            if (dd != INF) value += kWKeep * fp(dd + k);
            if (why.empty()) why = "keep";
        }
#endif
        if (slayCell_ >= 0) {
            int dd = dist[slayCell_];
            if (dd == INF) dd = distToHead(dist, slayCell_);
            if (dd == INF) dd = TeamPlan::torus(b, dest, slayCell_) + 4;  // beyond the BFS horizon: rough
            value += p_.wSlay * fp(dd + k);
            why = "slay";
        }
        if (BC_KEEP_FREE && scoutCell_ >= 0 && !scoutDist_.empty()) {
            int dd = scoutDist_[dest];
            if (dd < INF) value += p_.wScout * std::max(0.0, 1.0 - (dd + k) / 60.0);
            if (why.empty()) why = "scout";
        }
        if (finishCell_ >= 0) {
            int dd = dist[finishCell_];
            if (dd == INF) dd = distToHead(dist, finishCell_);
            if (dd == INF) dd = TeamPlan::torus(b, dest, finishCell_) + 4;  // beyond the BFS horizon: rough
            value += p_.wFinish * fp(dd + k);
            if (why.empty()) why = "finish";
        }
        for (Scav const& s : scav_) {
            int dd = s.ring ? distToHead(dist, s.cell) : dist[s.cell];
            if (dd == INF && !s.ring) dd = distToHead(dist, s.cell);
            if (dd == INF) continue;
            int m = s.ring ? std::abs(dd + k - s.ring) : dd + 1 + k;
            value += p_.wScav * s.w * sp(m);
            if (why.empty()) why = s.ring ? "stand-by" : "scavenge";
        }
        for (Hunt const& h : hunts_) {
            int dd = distToHead(dist, h.enemy->head);
            if (dd == INF) continue;
            if (h.job == Job::Attack) {
                // Aim at the head and at the tile it is facing into.
                int ahead = b.nb[h.enemy->head * 4 + h.enemy->headDir];
                if (ahead >= 0 && dist[ahead] != INF) dd = std::min(dd, dist[ahead]);
                value += p_.wHunt * (1.0 + h.gain / 4.0) * gp(dd + k);
                why = h.threat ? "intercept" : "hunt";
            } else {
                value += p_.wSupport * gp(std::abs(dd + k - p_.supportRing));
                if (why.empty()) why = "support";
            }
        }
        if (((BC_V9_KING && v9Feeder_) || finalFeeder_) && (feedHead_ >= 0 || feedPull_ >= 0)) {
            // King feeding: a leaving feeder walks the whole way; the pull falls linearly (80 steps to zero), so every step closer
            // is worth the same from afar. Beyond the BFS horizon the way on the known map counts.
            int goal = feedHead_ >= 0 ? feedHead_ : feedPull_;
            int dd = feedHead_ >= 0 ? distToHead(dist, goal) : dist[goal];
            if (dd == INF && feedPull_ >= 0) dd = distToHead(dist, goal);
            if (dd == INF && !goalDist_.empty() && goalDist_[dest] < INF) dd = goalDist_[dest];
            if (dd < INF) value += (finalFeeder_ ? p_.finalPull : p_.kFeedPull) * std::max(0.0, 1.0 - (dd + k) / 80.0);
            why = feedHead_ >= 0 ? "to-feed" : "to-feed-heard";
        } else if (feedHead_ >= 0) {
            int dd = distToHead(dist, feedHead_);
            if (dd != INF) value += p_.wFeed * gp(dd + k);
            why = "to-feed";
        } else if (feedPull_ >= 0) {
            int dd = dist[feedPull_];
            if (dd == INF) dd = distToHead(dist, feedPull_);
            if (dd != INF) value += p_.wFeed * gp(dd + k);
            why = "to-feed-heard";
        } else if (p_.teamOn && team_.myTarget >= 0 && dist[team_.myTarget] != INF) {
            value += p_.wTeamTarget * gp(dist[team_.myTarget] + 1 + k);
            if (why.empty()) why = "team-target";
        }
        if (escortCell_ >= 0) {  // queen escort (BC_QUEEN_ESCORT, off): our body between the queen and the nearest enemy head
            int dd = dist[escortCell_];
            if (dd == INF) dd = distToHead(dist, escortCell_);
            if (dd != INF) value += kEscortW * fp(dd + k);
            if (why.empty()) why = "escort-q";
        }
        if (escortOf_ >= 0) {
            int dd = distToHead(dist, escortOf_);
            if (dd != INF) value += p_.wEscort * gp(std::abs(dd + k - p_.escortRing));
            if (why.empty()) why = "escort";
        }

        double danger = 0;
        if (BC_QUEEN && BC_QUEEN_KINAVOID && k == 0 && kinQh_ >= 0) {
            // queen kin avoidance (a few percent of our queens' deaths in replays were head-ons by our own 2-3 long dragons or by her
            // fresh child): never onto a tile next to our queen's head - she moves first and may land there next turn
            for (int q = 0; q < 4; q++)
                if (b.nb[kinQh_ * 4 + q] == dest) danger += kKinW;
        }
        if (BC_QUEEN && BC_QUEEN_ROOMGUARD && k == 0 && guardQh_ >= 0 && b.cheb(dest, guardQh_) <= 2) {
            // BC_QUEEN_ROOMGUARD (friend discipline): a teammate's step within 2 of our queen that leaves her room < max(her length, 2) + 2 costs kRoomGuardW
            // (in replays about a third of the boxed queen deaths had the mouth closed by our own bodies)
            int need = std::max(guardQL_, 2) + 2, room = 0;
            std::vector<char> seen(b.NC, 0);
            std::vector<int> q{guardQh_};
            seen[guardQh_] = 1;
            for (size_t i = 0; i < q.size() && room < need; i++)
                for (int d = 0; d < 4; d++) {
                    int n = b.nb[q[i] * 4 + d];
                    if (n < 0 || seen[n] || blk[n] > 1 || n == dest) continue;
                    seen[n] = 1;
                    room++;
                    q.push_back(n);
                }
            if (room < need) danger += kRoomGuardW;
        }
#if BC_QUEEN_BREATHE
        if (BC_QUEEN && k == 0 && guardQh_ >= 0 && breatheVeto(dest, blk)) danger += kBreatheW;
#endif
#if BC_QUEEN_YIELD
        if (k == 0 && yieldQh_ >= 0) danger += kYieldW * std::max(0, 3 - b.cheb(dest, yieldQh_)) / 2;  // BC_QUEEN_YIELD (off): away from her first
#endif
        if (BC_QUEEN && BC_QUEEN_SAFE && queenMe_ && k == 0 && !enemies_.empty()) {
            // queen: the queen's safe room - tiles she reaches before any enemy head in view. Test deaths came from walking into
            // 1-wide corridors and boxes with an enemy on the way (one legal move for the last 3-5 turns, then a head-on).
            int want = std::max(need, kQueenSafeMin), safe = 0;
            for (int c = 0; c < b.NC && safe < want; c++)
                if (dist[c] < enemyDist_[c]) safe++;
            if (safe < want) danger += kQueenSafeW * (want - safe) / want;
            int exits = 0;
            for (int q = 0; q < 4; q++) {
                int n = b.nb[dest * 4 + q];
                if (n >= 0 && n != head && blk[n] <= 1) exits++;
            }
            if (exits <= 1) danger += kQueenPocket;  // a corridor or a pocket: one way on, and any enemy there blocks it
        }
        if (leaving_ && !exits_.empty()) {
            int best = INF;
            for (int e : exits_) best = std::min(best, dist[e]);
            if (best != INF) value += p_.wLeave * gp(best + k);
            if (why.empty()) why = "leave-pocket";
        } else if (prober_) {
            int dd = dist[probeEntry_];
            if (dd != INF) value += (escaping_ ? (hungryProbe_ ? p_.p2Pull : p_.wEscape) * std::max(0.0, 1.0 - (dd + k) / 40.0)
                                              : p_.wProbe * gp(dd + k));
            if (why.empty()) why = "to-probe";
#ifdef BC_DEBUG
            if (k == 0) why += "@" + std::to_string(w_.board.X(probeEntry_)) + "," + std::to_string(w_.board.Y(probeEntry_)) +
                               (escaping_ ? "e" : "");
#endif
        }
        if (p_.teamOn && team_.detached && leaderCell_ >= 0) {
            value += p_.wSpread * std::min(8, b.cheb(dest, leaderCell_)) / 8.0;  // leave the crowd
            if (why.empty()) why = "detach";
        }
        if (k == 0 && p_.teamOn) {
            // Whole-team level: other local teams' areas, as announced by their leaders.
            for (auto const& tm : w_.know.teams) {
                if (tm.id == team_.leaderId || w_.t.round - tm.round > 4) continue;
                int cd = b.cheb(dest, tm.cell);
                if (cd > p_.crowdRadius) continue;
                danger += p_.wCrowd * (p_.crowdRadius + 1 - cd) / (p_.crowdRadius + 1.0) * std::min(tm.len, 5) / 5.0;
            }
        }
        if (k == 0) {
            // enemy heads heard over sonar, out of our sight: keep away, more when long.
            for (auto const& e : w_.know.enemies) {
                int age = w_.t.round - e.round;
                if (age > p_.remoteMaxAge || w_.visible(e.cell)) continue;
                int cd = b.cheb(dest, e.cell);
                if (cd > p_.remoteRadius) continue;
                double near = static_cast<double>(p_.remoteRadius + 1 - cd) / (p_.remoteRadius + 1);
                double fresh = 1.0 - static_cast<double>(age) / (p_.remoteMaxAge + 1);
                danger += p_.wRemoteEnemy * newL * near * fresh;
            }
        }
        int enemyLen = 0;
        bool huntNext = false;
        double mult = grower_ ? p_.growerDanger : survival() ? p_.survivalDanger : 1.0;
        if (L_ <= p_.smallLen && !(BC_QUEEN && BC_QUEEN_SAFE && queenMe_)) mult *= p_.smallRisk;  // BC_SWARM: the swarm spends its small dragons (with BC_QUEEN_SAFE: never the queen)
        if (lateGame()) mult *= 2.0;
        if (kingMe_ || (BC_QUEEN && BC_QUEEN_SAFE && queenMe_)) {  // queen: her safety model whether or not she is the king
            mult *= p_.kingDanger;
            if (k == 0) {
                // the king also keeps out of sprint reach (2 to min(length - 1, reachCap) steps from a head)
                // queen (BC_QUEEN_SAFE): the queen out of the sprint reach: the free steps and the old reach in full, the paid steps
                // beyond them (up to kQueenReach) at kQueenPaidW
                for (EnemyField const& e : enemies_) {
                    int dd = e.dist[dest];
                    if (BC_QUEEN && BC_QUEEN_SAFE && queenMe_) {
                        int Le = std::max(e.visible, kQueenAssumeLen);  // a 3-long dragon can show as 1 segment at her view's edge
                        int full = std::min(sprintMaxSteps(Le), std::max(p_.reachCap, sprintFree(Le)));
                        if (dd >= 2 && dd <= full) danger += p_.wDanger * newL * mult;
                        else if (dd > full && dd <= queenReachOf(e.visible)) danger += kQueenPaidW * p_.wDanger * newL * mult;
                    } else if (dd >= 2 && dd <= reachOf(e.visible)) {
                        danger += p_.wDanger * newL * mult;
                    }
                }
                if (BC_QUEEN && BC_QUEEN_SAFE && queenMe_ && !tipDist_.empty()) {  // queen: a child born on a tip this round
                    int td = tipDist_[dest];
                    if (td <= 1) danger += p_.wEngage * newL * mult;
                    else if (td == 2) danger += 0.5 * p_.wEngage * newL * mult;
                }
            }
        }
        if (!(BC_ENGAGE && k == 0) && nextToEnemyHead(dest, enemyLen, huntNext)) {
            // Parking next to an enemy head hands it the choice (and the drops). Only a trade we would take anyway, with the local
            // numbers, makes that acceptable.
            bool wanted = !grower_ && (huntNext || tradeOk(newL, enemyLen));
            double favour = 1.0;
            if (p_.exposureMode == 0) favour = wanted ? 0.0 : 1.0;
            else if (p_.exposureMode == 1) favour = wanted && localOk(dest, -1) ? p_.exposedFavour : 1.0;
            else favour = wanted && localOk(dest, -1) ? 0.0 : 1.0;
            danger = p_.wDanger * newL * mult * favour;
        }
        if (k == 0 && p_.exposureMode != 0 && !BC_ENGAGE) {
            // Sprint reach: an enemy of length Le can MOVE up to Le - 1 steps onto our head.
            for (EnemyField const& e : enemies_) {
                int dd = e.dist[dest];
                if (dd <= 1 || dd > reachOf(e.visible)) continue;
                bool local = localOk(dest, -1);
                if (p_.exposureMode == 2 && local) continue;
                bool wanted = !grower_ && (huntAt(e.head) || tradeOk(newL, e.visible));
                double favour = wanted && local ? p_.exposedFavour : 1.0;
                danger += p_.wExposed * newL * mult * favour;
            }
        }
        if (BC_ENGAGE && k == 0) {
            // Engage, retreat or hold: an enemy that can reach where we stop (one step or a sprint of up to min(its length - 1,
            // reachCap)) would trade us there if it pays it; in a replay sample most of the enemy's kills of ours came from 2-3 tiles
            // away. Where it would not pay them (our heads nearer the drops, we the shorter), we hold and farm.
            for (EnemyField const& e : enemies_) {
                int dd = e.dist[dest];
                bool q = BC_QUEEN && BC_QUEEN_SAFE && queenMe_;
                int qr = q ? queenReachOf(e.visible) : reachOf(e.visible);
                if (dd < 1 || dd > qr) continue;
                double v = tradeValue(e.visible, newL, dest, false, e.id);
                // queen: the queen is worth her length twice to the enemy (round 500), nearer heads more (in a test a 4-step sprint of a
                // 5-long dragon seen as 2 segments took her)
                if (q) v = (std::max(v, 0.0) + newL) * (1 + qr - dd) / qr;
                if (v > 0) danger += p_.wEngage * v * mult;
            }
        }
        if (grower_) {
            // Keep a buffer: enemy heads two steps away are a problem too.
            for (World::Seen const& s : w_.others)
                if (isEnemy(s) && distToHead(dist, s.head) <= 1) danger += p_.wDanger * p_.growerDanger;
        }

        // Pockets: a tile whose only other way out is one more tile is where dragons get sealed in.
        int exits = 0;
        for (int q = 0; q < 4; q++) {
            int n = b.nb[dest * 4 + q];
            if (n >= 0 && n != head && blk[n] <= 1) exits++;
        }
        if (exits <= 1 && k == 0) danger += p_.wPocket;  // corridors would pile this up along a plan
        // Through a portal the far side may be out of vision. Only the real first step
        // pays for that (lookahead leaves vision all the time), and only much when we
        // recently saw dragons around the exit.
        if (k == 0 && !w_.visible(dest) && dest != b.id(b.X(head) + DX[d], b.Y(head) + DY[d])) {
            bool busy = false;
            for (int q = -1; q < 4 && !busy; q++) {
                int n = q < 0 ? dest : b.nb[dest * 4 + q];
                busy = n >= 0 && w_.occSeen[n] >= 0 && w_.t.round - w_.occSeen[n] <= p_.blindMemory;
            }
            danger += busy ? p_.wBlind * (1.0 + 0.25 * newL) : p_.wBlindQuiet;
        }
        // Heads parked next to a portal are what dragons coming through it crash into
        // (they cannot see us). Pass through or move on; do not loiter there.
        // Only pacing counts: staying beside a portal without going through it. Walking up
        // to a portal and crossing it is free (the penalty used to wall off whole regions).
        bool crossing = dest != b.id(b.X(head) + DX[d], b.Y(head) + DY[d]);
        if (k == 0 && b.portalSide[dest] && b.portalSide[head] && !crossing) danger += p_.wPortalLoiter;
#if BC_MN_EXPLORE
        if (k == 0) danger += kMapnetDangerW * w_.danger(dest);  // BC_MAPNET: lambda * danger (about 2 steps a unit)
#endif
        if (BC_ROOMCAP && p_.roomOn && k == 0 && crossing && roomFull(dest)) danger += p_.wRoomFull;

        double eatW = BC_V9_KING && v9Feeder_ ? p_.kFeedForage : 1.0;
        if ((BC_V9_KING || BC_V9_EKING) && kingHead_ >= 0 && b.cheb(dest, kingHead_) <= kingYield()) eatW = 0.0;
        if (BC_QUEEN && BC_QUEEN_GROW) {  // queen growth arm (BC_QUEEN_GROW, off): a short queen eats first and more
            if (growHead_ >= 0 && b.cheb(dest, growHead_) <= kGrowYield) eatW = 0.0;
            if (queenMe_) eatW *= L < 5 ? 2.5 : L < 9 ? 1.5 : 1.0;
        }
#if BC_EXPAND_OPENING && BC_EO_QUEEN_MOTHER
        if (BC_QUEEN && queenMe_ && w_.openMode) eatW *= kEoQueenEat;  // open mode (off): the mother's food first
#endif
#if BC_QUEEN_GROWUP
        eatW = growEat(dest, L, eatW);
#endif
#if BC_LONGEST_RESERVE >= 3
        if (reserveFeedHead_ >= 0 && b.cheb(dest, reserveFeedHead_) <= kFeedYield) eatW = 0.0;  // BC_LONGEST_RESERVE 3: the reserve's food
#endif
#if BC_QUEEN_FEED350
        if (queenFeedHead_ >= 0 && b.cheb(dest, queenFeedHead_) <= kQFeedYield) eatW = 0.0;  // BC_QUEEN_FEED350 (off): her food
#endif
#if BC_QUEEN_FEED350 >= 2
        if (queenFeeds()) eatW *= kQFeedEat;  // BC_QUEEN_FEED350 (off): she eats first
#endif
#if BC_QUEEN_GROWFIRST
        if (queenGrows()) eatW *= kGrowFirstEat;  // BC_QUEEN_GROWFIRST: she grows first
#endif
        c.stepTerm = (c.eat ? p_.eatBonus * eatW : 0.0) - danger;
        if (BC_ECON_BED && w_.t.round < kBedUntil && !(BC_QUEEN && queenMe_)) {
            // BC_ECON_BED (c) unlock: a pearl sitting on a bed voids that bed's next spawn (the countdown redraws anyway): eating it
            // frees the bed
            if (c.eat && b.bed[dest] == 1 && w_.maxCd[dest] > 0) c.stepTerm += kWUnlock * std::min(0.5, 1.0 / std::max(1, static_cast<int>(w_.maxCd[dest])));
            // BC_ECON_BED (d) NOBLOCK: our head on a visible bed whose pearl spawns at the next round start voids that spawn
            if (k == 0 && !c.eat && b.bed[dest] == 1 && w_.next[dest] == 1 && w_.visible(dest)) {
                c.stepTerm -= kWNoBlock;
                if (econTraceOn()) econTrace_ += " noblock";
            }
        }
        // Disperse: every step through the crowd costs, except one that eats (BC_EAT_NO_DISPERSE: with 2+ own heads around every
        // move cost more than the pearls in view were worth, and the crowd around the spawn fountains ate far less than the
        // enemy).
        if (dispW_ > 0 && !(BC_EAT_NO_DISPERSE && c.eat)) {
            c.stepTerm -= dispW_ * crowdField_[dest];
            if (why.empty()) why = "disperse";
        }
        if (viaReverse) c.stepTerm -= p_.wReverse;
        // King zone (BC_V9_KING / BC_V9_EKING): keep clear of the king's head (our own dragons boxed long kings in at the unit
        // limit)
        if ((BC_V9_KING || BC_V9_EKING) && kingZone_ >= 0 && !v9Feeder_) {
            int kc = b.cheb(dest, kingZone_);
            // queen: our own crowd boxed the queen in during tests: the others keep clear of her head, harder
            double zw = BC_QUEEN && BC_QUEEN_ZONE ? std::max(p_.ekZone, kQueenZone) : BC_V9_KING ? p_.kZone : p_.ekZone;
            if (kc <= 2) c.stepTerm -= zw * (3 - kc) / 3.0;
        }
        // Dead ends (BC_DEADEND): entering a tree-like branch. Too short to reverse out (4) means dying in it; otherwise it costs
        // the reverse split's stub (in a replay sample a large share of our own-move deaths, besides feeding, happened in such
        // branches on corridor-heavy maps).
        bool deadVeto = false;
        bool dipStep = false;  // BC_ECON_DIP: this step enters / continues a lane the dip gate allowed
        bool const laneEntry = BC_DEADEND && b.deadEnd[dest] && !b.deadEnd[head];
        // the tuned parameters have the dead-end veto off (deadEndOn 0 in every tuned group): a lane is only held back by the
        // space check
        if (BC_DEADEND && p_.deadEndOn && laneEntry && !(BC_ECON_DIP && newL < 4 && !(BC_QUEEN && queenMe_) && dipOk(head, dest, newL))) {
            if (newL < 4 || (BC_QUEEN && BC_QUEEN_SAFE && queenMe_)) {
                deadVeto = true;  // queen: a dead end is the queen's grave
            } else if (!viaReverse) {
                c.stepTerm -= p_.wReverse;
            }
        }
        if (BC_ECON_DIP && newL < 4 && !(BC_QUEEN && queenMe_) && w_.init.id >= 2) {
            if (laneEntry && dipOk(head, dest, newL)) {
                dipStep = true;  // BC_ECON_DIP (b): a short dragon dips for the food (the lane's pearls bring it to 4 and the reverse)
                if (econTraceOn()) econTrace_ += " dip-in";
            } else if (BC_DEADEND && b.deadEnd[dest] && b.deadEnd[head] && w_.t.units <= w_.init.unitLimit - 2 &&
                       laneAheadFood(dest, head, body) >= 4 - newL) {
                dipStep = true;  // inside the lane: enough food ahead to reach 4 and the reverse
            }
            if (dipStep) viaReverse = true;
        }
        // The step term is discounted like every other step, the last one included (an eat on the horizon used to count as much
        // as an eat now, so dragons kept deferring pearls and circled next to one for many rounds).
        c.score = value + c.stepTerm * gp(k) + p_.wSpace * std::min(space, need) / need;
        if ((space < need && !viaReverse) || deadVeto) {
            c.score = -1000.0 + space;  // vetoed, kept only as a last resort
            c.terminal = true;
        }
        c.why = deadVeto ? "dead-end" : space < need ? "cramped" : !why.empty() ? why : (c.eat ? "eat" : "forage");
        // Portal probe: the entry's only way on is the unpaired portal the lookahead cannot take, so every line through it looked
        // like death (-900); reaching it ends the line (the probe step follows next turn)
        if ((BC_V9_ESCAPE || BC_PORTAL2) && escaping_ && dest == probeEntry_ && c.score > -500) c.terminal = true;
#if BC_HARVEST
        if (harvestEntry_ >= 0 && dest == harvestEntry_ && c.score > -500) c.terminal = true;  // BC_HARVEST: next turn it steps through
#endif
        return c;
    }

    // Best score reachable from this state within `depth` more moves; NaN-free,
    // returns -1e18 when no move is legal. Sets timedOut_ when the deadline passes.
    double searchFrom(std::vector<int> const& body, int L, int depth, int k, std::vector<int>& eaten) {
        double best = -1e18;
        std::vector<int> next;
        for (int d = 0; d < 4; d++) {
            if (timeUp()) return best;
            Choice c = scoreFrom(body, L, d, k, eaten, &next);
            if (c.dest < 0) continue;
            double v = c.score;
            if (depth > 1 && !c.terminal) {
                if (c.eat) eaten.push_back(c.dest);
                std::vector<int> child = next;
                double rest = searchFrom(child, c.newL, depth - 1, k + 1, eaten);
                if (c.eat) eaten.pop_back();
                // A dead end deeper down is as bad as it looks; otherwise add what this step earned.
                v = rest <= -1e17 ? -900.0 : c.stepTerm * gp(k) + rest;
            }
            best = std::max(best, v);
        }
        // BC_ECON_DIP (c): a dragon at a lane's tip with no move left leaves by the reverse split: the stub it leaves is worth
        // -(2 - dipQ) pearls, not death (-900)
        if (BC_ECON_DIP && best <= -1e17 && dipStubOk(body, L)) return -(2.0 - kDipQ) * p_.eatBonus * gp(k);
        return best;
    }

    bool trySplit(Choice& out, Choice const& bestMove) {
        Board const& b = w_.board;
        // King reserve: the last kReserve unit slots are the king's (a boxed-in king at the limit cannot reverse-split)
#if BC_LONGEST_RESERVE
        if (reserveMe_) return false;  // BC_LONGEST_RESERVE: the team's longest keeps every segment
#endif
        bool reserve = BC_V9_KING || (BC_V9_EKING && w_.t.round >= p_.ekFrom);  // from the king's start only
        int cap = w_.init.unitLimit - (reserve && !v9King_ && !v9KingNow_ ? p_.kReserve : 0);
#if BC_UNIT_SLOT
        cap = unitCap(cap);
#endif
        if (w_.t.units >= cap || !w_.bodyKnown()) return false;
        if (w_.t.round >= p_.lateNoSplit) return false;  // the king's end game, no more bodies
        // Swarm (BC_SWARM): until swarmUntil every dragon of 4 or more splits in half, growers too, while eating too, in a crowd
        // too; kings and attackers stay whole and the safety checks below still apply.
        bool const swarm = BC_R5_SWARM_OFF w_.t.round < p_.swarmUntil && L_ >= BC_R5_SWARM_MINLEN && !kingMe_ && !leaving_ && w_.t.round < p_.feedRound &&
                           bestMove.why != "trade" && bestMove.why != "defend";
        // Opening prior's split: where the imitation net would split, whatever the role (the safety checks below still apply)
        bool const rl = BC_RL_PRIOR && rlSplit_ && L_ >= 4 && !kingMe_ && !leaving_ && w_.t.round < p_.feedRound &&
                        bestMove.why != "trade" && bestMove.why != "defend";
        bool earlySplit = w_.t.round < p_.esUntil || swarm || rl || queenBud() || (BC_QUEEN_TINY && queenMe_);  // early splits: the unit race first
        if ((bestMove.eat && !earlySplit) || bestMove.why == "trade" || bestMove.why == "defend") return false;
        for (Hunt const& h : hunts_)
            if (h.job == Job::Attack) return false;  // attackers stay whole and committed

        int n = 0;
        if (BC_QUEEN && BC_QUEEN_GROW && queenMe_ && L_ < kGrowSplitMin) return false;  // growth arm (BC_QUEEN_GROW, off): she keeps her free steps
#if BC_QUEEN_GROWUP
        if (growMe_) return false;  // BC_QUEEN_GROWUP (off): once she grows, no economic split (the swarm would halve her at 4 until r400); QSK's hold is not one
#endif
#if BC_MN_REGION
        if (queenMe_) {  // BC_MAPNET: no split inside a closed room smaller than 2L + 2
            World::Pocket pk = w_.pocket(w_.head, 2 * L_ + 2);
            if (pk.closed && pk.size < 2 * L_ + 2) return false;
        }
#endif
        if (escortCell_ >= 0) return false;  // queen escort (off): an escort keeps its body while it shields her
        if (BC_QUEEN && BC_QUEEN_SPLIT && queenMe_) {
            if (BC_QUEEN_TINY && L_ >= 4 && !queenThreatened()) {
                // tiny queen (BC_QUEEN_TINY, off): she keeps 2 segments and buds the rest (a reverse split: the child leaves from her old
                // tail). A 2-3 long queen offers an attacker little to trade into, and every 2 pearls
                // she eats become an army dragon; she grows in the feeding phase.
                n = L_ - 2;
            } else {
                // queen: no economic split for the queen, rebuild included; the one exception is the opening bud (queenBud)
                if (!queenBud() || queenThreatened()) return false;
                n = std::min(3, L_ - 4);
            }
        } else if (kingMe_ && !rebuild_) {
            return false;  // dragon plans, guard: the king keeps every segment
        }
#if BC_EXPAND_OPENING && BC_EO_QUEEN_MOTHER
        if (BC_QUEEN && queenMe_ && w_.openMode && L_ >= 4 && n == 0) n = 2;  // open mode (off): the mother buds a 2-long explorer (she keeps >= 2)
#endif
        if (n > 0) {
            // queen: the queen's bud, checked below like any split
        } else if (swarm || rl) {
            n = L_ / 2;
        } else if (leaving_) {
            return false;  // not the pocket's guard: get out first, do not add bodies here
        } else if (BC_ROOMCAP && p_.roomOn && roomCap_ > 0 && roomMembers_ >= roomCap_ && !rebuild_) {
            return false;  // room capacity: a full room gets no more of ours (a child born here cannot find the way out)
        } else if ((rebuild_ || pocketGuard_) && L_ >= 4) {
            n = 2;  // survival policy / pocket guard: reproduce; the child is the one that leaves
        } else if (p_.tacticsOn && crowd_ >= p_.crowdNoSplit) {
            return false;  // do not add bodies to a crowd
        } else if (BC_V9_DENSITY && w_.t.units >= std::max(p_.kDensMin, b.NC / std::max(1, p_.kDensity))) {
            return false;  // BC_V9_DENSITY: the map holds no more of ours
        } else if (BC_V9_KING ? (v9Feeder_ || w_.t.round >= 500 - p_.kWindow) : w_.t.round >= p_.feedRound) {
            return false;  // feeding phase: consolidate, never split (with the late king: until a dragon leaves to feed)
        } else if (grower_) {
            if (lateGame()) return false;  // the longest dragon decides the tiebreak
            int keep = p_.growerKeepBase + w_.t.round / p_.growerKeepEvery;
            if (L_ < keep + p_.growerChild) return false;
            n = p_.growerChild;
        } else {
#if BC_R5_SWARM_MINLEN > 4
            if (L_ < std::max(static_cast<int>(p_.swarmSplitLen), static_cast<int>(BC_R5_SWARM_MINLEN))) return false;  // BC_R5_SWARM_MINLEN: the swarm's minimum length binds the plain split too
#else
            if (L_ < p_.swarmSplitLen) return false;
#endif
#if BC_R5_NETSPLIT
            return false;  // BC_R5_NETSPLIT (off): only the net's trigger (rl), the rebuild / pocket guard / grower rules split
#endif
            // Late game: the tiebreak is the longest dragon, so stop splitting and let
            // length pile up, unless the army needs rebuilding.
            if (w_.t.round >= p_.growRound && w_.t.units >= p_.lateSplitUnits) return false;
            n = L_ / 2;
        }
        for (World::Seen const& s : w_.others)
            if (isEnemy(s) && b.cheb(s.head, w_.head) <= p_.splitEnemyDist) return false;

        int keep = L_ - n;
        std::vector<int> parent(w_.body.begin(), w_.body.begin() + keep);
        std::vector<int> child(w_.body.rbegin(), w_.body.rbegin() + n);

        // Parent must still have a roomy move with the child as a wall, and vice versa.
        std::vector<int> blk = base_;
        for (int c : child) blk[c] = INF;
        markBody(blk, parent, keep);
        if (!hasRoomyMove(parent.front(), blk, keep)) return false;

        blk = base_;
        for (int c : parent) blk[c] = INF;
        markBody(blk, child, n);
        if (!hasRoomyMove(child.front(), blk, n)) return false;

        out.split = true;
        out.n = n;
        out.why = BC_QUEEN && BC_QUEEN_SPLIT && queenMe_ ? "queen-bud" : swarm ? "swarm" : rebuild_ ? "rebuild" : grower_ ? "bud" : "split";
        return true;
    }

    // A sprint trade: MOVE 2..sprintMax steps (a length-L dragon has at most L - 1) through
    // free tiles onto an enemy head we are allowed to trade with, where we have the local
    // numbers to eat the drops. Picks the longest such enemy, then the shortest path.
    bool sprintTrade(std::string& path) {
        Board const& b = w_.board;
        if (grower_ || L_ < 3 || (BC_QUEEN && queenMe_) || lockNoTrade()) return false;  // queen: never the queen (LOCK: nobody)
        int maxSteps = std::min(p_.sprintMax, BC_QUEEN ? sprintMaxSteps(L_) : L_ - 1);  // sprint rule: ceil(L/4) free + paid
        std::vector<int> blk = ownBlk();
        // BFS over tiles free right now (blk <= 1: no dragon, not our body).
        std::vector<int> dist(b.NC, INF), from(b.NC, -1), dirTo(b.NC, -1);
        std::vector<int> q{w_.head};
        dist[w_.head] = 0;
        for (size_t qi = 0; qi < q.size(); qi++) {
            int c = q[qi];
            if (dist[c] + 1 >= maxSteps) continue;  // the last step goes onto the head itself
            for (int d = 0; d < 4; d++) {
                int n = b.nb[c * 4 + d];
                if (n < 0 || dist[n] != INF || blk[n] > 1) continue;
                dist[n] = dist[c] + 1;
                from[n] = c;
                dirTo[n] = d;
                q.push_back(n);
            }
        }
        if (BC_ENGAGE && w_.t.units <= kSurvivalUnits) return false;  // fixed survival policy
        int bestLen = -1, bestSteps = INF, bestEnd = -1, bestDir = -1;
        double bestValue = -1e9;
        for (EnemyField const& e : enemies_) {
            if (e.visible < 2) continue;
            double value = 0;
            if (BC_ENGAGE) {
                value = tradeValue(L_, e.visible, e.head, true, e.id);
                if (value < p_.engageAttack || value < bestValue) continue;
                if (BC_FIGHT && p_.fightTradeMin > -10 && e.visible - L_ < p_.fightTradeMin) continue;  // no even sprint swaps
            } else {
                bool allowed = huntAt(e.head) != nullptr || tradeOk(L_, e.visible);
                if (!allowed || !localOk(e.head, e.id) || !supportOk(e.head, e.id)) continue;
            }
            for (int d = 0; d < 4; d++) {
                // stand on `pre`, step d onto the head
                int pre = -1;
                for (int k = 0; k < 4; k++) {
                    int n = b.nb[e.head * 4 + k];
                    if (n >= 0 && b.nb[n * 4 + d] == e.head) pre = n;
                }
                if (pre < 0 || dist[pre] == INF) continue;
                int steps = dist[pre] + 1;
                if (steps < 2 || steps > maxSteps) continue;
                if (BC_ENGAGE ? (value > bestValue || (value == bestValue && steps < bestSteps))
                              : (e.visible > bestLen || (e.visible == bestLen && steps < bestSteps))) {
                    bestValue = value;
                    bestLen = e.visible;
                    bestSteps = steps;
                    bestEnd = pre;
                    bestDir = d;
                }
            }
        }
        if (bestEnd < 0) return false;
        std::string rev(1, DCH[bestDir]);
        for (int c = bestEnd; c != w_.head; c = from[c]) rev += DCH[dirTo[c]];
        path.assign(rev.rbegin(), rev.rend());
        return true;
    }

    // ---- portal tactics (probe unpaired portals, one guard per pocket) --------------------------
#if BC_QUEEN_BREATHE
    // BC_QUEEN_BREATHE: a teammate's first step onto one of our queen's sides that leaves her fewer than 2 free sides (blk <=
    // 1: free at move 1; in replays the boxed queens often had our own bodies and tails on their sides)
    // BC_QUEEN_BREATHE 2 (the bubble): every first step into the 8 tiles around her head - with mode 1 alone, test games
    // still had her sealed in by our own swarm, the box closing 2+ turns before she died
    bool breatheVeto(int dest, std::vector<int> const& blk) const {
        Board const& b = w_.board;
        if (BC_QUEEN_BREATHE >= 2) return dest != guardQh_ && b.cheb(dest, guardQh_) <= 1;
        bool onSide = false;
        int freeAfter = 0;
        for (int q = 0; q < 4; q++) {
            int n = b.nb[guardQh_ * 4 + q];
            if (n < 0) continue;
            if (n == dest) onSide = true;
            else if (blk[n] <= 1) freeAfter++;
        }
        return onSide && freeAfter < 2;
    }
#endif
#if BC_LONGEST_RESERVE
    bool reserveMe_ = false;  // BC_LONGEST_RESERVE: the team's longest from kReserveFrom (World::longestReserve): no split, the boxed reverse only
#endif
    bool prober_ = false, leaving_ = false, pocketGuard_ = false;
    bool escaping_ = false;       // portal escape: a starving prober
    int roomMembers_ = 0, roomCap_ = 0;  // room capacity (BC_ROOMCAP): our dragons in my closed room, how many it holds
    int probeEntry_ = -1, probeDir_ = 0;
    std::vector<int> exits_;      // landing tiles outside the pocket, through paired portals
    // Portal tactics (BC_PORTAL2): my region (tiles reachable on the known map, dragons ignored) is closed and poor
    bool hungryRegion_ = false, hungryProbe_ = false;
    std::vector<int> regionDist_;  // steps from my head inside the closed region, INF elsewhere

    void floodRegion() {
        Board const& b = w_.board;
        hungryRegion_ = false;
        regionDist_.assign(b.NC, INF);
        std::vector<int> q{w_.head};
        regionDist_[w_.head] = 0;
        bool unpaired = false;
        for (size_t qi = 0; qi < q.size(); qi++) {
            int c = q[qi];
            for (int d = 0; d < 4; d++) {
                Edge const& e = b.side(c, d);
                if (e.kind == 1) continue;
                int n = b.nb[c * 4 + d];
                if (n < 0) {
                    if (e.kind == 2) unpaired = true;
                    continue;
                }
                if (regionDist_[n] < INF) continue;
                if (w_.seenRound[n] < 0) {  // an unseen tile is reachable: the region is open
                    regionDist_.assign(b.NC, INF);
                    return;
                }
                regionDist_[n] = regionDist_[c] + 1;
                q.push_back(n);
            }
        }
        if (!unpaired) return;
        double rate = 0;
        for (int c : q)
            if (b.bed[c] == 1)
                rate += w_.cdObs[c] > 0 ? p_.p2GapScale / std::max<int>(1, w_.maxCd[c]) : p_.p2BedRate;
        int own = 1;
        for (World::Seen const& s : w_.others)
            if (!isEnemy(s) && regionDist_[s.head] < INF) own++;
        for (auto const& f : w_.know.friends)
            if (w_.t.round - f.round <= 2 && !w_.visible(f.cell) && regionDist_[f.cell] < INF) own++;
        hungryRegion_ = rate / own < p_.p2Poor || w_.t.round - w_.lastEatRound >= p_.p2Starve;
    }

    void planPortals() {
        Board const& b = w_.board;
        prober_ = leaving_ = pocketGuard_ = false;
        exits_.clear();
        if (lean_) return;
        roomMembers_ = roomCap_ = 0;
        // Pocket: tiles reachable over open edges only; every one seen, few, with a portal way out.
        if (p_.pocketOn || (BC_ROOMCAP && p_.roomOn)) {
            std::vector<int> region{w_.head};
            std::vector<char> in(b.NC, 0);
            in[w_.head] = 1;
            bool closed = true;
            int rawTile = -1, rawDir = -1;  // an unpaired portal end on the region (a way out we have not seen)
            for (size_t qi = 0; qi < region.size() && closed; qi++) {
                int c = region[qi];
                for (int d = 0; d < 4; d++) {
                    Edge const& e = b.side(c, d);
                    if (e.kind == 1) continue;
                    if (e.kind == 2) {
                        int n = b.nb[c * 4 + d];
                        if (n >= 0) exits_.push_back(n);
#if BC_MN_FACTS
                        else if (w_.portalState(c, d) != comms::G_UNKNOWN || w_.trapStep(c, d)) {}  // BC_MAPNET: known: not a probe
#endif
                        else if (rawTile < 0 || myDist_[c] < myDist_[rawTile]) rawTile = c, rawDir = d;
                        continue;
                    }
                    int n = b.id(b.X(c) + DX[d], b.Y(c) + DY[d]);
                    if (in[n]) continue;
                    if (w_.seenRound[n] < 0 || static_cast<int>(region.size()) >= p_.pocketMax) {
                        closed = false;
                        break;
                    }
                    in[n] = 1;
                    region.push_back(n);
                }
            }
            if (BC_ROOMCAP && p_.roomOn && closed && (!exits_.empty() || rawTile >= 0)) {
                // Room capacity: members are me, teammates in view and teammates heard in the last two rounds;
                // the draw ranks them by a hash of id and round / 50, the same in every member's process.
                std::vector<int> ids{w_.init.id & 4095};
                for (World::Seen const& s : w_.others)
                    if (!isEnemy(s) && in[s.head]) ids.push_back(s.id & 4095);
                for (auto const& f : w_.know.friends)
                    if (w_.t.round - f.round <= 2 && in[f.cell] && !w_.visible(f.cell)) ids.push_back(f.id);
                std::sort(ids.begin(), ids.end());
                ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
                roomMembers_ = static_cast<int>(ids.size());
                roomCap_ = std::max(1, static_cast<int>(region.size()) / std::max(1, p_.roomTiles));
                uint64_t epoch = static_cast<uint64_t>(w_.t.round / 50);
                auto ticket = [&](int id) { return comms::mix(static_cast<uint64_t>(id) * 1000003ull + epoch); };
                uint64_t mine = ticket(w_.init.id & 4095);
                int rank = 0;
                for (int id : ids)
                    if (id != (w_.init.id & 4095) && ticket(id) < mine) rank++;
                pocketGuard_ = rank < roomCap_;
                leaving_ = !pocketGuard_;
                if (leaving_ && exits_.empty()) {
                    // only unpaired portals lead out: go through one like a probe (the landing is unknown)
                    // BC_QGATE: the queen never probes; she stays and the room cap does not move her
                    if (!(BC_QGATE && queenMe_)) {
                        prober_ = true;
                        probeEntry_ = rawTile;
                        probeDir_ = rawDir;
                    }
                }
            } else if (closed && !exits_.empty()) {
                // own dragons in the pocket: me, teammates in view, teammates heard last round
                int guardLen = L_, guardId = w_.init.id & 4095;
                auto consider = [&](int len, int id) {
                    if (len < guardLen || (len == guardLen && id < guardId)) guardLen = len, guardId = id;
                };
                for (World::Seen const& s : w_.others)
                    if (!isEnemy(s) && in[s.head]) consider(s.visible, s.id & 4095);
                for (auto const& f : w_.know.friends)
                    if (w_.t.round - f.round <= 1 && in[f.cell]) consider(f.len, f.id);
                pocketGuard_ = guardId == (w_.init.id & 4095);
                leaving_ = !pocketGuard_;
            } else {
                exits_.clear();
            }
        }
        // Probe: the nearest unpaired portal end, if no shorter-or-equal teammate in view is closer.
        bool starving = BC_V9_ESCAPE && w_.t.round < p_.escUntil && w_.t.round - w_.lastEatRound >= p_.escStarve &&
                        w_.t.units >= 2;
        escaping_ = false;
        hungryProbe_ = false;
        bool early = false;
        if (BC_PORTAL2 && !leaving_ && L_ <= p_.p2MaxLen && w_.t.units >= 2) {
            if (w_.t.round < p_.p2Until) {
                floodRegion();
                hungryProbe_ = hungryRegion_;
            }
            early = !hungryProbe_ && w_.t.round < p_.p2Early && w_.t.round - w_.lastEatRound >= p_.p2EarlyStarve;
        }
#if BC_BEDRAID
        // BC_BEDRAID (off): an opening food probe - a hungry non-queen short dragon, whatever the unit count (strong teams cross portals within the first few rounds on some maps)
        bool const raid = !(BC_QUEEN && queenMe_) && L_ <= p_.p2MaxLen && w_.t.round < kRaidUntil && w_.t.units >= 1 &&
                          w_.t.round - w_.lastEatRound >= kRaidStarve;
        starving |= raid;
#endif
#if BC_EXPAND_OPENING && BC_EO_MIRROR_TARGET
        // open mode (off): with no reachable mirrored band (or no symmetry yet), a non-queen probes the nearest unknown portal
        if (w_.openMode && eoMirror_ < 0 && !(BC_QUEEN && queenMe_)) starving = true;
#endif
        if (p_.probeOn && !leaving_ && !myDist_.empty() &&
            (hungryProbe_ || early || (!grower_ && L_ <= p_.probeMaxLen && (w_.t.units > kSurvivalUnits || starving)))) {
            int bestD = INF;
            int range = hungryProbe_ ? b.NC : starving ? p_.escRange : early ? std::max(p_.probeRange, p_.p2Range) : p_.probeRange;
            for (int c = 0; c < b.NC; c++) {
                int dc = hungryProbe_ ? regionDist_[c] : myDist_[c];
                if (dc > range || dc >= bestD) continue;
                for (int d = 0; d < 4; d++) {
                    Edge const& e = b.side(c, d);
#if BC_MN_FACTS
                    if (w_.portalState(c, d) != comms::G_UNKNOWN || w_.trapStep(c, d)) continue;  // BC_MAPNET: probed once per team
#endif
                    if (e.kind == 2 && e.partnerOrient < 0) {
                        bestD = dc;
                        probeEntry_ = c;
                        probeDir_ = d;
                        break;
                    }
                }
            }
            if (bestD < INF) {
                // One prober per portal: no teammate already at the entry, none closer in view, and
                // none heard (sonar, last 2 rounds) closer by torus Manhattan distance.
                bool mine = true;
                int maxLen = hungryProbe_ || early ? p_.p2MaxLen : p_.probeMaxLen;
                for (World::Seen const& s : w_.others)
                    if (!isEnemy(s) && b.cheb(s.head, probeEntry_) <= 2) mine = false;
                for (FriendField const& f : friends_) {
                    int fl = 0;
                    for (World::Seen const& s : w_.others)
                        if (s.id == f.id) fl = s.visible;
                    if (fl > maxLen) continue;
                    int fd = f.dist[probeEntry_];
                    if (fd < bestD || (fd == bestD && f.id < w_.init.id)) mine = false;
                }
                int myTorus = TeamPlan::torus(b, w_.head, probeEntry_);
                for (auto const& f : w_.know.friends) {
                    if (w_.t.round - f.round > 2 || w_.visible(f.cell) || f.len > maxLen) continue;
                    // BC_PORTAL2: a heard teammate I cannot reach on the known map (behind a wall) is not nearer the portal whatever the
                    // torus distance says (otherwise probers deferred to teammates across a wall and nobody probed)
                    if (BC_PORTAL2 && myDist_[f.cell] >= INF) continue;
                    int fd = TeamPlan::torus(b, f.cell, probeEntry_);
                    if (fd < myTorus || (fd == myTorus && f.id < (w_.init.id & 4095))) mine = false;
                }
                if (BC_QGATE && queenMe_) mine = false;  // BC_QGATE: the queen never probes an unknown portal
                prober_ = mine;
                escaping_ = mine && (starving || hungryProbe_ || early);  // portal escape / BC_PORTAL2: a real pull to the portal
                hungryProbe_ = (hungryProbe_ || early) && mine;
#if BC_MN_FACTS
                if (prober_ && bestD <= 2) {
                    w_.announceCell = probeEntry_, w_.announceDir = probeDir_, w_.announceRound = w_.t.round;
                    w_.announceState = comms::G_CLAIM;  // holds the portal for the team, never read as a trap
                }
#endif
            }
        }
    }

    // Room capacity (BC_ROOMCAP): is `start` in a closed room (open edges only, all seen, at most pocketMax tiles)
    // already holding as many of ours as it may (teammates in view or heard in the last two rounds)?
    bool roomFull(int start) {
        Board const& b = w_.board;
        std::vector<int> region{start};
        std::vector<char> in(b.NC, 0);
        in[start] = 1;
        for (size_t qi = 0; qi < region.size(); qi++) {
            int c = region[qi];
            for (int d = 0; d < 4; d++) {
                if (b.side(c, d).kind != 0) continue;
                int n = b.id(b.X(c) + DX[d], b.Y(c) + DY[d]);
                if (in[n]) continue;
                if (w_.seenRound[n] < 0 || static_cast<int>(region.size()) >= p_.pocketMax) return false;
                in[n] = 1;
                region.push_back(n);
            }
        }
        int members = 0;
        for (World::Seen const& s : w_.others)
            if (!isEnemy(s) && in[s.head]) members++;
        for (auto const& f : w_.know.friends)
            if (w_.t.round - f.round <= 2 && in[f.cell] && !w_.visible(f.cell)) members++;
        return members >= std::max(1, static_cast<int>(region.size()) / std::max(1, p_.roomTiles));
    }

    bool hasRoomyMove(int head, std::vector<int> const& blk, int len) {
        Board const& b = w_.board;
        int need = std::min(std::max(static_cast<int>(p_.spaceFactor * len) + p_.spaceMargin, p_.splitRoom), b.NC / 2);
        for (int d = 0; d < 4; d++) {
            int n = b.nb[head * 4 + d];
            if (n < 0 || blk[n] > 1) continue;
            if (nav_.flood(b, n, blk, need + 1) >= need) return true;
        }
        return false;
    }
};

}  // namespace bc
