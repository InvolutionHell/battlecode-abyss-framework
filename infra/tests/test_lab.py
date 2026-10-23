import json
import os
import subprocess
import sys

import labconf

INFRA = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONFIG = os.path.join(INFRA, "config")


def job(tag, cand, opp, maps, repeat=1, pv=0, prio=5, state="pending", node=None, slots=0, submitted="01-01 00:00"):
    return {"id": f"{tag}/{cand}_vs_{opp}", "tag": tag, "cand": cand, "opp": opp, "repeat": repeat, "pv": pv,
            "maps": maps, "prio": prio, "state": state, "node": node, "pin": None, "slots": slots,
            "submitted": submitted}


SCHED = {"min_slots": 20, "big_games": 44, "big_min": 20, "push_workers": 2}


def test_plan_small_pairs_fill_the_freest_node(labenv):
    _, lab = labenv
    reg = {"jobs": [job("t", "a", "b", "m1 m2 m3 m4 m5")]}            # 10 games
    out = lab.plan(reg, {"n1": 30, "n2": 60}, SCHED)
    assert [(j["id"], n, s) for j, n, s in out] == [("t/a_vs_b", "n2", 10)]


def test_plan_big_pair_waits_and_reserves(labenv):
    _, lab = labenv
    big = job("t", "a", "b", " ".join(f"m{i}" for i in range(20)), repeat=2, prio=1)      # 80 games
    small = job("t", "c", "d", "m1 m2 m3 m4 m5", prio=5)                                    # 10 games
    busy = job("x", "e", "f", "m1", state="running", node="n1", slots=25)
    reg = {"jobs": [busy, big, small]}
    # n1 has 5 free, n2 has 10 free: the big pair cannot start (needs 20) and reserves n2 (the freest); the small one
    # needs min(20, 10) = 10 slots and may not take the reserved n2, and n1 has only 5
    out = lab.plan(reg, {"n1": 30, "n2": 10}, SCHED)
    assert out == []
    # with room, the big pair takes everything it can use on the freest node
    out = lab.plan({"jobs": [big, small]}, {"n1": 30, "n2": 60}, SCHED)
    assert [(j["cand"], n, s) for j, n, s in out] == [("a", "n2", 60), ("c", "n1", 10)]


def test_plan_priority_and_pause(labenv):
    _, lab = labenv
    late = job("t", "a", "b", "m1", prio=1, submitted="01-02 00:00")
    early = job("t", "c", "d", "m1", prio=5, submitted="01-01 00:00")
    out = lab.plan({"jobs": [early, late]}, {"n1": 2}, SCHED)
    assert [j["cand"] for j, _, _ in out] == ["a"]
    assert lab.plan({"jobs": [early]}, {"n1": 30}, SCHED, paused=True) == []


def test_wilson_and_summary_parsing(labenv, tmp_path):
    _, lab = labenv
    lo, hi = lab.wilson(50, 100)
    assert 0.40 < lo < 0.41 and 0.59 < hi < 0.60
    s = tmp_path / "summary.txt"
    s.write_text("abyss vs starter: 3W 1L 0D 0E over 4 games\n  default.map abyss vs starter: win r500\n")
    assert lab.parse_summary(str(s)) == (3, 1, 0, 0)
    c = tmp_path / "cpu.txt"
    c.write_text("default.map abyss vs starter: cand_max=71.5M opp_max=3.7M tle=0 opp_tle=2 secs=40\n"
                 "default.map starter vs abyss: cand_max=88.0M opp_max=3.7M tle=1 opp_tle=0 secs=41\n")
    assert lab.cpu_stats(str(c)) == (1, 88.0)


def test_launch_cmd_quotes_maps(labenv):
    _, lab = labenv
    j = job("t1", "a", "b", "default arena", repeat=2, pv=3)
    cmd = lab.launch_cmd(j, 12)
    assert "infra/lab/pair.sh t1 a b 12 2 'default arena' 3 0 - pv" in cmd
    assert cmd.endswith("echo launched")


def test_status_with_empty_registry(labenv):
    tmp, _ = labenv
    p = subprocess.run([sys.executable, os.path.join(INFRA, "lab", "lab.py"), "status"], capture_output=True, text=True,
                       env=dict(os.environ))
    assert p.returncode == 0
    assert "no pairs" in p.stdout
    assert not os.path.exists(tmp / "data" / "registry.json")


def test_submit_and_dry_run(labenv):
    tmp, lab = labenv
    lab.submit("t", ["a", "c"], ["b", "a"], 1, 0, 5, "default arena", None)
    reg = json.load(open(tmp / "data" / "registry.json"))
    assert sorted(j["id"] for j in reg["jobs"]) == ["t/a_vs_b", "t/c_vs_a", "t/c_vs_b"]   # a vs a is skipped
    assert lab.submit("t", ["a"], ["b"], 1, 0, 5, "default arena", None) == 0                 # no duplicates
    p = subprocess.run([sys.executable, os.path.join(INFRA, "lab", "lab.py"), "run", "--dry-run"],
                       capture_output=True, text=True, env=dict(os.environ))
    assert p.returncode == 0, p.stderr
    assert p.stdout.count("would start") == 3


def test_toml_subset_matches_tomllib():
    tomllib = __import__("tomllib") if sys.version_info >= (3, 11) else None
    for name in ("nodes.example.toml", "mapsets.example.toml"):
        text = open(os.path.join(CONFIG, name)).read()
        mine = labconf.parse_toml_subset(text)
        if tomllib:
            assert mine == tomllib.loads(text)
        assert mine


def test_mapsets_example_names_exist():
    sets, default = labconf.load_mapsets(os.path.join(CONFIG, "mapsets.example.toml"))
    assert default in sets
    official = set(sets["official"][0].split())
    for name, (maps, pv, _) in sets.items():
        assert set(maps.split()) <= official, name


def test_load_env_quoting(tmp_path, monkeypatch):
    env = tmp_path / "x.env"
    env.write_text("# comment\nLAB_T1=plain # trailing\nLAB_T2='$HOME/keep'\nLAB_T3=\"a b\"\nexport LAB_T4=4\n"
                   "LAB_T5=from-file\n")
    for k in ("LAB_T1", "LAB_T2", "LAB_T3", "LAB_T4"):
        monkeypatch.delenv(k, raising=False)
    monkeypatch.setenv("LAB_T5", "from-env")
    labconf.load_env(str(env))
    assert os.environ["LAB_T1"] == "plain"
    assert os.environ["LAB_T2"] == "$HOME/keep"
    assert os.environ["LAB_T3"] == "a b"
    assert os.environ["LAB_T4"] == "4"
    assert os.environ["LAB_T5"] == "from-env"


def test_rpath():
    import lab
    assert lab.rpath("~/abyss/lab/t") == "abyss/lab/t"
    assert lab.rpath("/abs/x") == "/abs/x"
