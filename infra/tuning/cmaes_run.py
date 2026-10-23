#!/usr/bin/env python3
"""CMA-ES over a bot's Params against a self-evolving league (the optional, legacy tuning backend).

  python3 infra/tuning/cmaes_run.py --base abyss --space infra/tuning/space.example.json --out runs/isl0 \
      --jobs 16 --budget 0.015 --paired
  (needs the `cma` package; pycapnp + tools/ add the length-margin term: uv run --with cma --with pycapnp ...)

Each generation CMA-ES proposes --popsize candidates (the dimensions of --space, see space.py); every candidate is
staged as the base bot with its tuned_params.hpp and plays --opps league members on --maps-per-opp maps, both sides,
at a reduced search budget (--budget seconds per turn; the final build keeps its own). Fitness is the mean game score:
win 1 / draw 0.5 / loss 0 plus a small length-margin term (0.1 tanh((longest + 0.1 total) / 20)) that breaks ties
between equally scored candidates.

--paired uses common random numbers: every candidate of a generation plays the same opponents on the same maps with
the same seeds. Without it each candidate draws its own opponents, maps and seeds, and a 20-game ranking is mostly
noise; the mean then drifts. Every --freeze-every generations the CMA mean joins the league as a hall-of-fame member
(and, with --shared-hof, other islands' leagues): the opponents evolve with the search.

Resumable: the state lives in --out (cma.pkl, league.json). Outputs: best.json + tuned_params.hpp (the best single
candidate so far), mean.json + mean_params.hpp (the CMA mean: far less noisy than the best candidate - promote this),
gens.jsonl (one line per generation), hof/ (frozen means).

Islands: several runs with different --seed / --space / --base share hall-of-fame snapshots through --shared-hof
(infra/pbs/islands.pbs runs one island per PBS job). A tuned mean is a candidate, not a release: test it like any
other change (paired census against the baseline at the full budget, CPU census) before it replaces anything.
"""
import argparse
import json
import math
import os
import pickle
import random
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
INFRA = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(INFRA, "lab"))
import labconf  # noqa: E402
import safeio  # noqa: E402
from space import Space  # noqa: E402
from evaluate import Batch  # noqa: E402
from league import League  # noqa: E402

labconf.load_env()


def game_score(winner, our_side, longest, total):
    sign = 1 if our_side == "A" else -1
    base = 0.5 if winner is None else (1.0 if winner == our_side else 0.0)
    return base + 0.1 * math.tanh(sign * (longest + 0.1 * total) / 20.0)


def pick_maps(rng, n, maps):
    """n distinct maps (fewer if the list is shorter)."""
    pool = list(dict.fromkeys(maps))
    return rng.sample(pool, min(n, len(pool)))


def full_mean(es, fixed, n):
    """The CMA mean in all n dimensions: with fixed_variables, es.mean holds only the free ones (in order)."""
    free = iter(es.mean)
    return [fixed[i] if i in fixed else next(free) for i in range(n)] if len(es.mean) < n else list(es.mean)


