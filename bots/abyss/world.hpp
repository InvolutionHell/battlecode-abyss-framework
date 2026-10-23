#pragma once

#include "board.hpp"
#include "comms.hpp"
#include "io.hpp"
#if BC_MAPNET
#include "region.hpp"
#endif

namespace bc {

// BC_MN_ROOMS: every write to an input of the rooms (a tile's known flag, bed, its top / left edge record) touches
// that tile, so RegionMap::recompute works on the changed tiles only (region.hpp)
#if BC_MN_ROOMS
#define MN_TOUCH(c) mn.touch((c), board.NC)
#else
#define MN_TOUCH(c) ((void)0)
#endif

// What one dragon process knows. Rebuilt from scratch by every process: a split child inherits nothing, so everything
// here must be recomputable.
//
// The bot is map-agnostic: no map database, no pearl timetable (the engine seeds pearl spawns randomly per match, and
// tournament maps are unseen). Terrain, pearl beds, their countdowns and how fast each bed refills are all learnt
// from vision.
struct World {
    Init init;
    Board board;

    // pearls, from vision only
    int simRound = -1;
    std::vector<int> next;        // countdown per tile while tracked from vision, -1 unknown
    std::vector<int> lastSpawn;   // round a tracked countdown ran out (a pearl likely spawned), -1 never
    std::vector<int> seenRound;   // last round the tile was in vision, -1 never
    std::vector<int8_t> seenPearl;
    std::vector<int16_t> maxCd;   // largest countdown ever seen on a bed: its refill gap, roughly
    std::vector<int16_t> cdObs;   // rounds a countdown was seen on the bed

    // this turn
    Turn t;
    int head = -1;
    std::vector<int> body;        // own body, head first; may be a prefix while the tail is unknown
    bool unknownStep = false;
    bool scavOn = false;
    bool crossedPortal = false;   // situational sonar: this turn's head landed through an unpaired portal
                                  // (unknownStep above: our last move went through an unpaired portal, the body was
                                  // not advanced; scavOn: the scavenger is on, Params::wScav > 0, set by the policy)
    std::vector<int> ownExtra;    // own segments seen in vision but not part of the tracked prefix
    std::vector<int> occ;         // tile -> dragon id occupying it (other dragons), -1 free
    std::vector<int> occHeadId;   // tile -> id if it holds another dragon's head
    std::vector<int> visRound;    // == t.round when the tile is in this turn's vision
    std::vector<int> occSeen;     // last round another dragon was seen on the tile, -1 never
    struct Seen { int id; char team; int head; int headDir; int visible; };
    std::vector<Seen> others;     // other dragons with a visible head

    comms::Knowledge know;        // facts heard from teammates (and our own, to relay)
    // Explored sectors: mine plus heard; a sector counts once half its tiles were seen.
    uint64_t teamSectors = 0;
    std::vector<int16_t> sectorSeen, sectorSize;
    int sectorOf(int c) const {
        return board.Y(c) * comms::kSectorsY / board.H * comms::kSectorsX + board.X(c) * comms::kSectorsX / board.W;
    }
    bool sectorExplored(int c) const { return teamSectors >> sectorOf(c) & 1ull; }
    // Portal ends seen, by portal id: (orient 0 = hE / 1 = vE, x, y). A portal is only
    // walkable once both ends are known (Board::stepRaw treats an unpaired end as a wall).
    struct PortalEnd { int orient, x, y; };
    std::vector<std::pair<int, std::vector<PortalEnd>>> portalEnds;
    int firstRound = -1;          // round this process first played (split children start late)
    int lastFriendRound = -1;     // last round a teammate's head was in view (birth counts: next to the parent)
    bool isolated() const { return t.round - lastFriendRound >= kIsolatedRounds; }
    int startUnits = -1;          // UNIT_COUNT on round 0 (starting dragons per team), -1 unknown

    // online map features (map groups without map names)
    int tilesSeen = 0;            // distinct tiles ever in vision
#if BC_QUEEN_BREATHE
    int qTight = 0;               // BC_QUEEN_BREATHE: the queen's turns in a row with <= 1 free side (her process
                                  // only)
#endif
#if BC_HARVEST
    int harvestRound = -1;        // BC_HARVEST: the round this process stepped into a harvest room (-1 never): it
                                  // keeps the room logic after that
#endif
#if BC_OPEN_EXPAND
    int firstHead = -1;           // BC_OPEN_EXPAND: this process's head on its first turn (its mirror: the enemy's
                                  // start)
    int expandGate = -1;          // ... BC_OPEN_EXPAND_GATE: -1 undecided, 0 off, 1 on (sticky)
#endif
#if BC_EXPAND_OPENING
    int eatCount = 0;             // BC_EXPAND_OPENING: pearls this process ate (its birth is not one)
    bool openMode = false;        // ... in the expand-opening mode this turn (Policy::planOpenMode)
    bool motherGate = false;      // ... QUEEN_MOTHER: her portal gate relaxed (from the turn after the mode starts)
#endif
#if BC_QUEEN_GROWUP
    int qTurns = 0, qTightN = 0;  // BC_QUEEN_GROWUP: the queen's turns before kGrowCheck, and those with <= 1 free
                                  // side (her process only)
    bool qGrow = false;           // ... latched at kGrowCheck in her process: she grows (mode 1: the share met; mode 2: always)
    int qGrowHeard = -1;          // ... a teammate's process: the newest round her ask was heard (SELF with kGrowAskVal)
    // her turn: the free sides at its start (other bodies, tails included, and her own block), counted before kGrowCheck; the
    // latch at the first turn from kGrowCheck
    void noteQueenTurn(int freeSides) {
        if (t.round < kGrowCheck) {
            qTurns++;
            qTightN += freeSides <= 1;
        } else if (qTurns >= 0) {
            qGrow = BC_QUEEN_GROWUP >= 2 || (qTurns > 0 && qTightN >= kGrowShare * qTurns);
            qTurns = -1;  // decided
        }
    }
    bool qGrowAsk() const { return qGrow && t.round < kGrowUntil && t.length < kGrowUpTo; }  // her: eat first, be left her food
#endif
#if BC_MN_ROOMS
    int mnRoomsCost = -1;         // BC_V11_TRACE: points of this turn's room recompute (-1: none ran)
    int mnMiss = 0;               // BC_V11_TRACE: tiles written without a touch, found after this turn's recompute
#endif
    int bedsSeen = 0;             // of those, pearl beds
    int hotBeds = 0;              // beds that refill within Params::hotGap rounds (fountains)
    double refillRate = 0;        // sum over seen beds of 1 / refill gap: pearls per round seen so far
    int bedsWithCd = 0;           // beds whose countdown we have seen
    double sumMaxCd = 0;          // sum of their largest countdowns (refill gap estimates)

    // Chance that a tile we have never seen holds a pearl now: the share of seen tiles that are beds, times the share
    // of a refill gap elapsed since the start, discounted for pearls someone else already ate.
    double unseenPearlProb(Params const& p) const {
        if (tilesSeen < p.unseenMinTiles || bedsWithCd == 0) return 0.0;
        double bedShare = static_cast<double>(bedsSeen) / tilesSeen;
        double meanGap = std::max(5.0, sumMaxCd / bedsWithCd);
        return std::min(0.9, bedShare * std::min(1.0, (t.round + 1) / meanGap) * p.unseenScale);
    }
    // The same for a never-seen tile known to be a bed (its mirror image is one).
    double unseenBedProb(Params const& p) const {
        if (tilesSeen < p.unseenMinTiles || bedsWithCd == 0) return 0.0;
        double meanGap = std::max(5.0, sumMaxCd / bedsWithCd);
        return std::min(0.9, std::min(1.0, (t.round + 1) / meanGap) * p.unseenScale);
    }

    // Map symmetry (BC_MIRROR): the beds are fixed and the map is a mirror image, so the beds we know on our side
    // tell where the enemy's are (raid them). Every official map is symmetric (SYMMETRY x / y / xy in the map file,
    // exact in beds, gaps and edges); a dragon is not told which. Candidate s stays alive while every tile we saw
    // whose mirror image under s we saw too matches it (bed or not, all four edges); one mismatch rules s out, and
    // teammates pool what they ruled out (SYM facts). The symmetry is known once a single candidate is left. Then
    // what we see is copied onto the never-seen mirror side: beds, fountain gaps and kelp, so the far half's
    // fountains pull (Policy: wMirrorHot) and paths through unseen land know its walls.
    int symAlive = 7;                 // bit s-1: candidate s not ruled out (by us or a teammate)
    int symKelp[4] = {0, 0, 0, 0};    // kelp edges matched per candidate (debug statistics)
    int symPairs[4] = {0, 0, 0, 0};   // tile pairs matched per candidate (debug statistics)
    std::vector<int8_t> mirrored;     // never-seen tile whose bed / gap / edges came from its mirror

    // Heat map (BC_HEAT): a fuzzy picture of where our dragons are and where the enemy's are, to plan with. Coarse 5
    // x 4 sectors; heads we see or hear of (SELF / ENEMY facts) warm their sector, teammates' HEAT facts keep far
    // sectors warm, everything cools by kHeatDecay a round. friendHeat leaves ourselves out (it is "covered by the
    // others").
    static constexpr int kHeatX = 5, kHeatY = 4, kHeat = kHeatX * kHeatY;
    static constexpr double kHeatDecay = 0.85;   // per round: about 4 rounds half-life
    static constexpr double kHeatOn = 0.5;       // a sector this warm counts as held
    double friendHeat[kHeat] = {}, enemyHeat[kHeat] = {};
    int heatRound = -1;
    int heatOf(int c) const { return board.Y(c) * kHeatY / board.H * kHeatX + board.X(c) * kHeatX / board.W; }
    void warm(double* heat, int c, double w) { heat[heatOf(c)] += w; }

