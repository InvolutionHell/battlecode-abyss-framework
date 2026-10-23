#pragma once
// Map network state (BC_MAPNET): rooms, portal gate states and danger. Compiled only with BC_MAPNET.
//
// Interface for the decision hooks:
//   regionOf(tile)          room index of a known tile, -1 when not in a computed room            (BC_MN_ROOMS fills rooms / roomOf)
//   portalState(tile, dir)  comms::GateState of the step from `tile` towards `dir` through a portal (gate facts)
//   danger(tile, round)     0..1, a death or a trap known on the tile, fading over kDangerAge rounds
//   recompute(...)          rooms over known open edges, walls and portals cut; called after Board::rebuild   (BC_MN_ROOMS)
//
// Gate table sources: our own landing (CLEARED), a landing seen with no open edge (DEAD, World::gatePortals), a
// teammate's GATE fact, a PROBING (sent on the crossing turn) that no CLEARED followed within kProbeWait rounds
// (TRAP: a prober dying in a 1-tile room loses its own rays; a CLAIM two steps out never infers), and the mirror of
// each of these once the symmetry is known.
#include <algorithm>
#include <vector>

#include "board.hpp"
#include "comms.hpp"

namespace bc {

struct RegionMap {
    struct Room {
        int anchor = -1;      // lowest tile id of the room
        int size = 0;
        int beds = 0;
        int exits = 0;        // portal edges and edges to unknown tiles
        bool closed = false;  // every tile known, no unknown neighbour, no unpaired portal
        int lastVisit = -1;
    };
#if BC_MN_EVIDENCE && BC_MAPNET_EVIDENCE >= 2
    static constexpr int kProbeWait = 12;   // BC_MAPNET_EVIDENCE 2: the prober's silence judged over 12 rounds
                                            // (relayed CLEARED / SELF facts arrive)
#else
    static constexpr int kProbeWait = 6;    // rounds a PROBING waits for its CLEARED before it reads as a TRAP
#endif
#if BC_MN_EVIDENCE
    static constexpr int kTrapAge = 60;     // BC_MN_EVIDENCE: an inferred TRAP is forgotten after this unless
                                            // confirmed again
#else
    static constexpr int kTrapAge = 150;    // an inferred TRAP is forgotten after this (the prober may have turned back)
#endif
    static constexpr int kDangerAge = 200;

    std::vector<int> roomOf;                // tile -> room index (BC_MN_ROOMS)
    std::vector<Room> rooms;                // (BC_MN_ROOMS)
    std::vector<int8_t> gate;               // tile * 4 + dir -> comms::GateState
    std::vector<int16_t> gateRound;
    std::vector<int16_t> gateLanding;       // CLEARED: the landing tile, else -1
#if BC_MN_EVIDENCE
    std::vector<int16_t> gateProber;        // BC_MN_EVIDENCE: PROBING: the prober's id (its fact's aux), else -1
#endif
#if BC_ECON_TRACE
    std::vector<int> inferredNow, lapsedNow;  // trace: the slots whose PROBING timed out this tick, into a TRAP / lapsed
#endif
    std::vector<int> gateList;              // slots with a state (relay rotation, timeouts)
    std::vector<int16_t> dangerRound;
    int relayAt = 0;

    void init(int nc) {
        if (static_cast<int>(gate.size()) == nc * 4) return;
        roomOf.assign(nc, -1);
        rooms.clear();
        gate.assign(nc * 4, comms::G_UNKNOWN);
        gateRound.assign(nc * 4, -1);
        gateLanding.assign(nc * 4, -1);
#if BC_MN_EVIDENCE
        gateProber.assign(nc * 4, -1);
#endif
        gateList.clear();
        dangerRound.assign(nc, -1);
    }

