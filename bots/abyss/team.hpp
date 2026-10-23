#pragma once
// Local teams: dragons that see each other form a team that acts as one for its tasks, while each stays an
// independent individual in its local space.
//
//   members   me + teammates whose heads are in my view + teammates heard (SELF, last round)
//             within Params::teamRadius. Every member computes the same team from the same
//             facts, so no election messages are needed.
//   leader    the longest member (ties: the lowest id).
//   forage    the team's pearl targets are split greedily: the closest (member, target) pair
//             first. Path distances (BFS on my map) for me and teammates in view, torus
//             Manhattan + 2 only for teammates heard but out of sight; unreachable pairs are
//             skipped. (Manhattan distances for everyone sent dragons at pearls behind walls
//             and ate about 15% fewer pearls in tests.)
//             A target assigned to someone else is (mostly) left alone; this replaces the
//             "a closer friend takes it" rule that let two dragons yield to each other forever.
//   spread    beyond Params::teamMax members, the lowest-ranked ones leave to explore.
// The whole team (global level) is in comms.hpp: leaders announce TEAM facts, and dragons keep away from other local
// teams' areas, so the teams spread over the map.
#include <algorithm>
#include <vector>

#include "world.hpp"

namespace bc {

struct TeamPlan {
    struct Member { int id, cell, len; };
    std::vector<Member> members;   // members[0] is the leader
    int leaderId = -1;
    int myRank = 0;                // position of this dragon in the leader-first order
    bool detached = false;         // too many members: explore instead of foraging here
    int myTarget = -1;             // cell assigned to this dragon, -1 none
    std::vector<int> owner;        // target cell -> member id it is assigned to, -1 none

    static int torus(Board const& b, int a, int c) {
        int dx = std::abs(b.X(a) - b.X(c)), dy = std::abs(b.Y(a) - b.Y(c));
        return std::min(dx, b.W - dx) + std::min(dy, b.H - dy);
    }

    // dist(member id, member cell, target) -> steps, or INF when unknown / unreachable.
    template <typename Dist>
    void build(World const& w, Params const& p, std::vector<int> const& targets, Dist&& dist) {
        Board const& b = w.board;
        members.clear();
        int me = w.init.id & 4095;
        members.push_back({me, w.head, w.t.length});
        for (World::Seen const& s : w.others)
            if (s.team == w.init.team) members.push_back({s.id & 4095, s.head, s.visible});
        for (auto const& f : w.know.friends) {
            if (w.t.round - f.round > 1 || b.cheb(f.cell, w.head) > p.teamRadius) continue;
            bool known = false;
            for (Member const& m : members) known |= m.id == f.id;
            if (!known) members.push_back({f.id, f.cell, f.len});
        }
        std::sort(members.begin(), members.end(), [](Member const& a, Member const& c) {
            return a.len != c.len ? a.len > c.len : a.id < c.id;
        });
        leaderId = members.front().id;
        for (size_t i = 0; i < members.size(); i++)
            if (members[i].id == me) myRank = static_cast<int>(i);
        detached = myRank >= p.teamMax;

        // Greedy assignment over the members still foraging here.
        owner.assign(b.NC, -1);
        myTarget = -1;
        int active = std::min<int>(static_cast<int>(members.size()), p.teamMax);
        if (active < 2) return;  // alone: plain individual play
        struct Pair { int d, m, t; };
        std::vector<Pair> pairs;
        for (int m = 0; m < active; m++)
            for (int t : targets) {
                int d = dist(members[m].id, members[m].cell, t);
                if (d < INF) pairs.push_back({d, m, t});
            }
        std::sort(pairs.begin(), pairs.end(), [](Pair const& a, Pair const& c) {
            return a.d != c.d ? a.d < c.d : a.m != c.m ? a.m < c.m : a.t < c.t;
        });
        std::vector<char> busy(active, 0);
        int left = active;
        for (Pair const& pr : pairs) {
            if (left == 0) break;
            if (busy[pr.m] || owner[pr.t] >= 0) continue;
            busy[pr.m] = 1;
            owner[pr.t] = members[pr.m].id;
            left--;
            if (members[pr.m].id == me) myTarget = pr.t;
        }
    }
};

}  // namespace bc