    // Scavenger: after a head-on trade nobody went for the dead dragons' pearls (in replay reviews the enemy ate
    // about half of the drops of head-on trades, and when no other head of ours was within 5 we got about a quarter).
    // Drops we see appear (a pearl where a dragon was last round, or off any bed) or hear of (DROPS facts) are kept a
    // while; Policy::planScavengers sends the nearest free dragons.
    struct Drop { int cell, count, round; };
    static constexpr int kDropAge = 15;           // rounds a drop cluster stays worth a trip
    std::vector<Drop> drops;
    int scavCell = -1;                            // the cluster (or stand-by site) I am on, for hysteresis
    // King tracking (BC_KING): the longest enemy seen or heard of in the last 100 rounds (visible segments only, so a
    // lower bound for long ones)
    int enemyLongest = 0, enemyLongestRound = -1000;
    // Dragon plans (BC_SLAY): the longest enemy dragon known, seen or heard of (ENEMY / EKING facts); visible
    // length only, so a lower bound. Forgotten after kKingAge rounds without news.
    static constexpr int kKingAge = 30;
    comms::Knowledge::Entry enemyKing{-1, 0, -1, -1};
    void noteEnemyKing(int cell, int len, int id, int round) {
        if (round > t.round || t.round - round > kKingAge) return;
        bool stale = enemyKing.cell < 0 || t.round - enemyKing.round > kKingAge;
        if (stale || len > enemyKing.len || (id == enemyKing.id && round >= enemyKing.round))
            enemyKing = {cell, std::max(len, id == enemyKing.id ? enemyKing.len : 0), id, round};
    }
    void noteEnemyLength(int len, int round) {
        if (len >= enemyLongest || round - enemyLongestRound > 100) enemyLongest = len, enemyLongestRound = round;
    }
    void noteDrop(int c, int count, int round) {
#if BC_MN_EXPLORE
        mn.init(board.NC);
        mn.noteDanger(c, round);  // BC_MAPNET: a death seen here
#endif
        for (Drop& d : drops)
            if (board.cheb(d.cell, c) <= 2) {
                if (round >= d.round) d.count = round == d.round ? d.count + count : count, d.round = round;
                return;
            }
        if (drops.size() < 16) drops.push_back({c, count, round});
    }

    // Starting dragons get the first ids (both teams). Round 0 tells us how many there are;
    // a split child born later cannot know, and assumes 2 per team.
    int initialCount() const { return startUnits > 0 ? 2 * startUnits : 4; }
    bool isInitial(int id) const { return id < initialCount(); }

    bool visible(int c) const { return visRound[c] == t.round; }

    bool bodyKnown() const { return static_cast<int>(body.size()) == t.length; }
    char enemy() const { return init.team == 'A' ? 'B' : 'A'; }
    // Rules 1.2.3: the queens are ids 0 and 1, one per team, but not by team letter (ids follow the map's DRAGON
    // lines, which alternate teams, and ladder games swap the sides). Ours: me if my id < 2; for a dragon alive at
    // round 0 the parity of its id (starters alternate teams); a child learns it by sight (a teammate with id < 2) or
    // from a teammate's SELF / LONGEST fact naming an id < 2 (facts decode only for our team). -1 until known.
    int ourQueen = -1;
    int queenId() const { return ourQueen; }
    // BC_QUEEN_HUNT / BC_QUEEN_MODES: the enemy queen (an enemy with id < 2) as last seen or heard, and the witnessed
    // deaths of both queens (seen within Chebyshev 2 last round, no part of her in view now; or a teammate's tomb
    // fact)
    struct EQ { int cell = -1, len = 0, id = -1, round = -1000; bool dead = false; int deadRound = -1000; } eq;
    bool ourQueenDead = false;
    int ourQueenDeadRound = -1000;
    int eqPrevHead = -1, eqPrevId = -1, eqPrevRound = -1000, oqPrevHead = -1, oqPrevId = -1, oqPrevRound = -1000;
    void noteEQ(int cell, int len, int id, int round) {
        if (eq.dead || round < eq.round) return;
        eq.cell = cell, eq.len = std::max(len, id == eq.id ? std::min(eq.len, 255) : 0), eq.id = id, eq.round = round;
    }
    void markEQDead(int round) {
        if (!eq.dead) eq.dead = true, eq.deadRound = round;
    }
    void markOurQueenDead(int round) {
        if (!ourQueenDead && init.id >= 2) ourQueenDead = true, ourQueenDeadRound = round;
    }
    // Witnessed deaths and sightings, once a turn after vision is read.
    void watchQueens() {
        int eh = -1, el = 0, ei = -1, oh = -1, oi = -1;
        for (Seen const& s : others) {
            if (s.id >= 2) continue;
            if (s.team != init.team) eh = s.head, el = s.visible, ei = s.id;
            else oh = s.head, oi = s.id;
        }
        auto anyPart = [&](int id) {
            for (Part const& p : t.parts)
                if (p.id == id) return true;
            return false;
        };
        if (eh >= 0) noteEQ(eh, el, ei, t.round);
        else if (eqPrevRound == t.round - 1 && eqPrevHead >= 0 && board.cheb(eqPrevHead, head) <= 2 && visible(eqPrevHead) &&
                 !anyPart(eqPrevId))
            markEQDead(t.round);
        if (oh < 0 && oqPrevRound == t.round - 1 && oqPrevHead >= 0 && board.cheb(oqPrevHead, head) <= 2 &&
            visible(oqPrevHead) && !anyPart(oqPrevId))
            markOurQueenDead(t.round);
        eqPrevHead = eh, eqPrevId = ei, eqPrevRound = t.round;
        oqPrevHead = oh, oqPrevId = oi, oqPrevRound = t.round;
    }
    void noteQueen(int id) {
        if (ourQueen >= 0 || id < 0 || id > 1) return;
        ourQueen = id;
        if (BC_QUEEN && BC_QUEEN_FEED) know.queenId = id;  // BC_QUEEN_FEED: LONGEST facts name the queen
    }

#if BC_LONGEST_RESERVE
    // BC_LONGEST_RESERVE: from kReserveFrom the team's longest non-queen dragon is the reserve - its own length
    // against the LONGEST facts (every dragon sends SELF with its length each turn; fresh within kReserveAge), a tie
    // to the lower id; with no fresh fact, kReserveMinLen+
    bool longestReserve() const {
        if (t.round < kReserveFrom || (BC_QUEEN && init.id >= 0 && init.id < 2)) return false;
        auto const& K = know.longest;
        int me = init.id & 4095;
        bool fresh = K.cell >= 0 && t.round - K.round <= kReserveAge;
        if (!fresh) return t.length >= kReserveMinLen;
        if (K.id == me) return true;
        return t.length > K.len || (t.length == K.len && me < K.id);
    }
#endif
    void start(Init const& in) {
        init = in;
        if (BC_QUEEN && in.id >= 0 && in.id < 2) noteQueen(in.id);
        board.init(in.w, in.h);
        next.assign(board.NC, -1);
        lastSpawn.assign(board.NC, -1);
        seenRound.assign(board.NC, -1);
        seenPearl.assign(board.NC, 0);
        visRound.assign(board.NC, -1);
        occSeen.assign(board.NC, -1);
        maxCd.assign(board.NC, 0);
        cdObs.assign(board.NC, 0);
        mirrored.assign(board.NC, 0);
        sectorSeen.assign(comms::kSectorsX * comms::kSectorsY, 0);
        sectorSize.assign(comms::kSectorsX * comms::kSectorsY, 0);
        for (int c = 0; c < board.NC; c++) sectorSize[sectorOf(c)]++;
    }

    int visTile(int col, int row) const { return board.id(board.X(head) + col - 3, board.Y(head) + row - 3); }

    // Map group for per-group Params: 1 fountain map, 0 pearl-poor, 2 the rest. Only as good
    // as what this dragon has seen; main.cpp re-picks the profile as knowledge grows.
    int mapGroup(Params const& p) const {
        if (hotBeds >= p.hotBedsForGroup) return 1;
        if (tilesSeen >= p.groupMinTiles && refillRate / tilesSeen < p.poorRate) return 0;
        return 2;
    }

    // ---- pearls ---------------------------------------------------------------

    void tickTo(int round) {
        while (simRound < round) {
            simRound++;
            for (int c = 0; c < board.NC; c++) {
                if (next[c] < 0) continue;
                if (--next[c] <= 0) {
                    lastSpawn[c] = simRound;
                    next[c] = -1;  // the next gap is unknown until we see the bed again
                }
            }
        }
    }

    // Probability a pearl is on tile c right now.
    double belief(int c, Params const& p) const {
        if (seenRound[c] == t.round) return seenPearl[c] ? 1.0 : 0.0;
        double b = 0;
        if (seenPearl[c]) b = std::pow(p.seenDecay, t.round - seenRound[c]);
        if (lastSpawn[c] > seenRound[c]) b = std::max(b, std::pow(p.spawnDecay, t.round - lastSpawn[c]));
        return b;
    }

