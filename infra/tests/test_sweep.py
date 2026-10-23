import importlib
import json
import os

import arms

INFRA = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def test_spec_items_and_arms_of():
    items = arms.spec_items([["BC_A", [1, 2]], {"defs": {"BC_B": 0, "BC_A": 3}}])
    assert items == [("BC_A=1", {"BC_A": "1"}), ("BC_A=2", {"BC_A": "2"}), ("BC_A=3+BC_B=0", {"BC_B": "0", "BC_A": "3"})]
    assert arms.arms_of([("X", ["1", "2"]), ("Y", ["a"])]) == [{"X": "1"}, {"X": "2"}, {"Y": "a"}]
    assert len(arms.arms_of([("X", ["1", "2"]), ("Y", ["a", "b"])], grid=True)) == 4


def test_outcomes_pair_by_slot(tmp_path):
    s = tmp_path / "summary.txt"
    s.write_text("c vs o: 2W 1L 1D 0E over 4 games\n"
                 "  default.map c vs o: win r500 (longer queen, 9 to 4)\n"
                 "  default.map o vs c: loss r200\n"
                 "  default.map c vs o: draw r500\n"
                 "  default.map o vs c: win r500\n")
    out = arms.outcomes_from_summary(str(s), "c")
    assert out == {("default", "A", 0): 1.0, ("default", "B", 0): 0.0, ("default", "A", 1): 0.5, ("default", "B", 1): 1.0}


def test_build_appends_defines(tmp_path, monkeypatch):
    bots = tmp_path / "bots"
    (bots / "car").mkdir(parents=True)
    (bots / "car" / "ablation.hpp").write_text("#define BC_X 1\n")
    (bots / "car" / "main.cpp").write_text("int main() {}\n")
    monkeypatch.setenv("XDG_CACHE_HOME", str(tmp_path / "cache"))
    out = arms.build("car", [{"BC_X": "0"}], "p", bots=str(bots))
    assert [n for n, _ in out] == ["pbase_c", "p0_c"]
    assert (bots / "pbase_c" / "ablation.hpp").read_text() == "#define BC_X 1\n"
    assert "#undef BC_X\n#define BC_X 0" in (bots / "p0_c" / "ablation.hpp").read_text()


class FakeLab:
    dry = False

    def __init__(self):
        self.summaries, self.tles, self.submits, self.pushed = {}, {}, [], []

    def busy(self):
        return {}

    def tle(self, tag, cand):
        return self.tles.get((tag, cand), 0)

    def summary(self, tag, cand, opp):
        return self.summaries.get((tag, cand, opp))

    def submit(self, tag, cands, opps, block, prio):
        self.submits.append((tag, tuple(cands), tuple(opps), block["seed"]))
        return True, "ok"

    def push(self, names):
        self.pushed += list(names)
        return True, "ok"


SLOTS = [("m1", "A", 0), ("m1", "B", 0), ("m2", "A", 0), ("m2", "B", 0)]


def res(*scores):
    return dict(zip(SLOTS, scores))


