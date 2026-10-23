// Entry point: one process per dragon. Each turn: observe (World), pick the parameters of the current (map group,
// phase), search (Policy::decide), apply the last legality checks, write the action and the sonar rays in one flush.
// The king end-game library (v10k.hpp, king/) is compiled in but never runs in this build (V10K_R0 = 501 > 500
// rounds). When active it would run before the search (v10k::pre: this turn's Params copy and World::know.longest), a
// FEED execution would output no action, and the rays would be merged (v10k::speak). The bot is map-agnostic: sonar
// coordination (comms.hpp), local / global teams (team.hpp, membership and TEAM facts) and a tactics library
// (dispersal from crowds of own heads, policy.hpp computeTactics).
#include <cstring>

#include "io.hpp"
#include "policy.hpp"
#include "world.hpp"
#include "v10k.hpp"

int main() {
    using namespace bc;
    clockNow();  // start the meter: setup on the first turn counts against it
    std::setvbuf(stdout, nullptr, _IOFBF, 1 << 16);
    Reader in;
    Init init;
    if (!readInit(in, init)) return 0;

    World world;
    world.start(init);
    Out out;
    Turn turn;
    bool first = true;
    while (readTurn(in, turn)) {
        // The first turn's clock started with the process (setup counts); later ones on arrival.
        double turnStart = first ? 0.0 : clockNow();
        first = false;
#if BC_FALLBACK95
        fb95::hit = nullptr;
        fb95::burned = nullptr;
#endif
        world.observe(turn, kParams);
#if BC_MAPNET
        world.observeMapnet();
#endif
        // Profile by map group, re-judged as the dragon learns the map (no map names are used): the tuned knobs of
        // the current (map group, phase)
        static Params mapParams = paramsFor(2, 0);
        static int paramsForKey = 2 * 4 + 0;
        int group = world.mapGroup(kParams);
        int key = group * 4 + phaseOf(turn.round);
        if (key != paramsForKey) {
            paramsForKey = key;
            mapParams = paramsFor(group, phaseOf(turn.round));
        }
        // king library: its turn before the search, only after this process's first crown event (inactive: V10K_R0 =
        // 501)
        v10k::Plan kp;
        Params const* P = &mapParams;
        if (V10K_ON && v10k::g_active && turn.round >= V10K_R0 && world.board.NC > V10K_MIN_TILES) v10k::pre(world, mapParams, kp, P, out);
        if (kp.execute) {
            v10k::executing(out);  // FEED execution: no action (we die here, ceil(L/2) pearls on segments 0, 2, 4, ...)
            out.flush();
            continue;
        }
        double deadline = turnStart + BC_TURN_BUDGET;
        if (kp.active) deadline -= V10K_RESERVE;
#if BC_LONGEST_RESERVE
        // BC_LONGEST_RESERVE: the team's longest from kReserveFrom eats first (its params' eatBonus x kReserveEat)
        // and keeps every segment
        Params earlyFeed = *P;
        if (reserveEarlyFeed(earlyFeed)) P = &earlyFeed;
        Params reserveParams;
        bool reserveMe = world.longestReserve();
        if (reserveMe) {
            reserveParams = *P;
            reserveParams.eatBonus *= kReserveEat;
            P = &reserveParams;
        }
#endif
        Policy policy(world, *P, deadline);
#if BC_LONGEST_RESERVE
        policy.setReserve(reserveMe);
#endif
        Choice c = policy.decide();
#if BC_FALLBACK95
        if (fb95::hit) fb95::mark(out, fb95::hit);  // Policy fell back at its lean test: c is the cheapest legal step
#endif
        // BC_ECON_B0 (queen assertion): our queen's multi-step moves are QSK's (or queen-flee's) alone; any other
        // source is cut to its first step (and counted in the trace)
        if (BC_ECON_B0 && policy.isQueen() && !c.split && c.sprint.size() >= 2 && c.why.rfind("qsk-", 0) != 0 &&
            c.why != "queen-flee" && !c.flee.empty()) {
#ifdef BC_DEBUG
            out.log("qassert multi-step by " + c.why);
#endif
            c.sprint.resize(1);
            c.flee.resize(1);
            c.fleeEat.resize(1);
            c.fleePaid = 0;
        }
        // A last legality check (an illegal SPLIT kills the parent; an unpayable sprint dies at its first unpaid
        // step)
#if BC_UNIT_SLOT
        // BC_UNIT_SLOT: a non-queen leaves the last unit slot to the queen (the policy's split sites hold it; this is
        // the backstop)
        if (c.split && !policy.isQueen() && world.t.units >= init.unitLimit - 1) c.n = 0;
#endif
        if (c.split && (c.n < 2 || world.t.length - c.n < 2 || world.t.units >= init.unitLimit)) {
            c.split = false;
            c.dest = -1;
            for (int d = 0; d < 4 && c.dest < 0; d++) {
                int n = world.board.nb[world.head * 4 + d];
                if (n >= 0 && world.occ[n] < 0 && std::find(world.body.begin(), world.body.end(), n) == world.body.end())
                    c.dir = d, c.dest = n;
            }
        }
        if (!c.sprint.empty()) {
            int free = (world.t.length + 3) / 4, eats = 0;
            size_t ok = 0;
            for (size_t i = 0; i < c.sprint.size(); i++) {
                if (i < c.fleeEat.size() && c.fleeEat[i]) eats++;
                int paid = std::max(0, static_cast<int>(i) + 1 - free);
                if (world.t.length + eats - paid < 2) break;
                ok = i + 1;
            }
            if (ok < c.sprint.size()) {
                c.sprint.resize(ok);
                if (c.flee.size() > ok) c.flee.resize(ok), c.fleeEat.resize(ok);
                c.fleePaid = std::max(0, static_cast<int>(ok) - free);
            }
        }
        if (c.split) {
            out.split(c.n);
            world.body.resize(std::max(2, world.t.length - c.n));
        } else if (c.sprint.size() == 1 && !c.flee.empty()) {
            out.move(dirFromChar(c.sprint[0]));
            world.advanceBody(c.flee[0], c.fleeEat[0]);
        } else if (!c.sprint.empty()) {
            out.moveSeq(c.sprint);  // sprint trade: we die with the target
            if (!c.flee.empty()) world.advancePath(c.flee, c.fleeEat, c.fleePaid);  // flee / QSK: she lives
        } else {
            out.move(c.dir);
            if (c.dest >= 0) world.advanceBody(c.dest, c.eat);
            else {
                world.unknownStep = true;  // an unpaired portal: the landing shows next turn
#if BC_MN_FACTS
                // BC_MAPNET facts: remember the step (CLEARED next turn if we live) and announce it (teammates infer
                // a TRAP otherwise)
                world.probeFrom = world.head, world.probeDir = c.dir, world.probeRound = turn.round;
                world.announceCell = world.head, world.announceDir = c.dir, world.announceRound = turn.round;
                world.announceState = comms::G_PROBING;  // the only announcement a TRAP is inferred from
#endif
            }
        }
#if V10K_TRACE
        v10k::traceV10(out, turn.round, turn.length, c, kp);
#endif
#if BC_FALLBACK95 && BC_T13_BURN
        if (turn.round % 10 == 5) fb95::burn("rays");
#endif
        if (kp.active) {
            v10k::speak(world, c, *P, policy, out, turnStart);  // the library's KING / SELF facts plus the regular
                                                                // facts
        } else {
        // Situational sonar (Params::sitEnemyR): before sonarFrom only position facts, and only when they matter
        bool quiet = BC_QUIET && turn.round < mapParams.sonarFrom;
        bool situational = false;
        if (quiet && mapParams.sonarOn) {
            if (mapParams.sitEnemyR > 0)
                for (World::Seen const& s : world.others)
                    if (s.team != init.team && world.board.cheb(s.head, world.head) <= mapParams.sitEnemyR) situational = true;
            if (mapParams.sitKingEvery > 0 && turn.round % mapParams.sitKingEvery == 0 &&
                (world.know.longest.cell < 0 || world.know.longest.id == (init.id & 4095) || turn.length >= world.know.longest.len))
                situational = true;
            if (mapParams.sitPortal && world.crossedPortal) situational = true;
#if BC_MN_FACTS
            // BC_MAPNET facts: a probe announced, a landing cleared or a room seen whole must reach the team now
            if (world.announceRound == turn.round || world.clearedRound_ == turn.round || world.regionRound_ == turn.round)
                situational = true;
#if BC_MN_VALUE
            if (world.valueRound_ == turn.round) situational = true;  // BC_MAPNET_VALUE: a bed room new to us
#endif
            // the trap set: every turn while it is news (10 rounds), then every third turn per dragon
            if (BC_MN_TRAPSET && BC_MN_TRAPSET_SONAR && world.trapMaskNow_ && (turn.round - world.maskChangedRound_ <= 10 || (turn.round + (init.id & 4095)) % 3 == 0))
                situational = true;
#endif
            // BC_QUEEN: the enemy queen seen this turn, or a fresh tomb, breaks the quiet opening
            if (BC_QUEEN && (BC_QUEEN_HUNT || BC_QUEEN_MODES) &&
                (world.eq.round == turn.round || (world.eq.dead && turn.round - world.eq.deadRound <= 2) ||
                 (world.ourQueenDead && turn.round - world.ourQueenDeadRound <= 2)))
                situational = true;
        }
        world.crossedPortal = false;
#if BC_FALLBACK95
        // BC_FALLBACK95: a new clock read, after the turn's clock-dependent work (nothing below compares the clock);
        // past the line the rays and LISTEN are skipped
        bool fbRays = fb95::past(clockNow());
        if (fbRays) fb95::mark(out, "rays");
        if (!fbRays && mapParams.sonarOn && (!quiet || situational)) {
#else
        if (mapParams.sonarOn && (!quiet || situational)) {
#endif
            // Rotate which fact goes which way: a ray reaches whoever is in that line.
            TeamPlan const& tp = policy.team();
            int lead = (!tp.members.empty() && tp.leaderId == (init.id & 4095)) ? static_cast<int>(tp.members.size()) : 1;
            std::vector<comms::Fact> facts = world.tell(mapParams, quiet ? 1 : lead);
            if (quiet) {  // position facts only: where we, the enemy, the king and the portals are
                std::vector<comms::Fact> keep;
                for (comms::Fact const& f : facts)
                    if (f.type == comms::SELF || f.type == comms::ENEMY || f.type == comms::LONGEST || f.type == comms::PORTAL)
                        keep.push_back(f);
#if BC_MN_FACTS
                    else if (f.type == comms::GATE || f.type == comms::REGION || f.type == comms::TRAPSET)
                        keep.push_back(f);  // BC_MAPNET: map network facts go out in the quiet opening too
#endif
                facts.swap(keep);
            }
            if (policy.v9Loud() && !facts.empty()) facts.assign(4, facts[0]);  // BC_V9_EKING: the king is heard
#if BC_MN_FACTS
            // BC_MAPNET: on the crossing turn the PROBING rides the slot opposite our new facing, the one ray that
            // leaves from the tail - from outside the portal, where the team is; the head's three rays may be walled
            // in by a 1-tile room (in testing, probes that left from the head reached the team too rarely for traps
            // to be inferred)
            if (world.announceRound == turn.round && world.announceState == comms::G_PROBING && !facts.empty()) {
                int back = world.announceDir ^ 2;
                size_t slot = static_cast<size_t>(((back - turn.round % 4) % 4 + 4) % 4);  // the i with (i + round) % 4 == back
                for (size_t j = 0; j < facts.size(); j++)
                    if (facts[j].type == comms::GATE && (facts[j].val >> 2) == comms::G_PROBING) {
                        while (facts.size() <= slot) facts.push_back(facts[0]);
                        std::swap(facts[j], facts[slot]);
                        break;
                    }
            }
#endif
            for (size_t i = 0; i < facts.size() && i < 4; i++)
                out.sonar(static_cast<int>((i + static_cast<size_t>(turn.round)) % 4), comms::encode(facts[i], init.team, turn.round));
        }
        // king library LISTEN: after the search and the action, silent (no rays, no World writes, no heap)
#if BC_FALLBACK95
        if (!fbRays && V10K_ON && turn.round >= V10K_R0 && world.board.NC > V10K_MIN_TILES) v10k::listen(world, c, out);
#else
        if (V10K_ON && turn.round >= V10K_R0 && world.board.NC > V10K_MIN_TILES) v10k::listen(world, c, out);
#endif
        }
        out.log("r" + std::to_string(turn.round) + " " + c.why + " group=" + std::to_string(paramsForKey) +
                " depth=" + std::to_string(policy.depthReached()) +
                " ms=" + std::to_string(static_cast<int>((clockNow() - turnStart) * 1000)) +
                " L=" + std::to_string(turn.length) +
                " body=" + std::to_string(world.body.size()) + " " + policy.v9Info() + (BC_ECON_TRACE ? " " + policy.econTrace() : std::string()));
#if BC_FALLBACK95
#if BC_T13_BURN
        if (turn.round % 10 == 9) fb95::burn("flush");
        if (fb95::burned && BC_V11_TRACE) {
            out.buf += "LOG t13burn ";
            out.buf += fb95::burned;
            out.buf += '\n';
        }
#endif
        if (fb95::past(clockNow())) {  // BC_FALLBACK95: the last check point (no clock read after it): drop the rays,
                                       // keep the action
            fb95::dropRays(out);
            fb95::mark(out, "flush");
        }
#endif
#if BC_MN_ROOMS && BC_V11_TRACE
        if (world.mnRoomsCost >= 0) {  // BC_MN_ROOMS (trace builds): the cost of this turn's room recompute and the
                                       // rooms it found
            out.buf += "LOG mnrooms " + std::to_string(world.mnRoomsCost) + " " + std::to_string(world.mn.rooms.size()) + " " +
                       std::to_string(world.mn.statFull) + " " + std::to_string(world.mn.statAdded) + " " +
                       std::to_string(world.mn.statRe) + " " + std::to_string(world.mn.statSwept) + " " +
                       std::to_string(world.mn.statCut) + "\n";
            if (world.mnMiss) out.buf += "LOG mnmiss " + std::to_string(world.mnMiss) + "\n";
            world.mnRoomsCost = -1;
        }
#endif
        out.flush();
    }
    return 0;
}
