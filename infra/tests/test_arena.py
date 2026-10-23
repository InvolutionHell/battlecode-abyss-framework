import arena
import cpu_check

# unswbc 1.2.9 output of a short sandbox game in which team B lost one turn to the CPU limit
OUT = """loaded wasm vs wasm in the judge's sandbox
seed 0x0000000000003039
round 7: bot 1 (team B) exceeded CPU limit
round 7: bot 1 (team B) died: no valid action
round 30: bot 3 (team B) died: no valid action
team A wins after 31 rounds (by elimination) (22.3s)
team A points per turn: p50 3.7M  p99 3.7M  mean 3.7M  max 3.7M  (62 turns)
team B points per turn: p50 52.6M  p99 98.2M  mean 56.1M  max 98.2M  (38 turns)
skipped writing replay
"""


def test_parse_output():
    m, cpu, fails = arena.parse_output(OUT)
    assert m.group(1) == "A" and m.group(2) == "31"
    assert cpu == {"A": 3.7, "B": 98.2}
    assert fails == {"A": 0, "B": 1}
    m, cpu, _ = arena.parse_output("draw after 500 rounds (equal queens) (9m)\nteam A points per turn: p50 900  "
                                   "p99 9,000  mean 950  max 12,345  (10 turns)\n")
    assert m.group(3) == "500"
    assert abs(cpu["A"] - 0.012345) < 1e-9


def test_paired_seeds():
    s1 = arena.seed_of("/x/maps/default.map", "cand-A", 0, 7, False)
    assert s1 == arena.seed_of("/other/maps/default.map", "cand-A", 0, 7, False)   # the slot, not the path
    assert s1 != arena.seed_of("/x/maps/default.map", "cand-B", 0, 7, False)
    assert s1 != arena.seed_of("/x/maps/default.map", "cand-A", 1, 7, False)
    assert s1 != arena.seed_of("/x/maps/default.map", "cand-A", 0, 8, False)
    assert arena.seed_of("m.map", "cand-A", 0, 7, True) == arena.seed_of("m.map", "cand-B", 0, 7, True)   # --fork


def test_cpu_check(tmp_path):
    run = tmp_path / "pair" / "orig"
    run.mkdir(parents=True)
    (run / "cpu.txt").write_text("default.map a vs b: cand_max=71.5M opp_max=3.7M tle=0 opp_tle=0 secs=40\n"
                                 "default.map b vs a: cand_max=96.0M opp_max=-M tle=0 opp_tle=0 secs=41\n")
    rows = cpu_check.read(str(run / "cpu.txt"))
    assert [r[3] for r in rows] == [71.5, 96.0]
    assert cpu_check.read(str(run / "cpu.txt"), opp=True)[1][3] is None
    assert cpu_check.files([str(tmp_path)]) == [str(run / "cpu.txt")]
