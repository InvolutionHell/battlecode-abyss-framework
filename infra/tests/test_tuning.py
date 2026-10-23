import json
import os
import random

import pytest

import space
from league import League

INFRA = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(INFRA)
ABYSS = os.path.join(REPO, "bots", "abyss")

COMMON = """
struct Params {
    double gamma = 0.8;
    int squadSize = 3;
    double wDanger = 1.0;
    double other = 2.5;
};
"""
TUNED = """// GENERATED
#pragma once
namespace bc {
inline void applyTuned(Params& p, int group, int phase) {
    if (group == 0 && phase == 0) {
        p.gamma = 0.75;
        p.other = 9.0;
    }
    if (group == 0 && phase == 1) {
        p.gamma = 0.85;
        p.other = 9.5;
    }
}
inline void applyTuned(Params& p, int group) { applyTuned(p, group, 1); }
}  // namespace bc
"""
SPEC = {"contexts": "group_phase", "groups": 1, "phases": 2,
        "knobs": {"gamma": {"lo": 0.7, "hi": 0.9, "per": "context"},
                  "squadSize": {"lo": 1, "hi": 5, "int": True},
                  "wDanger": {"lo": 0.0, "hi": 4.0, "per": "group"}}}


def test_space_names_defaults_and_header():
    sp = space.Space(SPEC, COMMON, TUNED)
    assert sp.names() == ["gamma@0@0", "gamma@0@1", "squadSize", "wDanger@0"]
    d = sp.defaults()
    assert d == {"gamma@0@0": 0.75, "gamma@0@1": 0.85, "squadSize": 3, "wDanger@0": 1.0}
    assert sp.decode(sp.encode(d)) == d
    h = sp.header(d)
    ctx = space.parse_header(h)
    assert ctx[(0, 0)] == {"gamma": "0.75", "other": "9.0", "squadSize": "3", "wDanger": "1.0"}
    assert ctx[(0, 1)]["other"] == "9.5"                       # untouched base fields are kept literally
    assert "inline void applyTuned(Params& p, int group) { applyTuned(p, group, 1); }" in h


def test_space_rejects_unknown_knob():
    with pytest.raises(ValueError):
        space.Space({"knobs": {"nope": {"lo": 0, "hi": 1}}}, COMMON)


def test_space_carry_between_shapes():
    sp = space.Space(SPEC, COMMON, TUNED)
    start = sp.defaults()
    out = sp.carry(start, {"gamma": 0.8, "squadSize@0": 4, "wDanger@0@0": 2.0, "wDanger@0@1": 3.0})
    assert out["gamma@0@0"] == out["gamma@0@1"] == 0.8          # coarser earlier value copied down
    assert out["squadSize"] == 4                                # finer earlier values averaged up
    assert out["wDanger@0"] == 2.5


def test_group_and_none_headers():
    sp = space.Space({"contexts": "group", "groups": 2, "knobs": {"gamma": {"lo": 0.7, "hi": 0.9, "per": "group"}}},
                     COMMON)
    h = sp.header({"gamma@0": 0.71, "gamma@1": 0.72})
    assert space.parse_header(h) == {(0, 0): {"gamma": "0.71"}, (1, 0): {"gamma": "0.72"}}
    sp = space.Space({"contexts": "none", "knobs": {"squadSize": {"lo": 1, "hi": 5, "int": True}}}, COMMON)
    assert "p.squadSize = 4;" in sp.header({"squadSize": 4.4})


@pytest.mark.skipif(not os.path.isfile(os.path.join(ABYSS, "tuned_params.hpp")), reason="bots/abyss not present")
def test_example_space_starts_at_the_released_bot():
    sp = space.Space.for_bot(os.path.join(INFRA, "tuning", "space.example.json"), ABYSS)
    base = open(os.path.join(ABYSS, "tuned_params.hpp")).read().splitlines()[1:]
    assert sp.header(sp.decode(sp.encode(sp.defaults()))).splitlines()[1:] == base


def test_league_pfsp_and_champion(tmp_path):
    def bot(name):
        d = tmp_path / name
        d.mkdir()
        (d / "bot.toml").write_text("[project]\n")
        return str(d)
    lg = League(str(tmp_path / "league.json"))
    for n in ("r1", "r2", "r3"):
        lg.add(n, bot(n), "reference")
    champ_dir = tmp_path / "evo" / "champion"
    (champ_dir / "c0_rel").mkdir(parents=True)
    (champ_dir / "c0_rel" / "bot.toml").write_text("[project]\n")
    (champ_dir / "current.json").write_text(json.dumps({"name": "rel", "dir": "c0_rel"}))
    assert lg.set_champion(str(champ_dir)) == "champ_rel"
    for _ in range(50):
        lg.record("r1", 1.0)      # r1 beats the candidates: drawn more often
        lg.record("r2", 0.0)
    rng = random.Random(1)
    draws = [m["name"] for _ in range(300) for m in lg.sample(2, rng)]
    assert draws.count("champ_rel") == 300                     # the champion is always in
    assert draws.count("r1") > 3 * draws.count("r2")
    lg.save()
    assert len(League(str(tmp_path / "league.json")).members) == 4


def test_game_score():
    import importlib
    cm = importlib.import_module("cmaes_run")
    assert cm.game_score("A", "A", 0, 0) == 1.0
    assert cm.game_score(None, "B", 0, 0) == 0.5
    assert 0.0 < cm.game_score("B", "A", 30, 0) < 0.1           # a loss with a length lead stays below a draw


def test_stage_writes_budget_and_header(tmp_path):
    import evaluate
    src = tmp_path / "src"
    src.mkdir()
    (src / "bot.toml").write_text("[project]\n")
    (src / "common.hpp").write_text('#if __has_include("eval_budget.hpp")\n#include "eval_budget.hpp"\n#endif\n'
                                    "#ifndef BC_TURN_BUDGET\n#define BC_TURN_BUDGET 0.075\n#endif\n")
    evaluate.stage(str(src), str(tmp_path / "a"), 0.015, "// header\n")
    assert (tmp_path / "a" / "eval_budget.hpp").read_text() == "#pragma once\n#define BC_TURN_BUDGET 0.015\n"
    assert (tmp_path / "a" / "tuned_params.hpp").read_text() == "// header\n"
    assert (tmp_path / "a" / "common.hpp").read_text() == (src / "common.hpp").read_text()
    (src / "common.hpp").write_text("#define BC_TURN_BUDGET 0.075\n")          # no hook: the define goes first
    evaluate.stage(str(src), str(tmp_path / "b"), 0.02)
    assert (tmp_path / "b" / "common.hpp").read_text().startswith("#define BC_TURN_BUDGET 0.02\n")