    int regionOf(int tile) const { return roomOf.empty() ? -1 : roomOf[tile]; }
    int portalState(int tile, int dir) const { return gate.empty() ? comms::G_UNKNOWN : gate[tile * 4 + dir]; }
    double danger(int tile, int round) const {
        if (dangerRound.empty() || dangerRound[tile] < 0) return 0.0;
        int age = round - dangerRound[tile];
        return age >= kDangerAge ? 0.0 : 1.0 - static_cast<double>(age) / kDangerAge;
    }
    void noteDanger(int tile, int round) {
        if (!dangerRound.empty() && dangerRound[tile] < round) dangerRound[tile] = static_cast<int16_t>(round);
    }

#if BC_MN_ROOMS
    // BC_MN_ROOMS: rooms = components of the known tiles (seen, or copied from the mirror) over open edges - walls
    // (kelp) and portals cut. A room records its size, beds (bed == 1), exits (its portal sides + its open sides onto
    // unknown tiles) and closed (no unknown neighbour, no unpaired portal side); anchor = its lowest tile; lastVisit
    // survives merges and splits (kept by anchor, as a full rebuild keeps it). Incremental, by change: World touches
    // the tiles whose inputs it writes (known, bed, the tile's own top / left edge record; MN_TOUCH in world.hpp),
    // and each recompute sweeps 1/16 of the map against the snapshot, a safety net for a write nobody touched
    // (statSwept; LOG mnmiss under BC_V11_TRACE). The rooms read only the snapshot (0 = the Board's pristine state),
    // so they are always the rooms of the snapshot. Per recompute: a changed edge record moves its known tiles'
    // counts and an edge opened between known tiles merges their rooms (relabelling the smaller); tiles that became
    // known flood into new components that merge with the old rooms they touch; for an edge cut between known tiles,
    // searches from both of its sides run in turn until they meet (still one room) or one side runs out (that side is
    // carved out as a room of its own). Returns true when the rooms changed. rebuild() is the full BFS (the reference
    // of the tests; the fallback should a tile ever become unknown). Inactive in the final build (BC_MAPNET_ROOMS 0):
    // the stub below is compiled instead.
    std::vector<int8_t> known_;             // known (seen or mirrored) as the rooms have it
    std::vector<int16_t> snap_;             // per tile: known | bed == 1 << 1 | top edge kind << 2 | paired << 4 | left kind << 5 | paired << 7
    std::vector<int> pex_, uex_, unp_;      // per room: portal sides, open sides onto unknown tiles, unpaired portal sides
    std::vector<std::vector<int>> members_; // per room: its tiles
    std::vector<int> dirty_;                // tiles touched since the last recompute
    std::vector<int8_t> mark_;              // on dirty_
    std::vector<int> added_, cuts_, touched_, joins_;  // recompute's scratch: tiles that became known, cut edges (pairs), tiles whose
                                                       // room's counts moved, the old rooms' tiles a new component touches
    std::vector<int> lab_;                  // per tile: the cut search that reached it (>= labBase_ in the current resolution)
    std::vector<std::vector<int>> sq_;      // per cut search: the tiles it reached (a queue; head in sh_)
    std::vector<int> sh_, sp_, sr_;         // per cut search: queue head, union-find parent, room
    int W_ = 0, H_ = 0, sweep_ = 0, labBase_ = 0;
    static constexpr int kDirtyBudget = 512;  // touched tiles a recompute takes (a symmetry backfill touches thousands at once)
    int statFull = 0, statAdded = 0, statRe = 0, statSwept = 0, statCut = 0;  // BC_V11_TRACE: the last recompute's full rebuild /
                                            // tiles added / rooms changed / tiles the sweep found changed but untouched / tiles the
                                            // cut searches reached