    // ---- portal gate (BC_QGATE, BC_DEADPORTAL) ----------------------------------------------- A paired portal
    // whose landing is unsafe is a wall in this dragon's adjacency (nb only; the Edge, vision, mirror and PORTAL
    // facts are untouched). BC_DEADPORTAL (every dragon): the landing is known (seen or mirrored) and has no open
    // edge, a 1-tile room whose only side is the way in. BC_QGATE (the queen, ids 0 / 1): also a landing never seen
    // nor mirrored, one with < 2 open edges, a dead end, or a small closed pocket (a queen was seen dying in a 4-tile
    // room behind a portal). deadEnd keeps the ungated rebuild's values (no self-made dead ends).
    int openEdges(int n) const {
        int k = 0;
        for (int d = 0; d < 4; d++)
            if (board.side(n, d).kind == 0) k++;
        return k;
    }
#if BC_QGATE || BC_DEADPORTAL  // with both gates 0 none of this exists (World's layout and the wasm are unchanged)
    // The pocket behind landing n: the tiles reached over open edges (no portal crossing). Small and closed when every tile is
    // known (seen or mirrored), no tile has an unpaired portal and it has fewer than `limit` tiles.
    bool smallPocket(int n, int limit) {
        pocketMark_.resize(board.NC, 0);
        if (++pocketStamp_ == 0) std::fill(pocketMark_.begin(), pocketMark_.end(), 0), pocketStamp_ = 1;
        pocketQueue_.clear();
        pocketQueue_.push_back(n);
        pocketMark_[n] = pocketStamp_;
        for (size_t i = 0; i < pocketQueue_.size(); i++) {
            int c = pocketQueue_[i];
            if (seenRound[c] < 0 && !mirrored[c]) return false;
            for (int d = 0; d < 4; d++) {
                Edge const& e = board.side(c, d);
                if (e.kind == 2 && e.partnerOrient < 0) return false;
                if (e.kind != 0) continue;
                int m = board.id(board.X(c) + DX[d], board.Y(c) + DY[d]);
                if (pocketMark_[m] == pocketStamp_) continue;
                pocketMark_[m] = pocketStamp_;
                pocketQueue_.push_back(m);
                if (static_cast<int>(pocketQueue_.size()) >= limit) return false;
            }
        }
        return true;
    }
    std::vector<uint32_t> pocketMark_;
    std::vector<int> pocketQueue_;
    uint32_t pocketStamp_ = 0;
    int gateLen_ = -1;          // the queen's length at her last gate (the pocket limit grows with her)

    void gatePortals(bool deadOnly) {
        int limit = std::max(6, t.length + 2);
        if (!deadOnly) gateLen_ = t.length;
        for (int c = 0; c < board.NC; c++) {
            if (!board.portalSide[c]) continue;
            for (int d = 0; d < 4; d++) {
                if (board.side(c, d).kind != 2) continue;
                int n = board.stepRaw(c, d);        // idempotent: a landing seen since the last rebuild opens again
                if (n < 0) continue;                // unpaired: rebuild already walls it
                bool known = seenRound[n] >= 0 || mirrored[n];
                bool dead = known && openEdges(n) == 0;
                bool unsafe = dead || (!deadOnly && (!known || openEdges(n) < 2 || (BC_DEADEND && board.deadEnd[n]) ||
                                                     smallPocket(n, limit)));
#if BC_EXPAND_OPENING && BC_EO_QUEEN_MOTHER
                if (!deadOnly && motherGate) unsafe = dead || !known || openEdges(n) < 2;  // BC_EO_QUEEN_MOTHER: dead ends and small pockets
                                                                                           // open
#endif
#if BC_MAPNET
                if (dead) setGate(c, d, comms::G_DEAD, t.round);  // BC_MAPNET: seen (or mirrored) to be a 1-tile room:
                                                                  // tell the team
                int gs = mn.portalState(c, d);
                if (gs == comms::G_DEAD || gs == comms::G_TRAP) unsafe = true;  // BC_MAPNET: a known trap is a wall for all
#if BC_MN_FACTS
                if (trapStep(c, d)) unsafe = true, gs = comms::G_TRAP;            // ... and so is a portal id in the trap set
#endif
#if BC_MN_REGION
                // BC_MN_REGION: the queen may enter a closed room with two or more beds that she fits in (e.g. 3-bed
                // rooms)
                if (!deadOnly && unsafe && !dead && known && gs != comms::G_DEAD && gs != comms::G_TRAP) {
                    Pocket pk = pocket(n, std::max(8, 2 * t.length + 4));
                    if (pk.closed && pk.beds >= 2 && pk.size >= t.length + 1 && openEdges(n) >= 1) unsafe = false;
                }
#endif
#endif
                board.nb[c * 4 + d] = unsafe ? -1 : n;
            }
        }
    }
#endif
    void applyGate() {
#if BC_QGATE || BC_DEADPORTAL
        if (BC_QGATE && init.id >= 0 && init.id < 2) gatePortals(false);
        else if (BC_DEADPORTAL) gatePortals(true);
#endif
    }
    void rebuildGated() {
        board.rebuild();
        applyGate();
    }

#if BC_MAPNET
    // ---- shared map network (BC_MAPNET, region.hpp) ----
    RegionMap mn;
    int probeFrom = -1, probeDir = -1, probeRound = -1;           // our step into an unpaired portal (main.cpp)
    int announceCell = -1, announceDir = -1, announceRound = -1;  // a probe we are about to make (policy) or making
    int announceState = 0;   // comms::G_CLAIM two steps out (policy), comms::G_PROBING on the crossing turn (main.cpp)
    comms::Fact clearedFact_, regionFact_;
    int clearedRound_ = -1, regionRound_ = -1;
    std::vector<int> regionsSent_;
#if BC_MN_VALUE
    // BC_MN_VALUE: the bed areas we know (our own sightings and teammates' VALUE facts), one a heat sector
    struct ValueRoom { int anchor, beds, exits, round; bool held; };
    std::vector<ValueRoom> valueRooms;
    comms::Fact valueFact_;
    int valueRound_ = -1, valueScanRound_ = -1000;
    int valueLastTarget = -1;  // the area Policy::planValue pulled us to last turn (its arrival check)
#if BC_ECON_TRACE
    int valueHeardN = 0;             // trace: VALUE facts heard since the last count
    mutable int valueRelayedN = 0;   // ... relayed
#endif
    // keyed by the anchor's heat sector (one entry a sector: anchors may differ between dragons' views)
    ValueRoom const* valueRoom(int anchor) const {
        for (ValueRoom const& v : valueRooms)
            if (heatOf(v.anchor) == heatOf(anchor)) return &v;
        return nullptr;
    }
    // a newer word on a sector replaces the older (anchor too); the table keeps the freshest kValueMaxRooms
    void noteValue(int anchor, int beds, int exits, bool held, int round) {
        for (ValueRoom& v : valueRooms)
            if (heatOf(v.anchor) == heatOf(anchor)) {
                if (round > v.round) v.anchor = anchor, v.beds = beds, v.exits = exits, v.held = held, v.round = round;
                return;
            }
        if (valueRooms.size() >= kValueMaxRooms) {
            auto old = std::min_element(valueRooms.begin(), valueRooms.end(), [](ValueRoom const& a, ValueRoom const& b) { return a.round < b.round; });
            if (old->round >= round) return;
            valueRooms.erase(old);
        }
        valueRooms.push_back({anchor, beds, exits, round, held});
    }
#endif
    uint64_t trapMaskHeard_ = 0, trapMaskNow_ = 0;  // TRAPSET: portal ids heard / heard + our own DEAD and TRAP gates
    int maskChangedRound_ = -1;

