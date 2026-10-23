"""The opponent league of the parameter tuner.

Mode "all": the league grows with the search. Members are fixed reference bots (--seed-members), extra opponent bot
folders picked up from a directory as they appear (--opponents), the island's own hall-of-fame snapshots (its CMA mean
every few generations), other islands' snapshots from a shared folder, and the current champion when there is one.

Mode "champions": the only opponents are the current champion, sparring partners (--sparring: always drawn, so a
behaviour the champions rarely show - say head-on trades - stays in the training signal) and the historical champions
close to the current one (<evo>/league/allow.json when present, else the four most recent).

Opponents are drawn by prioritized fictitious self-play (PFSP): members that beat recent candidates more often are
drawn more, so the search keeps working on its current weaknesses instead of farming easy wins.
"""
import glob
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import safeio  # noqa: E402


def is_bot(path):
    return os.path.isfile(os.path.join(path, "bot.toml"))


def usable(m):
    """Members whose folder is gone, that never finish a game (a snapshot copied mid-write does not compile) or that
    were retired (a RETIRED file in the folder) are never drawn."""
    return os.path.isdir(m["dir"]) and not m.get("broken") and not os.path.exists(os.path.join(m["dir"], "RETIRED"))


class League:
    def __init__(self, path):
        self.path = path
        self.members = []
        self.current_champion = None
        if os.path.exists(path):
            try:
                with open(path) as f:
                    self.members = json.load(f)["members"]
            except (ValueError, KeyError) as e:  # truncated by a full disk: rebuild (the statistics are lost)
                print(f"league file unreadable ({e}), rebuilding it", flush=True)
                self.members = []

    def save(self):
        safeio.write_json(self.path, {"members": self.members}, indent=1, ensure_ascii=False)

    def has(self, name):
        return any(m["name"] == name for m in self.members)

    def add(self, name, path, kind, note=""):
        if self.has(name):
            return False
        self.members.append({"name": name, "dir": os.path.abspath(path), "kind": kind, "note": note,
                             "games": 0, "wins": 0.0, "recent": []})
        return True

    def refresh_folder(self, folder, kind="extra"):
        """Bot folders that appeared in `folder` since the last call."""
        added = 0
        if not folder or not os.path.isdir(folder):
            return 0
        for d in sorted(os.listdir(folder)):
            full = os.path.join(folder, d)
            if is_bot(full) and self.add(d, full, kind):
                added += 1
        return added

    def refresh_shared(self, shared_dir, own_prefix):
        """Other islands' hall-of-fame snapshots from the shared folder. A snapshot counts once its READY marker exists
        (written last, after an atomic rename), so half-copied folders are never built."""
        added = 0
        if not shared_dir or not os.path.isdir(shared_dir):
            return 0
        for d in sorted(os.listdir(shared_dir)):
            full = os.path.join(shared_dir, d)
            if (own_prefix and d.startswith(own_prefix + "_")) or not os.path.isfile(os.path.join(full, "READY")):
                continue
            if self.add(d, full, "peer", note="another island's CMA mean"):
                added += 1
        return added

    def refresh_champions(self, champion_dirs):
        """Every crowned champion folder (c<n>_<name> under each champion dir) as kind "champion-history"."""
        added = 0
        for cdir in champion_dirs or []:
            run = os.path.basename(os.path.dirname(os.path.abspath(cdir)))
            for full in sorted(glob.glob(os.path.join(cdir, "c[0-9]*_*"))):
                if is_bot(full) and self.add(f"{run}_{os.path.basename(full)}", full, "champion-history",
                                             note="historical champion"):
                    added += 1
        return added

    def refresh_sparring(self, dirs):
        """Sparring partners (kind "sparring"): always drawn in champions mode."""
        added = 0
        for d in dirs or []:
            if is_bot(d) and self.add("spar_" + os.path.basename(os.path.normpath(d)), d, "sparring",
                                      note="sparring partner"):
                added += 1
        return added

    def champion_pool(self, allow_path):
        """Historical champions that may be drawn: those listed in allow.json when it was written for the current
        champion, else the four most recent (by run, then crown number)."""
        cur_dir = next((m["dir"] for m in self.members if m["name"] == self.current_champion), None)
        hist = [m for m in self.members if m["kind"] == "champion-history" and usable(m) and m["dir"] != cur_dir]
        allow = None
        try:
            with open(allow_path) as f:
                a = json.load(f)
            if a.get("champion") == (self.current_champion or "").replace("champ_", "", 1):
                allow = set(a.get("members", []))
        except (OSError, ValueError, TypeError):
            pass
        if allow is not None:
            return [m for m in hist if m["name"] in allow]

        def order(m):
            r = re.match(r"(.+?)_c(\d+)_", m["name"])
            return (r.group(1), int(r.group(2))) if r else ("", 0)
        return sorted(hist, key=order)[-4:]

    def win_rate_vs_candidates(self, m, window=60):
        recent = m["recent"][-window:]
        return (sum(recent) + 1) / (len(recent) + 2)  # Laplace: new members start at 50%

    def set_champion(self, champion_dir):
        """The current champion (<champion_dir>/current.json, written by champion.py) joins as kind "champion";
        earlier champions stay in the league. Returns its name, or None."""
        cur = os.path.join(champion_dir or "", "current.json")
        if not champion_dir or not os.path.exists(cur):
            return None
        with open(cur) as f:
            c = json.load(f)
        name = "champ_" + c["name"]
        self.add(name, os.path.join(champion_dir, c["dir"]), "champion", note=f"crowned {c.get('at', '?')}")
        self.current_champion = name
        return name

    def sample(self, k, rng, mode="all", allow_path=None):
        """k distinct opponents, PFSP-weighted; the current champion is always in. Mode "all" also always takes the
        newest hall-of-fame member; mode "champions" draws only from the champion, sparring and champion_pool()."""
        if not self.members:
            return []
        weights = {m["name"]: self.win_rate_vs_candidates(m) ** 2 + 0.05 for m in self.members}
        chosen = []
        champ = self.current_champion
        if champ and self.has(champ):
            chosen.append(next(m for m in self.members if m["name"] == champ))
        chosen = [m for m in chosen if usable(m)]
        if mode == "champions":
            spar = [m for m in self.members if m["kind"] == "sparring" and usable(m)]
            room = max(0, k - len(chosen) - 1)      # leave at least one slot for a historical champion
            if len(spar) > room:                    # more partners than slots: a random subset per candidate
                spar = rng.sample(spar, room)
            chosen += spar
            pool = self.champion_pool(allow_path)
            while len(chosen) < k and pool:
                pick = rng.choices(pool, weights=[weights[m["name"]] for m in pool])[0]
                chosen.append(pick)
                pool.remove(pick)
            return chosen
        hof = [m for m in self.members if m["kind"] == "hof" and usable(m)]
        if hof and hof[-1] not in chosen:
            chosen.append(hof[-1])
        pool = [m for m in self.members if m not in chosen and usable(m)]
        while len(chosen) < min(k, len(self.members)) and pool:
            pick = rng.choices(pool, weights=[weights[m["name"]] for m in pool])[0]
            chosen.append(pick)
            pool.remove(pick)
        return chosen[:max(k, 1)]

    def mark_broken(self, name):
        for m in self.members:
            if m["name"] == name and not m.get("broken"):
                m["broken"] = True
                return True
        return False

    def record(self, name, member_score):
        """member_score: 1 the member won, 0.5 draw, 0 the candidate won."""
        for m in self.members:
            if m["name"] == name:
                m["games"] += 1
                m["wins"] += member_score
                m["recent"] = (m["recent"] + [member_score])[-200:]
