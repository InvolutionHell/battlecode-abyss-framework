"""abyss_tools.kpi: spawn_share, value_trace, macro_measure / bedtiers, census_open and region_graph."""
import json
import os

import pytest

from abyss_tools.kpi import bedtiers, census_open, macro_measure, region_graph, spawn_share as ss, value_trace as vt
from abyss_tools.replay.io import arm_label
from abyss_tools.replay.state import Board


# ---------------------------------------------------------------------------------------------------------- spawn_share

def test_spawn_rows_of():
    spawn = {"r0-199": {"n": 10, "closer A": 6, "closer B": 3, "closer tie": 1}, "r200-499": {"n": 4, "closer B": 4}}
    rows = ss.rows_of("cand_L", "base_c_R", spawn, "trauma")
    assert rows[0] == ("trauma", "cand", {"r0-199": (6, 10), "r200-499": (0, 4)})
    assert rows[1] == ("trauma", "base_c", {"r0-199": (3, 10), "r200-499": (4, 4)})  # ties count in neither numerator


def test_spawn_rows_of_empty():
    rows = ss.rows_of("a", "b", {}, "maze")
    assert rows[0][2] == {"r0-199": (0, 0), "r200-499": (0, 0)}


def test_spawn_table_pools():
    rows = [("maze", "x", {"r0-199": (6, 10), "r200-499": (1, 2)}),
            ("maze", "x", {"r0-199": (1, 10), "r200-499": (0, 0)}),
            ("tower", "x", {"r0-199": (3, 4), "r200-499": (0, 0)})]
    t = ss.table(rows)
    assert t[2] == "| maze | x | 2 | 0.350 (7 / 20) | 0.500 (1 / 2) |"   # pooled, not a mean of shares
    assert t[3] == "| tower | x | 1 | 0.750 (3 / 4) | - |"
    assert t[-1] == "| **all** | x | 3 | 0.417 (10 / 24) | 0.500 (1 / 2) |"


def test_spawn_table_order():
    rows = [("b", "y", {"r0-199": (0, 1), "r200-499": (0, 1)}), ("a", "y", {"r0-199": (0, 1), "r200-499": (0, 1)})]
    t = ss.table(rows)
    assert t[2].startswith("| a |") and t[3].startswith("| b |") and t[4].startswith("| **all** |")


def test_spawn_arm_label():
    assert arm_label("/runs/cand/replays") == "cand" and arm_label("/runs/cand/replays/") == "cand"
    assert arm_label("~/x/base") == "base"


def test_spawn_on_fixtures(replays):
    rows = ss.collect([os.path.dirname(replays["mirror"])], jobs=1)
    by = {(bot, mp): per for mp, bot, per in rows}
    near, n = by[("starter [fixtures]", "arena")]["r0-199"]
    assert 0 <= near <= n and n > 0


# ---------------------------------------------------------------------------------------------------------- value_trace

def test_value_count():
    lines = [
        "r10 forage group=4 depth=8 ms=3 L=3 body=3 king=0 vr0",                 # no table yet
        "r11 forage group=4 depth=8 ms=3 L=3 body=3 king=0 vS vr1",              # sent one
        "r12 forage group=4 depth=8 ms=3 L=3 body=3 king=0 vH2 vR1 vr3@4,5",     # heard 2, relayed 1, a target
        "r13 forage group=4 depth=8 ms=3 L=3 body=3 king=0 vA1 vr3",             # arrived: a hit
        "r14 forage group=4 depth=8 ms=3 L=3 body=3 king=0 vA0 vr3@9,9",         # arrived: a miss; a new target
        "r15 eat group=4 depth=8 ms=3 L=3 body=3 king=0",                        # no VALUE tags: not a traced turn
        "LOG fb95 policy vS",                                                    # not a turn line
    ]
    c = vt.count(lines)
    assert c['turns'] == 5 and c['sent'] == 1 and c['heard'] == 2 and c['relayed'] == 1
    assert c['target'] == 2 and c['arrivals'] == 2 and c['hits'] == 1


def test_value_empty():
    assert vt.count([]) == {} and vt.count(["r1 forage vr0"])['turns'] == 1


def test_value_on_fixtures(replays):
    lines = vt.replay_lines(replays["vs_starter"])          # a plain build logs nothing; the starter logs "Moving in ..."
    assert vt.count(lines.get("abyss", [])) == {}