    static int mirrorDir(int d, int s) {
        if ((s == 1 || s == 3) && (d == N || d == S)) d ^= 2;
        if ((s == 2 || s == 3) && (d == E || d == W)) d ^= 2;
        return d;
    }
    int portalState(int c, int d) const { return mn.portalState(c, d); }
    // The portal id of the step from c towards d (portalEnds: ends seen or heard), -1 when unknown.
    int pidOfStep(int c, int d) const {
        int x = board.X(c), y = board.Y(c);
        int ex = d == E ? (x + 1) % board.W : x, ey = d == S ? (y + 1) % board.H : y, orient = d == N || d == S ? 0 : 1;
        for (auto const& pe : portalEnds)
            for (PortalEnd const& e : pe.second)
                if (e.orient == orient && e.x == ex && e.y == ey) return pe.first;
        return -1;
    }
    bool trapStep(int c, int d) const {
        if (!BC_MN_TRAPSET || !trapMaskNow_) return false;
        int pid = pidOfStep(c, d);
        return pid >= 0 && pid < comms::kTrapPids && (trapMaskNow_ >> pid & 1);
    }
    // TRAPSET: what we heard plus our own seen-DEAD gates (their mirrors are in the gate table too). Inferred TRAPs
    // stay per gate and expire (RegionMap::kTrapAge): with them in the mask a test lost most games (both ends of a
    // falsely trapped portal walled)
    void refreshTrapMask() {
        uint64_t m = trapMaskHeard_;
        for (int k : mn.gateList)
            if (mn.gate[k] == comms::G_DEAD) {  // seen only: an inferred TRAP may be a good portal
                int pid = pidOfStep(k / 4, k & 3);
                if (pid >= 0 && pid < comms::kTrapPids) m |= 1ull << pid;
            }
        if (m != trapMaskNow_) {
            trapMaskNow_ = m;
            maskChangedRound_ = t.round;
            applyGate();
        }
    }
    double danger(int c) const { return mn.danger(c, t.round); }
    // A gate state for the step from c towards d, and its mirror image once the symmetry is known. True when one changed.
#if BC_MN_EVIDENCE
    bool setGate(int c, int d, int st, int round, int landing = -1, int prober = -1) {
        mn.init(board.NC);
        bool ch = mn.setOne(c * 4 + d, st, round, landing, prober);
        if (board.sym > 0) {
            int m = board.mirror(c), md = mirrorDir(d, board.sym);
            if (m != c || md != d) ch |= mn.setOne(m * 4 + md, st, round, landing >= 0 ? board.mirror(landing) : -1, prober);
        }
        return ch;
    }
    // BC_MN_EVIDENCE: a timed-out PROBING of slot k earns its TRAP when its prober was neither heard nor seen after
    // the probe and its landing (when known) is in no known bed room - else the prober turned back or lived, or the
    // door is a food room's (in a test most inferred traps were false)
    bool trapEvidence(int k) {
        int const c = k / 4, d = k & 3, r0 = mn.gateRound[k], prober = mn.gateProber[k];
        for (auto const& f : know.friends)  // (no id, -1: no entry matches)
            if (f.id == prober && f.round > r0) return false;
        int const land = board.stepRaw(c, d);
        if (land >= 0 && (seenRound[land] >= 0 || mirrored[land]) && pocket(land, kEvidenceRoom).beds > 0) return false;
        return true;
    }
    std::vector<int> bedGates_;  // BC_MN_EVIDENCE: CLEARED slots whose landing is in a known bed room (refreshed
                                 // every kEvidenceEvery rounds)
    int bedGatesRound_ = -1000;
    mutable bool relayBedTurn_ = false;
#else
    bool setGate(int c, int d, int st, int round, int landing = -1) {
        mn.init(board.NC);
        bool ch = mn.setOne(c * 4 + d, st, round, landing);
        if (board.sym > 0) {
            int m = board.mirror(c), md = mirrorDir(d, board.sym);
            if (m != c || md != d) ch |= mn.setOne(m * 4 + md, st, round, landing >= 0 ? board.mirror(landing) : -1);
        }
        return ch;
    }
#endif
    // The edge stepped through from c towards d: a reference into hE / vE and its own coordinates.
    Edge& stepEdge(int c, int d, int& ex, int& ey) {
        int x = board.X(c), y = board.Y(c);
        ex = d == E ? (x + 1) % board.W : x;
        ey = d == S ? (y + 1) % board.H : y;
        return d == N || d == S ? board.hE[board.id(ex, ey)] : board.vE[board.id(ex, ey)];
    }
    // CLEARED: the step from c towards d lands on `landing`; pair the portal if we had not (its partner edge has the same
    // orientation and sits on the landing's far side, Board::stepRaw).
    bool pairByLanding(int c, int d, int landing) {
        int ex, ey;
        Edge& e = stepEdge(c, d, ex, ey);
        if (e.kind == 2 && e.partnerOrient >= 0) return false;
        int lx = board.X(landing), ly = board.Y(landing), px, py, orient;
        if (d == N || d == S) orient = 0, px = lx, py = d == S ? ly : (ly + 1) % board.H;
        else orient = 1, px = d == E ? lx : (lx + 1) % board.W, py = ly;
        Edge& q = orient == 0 ? board.hE[board.id(px, py)] : board.vE[board.id(px, py)];
        e.kind = 2, e.partnerOrient = static_cast<int8_t>(orient), e.px = static_cast<int16_t>(px), e.py = static_cast<int16_t>(py);
        q.kind = 2, q.partnerOrient = static_cast<int8_t>(orient), q.px = static_cast<int16_t>(ex), q.py = static_cast<int16_t>(ey);
        MN_TOUCH(board.id(ex, ey)), MN_TOUCH(board.id(px, py));
        return true;
    }
    // The closed pocket around tile n over known open edges: its size (capped), beds, paired portal exits, closed flag.
    struct Pocket { int size = 0, beds = 0, exits = 0, anchor = -1; bool closed = false; };
    Pocket pocket(int n, int cap) {
        Pocket r;
        pocketMark_.resize(board.NC, 0);
        if (++pocketStamp_ == 0) std::fill(pocketMark_.begin(), pocketMark_.end(), 0), pocketStamp_ = 1;
        pocketQueue_.clear();
        pocketQueue_.push_back(n);
        pocketMark_[n] = pocketStamp_;
        r.closed = true;
        r.anchor = n;
        for (size_t i = 0; i < pocketQueue_.size(); i++) {
            int c = pocketQueue_[i];
            r.anchor = std::min(r.anchor, c);
            if (seenRound[c] < 0 && !mirrored[c]) r.closed = false;
            if (board.bed[c] == 1) r.beds++;
            for (int d = 0; d < 4; d++) {
                Edge const& e = board.side(c, d);
                if (e.kind == 2) {
                    if (e.partnerOrient < 0) r.closed = false;
                    else r.exits++;
                }
                if (e.kind != 0) continue;
                int m = board.id(board.X(c) + DX[d], board.Y(c) + DY[d]);
                if (pocketMark_[m] == pocketStamp_) continue;
                pocketMark_[m] = pocketStamp_;
                pocketQueue_.push_back(m);
                if (static_cast<int>(pocketQueue_.size()) >= cap) {
                    r.size = cap;
                    r.closed = false;
                    return r;
                }
            }
        }
        r.size = static_cast<int>(pocketQueue_.size());
        return r;
    }

    // Once a turn, after the view: timeouts, our own landing through an unpaired portal, a closed room seen whole.
    void observeMapnet() {
        mn.init(board.NC);
#if BC_MN_EVIDENCE
        if (mn.tick(t.round, [&](int k) { return trapEvidence(k); })) applyGate();
        if (t.round - bedGatesRound_ >= kEvidenceEvery) {
            bedGatesRound_ = t.round;
            bedGates_.clear();
            for (int k : mn.gateList)
                if (mn.gate[k] == comms::G_CLEARED && mn.gateLanding[k] >= 0 && pocket(mn.gateLanding[k], kEvidenceRoom).beds > 0)
                    bedGates_.push_back(k);
        }
#else
        if (mn.tick(t.round)) applyGate();
#endif
#if BC_MN_TRAPSET
        refreshTrapMask();
#endif
        if (head < 0 || head >= board.NC) return;
#if BC_MN_FACTS
        if (crossedPortal && probeFrom >= 0 && probeRound == t.round - 1) {
            // we landed alive: a 1-tile room or a 2-tile dead end would kill us this turn, and a dragon that dies in
            // its own turn loses its rays, so CLEARED goes out only from a landing that lets us live
            Pocket pk = pocket(head, 8);
            bool safe = openEdges(head) >= 1 && !(pk.closed && pk.size < 3);
            if (safe) {
                if (pairByLanding(probeFrom, probeDir, head)) rebuildGated();
                setGate(probeFrom, probeDir, comms::G_CLEARED, t.round, head);
                clearedFact_ = comms::Fact{};
                clearedFact_.type = comms::GATE;
                clearedFact_.x = board.X(probeFrom);
                clearedFact_.y = board.Y(probeFrom);
                clearedFact_.val = probeDir | comms::G_CLEARED << 2;
                clearedFact_.aux = board.X(head) | board.Y(head) << 6;
                clearedFact_.round = t.round;
                clearedRound_ = t.round;
            }
            probeFrom = -1;
        }
        // a closed room seen whole for the first time: tell the team once (size class, beds, exits, status)
        if (regionRound_ < t.round - 3) {
            Pocket pk = pocket(head, 33);
            if (pk.closed && pk.size <= 32 && std::find(regionsSent_.begin(), regionsSent_.end(), pk.anchor) == regionsSent_.end()) {
                regionsSent_.push_back(pk.anchor);
                bool dangerous = false;
                for (int c : pocketQueue_) dangerous |= danger(c) > 0.5;
                int sizeClass = pk.size <= 1 ? 0 : pk.size <= 3 ? 1 : pk.size <= 6 ? 2 : pk.size <= 10 ? 3 : pk.size <= 16 ? 4 : 5;
                int status = dangerous ? 3 : pk.beds > 0 ? 1 : 0;
                regionFact_ = comms::Fact{};
                regionFact_.type = comms::REGION;
                regionFact_.x = board.X(pk.anchor);
                regionFact_.y = board.Y(pk.anchor);
                regionFact_.val = sizeClass | status << 3;
                regionFact_.aux = std::min(pk.beds, 15) | std::min(pk.exits, 7) << 4;
                regionFact_.round = t.round;
                regionRound_ = t.round;
            }
        }
#if BC_MN_VALUE
        // BC_MN_VALUE: every kValueEvery rounds, the heat sectors with kValueMinBeds+ known beds (seen or mirrored;
        // the open maps' bed areas are no closed rooms); the first one new to us goes out, anchored at its known bed
        // nearest the sector's centre
        if (t.round - valueScanRound_ >= kValueEvery) {
            valueScanRound_ = t.round;
            int beds[kHeat] = {}, anchor[kHeat], best[kHeat];
            for (int s = 0; s < kHeat; s++) anchor[s] = -1, best[s] = 1 << 30;
            for (int c = 0; c < board.NC; c++) {
                if (board.bed[c] != 1 || (seenRound[c] < 0 && !mirrored[c])) continue;
                int const s = heatOf(c);
                beds[s]++;
                int const cx = (s % kHeatX * 2 + 1) * board.W / (2 * kHeatX), cy = (s / kHeatX * 2 + 1) * board.H / (2 * kHeatY);
                int const d = board.cheb(c, board.id(cx, cy));
                if (d < best[s] || (d == best[s] && c < anchor[s])) best[s] = d, anchor[s] = c;
            }
            for (int s = 0; s < kHeat; s++) {
                if (beds[s] < kValueMinBeds || valueRoom(anchor[s])) continue;
                bool held = heatOf(head) == s;
                for (Seen const& o : others) held |= o.team == init.team && heatOf(o.head) == s;
                noteValue(anchor[s], std::min(beds[s], 15), 0, held, t.round);
                valueFact_ = comms::Fact{};
                valueFact_.type = comms::REGION;
                valueFact_.x = board.X(anchor[s]);
                valueFact_.y = board.Y(anchor[s]);
                valueFact_.val = 5 | 1 << 3;  // the largest size class
                valueFact_.aux = std::min(beds[s], 15) | (held ? 1 : 0) << 7 | 1 << 11;
                valueFact_.round = t.round;
                valueRound_ = t.round;
                break;
            }
        }
#endif
#endif
    }

#if BC_MN_FACTS
    // A teammate's GATE / REGION fact (World::hear).
    void hearMapnet(comms::Fact const& f, int c) {
        mn.init(board.NC);
        if (f.type == comms::GATE) {
            int d = f.val & 3, st = f.val >> 2;
            if (st < comms::G_PROBING || st > comms::G_CLAIM) return;
            int landing = -1;
            if (st == comms::G_CLEARED) {
                int lx = f.aux & 63, ly = f.aux >> 6 & 63;
                if (lx >= board.W || ly >= board.H) return;
                landing = board.id(lx, ly);
                if (pairByLanding(c, d, landing)) rebuildGated();
            }
#if BC_MN_EVIDENCE
            if (setGate(c, d, st, f.round, landing, st == comms::G_PROBING ? f.aux : -1) && st >= comms::G_DEAD) applyGate();
#else
            if (setGate(c, d, st, f.round, landing) && st >= comms::G_DEAD) applyGate();
#endif
        } else if (f.type == comms::REGION) {
#if BC_MN_VALUE
            if (f.aux >> 11 & 1) {  // BC_MN_VALUE: a VALUE fact (a REGION flagged in aux bit 11)
                noteValue(c, f.aux & 15, f.aux >> 4 & 7, (f.aux >> 7 & 1) != 0, f.round);
#if BC_ECON_TRACE
                valueHeardN++;
#endif
                return;
            }
#endif
            if ((f.val >> 3) == 3) mn.noteDanger(c, f.round);  // a dangerous room
        } else if (BC_MN_TRAPSET && f.type == comms::TRAPSET) {
            if (f.bits & ~trapMaskHeard_) {
                trapMaskHeard_ |= f.bits;
                refreshTrapMask();
            }
        }
    }