def test_autosweep_stages(labenv, monkeypatch):
    tmp, _ = labenv
    import autosweep
    importlib.reload(autosweep)
    monkeypatch.setattr(autosweep.armlib, "build", lambda carrier, defs, prefix, baseline=True, names=None, bots=None:
                        [(n, d) for n, d in zip(names or [], defs)])
    stage = {"maps": ["m1", "m2"], "repeat": 1}
    spec = {"name": "t", "carrier": "car", "prefix": "sw", "mirror": "mir", "judges": ["j1"], "reported": ["h1"],
            "stage1": {"mirror": dict(stage, seed=1), "judges": dict(stage, seed=2)},
            "confirm": {"mirror": dict(stage, seed=3), "judges": dict(stage, seed=4)},
            "rule": {"judge_min": 0, "mirror_floor": -8}, "arms": [["BC_X", [1, 2, 3]], ["BC_Y", [0]]]}
    spec = autosweep.validate_spec(spec)
    fl = FakeLab()
    sw = autosweep.Sweep(spec, fl)
    assert sw.tick(10, 12) is True
    tags = [s[0] for s in fl.submits]
    assert tags == ["t_b1m", "t_b1j", "t_s1m", "t_s1j"]
    assert fl.submits[1][2] == ("j1", "h1")
    names = ["sw1x0_c", "sw1x1_c", "sw1x2_c", "sw1x3_c"]
    assert list(sw.st["arms"]) == names
    base = res(1, 0, 1, 0)
    for opp, tag in (("mir", "t_b1m"), ("j1", "t_b1j"), ("h1", "t_b1j")):
        fl.summaries[(tag, "swbase_c", opp)] = base
    # arm 0 changes nothing, arm 1 wins one more judged game and loses one mirror game, arm 2 loses a judged game,
    # arm 3 would pass but lost a turn to the CPU limit
    plan = {"sw1x0_c": (base, base), "sw1x1_c": (res(0, 0, 1, 0), res(1, 1, 1, 0)),
            "sw1x2_c": (base, res(0, 0, 1, 0)), "sw1x3_c": (base, res(1, 1, 1, 0))}
    for n, (m, j) in plan.items():
        fl.summaries[("t_s1m", n, "mir")] = m
        fl.summaries[("t_s1j", n, "j1")] = j
        fl.summaries[("t_s1j", n, "h1")] = base
    fl.tles[("t_s1j", "sw1x3_c")] = 1
    fl.submits.clear()
    assert sw.tick(10, 12) is True
    st = {n: a["stage"] for n, a in sw.st["arms"].items()}
    assert st == {"sw1x0_c": "no-effect", "sw1x1_c": "confirm", "sw1x2_c": "out", "sw1x3_c": "out-cpu"}
    a1 = sw.st["arms"]["sw1x1_c"]
    assert (a1["m"], a1["j"], a1["h"]) == (-1, 1, 0)
    assert [s[0] for s in fl.submits] == ["t_bcm", "t_bcj", "t_cm_sw1x1_c", "t_cj_sw1x1_c"]
    assert {s[3] for s in fl.submits} == {3, 4}                    # fresh seeds for the confirmation
    for opp, tag in (("mir", "t_bcm"), ("j1", "t_bcj"), ("h1", "t_bcj")):
        fl.summaries[(tag, "swbase_c", opp)] = base
    fl.summaries[("t_cm_sw1x1_c", "sw1x1_c", "mir")] = base
    fl.summaries[("t_cj_sw1x1_c", "sw1x1_c", "j1")] = res(1, 1, 1, 1)
    fl.summaries[("t_cj_sw1x1_c", "sw1x1_c", "h1")] = base
    assert sw.pending_stage1({"t_s1m": 2, "t_s1j": 3, "t_b1m": 1, "t_cm_sw1x1_c": 1, "tt_s1m": 5}) == 5
    assert sw.tick(10, 12) is False                                # spec exhausted, nothing in flight
    assert sw.st["arms"]["sw1x1_c"]["stage"] == "candidate"
    assert sw.st["arms"]["sw1x1_c"]["cj"] == 2
    text = sw.report()
    assert "| sw1x1_c | BC_X=2 | candidate |" in text
    assert json.load(open(tmp / "data" / "autosweep" / "t" / "state.json"))["round"] == 1


def test_example_spec_loads(labenv):
    import autosweep
    importlib.reload(autosweep)
    spec = autosweep.load_spec(os.path.join(INFRA, "sweep", "sweep_spec.example.json"))
    assert spec["rule"]["mirror_floor"] == -8
    assert autosweep.judges(spec) == ["starter"]
    assert len(autosweep.stage_maps(spec["confirm"]["mirror"])) == 22
    assert len(arms.spec_items(spec["arms"])) == 7