    // an input of tile c (its known flag, bed, top or left edge record) may have changed
    void touch(int c, int nc) {
        if (static_cast<int>(mark_.size()) != nc) mark_.assign(nc, 0), dirty_.clear();
        if (!mark_[c]) mark_[c] = 1, dirty_.push_back(c);
    }
    void touchAll(int nc) {
        for (int c = 0; c < nc; c++) touch(c, nc);
    }
    static int16_t snapOf(bool known, Board const& b, int c) {
        Edge const& h = b.hE[c];
        Edge const& v = b.vE[c];
        return static_cast<int16_t>(known | (b.bed[c] == 1) << 1 | (h.kind & 3) << 2 | (h.partnerOrient >= 0) << 4 | (v.kind & 3) << 5 |
                                    (v.partnerOrient >= 0) << 7);
    }
    int nbOf(int c, int d) const {  // the tile one ordinary step from c (torus)
        int x = c % W_;
        switch (d) {
        case N: return c >= W_ ? c - W_ : c + (H_ - 1) * W_;
        case S: return c + W_ < W_ * H_ ? c + W_ : x;
        case E: return x + 1 < W_ ? c + 1 : c - x;
        default: return x > 0 ? c - 1 : c + W_ - 1;
        }
    }
    // the four sides of tile c as the snapshot has them: the tile across (torus), the kind (0 open, 1 wall, 2 portal), paired
    struct Around {
        int nb[4], kind[4];
        bool paired[4];
    };
    void around(int c, Around& a) const {
        int x = c % W_;
        a.nb[N] = c >= W_ ? c - W_ : c + (H_ - 1) * W_;
        a.nb[S] = c + W_ < W_ * H_ ? c + W_ : x;
        a.nb[E] = x + 1 < W_ ? c + 1 : c - x;
        a.nb[Dir::W] = x > 0 ? c - 1 : c + W_ - 1;
        int s0 = snap_[c], ss = snap_[a.nb[S]], se = snap_[a.nb[E]];
        a.kind[N] = (s0 >> 2) & 3, a.paired[N] = (s0 >> 4) & 1;
        a.kind[Dir::W] = (s0 >> 5) & 3, a.paired[Dir::W] = (s0 >> 7) & 1;
        a.kind[S] = (ss >> 2) & 3, a.paired[S] = (ss >> 4) & 1;
        a.kind[E] = (se >> 5) & 3, a.paired[E] = (se >> 7) & 1;
    }
    // the counts a side of known tile t adds to room r (sgn -1 takes them back): a portal side (unpaired?), an open side onto an
    // unknown tile n
    void count(int r, int kind, bool paired, int n, int sgn) {
        if (kind == 2) pex_[r] += sgn, unp_[r] += paired ? 0 : sgn;
        else if (kind == 0 && !known_[n]) uex_[r] += sgn;
    }
    void finish(int r) {
        Room& x = rooms[r];
        x.exits = pex_[r] + uex_[r];
        x.closed = uex_[r] == 0 && unp_[r] == 0;
    }
    int newRoom() {
        rooms.push_back(Room{});
        pex_.push_back(0), uex_.push_back(0), unp_.push_back(0), members_.emplace_back();
        return static_cast<int>(rooms.size()) - 1;
    }
    void dropRoom(int r) {  // swap-remove room r (its tiles already relabelled); the last room takes index r
        int last = static_cast<int>(rooms.size()) - 1;
        if (r != last) {
            rooms[r] = rooms[last], pex_[r] = pex_[last], uex_[r] = uex_[last], unp_[r] = unp_[last];
            members_[r].swap(members_[last]);
            for (int t : members_[r]) roomOf[t] = r;
        }
        rooms.pop_back(), pex_.pop_back(), uex_.pop_back(), unp_.pop_back(), members_.pop_back();
    }
    void merge(int a, int z) {  // rooms a and z become one; the larger member list survives (callers re-read roomOf)
        int keep = members_[a].size() >= members_[z].size() ? a : z, gone = keep == a ? z : a;
        for (int t : members_[gone]) roomOf[t] = keep, members_[keep].push_back(t);
        Room& k = rooms[keep];
        Room const& g = rooms[gone];
        k.anchor = std::min(k.anchor, g.anchor), k.size += g.size, k.beds += g.beds, k.lastVisit = std::max(k.lastVisit, g.lastVisit);
        pex_[keep] += pex_[gone], uex_[keep] += uex_[gone], unp_[keep] += unp_[gone];
        members_[gone].clear();
        finish(keep);
        dropRoom(gone);  // the last room moves into `gone`
    }
    // a room for the tiles in `q` (each with roomOf idx already): size, beds, anchor and the counts of their sides
    void tally(int idx) {
        Room& r = rooms[idx];
        r.size = static_cast<int>(members_[idx].size()), r.beds = 0, r.anchor = members_[idx].empty() ? -1 : members_[idx][0];
        int pex = 0, uex = 0, unp = 0;
        Around a;
        for (int c : members_[idx]) {
            r.beds += (snap_[c] >> 1) & 1;
            r.anchor = std::min(r.anchor, c);
            around(c, a);
            for (int d = 0; d < 4; d++) {
                if (a.kind[d] == 2) pex++, unp += !a.paired[d];
                else if (a.kind[d] == 0 && !known_[a.nb[d]]) uex++;
            }
        }
        pex_[idx] += pex, uex_[idx] += uex, unp_[idx] += unp;
        finish(idx);
    }
    // BFS one component from c0 over open edges between known tiles not in a room yet (rebuild, and the tiles that became known);
    // an open side onto a tile of an older room takes that room's unknown side back and is noted in joins_
    int grow(int c0) {
        int idx = newRoom();
        std::vector<int>& q = members_[idx];
        q.push_back(c0);
        roomOf[c0] = idx;
        int beds = 0, anchor = c0, pex = 0, uex = 0, unp = 0;
        Around a;
        for (size_t qi = 0; qi < q.size(); qi++) {
            int c = q[qi];
            beds += (snap_[c] >> 1) & 1;
            anchor = std::min(anchor, c);
            around(c, a);
            for (int d = 0; d < 4; d++) {
                int n = a.nb[d];
                if (a.kind[d] == 2) pex++, unp += !a.paired[d];
                else if (a.kind[d] == 1) continue;
                else if (!known_[n]) uex++;
                else if (roomOf[n] < 0) roomOf[n] = idx, q.push_back(n);
                else if (roomOf[n] != idx) uex_[roomOf[n]]--, joins_.push_back(n);  // its open side onto c, unknown until now
            }
        }
        Room& r = rooms[idx];
        r.size = static_cast<int>(q.size()), r.beds = beds, r.anchor = anchor;
        pex_[idx] = pex, uex_[idx] = uex, unp_[idx] = unp;
        finish(idx);
        return idx;
    }
    int find(int s) {
        while (sp_[s] != s) s = sp_[s] = sp_[sp_[s]];
        return s;
    }
    // the cuts of this recompute (cuts_: pairs of known tiles whose shared edge was cut): per room, searches from the cut edges'
    // sides run one tile each in turn; searches that meet are joined; a group of searches that runs out of tiles is a whole
    // component and is carved out of its room; a room is done when at most one of its groups still runs
    int resolveCuts() {
        int ns = 0, base = labBase_, reached = 0;
        auto source = [&](int t) {
            if (lab_[t] >= base) return;
            if (static_cast<int>(sq_.size()) <= ns) sq_.emplace_back(), sh_.push_back(0), sp_.push_back(0), sr_.push_back(0);
            sq_[ns].assign(1, t), sh_[ns] = 0, sp_[ns] = ns, sr_[ns] = roomOf[t];
            lab_[t] = base + ns++;
        };
        for (size_t i = 0; i + 1 < cuts_.size(); i += 2)
            if (roomOf[cuts_[i]] == roomOf[cuts_[i + 1]]) source(cuts_[i]), source(cuts_[i + 1]);
        labBase_ += ns;
        if (ns == 0) return 0;
        std::vector<int8_t> live(ns);
        Around a;
        for (;;) {
            // the groups still running, per room: a room with two or more keeps searching
            std::fill(live.begin(), live.end(), 0);
            for (int s = 0; s < ns; s++)
                if (sh_[s] < static_cast<int>(sq_[s].size())) live[find(s)] = 1;
            bool more = false;
            for (int s = 0; s < ns && !more; s++)
                if (find(s) == s && live[s])
                    for (int u = 0; u < ns && !more; u++)
                        if (u != s && find(u) == u && live[u] && sr_[u] == sr_[s]) more = true;
            if (!more) break;
            for (int s = 0; s < ns; s++) {
                if (sh_[s] >= static_cast<int>(sq_[s].size())) continue;
                int c = sq_[s][sh_[s]++];
                reached++;
                around(c, a);
                for (int d = 0; d < 4; d++) {
                    int n = a.nb[d];
                    if (a.kind[d] != 0 || !known_[n]) continue;
                    if (lab_[n] >= base) {
                        int a = find(s), z = find(lab_[n] - base);
                        if (a != z) sp_[a] = z;
                    } else {
                        lab_[n] = base + s;
                        sq_[s].push_back(n);
                    }
                }
            }
        }
        // carve every group that ran out (a whole component) out of its room; the room keeps the rest (one component, or nothing)
        std::fill(live.begin(), live.end(), 0);
        for (int s = 0; s < ns; s++)
            if (sh_[s] < static_cast<int>(sq_[s].size())) live[find(s)] = 1;
        touched_.clear();  // the rooms that lost tiles
        for (int g = 0; g < ns; g++) {
            if (find(g) != g || live[g]) continue;
            int r = sr_[g], idx = newRoom();
            for (int s = 0; s < ns; s++)
                if (find(s) == g)
                    for (int t : sq_[s]) roomOf[t] = idx, members_[idx].push_back(t);
            tally(idx);
            rooms[r].size -= rooms[idx].size, rooms[r].beds -= rooms[idx].beds;
            pex_[r] -= pex_[idx], uex_[r] -= uex_[idx], unp_[r] -= unp_[idx];
            if (roomOf[rooms[r].anchor] == idx) rooms[idx].lastVisit = rooms[r].lastVisit, rooms[r].lastVisit = -1;
            touched_.push_back(r);
        }
        for (int r : touched_) {
            std::vector<int>& m = members_[r];
            size_t w = 0;
            int anchor = -1;
            for (int t : m)
                if (roomOf[t] == r) m[w++] = t, anchor = anchor < 0 ? t : std::min(anchor, t);
            m.resize(w);
            rooms[r].anchor = anchor;
            finish(r);
        }
        std::sort(touched_.begin(), touched_.end());
        for (int i = static_cast<int>(touched_.size()) - 1; i >= 0; i--)
            if (members_[touched_[i]].empty() && (i + 1 == static_cast<int>(touched_.size()) || touched_[i + 1] != touched_[i]))
                dropRoom(touched_[i]);
        return reached;
    }
    void reset(int nc) {
        roomOf.assign(nc, -1);
        rooms.clear(), pex_.clear(), uex_.clear(), unp_.clear(), members_.clear();
        lab_.assign(nc, -1), labBase_ = 0;
    }
    void rebuild(Board const& b, std::vector<int> const& seenRound, std::vector<int8_t> const& mirrored) {
        int const nc = b.NC;
        std::vector<int> visit;  // lastVisit by anchor
        for (Room const& r : rooms)
            if (r.lastVisit >= 0) visit.push_back(r.anchor), visit.push_back(r.lastVisit);
        W_ = b.W, H_ = b.H;
        reset(nc);
        known_.resize(nc), snap_.resize(nc);
        for (int c = 0; c < nc; c++) {
            known_[c] = seenRound[c] >= 0 || mirrored[c];
            snap_[c] = snapOf(known_[c], b, c);
        }
        joins_.clear();
        for (int c0 = 0; c0 < nc; c0++) {
            if (roomOf[c0] >= 0 || !known_[c0]) continue;
            Room& r = rooms[grow(c0)];
            for (size_t i = 0; i + 1 < visit.size(); i += 2)
                if (visit[i] == r.anchor) r.lastVisit = visit[i + 1];
        }
        for (int c : dirty_) mark_[c] = 0;
        dirty_.clear();
    }
    bool recompute(Board const& b, std::vector<int> const& seenRound, std::vector<int8_t> const& mirrored) {
        int const nc = b.NC;
        if (W_ != b.W || H_ != b.H || static_cast<int>(known_.size()) != nc) {  // a new board: everything pristine
            W_ = b.W, H_ = b.H;
            known_.assign(nc, 0), snap_.assign(nc, 0);
            reset(nc);
            sweep_ = 0;
        }
        // the sweep: a slice of the map against the snapshot; a changed tile nobody touched is processed too
        statSwept = 0;
        int slice = (nc + 15) / 16;
        for (int i = 0; i < slice; i++, sweep_ = sweep_ + 1 < nc ? sweep_ + 1 : 0) {
            int c = sweep_;
            if (snap_[c] != snapOf(seenRound[c] >= 0 || mirrored[c], b, c) && !(mark_.size() == static_cast<size_t>(nc) && mark_[c]))
                touch(c, nc), statSwept++;
        }
        statFull = statAdded = statCut = 0;
        if (dirty_.empty()) return false;
        bool changed = false, full = false;
        added_.clear(), cuts_.clear(), touched_.clear();
        int nd = static_cast<int>(dirty_.size()), take = std::min(nd, kDirtyBudget);
        for (int i = nd - 1; i >= nd - take; i--) {  // the newest first (this turn's view); the rest wait for the next call
            int c = dirty_[i];
            mark_[c] = 0;
            bool k = seenRound[c] >= 0 || mirrored[c];
            int16_t sn = snapOf(k, b, c), od = snap_[c];
            if (sn == od) continue;
            snap_[c] = sn;
            changed = true;
            if ((od & 1) && !k) full = true;  // a tile forgotten: never happens (seenRound and mirrored only grow); start over
            if (full) continue;
            for (int o = 0; o < 2; o++) {     // o 0: the top record (c's N side, up's S side); 1: the left one (c's W, left's E)
                int sh = o == 0 ? 2 : 5;
                int ko = (od >> sh) & 3, kn = (sn >> sh) & 3;
                bool po = (od >> (sh + 2)) & 1, pn = (sn >> (sh + 2)) & 1;
                if (ko == kn && po == pn) continue;
                int other = nbOf(c, o == 0 ? N : Dir::W);
                for (int t : {c, other})
                    if (known_[t]) {
                        int u = t == c ? other : c;
                        count(roomOf[t], ko, po, u, -1);
                        count(roomOf[t], kn, pn, u, 1);
                        touched_.push_back(t);
                    }
                if (known_[c] && known_[other]) {
                    if (ko == 0 && kn != 0) cuts_.push_back(c), cuts_.push_back(other);  // possibly two rooms now
                    else if (ko != 0 && kn == 0 && roomOf[c] != roomOf[other]) merge(roomOf[c], roomOf[other]);
                }
            }
            if (known_[c] && ((od ^ sn) & 2)) rooms[roomOf[c]].beds += (sn & 2) ? 1 : -1;  // a bed learnt late
            if (!known_[c] && k) added_.push_back(c);
        }
        dirty_.resize(nd - take);
        if (full) {
            rebuild(b, seenRound, mirrored);
            statFull = 1;
            return true;
        }
        for (int t : touched_) finish(roomOf[t]);
        // the tiles that became known: their components, each merged with the old rooms it touches
        for (int t : added_) known_[t] = 1;
        for (int t : added_) {
            if (roomOf[t] >= 0) continue;
            joins_.clear();
            grow(t);
            for (int n : joins_)
                if (roomOf[n] != roomOf[t]) merge(roomOf[t], roomOf[n]);
            finish(roomOf[t]);
        }
        statAdded = static_cast<int>(added_.size());
        if (!cuts_.empty()) statCut = resolveCuts();
        return changed;
    }
    // BC_V11_TRACE: tiles whose inputs differ from the snapshot right after a recompute and are not waiting on the touched list
    // (a write nobody touched)
    int audit(Board const& b, std::vector<int> const& seenRound, std::vector<int8_t> const& mirrored) const {
        int n = 0;
        bool marks = mark_.size() == snap_.size();
        for (int c = 0; c < b.NC && c < static_cast<int>(snap_.size()); c++)
            n += snap_[c] != snapOf(seenRound[c] >= 0 || mirrored[c], b, c) && !(marks && mark_[c]);
        return n;
    }
    void visit(int tile, int round) {  // the room of `tile` was entered this round (hooks)
        int r = regionOf(tile);
        if (r >= 0) rooms[r].lastVisit = round;
    }
#else
    // BC_MN_ROOMS off (final build): rooms over known open edges are not computed. The stub keeps every tile outside
    // a room (the hooks use World::pocket instead).
    void recompute(Board const& b, std::vector<int> const& seenRound, std::vector<int8_t> const& mirrored) {
        (void)b, (void)seenRound, (void)mirrored;
    }
#endif