    // Facts first in line this turn (World::tell): a probe we are about to make, our safe landing, a room seen whole.
    void tellMapnetUrgent(std::vector<comms::Fact>& out) const {
        if (announceRound == t.round && announceCell >= 0 && out.size() < 4) {
            comms::Fact f;
            f.type = comms::GATE;
            f.x = board.X(announceCell);
            f.y = board.Y(announceCell);
            f.val = announceDir | announceState << 2;
#if BC_MN_EVIDENCE
            if (announceState == comms::G_PROBING) f.aux = init.id & 4095;  // BC_MN_EVIDENCE: the prober, for the teammates'
                                                                            // silence check
#endif
            f.round = t.round;
            out.push_back(f);
        }
        if (clearedRound_ == t.round && out.size() < 4) out.push_back(clearedFact_);
        if (regionRound_ == t.round && out.size() < 4) out.push_back(regionFact_);
#if BC_MN_VALUE
        if (valueRound_ == t.round && out.size() < 4) out.push_back(valueFact_);
#endif
        if (BC_MN_TRAPSET && trapMaskNow_ && out.size() < 4) {
            comms::Fact f;
            f.type = comms::TRAPSET;
            f.bits = trapMaskNow_;
            f.round = t.round;
            out.push_back(f);
        }
    }
#if BC_MN_VALUE
    // BC_MN_VALUE: the relay slot on alternate turns: the bed rooms we know (fresh within kValueAge), one a turn,
    // round-robin
    bool tellValueRelay(std::vector<comms::Fact>& out) const {
        size_t n = valueRooms.size(), start = static_cast<size_t>(t.round / 2 + (init.id & 4095));
        for (size_t tries = 0; tries < n; tries++) {
            ValueRoom const& v = valueRooms[(start + tries) % n];
            if (t.round - v.round > kValueAge) continue;
            comms::Fact f;
            f.type = comms::REGION;
            f.x = board.X(v.anchor);
            f.y = board.Y(v.anchor);
            f.val = 1 << 3;
            f.aux = v.beds | v.exits << 4 | (v.held ? 1 : 0) << 7 | 1 << 11;
            f.round = v.round;
            out.push_back(f);
#if BC_ECON_TRACE
            valueRelayedN++;
#endif
            return true;
        }
        return false;
    }
#endif
    // The relay slot of the rotation: the gate states we know, one a turn, round-robin (no CLAIM / PROBING: short-lived).
    bool tellMapnetRelay(std::vector<comms::Fact>& out) const {
        size_t n = mn.gateList.size(), start = static_cast<size_t>(t.round + (init.id & 4095));
#if BC_MN_EVIDENCE
        // BC_MN_EVIDENCE: every other relay turn a CLEARED gate into a bed room first (the food doors), round-robin
        // among them
        relayBedTurn_ = !relayBedTurn_;
        size_t const nb = bedGates_.size();
        for (size_t tries = 0; relayBedTurn_ && tries < nb; tries++) {
            int k = bedGates_[(start + tries) % nb];
            if (mn.gate[k] != comms::G_CLEARED) continue;
            comms::Fact f;
            f.type = comms::GATE;
            f.x = board.X(k / 4);
            f.y = board.Y(k / 4);
            f.val = (k & 3) | comms::G_CLEARED << 2;
            int L = mn.gateLanding[k];
            f.aux = L >= 0 ? board.X(L) | board.Y(L) << 6 : 0;
            f.round = mn.gateRound[k];
            out.push_back(f);
            return true;
        }
#endif
        for (size_t tries = 0; tries < n; tries++) {
            int k = mn.gateList[(start + tries) % n];
            int st = mn.gate[k];
            if (st == comms::G_PROBING || st == comms::G_CLAIM) continue;
            comms::Fact f;
            f.type = comms::GATE;
            f.x = board.X(k / 4);
            f.y = board.Y(k / 4);
            f.val = (k & 3) | st << 2;
            int L = mn.gateLanding[k];
            f.aux = L >= 0 ? board.X(L) | board.Y(L) << 6 : 0;
            f.round = mn.gateRound[k];
            out.push_back(f);
            return true;
        }
        return false;
    }
#endif
#endif

    // ---- per turn --------------------------------------------------------------

    void observe(Turn const& turn, Params const& p) {
        t = turn;
        if (firstRound < 0) firstRound = lastFriendRound = lastEatRound = t.round;
        if (BC_QUEEN && t.round == 0) noteQueen(init.id & 1);  // a starter: the queen of my team shares my id's parity
        if (t.round == 0 && startUnits < 0) startUnits = t.units;
        // own head from the parts list (segments of one dragon come head first)
        std::vector<int> visibleOwn;
        for (Part const& part : t.parts)
            if (part.id == init.id) visibleOwn.push_back(board.id(part.x, part.y));
        head = visibleOwn.empty() ? board.id(t.tiles[24].x, t.tiles[24].y) : visibleOwn.front();

        tickTo(t.round);

        // learn edges; portal ends are remembered by id and paired once both are seen
        bool changed = false;
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 7; c++) {
                int cell = visTile(c, r);
                Edge& e = board.hE[cell];
                if (e.kind != t.hRows[r][c].kind) e = Edge{t.hRows[r][c].kind}, changed = true;
                if (e.kind == 2) changed |= notePortal(t.hRows[r][c].pid, 0, board.X(cell), board.Y(cell));
            }
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 8; c++) {
                int cell = visTile(c, r);
                Edge& e = board.vE[cell];
                if (e.kind != t.vRows[r][c].kind) e = Edge{t.vRows[r][c].kind}, changed = true;
                if (e.kind == 2) changed |= notePortal(t.vRows[r][c].pid, 1, board.X(cell), board.Y(cell));
            }
        if (changed) rebuildGated();