# ------------------------------------------------------------------------------------------------ macro_measure / tiers

def test_bed_tiers():
    beds = {(x, 0): (1, 1) for x in range(3)}                                  # 3 fountains
    beds.update({(x, 1): (10, 20) for x in range(10)})                         # 10 fast beds
    beds.update({(x, 2): (100, 200) for x in range(20)})                       # 20 slow beds
    tiers, g = bedtiers.tiers_of(beds)
    assert tiers["fount"] == {(x, 0) for x in range(3)}
    assert tiers["fast"] == tiers["fount"] | {(x, 1) for x in range(10)}
    assert g == 15.0 and tiers["best"] == tiers["fast"]        # the 8th fastest bed has mean gap 15: ties included
    assert bedtiers.tiers_of({}) == ({"fount": set(), "fast": set(), "best": set()}, None)


def test_bfs_field():
    text = "MAP 5 3\nSYMMETRY xy\nMAP_NAME t\nTILE_COUNT 15\nEDGE_COUNT 0\nDRAGON_COUNT 0\nEND\n"
    bd = Board(text)
    D, L = bedtiers.bfs_field(bd, {(0, 0)}, labels=True)
    assert D[0][0] == 0 and D[0][4] == 1 and D[2][0] == 1 and D[1][2] == 3     # the map wraps
    assert L[1][2] == (0, 0)


def test_macro_on_fixtures(replays, tmp_path):
    row = macro_measure.macro(replays["vs_starter"])
    assert row["map"] == "Arena" and row["winner"] == "A" and row["how"] == "wipe" and row["rounds"] == 45
    assert row["start_units"] == {"A": 1, "B": 1} and row["units"]["A"]["0"] == 1
    assert row["final"]["B"][0] == 0 and row["deaths"]["B"] == {"hitHeadToHead": 1}
    assert set(row["t1"]["A"]) == set(row["bed_tiers"])         # local replays carry the bed gaps: tiers exist
    assert row["eat_at"]["A"]["50"] >= row["eatn_at"]["A"]["50"] >= 0
    y = macro_measure.yield_kpis(row, "A")["pearls / 100 dragon-turns (r12-59)"]
    assert y is None or y >= 0
    k = macro_measure.bed_kpis(row, "A")
    assert "t1 best (never = 200)" in k and k["units@60"] is None    # the game ended at r45
    out = tmp_path / "rows.jsonl"
    assert macro_measure.run(str(out), [replays["vs_starter"], replays["mirror"]], jobs=1, quiet=True) == 2
    assert macro_measure.run(str(out), [replays["vs_starter"]], jobs=1, quiet=True) == 0    # resumed: nothing to do
    assert len(out.read_text().splitlines()) == 2
    assert "error" in macro_measure.safe(str(tmp_path / "missing.replay"))


# -------------------------------------------------------------------------------------------------------- census_open

