#!/usr/bin/env python3
"""Continuous, pre-registered sweep of compile-time macros on the lab: two stages, confirmation on fresh seeds.

One factor at a time on a carrier bot. Every round builds up to --per-round arms (infra/sweep/arms.py: the carrier +
`#undef / #define` lines appended to its ablation.hpp), pushes them to the nodes (lab.py push --bots) and submits:

  stage 1 / mirror   arms vs the mirror opponent (usually a frozen copy of your current release), spec stage1.mirror
  stage 1 / judges   arms vs every judged and every reported opponent, spec stage1.judges

The baseline is a renamed copy of the carrier (spec "baseline", default <prefix>base_c), played once on the same seeds
and cached: on a fixed set of paired seeds even an identical bot does not score 50% (the seed set carries its own
bias), so every arm is judged by its paired difference to that baseline, slot by slot (map, side, repeat), never
against 50%.

The rule is pre-registered in the spec (rule.*) and applied mechanically:

  pass      judged difference >= rule.judge_min   and   mirror difference >= rule.mirror_floor
            (the mirror is a safety item: no significant regression, about one paired standard error of its games;
            reported opponents are shown, never judged)
  no-effect no game outcome differs from the baseline's in any stage-1 pair: the knob did not change these games
            (or the build did not pick it up); such an arm is not confirmed
  out-cpu   the arm lost a turn to the CPU limit in any pair (a safety gate that results cannot outweigh)

A stage-1 pass is re-run on fresh seeds and the full sets (spec confirm.*) against a baseline run once on those seeds:
with many arms screened one factor at a time, the best stage-1 result is partly luck (the winner's curse), and only
a confirmation on games the screen never saw separates the two. Passing both stages makes the arm a candidate; merging
it into the bot is a separate, human decision.

  python3 infra/sweep/autosweep.py arms   --spec infra/sweep/sweep_spec.example.json   # the arms still to run
  python3 infra/sweep/autosweep.py run    --spec ... [--per-round 10] [--every 600] [--max-pending 12] [--once]
  python3 infra/sweep/autosweep.py report --spec ...                                    # the markdown table

The lab daemon (lab.py daemon) must be running: this script only builds, pushes, submits and reads results. State,
log and report live in $LAB_DATA/autosweep/<name>/.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
INFRA = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(INFRA, "lab"))
import labconf  # noqa: E402
import arms as armlib  # noqa: E402

labconf.load_env()
LAB = [sys.executable, os.path.join(INFRA, "lab", "lab.py")]
STAGE_ORDER = {"candidate": 0, "confirm": 1, "out-confirm": 2, "stage1": 3, "no-effect": 4, "out": 5, "out-cpu": 6}
DEFAULT_RULE = {"judge_min": 0, "mirror_floor": -8}
DEFAULT_PRIO = {"stage1": 3, "confirm": 2, "baseline": 2}


# ---------------------------------------------------------------- spec
def load_spec(path):
    with open(path) as f:
        return validate_spec(json.load(f), path)


def validate_spec(spec, where="sweep spec"):
    """Check a spec's required keys and fill in the default rule and priorities."""
    for key in ("name", "carrier", "prefix", "mirror", "stage1", "confirm", "arms"):
        if key not in spec:
            raise SystemExit(f"{where}: missing {key!r}")
    for stage in ("stage1", "confirm"):
        for part in ("mirror", "judges"):
            block = spec[stage].get(part)
            if not block or ("maps" not in block and "set" not in block) or "seed" not in block:
                raise SystemExit(f"{where}: {stage}.{part} needs \"maps\" or \"set\", and \"seed\"")
    if spec["stage1"]["mirror"]["seed"] == spec["confirm"]["mirror"]["seed"] or \
            spec["stage1"]["judges"]["seed"] == spec["confirm"]["judges"]["seed"]:
        raise SystemExit(f"{where}: the confirmation must use fresh seeds (seeds other than stage 1's)")
    spec["rule"] = dict(DEFAULT_RULE, **spec.get("rule", {}))
    spec["prio"] = dict(DEFAULT_PRIO, **spec.get("prio", {}))
    return spec