        std::vector<int> fresh;           // tiles in view for the first time
        for (VisTile const& v : t.tiles) {
            int c = board.id(v.x, v.y);
            if (seenRound[c] < 0) {
                tilesSeen++;
                int sec = sectorOf(c);
                if (++sectorSeen[sec] * 2 >= sectorSize[sec]) teamSectors |= 1ull << sec;
                fresh.push_back(c);
                mirrored[c] = 0;              // our own eyes from now on
            }
            if (BC_SCAV && v.pearl && seenRound[c] == t.round - 1 && !seenPearl[c] &&
                (occSeen[c] == t.round - 1 || board.bed[c] == 0))
                noteDrop(c, 1, t.round);
            seenRound[c] = t.round;
            visRound[c] = t.round;
            seenPearl[c] = v.pearl;
            if (v.pearlIn >= 0) {
                if (board.bed[c] != 1) {
                    board.bed[c] = 1;
                    bedsSeen++;
                }
                next[c] = v.pearlIn;
                learnGap(c, v.pearlIn, p);
            } else {
                board.bed[c] = 0;
            }
        }
        if (BC_SCAV) {
            // a cluster is done when it is old, or in view with no pearl left around it
            drops.erase(std::remove_if(drops.begin(), drops.end(), [&](Drop const& d) {
                if (t.round - d.round > kDropAge) return true;
                if (!visible(d.cell)) return false;
                for (int dy = -2; dy <= 2; dy++)
                    for (int dx = -2; dx <= 2; dx++) {
                        int n = board.id(board.X(d.cell) + dx, board.Y(d.cell) + dy);
                        if (!visible(n) || seenPearl[n]) return false;
                    }
                return true;
            }), drops.end());
        }
#if BC_QGATE || BC_DEADPORTAL
        for (int c : fresh)
            if (board.portalSide[c]) { applyGate(); break; }
        if (BC_QGATE && init.id >= 0 && init.id < 2 && t.length != gateLen_) applyGate();  // her pocket limit moved
#endif
        if (BC_MIRROR && board.sym < 0) inferSymmetry(fresh, p);
        if (BC_MIRROR && board.sym > 0) mirrorView(p);
#if BC_MN_ROOMS
        {   // BC_MN_ROOMS: the rooms of the known map (region.hpp); the BFS runs only when the known tiles or the
            // edges changed
#if BC_V11_TRACE
            double r0 = clockNow();
#endif
            for (int r = 0; r < 8; r++)   // this turn's view: its tiles (known, bed) and edge records (8 x 8 covers both)
                for (int c = 0; c < 8; c++) MN_TOUCH(visTile(c, r));
            mn.init(board.NC);
            bool re = mn.recompute(board, seenRound, mirrored);
#if BC_V11_TRACE
            mnRoomsCost = static_cast<int>((clockNow() - r0) * 1e9), mn.statRe = re;
            mnMiss = mn.audit(board, seenRound, mirrored);
#endif
            (void)re;
        }
#endif

        // own body: the contiguous visible prefix is authoritative, older tracking fills
        // the rest. A body that leaves vision and comes back shows up non-contiguous.
        size_t k = visibleOwn.empty() ? 0 : 1;
        while (k < visibleOwn.size() && adjacent(visibleOwn[k - 1], visibleOwn[k])) k++;
        ownExtra.assign(visibleOwn.begin() + k, visibleOwn.end());
        visibleOwn.resize(k);
        std::vector<int> merged = visibleOwn;
        if (!body.empty() && !visibleOwn.empty()) {
            // body was advanced by our last move; keep the tracked part beyond the visible prefix. A step through an
            // unpaired portal (a probe, a doomed dragon's way out) lands on a tile we did not know, so the body was
            // not advanced: our old head is the neck. Without this the merge dropped it and the lookahead took the
            // way back through the portal as free (in testing every prober died of hitSelf the turn after crossing).
            size_t from = unknownStep ? visibleOwn.size() - 1 : visibleOwn.size();
            if (unknownStep) crossedPortal = true;
            for (size_t i = from; i < body.size(); i++) merged.push_back(body[i]);
        }
        unknownStep = false;
        // The neck is always one step back along DIR (portals are symmetric), even when
        // it is out of vision because the body runs through a portal.
        if (merged.size() == 1 && t.length >= 2) {
            int neck = board.nb[head * 4 + (t.dir ^ 2)];
            if (neck >= 0) merged.push_back(neck);
        }
        if (static_cast<int>(merged.size()) > t.length) merged.resize(t.length);
        body.swap(merged);