def row(fname, winner, u100, opp_u100, eat100=10, how="r500", rounds=500):
    """a synthetic macro_measure row of a game CAND (side A) vs OPP"""
    return {"file": fname, "map": "M", "winner": winner, "how": how, "rounds": rounds,
            "units": {"A": {"50": u100 // 2, "100": u100}, "B": {"50": opp_u100 // 2, "100": opp_u100}},
            "eat_at": {"A": {"100": eat100}, "B": {"100": 5}}, "deaths": {"A": {"hitWall": 1}, "B": {}},
            "final": {"A": [3, 9, 20, 6], "B": [2, 7, 12, 5]}}


def test_census_open_paired_report():
    rows = [row("m1__arm_vs_opp.replay", "A", 10, 8), row("m1__base_vs_opp.replay", "B", 8, 8),
            row("m1__arm_vs_opp_r1.replay", "A", 12, 8), row("m1__base_vs_opp_r1.replay", "A", 9, 8),
            row("m2__arm_vs_opp.replay", "B", 4, 9, how="wipe", rounds=120), row("m2__base_vs_opp.replay", "B", 5, 9),
            row("m3__base_vs_opp.replay", "A", 9, 9)]                        # no arm game on m3: left out
    A, B = census_open.index(rows, "arm"), census_open.index(rows, "base")
    assert set(A) == {("m1", "opp", "A", 0), ("m1", "opp", "A", 1), ("m2", "opp", "A", 0)} and len(B) == 4
    assert A[("m2", "opp", "A", 0)]["wiped"] and A[("m2", "opp", "A", 0)]["wround"] == 120
    out = census_open.report(rows, "base", groups_of={"pool": {"M"}})
    assert "**arm** - base (paired games: 3)" in out
    line = next(l for l in out.splitlines() if l.startswith("| all |"))
    assert line.startswith("| all | 3 | 2 / 1 | 1 / 0 | 120 / - | +1.0 ± 0.6 | +1.3 ± 1.2 | +0.3 / -1.0 |")
    assert any(l.startswith("| pool | 3 |") for l in out.splitlines())
    assert census_open.mse([1.0, 3.0])[0] == 2.0 and census_open.mse([]) == (0.0, 0.0)
    assert "no paired games" in census_open.report(rows, "base", arms=["nobody"])
    with pytest.raises(ValueError):
        census_open.parse_groups(["bad"])


def test_census_open_cli(replays, tmp_path, capsys):
    rows = tmp_path / "rows.jsonl"
    census_open.main(["collect", str(rows), os.path.dirname(replays["mirror"]), "-j", "1"])
    assert len(rows.read_text().splitlines()) == 2
    census_open.main(["report", str(rows), "--base", "abyss", "--md", str(tmp_path / "o.md")])
    assert (tmp_path / "o.md").exists()
    assert all("file" in json.loads(l) for l in rows.read_text().splitlines())


# -------------------------------------------------------------------------------------------------------- region_graph

def test_region_graph_rooms():
    # a 6 x 4 map; a wall ring around the 2-tile room {(1, 1), (2, 1)}, one of its sides a portal to (4, 2)'s west side
    W, H = 6, 4

    def eidx(k, x, y):
        return (2 * y) * (W + 1) + x if k == "h" else (2 * y + 1) * (W + 1) + x
    walls = [("h", 1, 1), ("h", 2, 1), ("h", 1, 2), ("h", 2, 2), ("v", 1, 1)]
    edges = [(eidx(*w), 1, -1) for w in walls] + [(eidx("v", 3, 1), 2, 0), (eidx("v", 4, 2), 2, 0)]
    lines = [f"MAP {W} {H}", "SYMMETRY xy", "MAP_NAME t", f"TILE_COUNT {W * H}"]
    lines += [f"TILE {x} {y} {'1 5' if (x, y) == (1, 1) else '0 0'}" for y in range(H) for x in range(W)]
    lines += [f"EDGE_COUNT {len(edges)}"] + [f"EDGE {i} {k} {p}" for i, k, p in sorted(edges)]
    lines += ["DRAGON_COUNT 0", "END"]
    rooms, steps = region_graph.graph("\n".join(lines) + "\n")
    small = [r for r in rooms if r[1] == 2]
    assert small == [(1 * W + 1, 2, 1, 1, True)]             # anchor, size, beds, exits (portal sides), closed
    assert sum(r[1] for r in rooms) == W * H and len(rooms) == 2
    assert steps[(1 * W + 2, 1)] == 2 * W + 4                 # east from (2, 1) through the portal lands on (4, 2)


def test_region_graph_on_fixture(replays, capsys):
    text, seen = region_graph.map_text(replays["vs_starter"])
    assert text.startswith("MAP 11 11") and seen
    rooms, _ = region_graph.graph(text)
    assert sum(r[1] for r in rooms) == 121


def test_region_dump_matches_the_bot(tmp_path):
    """the bot's room module (bots/abyss/region.hpp, built natively) and region_graph agree on generated maps with portals"""
    from abyss_tools.maps import mapgen
    from abyss_tools.paths import bots_dir
    from helpers import need_cxx
    need_cxx()
    include = os.path.join(bots_dir(), "abyss")
    if not os.path.exists(os.path.join(include, "region.hpp")):
        pytest.skip("no bots/abyss/region.hpp in this checkout")
    maps = []
    for seed in range(1, 40):
        text = mapgen.make(seed, f"g{seed}")
        if "EDGE" in text and " 2 " in "".join(l for l in text.splitlines() if l.startswith("EDGE ")):
            p = tmp_path / f"g{seed}.map"
            p.write_text(text)
            maps.append(str(p))
        if len(maps) == 4:
            break
    assert maps, "no generated map with a portal"
    with pytest.raises(SystemExit) as e:
        region_graph.main(["--check", "--include", include] + maps)
    assert e.value.code == 0