def bot_path(bots, name):
    p = os.path.expanduser(name)
    return p if os.path.isabs(p) or os.path.isdir(p) and os.sep in name else os.path.join(bots, name)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", required=True, help="bot folder (under the bots workspace) whose Params are tuned")
    ap.add_argument("--space", required=True, help="search space JSON (space.py)")
    ap.add_argument("--out", required=True, help="run folder (state, outputs; resumable)")
    ap.add_argument("--bots-dir", default=None, help="unswbc workspace (default $BC_BOTS_DIR or <repo>/bots)")
    ap.add_argument("--seed-members", nargs="*", default=["starter"], help="fixed reference opponents (mode all)")
    ap.add_argument("--opponents", default=None, help="folder whose bot folders join the league as they appear")
    ap.add_argument("--gens", type=int, default=40)
    ap.add_argument("--popsize", type=int, default=16)
    ap.add_argument("--sigma", type=float, default=0.15, help="initial step size in the [0, 1]-scaled space")
    ap.add_argument("--opps", type=int, default=4, help="league members per candidate")
    ap.add_argument("--maps-per-opp", type=int, default=2)
    ap.add_argument("--maps", nargs="*", default=None, help="map names (default: the default map set)")
    ap.add_argument("--set", default=None, help="map set from the map sets file instead of --maps")
    ap.add_argument("--paired", action="store_true", help="common random numbers across the generation's candidates")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    ap.add_argument("--budget", type=float, default=0.015, help="search seconds per turn during tuning")
    ap.add_argument("--freeze-every", type=int, default=4)
    ap.add_argument("--scratch", default=None, help="where per-generation build folders go (default: the temp dir)")
    ap.add_argument("--seed", type=int, default=1, help="CMA-ES and opponent sampling seed")
    ap.add_argument("--island", default=None, help="name for shared hall-of-fame snapshots (default: --out's name)")
    ap.add_argument("--shared-hof", default=None, help="folder of hall-of-fame snapshots shared by all islands")
    ap.add_argument("--champion-dir", default=None, help="champion folder (default: <shared-hof>/../champion)")
    ap.add_argument("--champion-dirs", nargs="*", default=None,
                    help="champion folders whose historical champions join the league (default: --champion-dir)")
    ap.add_argument("--league", choices=["all", "champions"], default="all")
    ap.add_argument("--sparring", nargs="*", default=None, help="sparring partners, drawn for every candidate in "
                    "champions mode")
    ap.add_argument("--set-value", nargs="*", default=[], metavar="DIM=VALUE", help="start point overrides (new runs)")
    ap.add_argument("--fix", nargs="*", default=[], metavar="DIM=VALUE", help="pinned dimensions (new runs)")
    ap.add_argument("--init-from", default=None, help="mean.json of an earlier run: start point for new runs")
    ap.add_argument("--tune-knobs", nargs="*", default=None, help="only these knobs move (the rest stay at the start)")
    ap.add_argument("--tune-groups", nargs="*", type=int, default=None, help="only these map groups' dimensions move")
    args = ap.parse_args()
    import cma  # after argparse, so --help works without the package

    bots = os.path.abspath(args.bots_dir or labconf.bots_dir())
    base_dir = bot_path(bots, args.base)
    island = args.island or os.path.basename(os.path.normpath(args.out))
    if args.shared_hof:
        evo_root = os.path.dirname(os.path.normpath(args.shared_hof))
        args.champion_dir = args.champion_dir or os.path.join(evo_root, "champion")
    if args.maps:
        maps = args.maps
    else:
        sets, default = labconf.load_mapsets()
        name = args.set or default
        if name not in sets:
            raise SystemExit(f"map set {name!r} not in {labconf.mapsets_path()}")
        maps = sets[name][0].split()

    os.makedirs(os.path.join(args.out, "hof"), exist_ok=True)
    sp = Space.for_bot(args.space, base_dir)
    names = sp.names()
    league = League(os.path.join(args.out, "league.json"))
    champion_dirs = args.champion_dirs if args.champion_dirs is not None else [d for d in [args.champion_dir] if d]
    allow_path = os.path.join(os.path.dirname(os.path.normpath(args.champion_dir)), "league", "allow.json") \
        if args.champion_dir else None
    for m in (args.seed_members if args.league == "all" else []):
        league.add(os.path.basename(m.rstrip("/")), bot_path(bots, m), "reference")
    hof_dir = os.path.join(args.out, "hof")
    for d in sorted(os.listdir(hof_dir), key=lambda x: int(x.rsplit("_g", 1)[-1]) if x.rsplit("_g", 1)[-1].isdigit() else 0):
        if os.path.isfile(os.path.join(hof_dir, d, "tuned_params.hpp")):
            league.add(d, os.path.join(hof_dir, d), "hof", note="restored")
    league.save()

    start = sp.defaults()
    if args.init_from and os.path.exists(args.init_from):
        with open(args.init_from) as f:
            start = sp.carry(start, json.load(f).get("values", {}))
    fixed_vals = {}
    for kv in args.set_value + args.fix:
        k, v = kv.split("=", 1)
        if k not in start:
            raise SystemExit(f"unknown dimension {k} (space {args.space})")
        start[k] = float(v)
        if kv in args.fix:
            fixed_vals[k] = float(v)
    x0 = sp.encode(start)

    def fixed_dims(x):
        fixed = {i: x[i] for i, n in enumerate(names) if n in fixed_vals}
        if args.tune_groups is not None:
            fixed.update({i: x[i] for i, n in enumerate(names)
                          if sp.group_of(n) is not None and sp.group_of(n) not in args.tune_groups})
        if args.tune_knobs is not None:
            fixed.update({i: x[i] for i, n in enumerate(names) if sp.knob(n) not in args.tune_knobs})
        return fixed

    def new_es(x, sigma, seed):
        opts = {"bounds": [0, 1], "popsize": args.popsize, "seed": seed, "verbose": -9}
        if len(names) > 500:  # separable CMA-ES: linear in the dimension
            opts["CMA_diagonal"] = True
        fx = fixed_dims(x)
        if fx:
            opts["fixed_variables"] = fx
        return cma.CMAEvolutionStrategy(x, sigma, opts), fx

    ckpt = os.path.join(args.out, "cma.pkl")
    fixed = fixed_dims(x0)
    if os.path.exists(ckpt):
        with open(ckpt, "rb") as f:
            es, gen0, best = pickle.load(f)
        if es.N != len(names) - len(fixed):
            # the space changed (a knob added or removed): a new CMA from the old mean in the new space, same sigma
            mean_path = os.path.join(args.out, "mean.json")
            prev = json.load(open(mean_path)).get("values", {}) if os.path.exists(mean_path) else best.get("values", {})
            x0 = sp.encode(sp.carry(start, prev))
            sigma = min(args.sigma, float(es.sigma))
            print(f"search space changed ({es.N} -> {len(names) - len(fixed)} dims): new CMA from the old mean, "
                  f"sigma {sigma:.3f}, generation {gen0}", flush=True)
            es, fixed = new_es(x0, sigma, args.seed + gen0)
            best = {"fitness": -1, "values": sp.decode(x0), "gen": gen0 - 1}
        else:
            print(f"resuming at generation {gen0}", flush=True)
    else:
        es, fixed = new_es(x0, args.sigma, args.seed)
        gen0, best = 0, {"fitness": -1, "values": sp.decode(x0), "gen": -1}
        safeio.write_json(os.path.join(args.out, "config.json"),
                          {"args": vars(args), "dims": len(names), "fixed": sorted(names[i] for i in fixed)}, indent=1)

    for gen in range(gen0, gen0 + args.gens):
        t0 = time.time()
        if args.league == "champions":
            new = peers = 0
            league.refresh_champions(champion_dirs)
            league.refresh_sparring([bot_path(bots, d) for d in (args.sparring or [])])
        else:
            new = league.refresh_folder(args.opponents)
            peers = league.refresh_shared(args.shared_hof, island)
        champ = league.set_champion(args.champion_dir)
        rng = random.Random(gen if args.seed == 1 else args.seed * 100003 + gen)
        xs = es.ask()
        cands = [sp.decode(x) for x in xs]
        batch = Batch(bots, args.jobs, args.budget, root=args.scratch)
        try:
            for i, vals in enumerate(cands):
                batch.add_bot(f"g{gen}_c{i}", base_dir, sp.header(vals))
            if args.paired:   # one opponent set, one map draw and one seed per slot for the whole generation
                shared = league.sample(args.opps, rng, mode=args.league, allow_path=allow_path)
                slots = [(m, mp, rng.getrandbits(32)) for m in shared for mp in pick_maps(rng, args.maps_per_opp, maps)]
                opp_for = [shared for _ in cands]
            else:
                opp_for = [league.sample(args.opps, rng, mode=args.league, allow_path=allow_path) for _ in cands]
            for m in {m["name"]: m for ms in opp_for for m in ms}.values():
                batch.add_bot("L_" + m["name"], m["dir"])
            batch.build_all()
            games, meta = [], []
            for i, opps in enumerate(opp_for):
                if args.paired:
                    todo = slots
                else:
                    todo = [(m, mp, rng.getrandbits(32)) for m in opps for mp in pick_maps(rng, args.maps_per_opp, maps)]
                for m, mp, seed in todo:
                    games.append((f"g{gen}_c{i}", "L_" + m["name"], mp, seed))
                    meta.append((i, m["name"], "A"))
                    games.append(("L_" + m["name"], f"g{gen}_c{i}", mp, seed))
                    meta.append((i, m["name"], "B"))
            results = batch.run(games)
        finally:
            batch.close()

        scores = [[] for _ in cands]
        errors = 0
        per_member = {}
        for (i, member, side), (winner, longest, total) in zip(meta, results):
            done_, err_ = per_member.get(member, (0, 0))
            per_member[member] = (done_ + (winner != "E"), err_ + (winner == "E"))
            if winner == "E":   # did not finish: no information
                errors += 1
                continue
            s = game_score(winner, side, longest, total)
            scores[i].append(s)
            league.record(member, 1.0 - min(1.0, max(0.0, round(s))))
        fitness = [sum(s) / len(s) if s else 0.0 for s in scores]
        for member, (done_, err_) in per_member.items():   # never finishes a game: a broken snapshot
            if done_ == 0 and err_ >= 4 and league.mark_broken(member):
                print(f"league member {member} never finished a game ({err_} errors): marked broken", flush=True)
        es.tell(xs, [-f for f in fitness])
        top = max(range(len(cands)), key=lambda i: fitness[i])
        if fitness[top] > best["fitness"]:
            best = {"fitness": fitness[top], "values": cands[top], "gen": gen}
            safeio.write_json(os.path.join(args.out, "best.json"), best, indent=1)
            safeio.write_text(os.path.join(args.out, "tuned_params.hpp"), sp.header(cands[top]))
        mean_vals = sp.decode(full_mean(es, fixed, len(names)))
        if (gen + 1) % args.freeze_every == 0:
            dest = os.path.join(hof_dir, f"hof_g{gen + 1}")
            if not os.path.exists(dest):
                shutil.copytree(base_dir, dest, ignore=shutil.ignore_patterns(".unswbc-build"))
                safeio.write_text(os.path.join(dest, "tuned_params.hpp"), sp.header(mean_vals))
            league.add(f"hof_g{gen + 1}", dest, "hof", note=f"CMA mean after generation {gen + 1}")
            if args.shared_hof:
                share = os.path.join(args.shared_hof, f"{island}_g{gen + 1}")
                if not os.path.exists(share):
                    tmp = share + ".tmp"
                    shutil.rmtree(tmp, ignore_errors=True)
                    shutil.copytree(dest, tmp)
                    safeio.write_text(os.path.join(tmp, "READY"), f"{island} generation {gen + 1} {args.base}\n")
                    os.rename(tmp, share)
        safeio.write_json(os.path.join(args.out, "mean.json"), {"gen": gen, "values": mean_vals}, indent=1)
        safeio.write_text(os.path.join(args.out, "mean_params.hpp"), sp.header(mean_vals))
        league.save()
        safeio.write_pickle(ckpt, (es, gen + 1, best))
        line = {"gen": gen, "secs": round(time.time() - t0), "games": len(games),
                "fit_mean": round(sum(fitness) / len(fitness), 4), "fit_max": round(fitness[top], 4),
                "best": round(best["fitness"], 4), "sigma": round(es.sigma, 4), "league": len(league.members),
                "new_opponents": new, "new_peers": peers, "champion": champ, "errors": errors}
        safeio.append_line(os.path.join(args.out, "gens.jsonl"), json.dumps(line))
        print(json.dumps(line), flush=True)


if __name__ == "__main__":
    main()