        occ.assign(board.NC, -1);
        occHeadId.assign(board.NC, -1);
        others.clear();
        for (Part const& part : t.parts) {
            if (part.id == init.id) continue;
            if (BC_QUEEN && part.team == init.team && part.id < 2) noteQueen(part.id);  // our queen in view
            int c = board.id(part.x, part.y);
            occ[c] = part.id;
            occSeen[c] = t.round;
            if (part.head) {
                occHeadId[c] = part.id;
                others.push_back({part.id, part.team, c, part.facing, 0});
            }
        }
        for (Seen& s : others) {
            for (Part const& part : t.parts)
                if (part.id == s.id) s.visible++;
            if (s.team == init.team) lastFriendRound = t.round;
            else if (BC_KING) noteEnemyLength(s.visible, t.round);
            if (BC_SLAY && s.team != init.team) noteEnemyKing(s.head, s.visible, s.id & 4095, t.round);
        }
        if (BC_QUEEN && (BC_QUEEN_HUNT || BC_QUEEN_MODES)) watchQueens();
        if (BC_MIRROR && t.round == 0 && firstRound == 0 && bodyKnown() && board.sym < 0) startTest(p);
        if (BC_HEAT) {
            double f = heatRound < 0 ? 0.0 : std::pow(kHeatDecay, t.round - heatRound);
            for (int s = 0; s < kHeat; s++) friendHeat[s] *= f, enemyHeat[s] *= f;
            heatRound = t.round;
            for (Seen const& s : others) warm(s.team == init.team ? friendHeat : enemyHeat, s.head, 1.0);
        }
        hear(p);
    }

    // Sonar: read teammates' facts, add our own (so we relay them), drop stale ones.
    void hear(Params const& p) {
        using comms::Knowledge;
        comms::Fact f;
        for (uint64_t m : t.msgs) {
            if (!comms::decode(m, init.team, t.round, f)) continue;
            if (f.x >= board.W || f.y >= board.H || f.round > t.round) continue;
#if BC_QUEEN_GROWUP
            if (f.type == comms::SELF && f.aux < 2 && f.val >= kGrowAskVal) {  // BC_QUEEN_GROWUP: our queen asks (no queen is 128
                                                                               // long)
                f.val -= kGrowAskVal;
                qGrowHeard = std::max(qGrowHeard, f.round);
            }
#endif
            int c = board.id(f.x, f.y);
            Knowledge::Entry e{c, f.val, f.aux, f.round};
#if BC_MN_FACTS
            if (f.type == comms::GATE || f.type == comms::REGION || f.type == comms::TRAPSET) {
                hearMapnet(f, c);
                continue;
            }
#endif
            if (BC_QUEEN && (f.type == comms::SELF || f.type == comms::LONGEST) && f.aux < 2) {
                if (f.val == 0) {  // BC_QUEEN: a tomb of our queen (a teammate witnessed her death)
                    if (BC_QUEEN_MODES) markOurQueenDead(f.round);
                    continue;
                }
                noteQueen(f.aux);
            }
            if (BC_QUEEN && (BC_QUEEN_HUNT || BC_QUEEN_MODES) && f.type == comms::ENEMY && f.aux < 2) {
                if (f.val == 0) {  // their queen's tomb
                    markEQDead(f.round);
                    continue;
                }
                noteEQ(c, f.val, f.aux, f.round);  // their queen, seen by a teammate
            }
            if (f.type == comms::SELF) {
                if (f.aux != (init.id & 4095)) Knowledge::upsert(know.friends, e, 24);
                know.noteLongest(e);
            } else if (f.type == comms::ENEMY) {
                Knowledge::upsert(know.enemies, e, 24);
                if (BC_KING) noteEnemyLength(f.val, f.round);
                if (BC_SLAY) noteEnemyKing(c, f.val, f.aux, f.round);
            } else if (f.type == comms::LONGEST) {
                know.noteLongest(e);
            } else if (f.type == comms::EXPLORED) {
                teamSectors |= f.bits;
                continue;
            } else if (f.type == comms::TEAM) {
                Knowledge::upsert(know.teams, e, 16);
            } else if (f.type == comms::PORTAL && f.val <= 1) {
                Edge& pe = f.val == 0 ? board.hE[c] : board.vE[c];
                if (pe.kind != 2) pe = Edge{2};
                MN_TOUCH(c);
                if (notePortal(f.aux, f.val, f.x, f.y)) rebuildGated();
            } else if (f.type == comms::HOT && f.val >= 1 && f.val <= p.hotGap) {
                Knowledge::upsert(know.hot, {c, f.val, c, f.round}, 64);
                if (cdObs[c] < p.hotObs) {  // our own watching wins once we have it
                    board.bed[c] = 1;
                    MN_TOUCH(c);
                    board.gapMin[c] = board.gapMax[c] = static_cast<int16_t>(f.val);
                }
                if (board.sym > 0) {        // and its mirror image is a fountain too
                    int m = board.mirror(c);
                    if (seenRound[m] < 0 && cdObs[m] < p.hotObs) {
                        board.bed[m] = 1;
                        board.gapMin[m] = board.gapMax[m] = static_cast<int16_t>(f.val);
                        mirrored[m] = 1;
                        MN_TOUCH(m);
                    }
                }
            } else if (BC_MIRROR && f.type == comms::SYM && f.sub == 0 && f.val >= 1 && f.val <= 7) {
                symAlive &= f.val;          // a teammate's eliminations are as good as ours
                settleSymmetry(p);
            } else if (BC_SLAY && f.type == comms::SYM && f.sub == 3) {
                noteEnemyKing(c, f.val, f.aux, f.round);
            } else if (BC_SCAV && f.type == comms::SYM && f.sub == 2 && f.val >= 1 && t.round - f.round <= kDropAge) {
                noteDrop(c, f.val, f.round);
            } else if (BC_HEAT && f.type == comms::SYM && f.sub == 1) {
                for (int s = 0; s < kHeat; s++) {  // a teammate's view keeps far sectors warm
                    if (f.bits >> s & 1) friendHeat[s] = std::max(friendHeat[s], kHeatOn);
                    if (f.bits >> (s + 20) & 1) enemyHeat[s] = std::max(enemyHeat[s], kHeatOn);
                }
            }
            if (BC_HEAT && (f.type == comms::SELF || f.type == comms::ENEMY) && f.round >= t.round - 1 &&
                !(f.type == comms::SELF && f.aux == (init.id & 4095)) && !visible(c))
                warm(f.type == comms::SELF ? friendHeat : enemyHeat, c, 1.0);
        }
        int me = init.id & 4095;
        know.noteLongest({head, t.length, me, t.round});
        for (Seen const& s : others) {
            Knowledge::Entry e{s.head, s.visible, s.id & 4095, t.round};
            if (s.team == init.team) {
                Knowledge::upsert(know.friends, e, 24);
                know.noteLongest(e);
            } else {
                Knowledge::upsert(know.enemies, e, 24);
            }
        }
        for (VisTile const& v : t.tiles) {
            int c = board.id(v.x, v.y);
            if (v.pearlIn >= 0 && cdObs[c] >= p.hotObs && maxCd[c] <= p.hotGap)
                Knowledge::upsert(know.hot, {c, maxCd[c], c, t.round}, 64);
        }
        know.prune(t.round, std::max(p.remoteMaxAge, p.longestMaxAge),
                   BC_V9_KING ? p.kLongestAge : std::max(p.remoteMaxAge, p.longestMaxAge));
    }

    // Up to four facts for this turn's sonars: ourselves, the freshest enemy head, the
    // longest teammate we know of, and a fountain (round-robin).
    std::vector<comms::Fact> tell(Params const& p, int teamSize = 1) const {
        std::vector<comms::Fact> out;
        auto fact = [&](int type, int cell, int val, int aux, int round) {
            out.push_back({type, board.X(cell), board.Y(cell), val, aux & 4095, round});
        };
#if BC_QUEEN_GROWUP
        fact(comms::SELF, head, t.length + (qGrowAsk() ? kGrowAskVal : 0), init.id, t.round);
#else
        fact(comms::SELF, head, t.length, init.id, t.round);
#endif
#if BC_MN_FACTS
        tellMapnetUrgent(out);
#endif
        if (BC_QUEEN && (BC_QUEEN_HUNT || BC_QUEEN_MODES)) {
            // BC_QUEEN: the enemy queen (or her tomb) and our queen's tomb go out right after SELF
            if (eq.dead && t.round - eq.deadRound <= 40) fact(comms::ENEMY, eq.cell >= 0 ? eq.cell : head, 0, eq.id >= 0 ? eq.id : 0, eq.deadRound);
            else if (eq.cell >= 0 && t.round - eq.round <= 10) fact(comms::ENEMY, eq.cell, std::max(eq.len, 1), eq.id, eq.round);
            if (ourQueenDead && t.round - ourQueenDeadRound <= 40)
                fact(comms::LONGEST, head, 0, ourQueen >= 0 ? ourQueen : 0, ourQueenDeadRound);
        }
        if (teamSize >= 2) fact(comms::TEAM, head, teamSize, init.id, t.round);  // we lead a local team
        int best = -1;
        for (size_t i = 0; i < know.enemies.size(); i++)
            if (t.round - know.enemies[i].round <= p.remoteMaxAge &&
                (best < 0 || know.enemies[i].round > know.enemies[best].round ||
                 (know.enemies[i].round == know.enemies[best].round && know.enemies[i].len > know.enemies[best].len)))
                best = static_cast<int>(i);
        if (best >= 0) {
            auto const& e = know.enemies[best];
            fact(comms::ENEMY, e.cell, e.len, e.id, e.round);
        }
        // The other slots rotate between the longest teammate, a fountain and a portal end.
        std::vector<PortalEnd const*> ends;
        std::vector<int> pids;
        for (auto const& pe : portalEnds)
            for (PortalEnd const& e : pe.second) {
                ends.push_back(&e);
                pids.push_back(pe.first);
            }
        // BC_V9_EKING: in the endgame the longest teammate goes out first, every turn (feeders need the king)
        bool kingFirst = (BC_V9_KING && t.round >= p.kStreamFrom - 80) || (BC_V9_EKING && t.round >= p.ekFrom);
        if (kingFirst && out.size() < 4 && know.longest.cell >= 0 && know.longest.id != (init.id & 4095))
            fact(comms::LONGEST, know.longest.cell, know.longest.len, know.longest.id, know.longest.round);
#if BC_MN_FACTS
        int rota[9] = {0, 1, 2, 3, 0, 0, 0, 0, 0}, kinds = 4;
        rota[kinds++] = 8;               // BC_MAPNET: relay one known gate state
#else
        int rota[8] = {0, 1, 2, 3, 0, 0, 0, 0}, kinds = 4;
#endif
        if (BC_MIRROR) rota[kinds++] = 4;
        if (BC_HEAT) rota[kinds++] = 5;
        if (BC_SCAV && scavOn) rota[kinds++] = 6;
        if (BC_SLAY && t.round >= p.slayFrom - 50) rota[kinds++] = 7;  // BC_SLAY: no EKING facts before slay matters
        for (int k = 0; k < kinds && out.size() < 4; k++) {
            int kind = rota[(k + t.round) % kinds];
#if BC_MN_FACTS
            if (kind == 8) {
#if BC_MN_VALUE
                if ((t.round + (init.id & 4095)) % 2 == 0 && tellValueRelay(out)) continue;  // BC_MN_VALUE: the bed rooms on alternate turns
#endif
                tellMapnetRelay(out);
                continue;
            }
#endif
            if (kind == 4) {                  // the symmetry candidates we have not ruled out
                if (symAlive != 7 && symAlive != 0) out.push_back({comms::SYM, 0, 0, symAlive, 0, t.round});
                continue;
            }
            if (kind == 7) {                  // dragon plans: the longest enemy we know of
                if (enemyKing.cell >= 0 && t.round - enemyKing.round <= kKingAge) {
                    comms::Fact f;
                    f.type = comms::SYM;
                    f.sub = 3;
                    f.x = board.X(enemyKing.cell);
                    f.y = board.Y(enemyKing.cell);
                    f.val = enemyKing.len;
                    f.aux = enemyKing.id;
                    f.round = enemyKing.round;
                    out.push_back(f);
                }
                continue;
            }
            if (kind == 6) {                  // scavenger: the freshest drops we know of
                Drop const* best = nullptr;
                for (Drop const& d : drops)
                    if (t.round - d.round <= 3 && (!best || d.round > best->round)) best = &d;
                if (best) {
                    comms::Fact f;
                    f.type = comms::SYM;
                    f.sub = 2;
                    f.x = board.X(best->cell);
                    f.y = board.Y(best->cell);
                    f.val = best->count;
                    f.round = best->round;
                    out.push_back(f);
                }
                continue;
            }
            if (kind == 5) {                  // heat: where we and they are, as far as we know
                comms::Fact f;
                f.type = comms::SYM;
                f.sub = 1;
                f.bits = 1ull << heatOf(head);  // ourselves too: we are the others' friend
                for (int s = 0; s < kHeat; s++) {
                    if (friendHeat[s] >= kHeatOn) f.bits |= 1ull << s;
                    if (enemyHeat[s] >= kHeatOn) f.bits |= 1ull << (s + 20);
                }
                out.push_back(f);
                continue;
            }
            if (kind == 3) {
                if (teamSectors) {
                    comms::Fact f;
                    f.type = comms::EXPLORED;
                    f.bits = teamSectors;
                    out.push_back(f);
                }
                continue;
            }
            if (kind == 0 && kingFirst) continue;  // already sent
            if (kind == 0 && know.longest.cell >= 0 && know.longest.id != (init.id & 4095)) {
                fact(comms::LONGEST, know.longest.cell, know.longest.len, know.longest.id, know.longest.round);
            } else if (kind == 1 && !know.hot.empty()) {
                auto const& h = know.hot[static_cast<size_t>(t.round) % know.hot.size()];
                fact(comms::HOT, h.cell, h.len, 0, h.round);
            } else if (kind == 2 && !ends.empty()) {
                size_t i = static_cast<size_t>(t.round) % ends.size();
                out.push_back({comms::PORTAL, ends[i]->x, ends[i]->y, ends[i]->orient, pids[i] & 4095, t.round});
            }
        }
        return out;
    }

    // ---- map symmetry (see symAlive) ----------------------------------------------

    // Test the candidates on pairs completed this turn: a tile seen for the first time whose
    // mirror image was seen before (or now). Each pair is judged once.
    void inferSymmetry(std::vector<int> const& fresh, Params const& p) {
        for (int c : fresh)
            for (int s = 1; s <= 3; s++) {
                if (!(symAlive >> (s - 1) & 1)) continue;
                int m = board.mirrorAs(c, s);
                if (m == c || seenRound[m] < 0) continue;
                bool same = board.bed[c] == board.bed[m];
                int kelp = 0;
                for (int d = 0; d < 4 && same; d++) {
                    int k1 = board.side(c, d).kind, k2 = board.side(m, Board::mirrorDir(d, s)).kind;
                    same = k1 == k2;
                    kelp += k1 != 0;
                }
                if (!same) {
                    symAlive &= ~(1 << (s - 1));
                } else {
                    symPairs[s]++;
                    symKelp[s] += kelp;
                }
            }
        settleSymmetry(p);
    }

    // Only eliminations count, and they are exact, so teammates pool them (SYM facts carry symAlive) and the last
    // candidate standing is the symmetry. Matches prove little: a region can match a wrong candidate for long (one
    // official map is top-bottom symmetric in every edge and differs in 56 of 188 beds; in a replay study choosing
    // the best-matched candidate picked a wrong one for over a quarter of the dragons, elimination for none).
    void settleSymmetry(Params const& p) {
        if (board.sym >= 0) return;
        if (symAlive == 1 || symAlive == 2 || symAlive == 4) setSymmetry(symAlive == 1 ? 1 : symAlive == 2 ? 2 : 3, p);
    }

    // A starting dragon's first turn: the enemy's starting dragons are the mirror image of ours,
    // so a candidate that puts the mirror of our starting body in view, on free tiles, is wrong
    // (an enemy that already moved this round vacated at most its tail tile).
    void startTest(Params const& p) {
        std::vector<char> enemyAt(board.NC, 0);
        for (Part const& part : t.parts)
            if (part.team != init.team) enemyAt[board.id(part.x, part.y)] = 1;
        for (int s = 1; s <= 3; s++) {
            int inView = 0, enemy = 0;
            for (int c : body) {
                int m = board.mirrorAs(c, s);
                if (m == c || !visible(m)) continue;
                inView++;
                enemy += enemyAt[m];
            }
            if (inView - enemy >= 2) symAlive &= ~(1 << (s - 1));
        }
        settleSymmetry(p);
    }

    void setSymmetry(int s, Params const& p) {
        board.sym = s;
#ifdef BC_DEBUG
        std::fprintf(stderr, "SYM r%d id %d sym %d pairs %d/%d/%d kelp %d/%d/%d\n", t.round, init.id, s, symPairs[1],
                     symPairs[2], symPairs[3], symKelp[1], symKelp[2], symKelp[3]);
#endif
        bool changed = false;
        for (int c = 0; c < board.NC; c++)
            if (seenRound[c] >= 0) changed |= copyToMirror(c, p);
        // fountains teammates told us about, where we have not looked ourselves
        for (auto const& h : know.hot) {
            int m = board.mirror(h.cell);
            if (seenRound[h.cell] < 0 && seenRound[m] < 0 && cdObs[m] < p.hotObs && h.len >= 1) {
                board.bed[m] = 1;
                board.gapMin[m] = board.gapMax[m] = static_cast<int16_t>(h.len);
                mirrored[m] = 1;
                MN_TOUCH(m);
            }
        }
        if (changed) rebuildGated();
    }

    void mirrorView(Params const& p) {
        bool changed = false;
        for (VisTile const& v : t.tiles) changed |= copyToMirror(board.id(v.x, v.y), p);
        if (changed) rebuildGated();
    }

    // Copy what we know of seen tile c onto its mirror image, if we never saw that one: bed or
    // not, a fountain's gap, and each edge nobody on our side of it has seen (kelp or open;
    // portals are left alone, pairing them needs both ends). True when an edge changed.
    bool copyToMirror(int c, Params const& p) {
        int s = board.sym;
        int m = board.mirrorAs(c, s);
        if (m == c || seenRound[m] >= 0) return false;
        mirrored[m] = 1;
        MN_TOUCH(m);
        if (board.bed[c] >= 0) board.bed[m] = board.bed[c];
        if (cdObs[m] < p.hotObs && (cdObs[c] >= p.hotObs || board.gapMax[c] > 0)) {  // a verdict on c, either way
            board.gapMin[m] = board.gapMin[c];
            board.gapMax[m] = board.gapMax[c];
        }
        bool changed = false;
        for (int d = 0; d < 4; d++) {
            int kind = board.side(c, d).kind;
            if (kind == 2) continue;
            int dm = Board::mirrorDir(d, s);
            int across = board.id(board.X(m) + DX[dm], board.Y(m) + DY[dm]);
            if (seenRound[across] >= 0) continue;   // that edge was in view from its other tile
            Edge& e = board.sideRef(m, dm);
            if (e.kind == 2 || e.kind == kind) continue;
            e = Edge{static_cast<int8_t>(kind)};
            MN_TOUCH(dm == S || dm == E ? across : m);  // the record of m's side dm: m's own (N, W) or the tile across's
            changed = true;
        }
        return changed;
    }

    // Remember a portal end; pair both ends (like the engine: the two edges sharing an id)
    // as soon as both are known. Returns true when the board's links changed.
    bool notePortal(int pid, int orient, int x, int y) {
        std::vector<PortalEnd>* ends = nullptr;
        for (auto& pe : portalEnds)
            if (pe.first == pid) ends = &pe.second;
        if (!ends) {
            portalEnds.push_back({pid, {}});
            ends = &portalEnds.back().second;
        }
        for (PortalEnd const& e : *ends)
            if (e.orient == orient && e.x == x && e.y == y) return false;
        ends->push_back({orient, x, y});
        if (ends->size() != 2) return false;
        PortalEnd const& a = (*ends)[0];
        PortalEnd const& b = (*ends)[1];
        Edge& ea = a.orient == 0 ? board.hE[board.id(a.x, a.y)] : board.vE[board.id(a.x, a.y)];
        Edge& eb = b.orient == 0 ? board.hE[board.id(b.x, b.y)] : board.vE[board.id(b.x, b.y)];
        ea.kind = eb.kind = 2;
        ea.partnerOrient = static_cast<int8_t>(b.orient);
        ea.px = static_cast<int16_t>(b.x);
        ea.py = static_cast<int16_t>(b.y);
        eb.partnerOrient = static_cast<int8_t>(a.orient);
        eb.px = static_cast<int16_t>(a.x);
        eb.py = static_cast<int16_t>(a.y);
        MN_TOUCH(board.id(a.x, a.y)), MN_TOUCH(board.id(b.x, b.y));
        return true;
    }

    // A bed's countdown right after a spawn is its whole gap, so the largest one seen
    // estimates the gap from above as the bed keeps being watched. Beds are counted
    // as hot (fountains) once enough small countdowns have been seen.
    void learnGap(int c, int cd, Params const& p) {
        int before = maxCd[c];
        if (cdObs[c] == 0) bedsWithCd++;
        bool wasHot = cdObs[c] >= p.hotObs && before <= p.hotGap;
        double oldRate = cdObs[c] > 0 ? 1.0 / std::max(1, before) : 0.0;
        if (cd > maxCd[c]) maxCd[c] = static_cast<int16_t>(std::min(cd, 30000));
        sumMaxCd += maxCd[c] - before;
        if (cdObs[c] < 30000) cdObs[c]++;
        bool hot = cdObs[c] >= p.hotObs && maxCd[c] <= p.hotGap;
        hotBeds += (hot ? 1 : 0) - (wasHot ? 1 : 0);
        refillRate += 1.0 / std::max<int>(1, maxCd[c]) - oldRate;
        // Policy's hot-bed pull reads the gap range from the board.
        // (a fountain heard over sonar keeps its gap until our own watching is conclusive)
        if (hot || cdObs[c] >= p.hotObs)
            board.gapMin[c] = board.gapMax[c] = static_cast<int16_t>(hot ? std::max<int>(1, maxCd[c]) : 0);
    }

    bool adjacent(int a, int b) const {
        for (int d = 0; d < 4; d++)
            if (board.nb[a * 4 + d] == b) return true;
        return false;
    }

    // Called after choosing a move so next turn's merge has the right prefix.
    int lastEatRound = -1;        // escape: the last round this process ate (its birth counts)
    // A multi-step MOVE we live through (flee / QSK): each step a new head; a step that eats keeps the tail, the
    // others drop it; a paid step drops one more (rules 1.2.3). len = the length after the move.
    void advancePath(std::vector<int> const& cells, std::vector<char> const& eats, int paid) {
        int len = t.length - paid;
        for (size_t i = 0; i < cells.size(); i++) {
            if (i < eats.size() && eats[i]) len++, lastEatRound = t.round;
#if BC_EXPAND_OPENING
            if (i < eats.size() && eats[i]) eatCount++;
#endif
            body.insert(body.begin(), cells[i]);
        }
        if (static_cast<int>(body.size()) > len) body.resize(std::max(2, len));
    }
    void advanceBody(int dest, bool grows) {
        if (grows) lastEatRound = t.round;
#if BC_EXPAND_OPENING
        if (grows) eatCount++;
#endif
        body.insert(body.begin(), dest);
        if (!grows && !body.empty() && static_cast<int>(body.size()) > t.length) body.pop_back();
    }
};

}  // namespace bc
