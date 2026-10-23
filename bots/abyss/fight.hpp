#pragma once
// Local fight search (FightSearch). Compiled in, but its weights are 0 in the shipped parameters, so it never changes
// a decision. The lookahead treats enemies as static danger fields; when the nearest enemy head is within
// fightRadius, this plays out our move, its reply and our next move (dragons act one at a time, in id order: after
// our move the enemy moves before we move again), with single steps and two-step sprints (in replay reviews most
// kills happened from 2-3 tiles away), under the game's rules: a head stepping onto kelp or any body dies, two heads
// meeting both die, a dead dragon of length L drops ceil(L/2) pearls. The enemy's body is known only as the tiles it
// occupies (not its order), so it stays put while its head moves (conservative). Values are in lengths: a kill earns
// the enemy's length, a death costs ours, the drops count half for the survivor. Enemy nodes blend its best reply
// (paranoid) with the mean of its replies (fightParanoia).
#include <algorithm>
#include <vector>

#include "world.hpp"

namespace bc {

struct FightSearch {
    Board const& b;
    std::vector<int> const& occ;      // tile -> other dragon id, -1 free
    int enemyId, enemyHead, enemyDir, enemyLen;
    std::vector<int> myBody;          // head first
    bool myTailKnown;
    double paranoia;
    long nodes = 0;

    // A move: up to two steps (the second costs a tail segment). dirs[1] < 0 for a single step.
    struct Move { int d0, d1; };

    static std::vector<Move> moves(int dir, bool sprint) {
        std::vector<Move> out;
        for (int d = 0; d < 4; d++) {
            if (d == (dir ^ 2)) continue;              // never back into the neck
            out.push_back({d, -1});
            if (sprint)
                for (int e = 0; e < 4; e++)
                    if (e != (d ^ 2)) out.push_back({d, e});
        }
        return out;
    }

    // Outcome of one side's move. Returns 0 alive, 1 died, 2 head-on (both die). newHead gets the final head tile.
    int stepMine(Move m, std::vector<int> const& body, int otherHead, std::vector<int>& nb, int& newHead) const {
        int head = body.front();
        int steps = m.d1 < 0 ? 1 : 2;
        int L = static_cast<int>(body.size());
        if (steps == 2 && L < 3) return -1;           // a sprint of 2 needs length 3
        int cur = head;
        std::vector<int> nbody = body;
        for (int s = 0; s < steps; s++) {
            int d = s == 0 ? m.d0 : m.d1;
            int nx = b.nb[cur * 4 + d];
            if (nx < 0) return -1;                    // kelp or an unknown portal: not a move we would make
            // the tail moves away this step (and one more segment per extra step)
            int keep = static_cast<int>(nbody.size()) - (myTailKnown ? 1 : 0) - s;
            for (int i = 0; i < keep && i < static_cast<int>(nbody.size()); i++)
                if (nbody[i] == nx) return 1;         // own body
            if (nx == otherHead) return 2;
            if (occ[nx] >= 0) return 1;               // any other body (the enemy's included)
            nbody.insert(nbody.begin(), nx);
            nbody.pop_back();
            if (s >= (BC_QUEEN ? (L + 3) / 4 : 1) && nbody.size() > 2) nbody.pop_back();  // rules 1.2.3: ceil(L/4) free steps
            cur = nx;
        }
        nb = nbody;
        newHead = cur;
        return 0;
    }

    int stepEnemy(Move m, int head, std::vector<int> const& myB, int& newHead) const {
        int steps = m.d1 < 0 ? 1 : 2;
        if (steps == 2 && enemyLen < 3) return -1;
        int cur = head;
        for (int s = 0; s < steps; s++) {
            int d = s == 0 ? m.d0 : m.d1;
            int nx = b.nb[cur * 4 + d];
            if (nx < 0) return -1;
            if (!myB.empty() && nx == myB.front()) return 2;
            for (size_t i = 1; i < myB.size(); i++)
                if (myB[i] == nx) return 1;
            if (occ[nx] >= 0 && occ[nx] != enemyId) return 1;
            if (occ[nx] == enemyId && nx != head) return 1;   // its own body
            cur = nx;
        }
        newHead = cur;
        return 0;
    }

    double myDeath() const { return -static_cast<double>(myBody.size()) - 0.5 * ((myBody.size() + 1) / 2); }
    double enemyDeath() const { return enemyLen + 0.5 * ((enemyLen + 1) / 2); }
    double bothDie() const { return enemyLen - static_cast<double>(myBody.size()); }

    // Mobility: free neighbours of a head (a trapped dragon is in trouble).
    int freeAround(int head, std::vector<int> const& myB, int otherHead) const {
        int n = 0;
        for (int d = 0; d < 4; d++) {
            int nx = b.nb[head * 4 + d];
            if (nx < 0 || occ[nx] >= 0 || nx == otherHead) continue;
            if (std::find(myB.begin(), myB.end(), nx) != myB.end()) continue;
            n++;
        }
        return n;
    }

    double leaf(std::vector<int> const& myB, int eHead) const {
        int mine = freeAround(myB.front(), myB, eHead);
        int theirs = freeAround(eHead, myB, myB.front());
        double v = 0.15 * (mine - theirs);
        if (mine == 0) v -= 0.5 * myB.size();         // boxed: most likely dead next turn
        if (theirs == 0) v += 0.5 * enemyLen;
        return v;
    }

    // Our move at depth `plies` left (odd plies: ours), enemy reply after.
    double ours(std::vector<int> const& myB, int eHead, int eDir, int plies) {
        double best = -1e9;
        int myDir = myB.size() >= 2 ? dirBetween(myB[1], myB[0]) : -1;
        for (Move m : moves(myDir, myB.size() >= 3)) {
            nodes++;
            std::vector<int> nb;
            int nh = -1;
            int r = stepMine(m, myB, eHead, nb, nh);
            if (r < 0) continue;
            double v = r == 1 ? myDeath() : r == 2 ? bothDie() : (plies > 1 ? theirs(nb, eHead, eDir, plies - 1) : leaf(nb, eHead));
            best = std::max(best, v);
        }
        return best < -1e8 ? myDeath() : best;         // no legal move: we die
    }

    double theirs(std::vector<int> const& myB, int eHead, int eDir, int plies) {
        double worst = 1e9, sum = 0;
        int n = 0;
        for (Move m : moves(eDir, enemyLen >= 3)) {
            nodes++;
            int nh = -1;
            int r = stepEnemy(m, eHead, myB, nh);
            if (r < 0) continue;
            double v = r == 1 ? enemyDeath() : r == 2 ? bothDie()
                     : (plies > 1 ? ours(myB, nh, m.d1 < 0 ? m.d0 : m.d1, plies - 1) : leaf(myB, nh));
            worst = std::min(worst, v);
            sum += v;
            n++;
        }
        if (n == 0) return enemyDeath();                // it has no move: it dies
        return paranoia * worst + (1.0 - paranoia) * sum / n;
    }

    int dirBetween(int from, int to) const {
        for (int d = 0; d < 4; d++)
            if (b.nb[from * 4 + d] == to) return d;
        return -1;
    }

    // Value of our single-step move d (then the enemy, then us: `plies` in all, >= 2).
    double rootStep(int d, int plies) {
        std::vector<int> nb;
        int nh = -1;
        int r = stepMine({d, -1}, myBody, enemyHead, nb, nh);
        if (r < 0) return 0.0;
        if (r == 1) return myDeath();
        if (r == 2) return bothDie();
        return theirs(nb, enemyHead, enemyDir, plies - 1);
    }
};

}  // namespace bc