def resolve(path):
    """A path from the spec: absolute as given, else relative to the repository root."""
    path = os.path.expanduser(path)
    return path if os.path.isabs(path) else os.path.join(labconf.repo_root(), path)


def read_list(path):
    """Bot folder names, one per line; '#' starts a comment."""
    out = []
    with open(path) as f:
        for line in f:
            name = line.split("#", 1)[0].strip()
            if name:
                out.append(name)
    return out


def judges(spec):
    """The judged opponents, re-read on every call (a judge may be added while the sweep runs)."""
    if spec.get("judges_file"):
        try:
            return read_list(resolve(spec["judges_file"]))
        except OSError:
            pass
    return list(spec.get("judges", []))


def stage_maps(block):
    """The map names of a stage block ({"maps": [...]} or {"set": NAME} from the map sets file)."""
    if "maps" in block:
        return list(block["maps"])
    sets, _ = labconf.load_mapsets()
    if block["set"] not in sets:
        raise SystemExit(f"map set {block['set']!r} not in {labconf.mapsets_path()}")
    return sets[block["set"]][0].split()


def submit_args(block, prio):
    return ["--maps", *stage_maps(block), "--repeat", str(block.get("repeat", 1)), "--pv", "0", "--prio", str(prio),
            "--seed-base", str(block["seed"])]


# ---------------------------------------------------------------- the lab
class Lab:
    """The lab, through lab.py's command line (so the registry lock is always respected) and its files."""

    def __init__(self, dry=False):
        self.dry = dry

    def call(self, *args, timeout=1800):
        if self.dry:
            print("[dry] lab.py " + " ".join(f'"{a}"' if " " in a else a for a in args))
            return 0, ""
        p = subprocess.run(LAB + list(args), capture_output=True, text=True, timeout=timeout)
        return p.returncode, p.stdout + p.stderr

    def registry(self):
        path = os.path.join(labconf.data_dir(), "registry.json")
        try:
            with open(path) as f:
                return json.load(f)
        except (OSError, ValueError):
            return {"jobs": []}

    def busy(self):
        """tag -> number of pending / running pairs"""
        out = {}
        for j in self.registry()["jobs"]:
            if j["state"] in ("pending", "running"):
                out[j["tag"]] = out.get(j["tag"], 0) + 1
        return out

    def tle(self, tag, cand):
        """CPU / wall-clock failures of cand in a tag's finished pairs"""
        return sum(j.get("tle") or 0 for j in self.registry()["jobs"]
                   if j["tag"] == tag and j["cand"] == cand and j["state"] == "done")

    def summary(self, tag, cand, opp):
        """{(map, side, repeat): score} of a finished pair, None while it has no summary"""
        path = os.path.join(labconf.data_dir(), tag, f"{cand}_vs_{opp}", "orig", "summary.txt")
        if not os.path.exists(path):
            return None
        return armlib.outcomes_from_summary(path, cand)

    def submit(self, tag, cands, opps, block, prio):
        rc, out = self.call("submit", tag, " ".join(cands), " ".join(opps), *submit_args(block, prio))
        last = (out.strip().splitlines() or [""])[-1]
        return rc == 0, re.sub(r"^\[[^\]]*\]\s*(submit \S+:\s*)?", "", last)   # lab.py's own time stamp and prefix

    def push(self, names):
        if self.dry:
            self.call("push", "--bots", *names, "--workers", "4")
            return True, "dry run"
        nodes, _ = labconf.load_pool()
        rc, out = self.call("push", "--bots", *names, "--workers", "4")
        ok = out.count(": ok")
        return rc == 0 and ok == len(nodes), f"{ok}/{len(nodes)} nodes ok"