    static constexpr int kClaimAge = 10;    // a claim (the prober two steps out) holds the portal this long
    static int rank(int s) {
        return s == comms::G_CLEARED ? 5 : s == comms::G_DEAD ? 4 : s == comms::G_TRAP ? 3 : s == comms::G_PROBING ? 2
             : s == comms::G_CLAIM ? 1 : 0;
    }
    // One slot; true when the state changed. CLEARED > DEAD > TRAP > PROBING > UNKNOWN; the same state only refreshes its round.
#if BC_MN_EVIDENCE
    bool setOne(int k, int st, int round, int landing, int prober = -1) {
#else
    bool setOne(int k, int st, int round, int landing) {
#endif
        if (rank(st) < rank(gate[k]) || (st == gate[k] && round <= gateRound[k])) return false;
        bool changed = st != gate[k];
        if (gate[k] == comms::G_UNKNOWN) gateList.push_back(k);
        gate[k] = static_cast<int8_t>(st);
        gateRound[k] = static_cast<int16_t>(round);
        if (landing >= 0) gateLanding[k] = static_cast<int16_t>(landing);
#if BC_MN_EVIDENCE
        if (st == comms::G_PROBING) gateProber[k] = static_cast<int16_t>(prober);
#endif
        return changed;
    }
    // Timeouts, once a turn: an unanswered PROBING becomes a TRAP; an inferred TRAP is forgotten after kTrapAge.
    // BC_MN_EVIDENCE: `evidence(k)` - the timed-out PROBING of slot k has earned its TRAP (else it lapses).
#if BC_MN_EVIDENCE
    template <class Evidence>
    bool tick(int round, Evidence evidence) {
#else
    bool tick(int round) {
#endif
        bool changed = false;
        size_t w = 0;
#if BC_ECON_TRACE
        inferredNow.clear(), lapsedNow.clear();
#endif
        for (size_t i = 0; i < gateList.size(); i++) {
            int k = gateList[i];
            if (gate[k] == comms::G_PROBING && round - gateRound[k] > kProbeWait) {
#if BC_MN_EVIDENCE
                gate[k] = BC_MN_TRAP_INFER && evidence(k) ? comms::G_TRAP : comms::G_UNKNOWN;
#else
                gate[k] = BC_MN_TRAP_INFER ? comms::G_TRAP : comms::G_UNKNOWN;
#endif
#if BC_ECON_TRACE
                (gate[k] == comms::G_TRAP ? inferredNow : lapsedNow).push_back(k);
#endif
                gateRound[k] = static_cast<int16_t>(round);
                changed = true;
            } else if (gate[k] == comms::G_CLAIM && round - gateRound[k] > kClaimAge) {
                gate[k] = comms::G_UNKNOWN;
            } else if (gate[k] == comms::G_TRAP && round - gateRound[k] > kTrapAge) {
                gate[k] = comms::G_UNKNOWN;
                changed = true;
            }
            if (gate[k] != comms::G_UNKNOWN) gateList[w++] = k;
        }
        gateList.resize(w);
        return changed;
    }
};

}  // namespace bc