# ---------------------------------------------------------------- paired totals
def paired(lab, tag, cand, base_tag, base, opps, maps=None):
    """(difference, games, outcomes changed, cand score, base score) of cand against the baseline over their common
    slots against opps, or None while a pair is missing. maps: only these map names (the same map, side and repeat
    replay the same game under one seed base, so a subset of a larger baseline run is a valid baseline)."""
    keep = {m.split("/")[-1] for m in maps} if maps else None
    diff = games = changed = s_c = s_b = 0.0
    for o in opps:
        a, b = lab.summary(tag, cand, o), lab.summary(base_tag, base, o)
        if a is None or b is None:
            return None
        for k in set(a) & set(b):
            if keep is not None and k[0].rsplit(".map", 1)[0] not in keep:
                continue
            games += 1
            s_c += a[k]
            s_b += b[k]
            diff += a[k] - b[k]
            changed += a[k] != b[k]
    return diff, int(games), int(changed), s_c, s_b


# ---------------------------------------------------------------- the sweep
class Sweep:
    def __init__(self, spec, lab):
        self.spec = spec
        self.lab = lab
        self.dir = os.path.join(labconf.data_dir(), "autosweep", spec["name"])
        self.state_path = os.path.join(self.dir, "state.json")
        self.st = self.load()

    # tags
    def tag(self, kind, arm=None, rnd=None):
        n = self.spec["name"]
        return {"s1m": f"{n}_s{rnd}m", "s1j": f"{n}_s{rnd}j", "cm": f"{n}_cm_{arm}", "cj": f"{n}_cj_{arm}",
                "b1m": f"{n}_b1m", "b1j": f"{n}_b1j", "bcm": f"{n}_bcm", "bcj": f"{n}_bcj"}[kind]

    @property
    def base(self):
        """The same-seed baseline: spec "baseline", default <prefix>base_c."""
        return self.spec.get("baseline") or f"{self.spec['prefix']}base_c"

    # state
    def load(self):
        if os.path.exists(self.state_path):
            with open(self.state_path) as f:
                return json.load(f)
        return {"round": 0, "arms": {}, "done_specs": [], "baseline_built": False, "b1_opps": [], "bc_opps": [],
                "b1m": False, "bcm": False}

    def save(self):
        if self.lab.dry:
            return
        os.makedirs(self.dir, exist_ok=True)
        tmp = self.state_path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(self.st, f, indent=1)
        os.replace(tmp, self.state_path)

    def log(self, msg):
        line = f"[{time.strftime('%m-%d %H:%M')}] {msg}"
        print(line, flush=True)
        if not self.lab.dry:
            os.makedirs(self.dir, exist_ok=True)
            with open(os.path.join(self.dir, "autosweep.log"), "a") as f:
                f.write(line + "\n")

    def opponents(self):
        """(judged, reported) opponents now"""
        return judges(self.spec), list(self.spec.get("reported", []))

    # baselines
    def ensure_baseline(self):
        """Build and push the baseline once; submit its stage-1 pairs, and pairs for opponents added later."""
        if not self.st["baseline_built"]:
            # a renamed copy of the carrier, unless the spec names a baseline folder that already exists (it must then
            # be the carrier's code under another name)
            if not (self.spec.get("baseline") and os.path.isdir(os.path.join(labconf.bots_dir(), self.base))):
                armlib.build(self.spec["carrier"], [{}], self.spec["prefix"], baseline=False, names=[self.base])
            ok, msg = self.lab.push([self.base])
            self.log(f"baseline {self.base} built, push {msg}")
            if not ok:
                return False
            self.st["baseline_built"] = True
        s1, prio = self.spec["stage1"], self.spec["prio"]["baseline"]
        if not self.st["b1m"]:
            ok, msg = self.lab.submit(self.tag("b1m"), [self.base], [self.spec["mirror"]], s1["mirror"], prio)
            self.st["b1m"] = ok
            self.log(f"submit {self.tag('b1m')}: {msg}")
        js, rep = self.opponents()
        need = [o for o in js + rep if o not in self.st["b1_opps"]]
        if need:
            ok, msg = self.lab.submit(self.tag("b1j"), [self.base], need, s1["judges"], prio)
            if ok:
                self.st["b1_opps"] = sorted(set(self.st["b1_opps"]) | set(need))
            self.log(f"submit {self.tag('b1j')} ({' '.join(need)}): {msg}")
        self.save()
        return True

    def ensure_conf_baseline(self, opps):
        c, prio = self.spec["confirm"], self.spec["prio"]["baseline"]
        if not self.st["bcm"]:
            ok, msg = self.lab.submit(self.tag("bcm"), [self.base], [self.spec["mirror"]], c["mirror"], prio)
            self.st["bcm"] = ok
            self.log(f"submit {self.tag('bcm')}: {msg}")
        need = [o for o in opps if o not in self.st["bc_opps"]]
        if need:
            ok, msg = self.lab.submit(self.tag("bcj"), [self.base], need, c["judges"], prio)
            if ok:
                self.st["bc_opps"] = sorted(set(self.st["bc_opps"]) | set(need))
            self.log(f"submit {self.tag('bcj')} ({' '.join(need)}): {msg}")

    # rounds
    def remaining(self):
        return [(k, d) for k, d in armlib.spec_items(self.spec["arms"]) if k not in self.st["done_specs"]]

    def launch_round(self, per_round):
        todo = self.remaining()[:per_round]
        if not todo:
            return False
        r = self.st["round"] + 1
        names = [f"{self.spec['prefix']}{r}x{k}_c" for k in range(len(todo))]
        built = armlib.build(self.spec["carrier"], [d for _, d in todo], self.spec["prefix"], baseline=False, names=names)
        ok, msg = self.lab.push(names)
        self.log(f"round {r}: built {len(built)} arms, push {msg}")
        if not ok:
            self.log(f"round {r}: push failed, not submitted (retried next tick)")
            return False
        self.st["round"] = r
        js, rep = self.opponents()
        s1, prio = self.spec["stage1"], self.spec["prio"]["stage1"]
        _, m1 = self.lab.submit(self.tag("s1m", rnd=r), names, [self.spec["mirror"]], s1["mirror"], prio)
        _, m2 = self.lab.submit(self.tag("s1j", rnd=r), names, js + rep, s1["judges"], prio)
        self.log(f"submit {self.tag('s1m', rnd=r)}: {m1}; {self.tag('s1j', rnd=r)}: {m2}")
        for (key, defs), name in zip(todo, names):
            self.st["arms"][name] = {"defs": defs, "round": r, "stage": "stage1", "judges": js, "reported": rep,
                                     "mmaps": stage_maps(s1["mirror"]), "jmaps": stage_maps(s1["judges"])}
            self.st["done_specs"].append(key)
        self.save()
        self.log(f"round {r}: " + ", ".join(f"{n} {d}" for n, d in zip(names, [d for _, d in todo])))
        return True

    def pending_stage1(self, busy):
        pat = re.compile(re.escape(self.spec["name"]) + r"_s\d+[mj]$")
        return sum(c for t, c in busy.items() if pat.match(t))

    # judging
    def passes(self, m, j):
        rule = self.spec["rule"]
        return j >= rule["judge_min"] and m >= rule["mirror_floor"]

    def evaluate(self, busy):
        base, mirror = self.base, self.spec["mirror"]
        for name, a in self.st["arms"].items():
            r, js, rep = a["round"], a["judges"], a["reported"]
            if a["stage"] == "stage1":
                tm, tj = self.tag("s1m", rnd=r), self.tag("s1j", rnd=r)
                if busy.get(tm) or busy.get(tj) or busy.get(self.tag("b1m")) or busy.get(self.tag("b1j")):
                    continue
                m = paired(self.lab, tm, name, self.tag("b1m"), base, [mirror], a["mmaps"])
                j = paired(self.lab, tj, name, self.tag("b1j"), base, js, a["jmaps"])
                h = paired(self.lab, tj, name, self.tag("b1j"), base, rep, a["jmaps"]) if rep else (0, 0, 0, 0, 0)
                if m is None or j is None or h is None:
                    continue
                a.update(m=m[0], j=j[0], h=h[0], mg=m[1], jg=j[1], hg=h[1])
                if self.lab.tle(tm, name) + self.lab.tle(tj, name) > 0:
                    a["stage"] = "out-cpu"
                    self.log(f"{name} {a['defs']} out: CPU failures in stage 1")
                elif m[2] == 0 and j[2] == 0 and h[2] == 0:
                    a["stage"] = "no-effect"
                    self.log(f"{name} {a['defs']} no effect at stage 1 (no outcome changed in {m[1] + j[1] + h[1]} "
                             f"paired games) - not confirmed")
                elif self.passes(a["m"], a["j"]):
                    a["stage"] = "confirm"
                    self.ensure_conf_baseline(js + rep)
                    c, prio = self.spec["confirm"], self.spec["prio"]["confirm"]
                    self.lab.submit(self.tag("cm", arm=name), [name], [mirror], c["mirror"], prio)
                    self.lab.submit(self.tag("cj", arm=name), [name], js + rep, c["judges"], prio)
                    a["cmaps"], a["cjmaps"] = stage_maps(c["mirror"]), stage_maps(c["judges"])
                    self.log(f"{name} {a['defs']} passes stage 1 (mirror {a['m']:+g}, judged {a['j']:+g}, "
                             f"reported {a['h']:+g}) -> confirmation on fresh seeds")
                else:
                    a["stage"] = "out"
                    self.log(f"{name} {a['defs']} out at stage 1 (mirror {a['m']:+g}, judged {a['j']:+g}, "
                             f"reported {a['h']:+g})")
            elif a["stage"] == "confirm":
                tm, tj = self.tag("cm", arm=name), self.tag("cj", arm=name)
                if any(busy.get(t) for t in (tm, tj, self.tag("bcm"), self.tag("bcj"))):
                    continue
                m = paired(self.lab, tm, name, self.tag("bcm"), base, [mirror], a.get("cmaps"))
                j = paired(self.lab, tj, name, self.tag("bcj"), base, js, a.get("cjmaps"))
                h = paired(self.lab, tj, name, self.tag("bcj"), base, rep, a.get("cjmaps")) if rep else (0, 0, 0, 0, 0)
                if m is None or j is None or h is None:
                    continue
                a.update(cm=m[0], cj=j[0], ch=h[0], cmg=m[1], cjg=j[1], chg=h[1])
                if self.lab.tle(tm, name) + self.lab.tle(tj, name) > 0:
                    a["stage"] = "out-cpu"
                else:
                    a["stage"] = "candidate" if self.passes(a["cm"], a["cj"]) else "out-confirm"
                if a["stage"] == "candidate":
                    a["candidate_at"] = time.strftime("%Y-%m-%d %H:%M")
                self.log(f"{name} {a['defs']} confirmation: mirror {a['cm']:+g}, judged {a['cj']:+g}, "
                         f"reported {a['ch']:+g} -> {a['stage']}")
        self.save()

    # report
    def report(self):
        rule = self.spec["rule"]
        rows = ["| arm | defines | stage | mirror | judged | reported | confirm mirror | confirm judged | "
                "confirm reported | candidate at |", "|---|---|---|---|---|---|---|---|---|---|"]

        def cell(a, k, g):
            return f"{a[k]:+g} / {a[g]}" if k in a and a.get(g) else "-"
        for n, a in sorted(self.st["arms"].items(),
                           key=lambda kv: (STAGE_ORDER.get(kv[1]["stage"], 9), -(kv[1].get("m", -99) + kv[1].get("j", -99)))):
            defs = " ".join(f"{k}={v}" for k, v in a["defs"].items())
            rows.append(f"| {n} | {defs} | {a['stage']} | {cell(a, 'm', 'mg')} | {cell(a, 'j', 'jg')} | "
                        f"{cell(a, 'h', 'hg')} | {cell(a, 'cm', 'cmg')} | {cell(a, 'cj', 'cjg')} | "
                        f"{cell(a, 'ch', 'chg')} | {a.get('candidate_at', '-')} |")
        head = (f"# Sweep {self.spec['name']}: arms of {self.spec['carrier']} (baseline {self.base}, mirror "
                f"{self.spec['mirror']}; updated {time.strftime('%Y-%m-%d %H:%M')})\n\n"
                f"Cells: paired score difference to the baseline / paired games (win 1, draw 0.5). Rule (pre-registered): "
                f"judged >= {rule['judge_min']:+g} and mirror >= {rule['mirror_floor']:+g}; reported opponents are "
                f"shown only; no changed outcome at stage 1 = no-effect; any CPU failure = out-cpu. A stage-1 pass is "
                f"re-run on fresh seeds; passing both makes a candidate.\n\n")
        text = head + "\n".join(rows) + "\n"
        if not self.lab.dry:
            os.makedirs(self.dir, exist_ok=True)
            with open(os.path.join(self.dir, "report.md"), "w") as f:
                f.write(text)
        return text

    def tick(self, per_round, max_pending):
        """One pass; returns False when the spec is exhausted and nothing is in flight."""
        busy = self.lab.busy()
        if not self.ensure_baseline():
            return True
        self.evaluate(busy)
        launched = False
        if self.pending_stage1(busy) < max_pending:
            launched = self.launch_round(per_round)
        self.report()
        in_flight = any(a["stage"] in ("stage1", "confirm") for a in self.st["arms"].values())
        return launched or in_flight or bool(self.remaining())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["run", "report", "arms"])
    ap.add_argument("--spec", required=True, help="sweep spec JSON (see infra/sweep/sweep_spec.example.json)")
    ap.add_argument("--per-round", type=int, default=None, help="arms per round (default: the spec's per_round, else 10)")
    ap.add_argument("--every", type=int, default=600, help="seconds between passes")
    ap.add_argument("--max-pending", type=int, default=12,
                    help="start a new round while fewer stage-1 pairs of this sweep than this are queued or running")
    ap.add_argument("--once", action="store_true", help="one pass, then exit")
    ap.add_argument("--dry-run", action="store_true", help="print the lab.py calls instead of making them (arms are "
                    "still built locally; nothing is pushed, submitted or saved)")
    a = ap.parse_args()
    spec = load_spec(resolve(a.spec))
    per_round = a.per_round or int(spec.get("per_round", 10))
    sw = Sweep(spec, Lab(dry=a.dry_run))
    if a.cmd == "arms":
        rest = sw.remaining()
        print(f"{len(rest)} arms to run (of {len(armlib.spec_items(spec['arms']))}); judged opponents: "
              f"{' '.join(judges(spec)) or '-'}; reported: {' '.join(spec.get('reported', [])) or '-'}")
        for k, _ in rest:
            print("  " + k)
        return
    if a.cmd == "report":
        print(sw.report())
        return
    while True:
        try:
            more = sw.tick(per_round, a.max_pending)
            if not more:
                sw.log("spec exhausted and nothing in flight: stop")
                return
        except Exception as e:  # noqa: BLE001 - keep the loop alive, log and retry next pass
            sw.log(f"error: {e!r}")
        if a.once:
            return
        time.sleep(a.every)


if __name__ == "__main__":
    main()
