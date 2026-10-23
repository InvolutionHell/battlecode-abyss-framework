"""abyss_tools.kpi.queen_box / queen_feed / queen_kill / queen_early_deaths: the queen KPIs."""
import os

from abyss_tools.kpi import queen_box as qb
from abyss_tools.kpi import queen_early_deaths as qd
from abyss_tools.kpi import queen_feed as qk
from abyss_tools.kpi import queen_kill as kk
from abyss_tools.replay.state import Board


# ------------------------------------------------------------------------------------------------------------ queen_box

def own(block, free):
    return {"block": block, "free": free}


def test_box_alive():
    assert qb.classify(None, None) == {"games": 1}


def test_box_enemy_death_not_own():
    c = qb.classify(250, None)
    assert c["deaths"] == 1 and c["deaths < r300"] == 1 and c["own move"] == 0


def test_box_teammate_involved():
    c = qb.classify(320, own({"N": "team tail", "E": "wall", "S": "self"}, 1))
    assert c["deaths"] == 1 and c["deaths < r300"] == 0 and c["own move"] == 1
    assert c["teammate-involved"] == 1 and c["teammate-involved, free 1"] == 1
    c = qb.classify(100, own({"N": "team", "E": "team", "S": "self", "W": "wall"}, 0))
    assert c["teammate-involved, free 0"] == 1
    c = qb.classify(100, own({"N": "team"}, 3))
    assert c["teammate-involved, free 2+"] == 1


def test_box_no_teammate():
    c = qb.classify(100, own({"N": "enemy", "E": "wall", "S": "self"}, 1))
    assert c["own move"] == 1 and c["teammate-involved"] == 0


def test_box_table():
    rows = [("x [on]", qb.classify(100, own({"N": "team"}, 0))), ("x [on]", qb.classify(None, None))]
    t = qb.table(rows)
    assert t[2] == "| x [on] | 2 | 1 | 1 | 1 | 1 | 1 | 0 | 0 | 0 |"
    rows[0][1]["sacrifices"] = 2
    assert qb.table(rows)[2].endswith("| 2 |")


def test_box_on_fixtures(replays):
    # team A's queen dies on her own move in both games, boxed in with a teammate on one side; in the mirror game team B's
    # queen too, with a teammate's tail on her north side
    s = qb.queen_self(replays["vs_starter"], "A")
    assert (s["round"], s["reason"], s["len"], s["free"]) == (33, "hitWall", 2, 0)
    assert s["block"] == {"N": "wall", "E": "wall", "S": "self tail", "W": "team tail"}
    s = qb.queen_self(replays["mirror"], "A")
    assert (s["round"], s["reason"], s["free"]) == (148, "hitWall", 0) and s["block"]["E"] == "team"
    s = qb.queen_self(replays["mirror"], "B")
    assert (s["round"], s["reason"], s["free"]) == (147, "hitOtherBody", 0) and s["block"]["N"] == "team tail"
    assert qb.queen_self(replays["vs_starter"], "B") is None
    rows = dict(qb.job(replays["vs_starter"]))
    assert rows["abyss"]["teammate-involved, free 0"] == 1 and rows["starter"].get("own move", 0) == 0


# ----------------------------------------------------------------------------------------------------------- queen_feed

def track():
    # queen A (id 0) 3 long heading east along y 0, queen B (id 1) 2 long at y 5
    return qk.QueenTrack({0: [(3, 0), (2, 0), (1, 0)], 1: [(3, 5), (2, 5)]}, {0: "A", 1: "B"})


def test_feed_eats_after_r350_only():
    t = track()
    t.on_round(349)
    t.on_update(0, (4, 0), (1, 0))            # an eat before r350: not counted (the tail stays at (1, 0))
    assert len(t.body[0]) == 4
    t.on_round(350)
    assert t.at == {"A": 4, "B": 2}
    t.on_update(0, (5, 0), (1, 0))            # eats: head moves, tail stays -> +1
    t.on_update(0, (6, 0), (2, 0))            # a plain step: no growth
    t.on_update(1, (4, 5), (2, 5))            # B eats once (its tail stays at (2, 5))
    assert t.eats == {"A": 1, "B": 1}


def test_feed_split_is_no_eat():
    t = track()
    t.on_round(350)
    t.on_split(0, 9, "A", [(3, 0), (2, 0)], [(1, 0)])
    t.on_update(0, (4, 0), (3, 0))            # after the split: a plain step from 2 long
    assert t.eats["A"] == 0 and len(t.body[0]) == 2 and t.team[9] == "A"


def test_feed_death_and_rows():
    t = track()
    t.on_round(350)
    t.on_update(0, (4, 0), (1, 0))            # A eats 1
    t.on_round(420)
    t.on_death(1)                             # B's queen dies r420
    rows = dict(t.rows({"A": "cand", "B": "base"}, winner="A"))
    assert rows["cand"]["eats"] == 1 and rows["cand"]["alive end"] == 1 and rows["cand"]["L end"] == 4
    assert rows["base"]["died"] == 1 and rows["base"]["alive end"] == 0 and rows["base"]["L end"] == 0
    assert rows["cand"]["both alive"] == 0 and rows["cand"]["wins"] == 1


def test_feed_dead_before_r350():
    t = track()
    t.on_round(300)
    t.on_death(1)
    t.on_round(350)
    t.on_round(400)
    rows = dict(t.rows({"A": "a", "B": "b"}, winner=None))
    assert rows["b"]["alive r350"] == 0 and rows["b"]["died"] == 0     # died before the window: not a window death
    assert rows["a"]["short r350"] == 1 and rows["a"]["L r350"] == 3


def test_feed_table():
    rows = [("x [on]", {"games": 1, "wins": 1, "alive r350": 1, "short r350": 1, "L r350": 3, "eats": 4, "died": 0,
                        "alive end": 1, "L end": 7, "both alive": 1, "both wins": 1}),
            ("x [on]", {"games": 1, "wins": 0, "alive r350": 1, "short r350": 1, "L r350": 5, "eats": 2, "died": 1,
                        "alive end": 0, "L end": 0, "both alive": 0, "both wins": 0})]
    t = qk.table(rows)
    assert t[2] == "| x [on] | 2 | 1 | 2 | 2 | 4.0 | 6 | 3.0 | 1 | 1 | 7.0 | 1 (1) |"


def test_feed_on_fixtures(replays):
    rows = dict(qk.job(replays["vs_starter"]))                # a game over by r45: no late game
    assert rows["abyss"]["wins"] == 1 and rows["abyss"]["alive r350"] == 0 and rows["starter"]["wins"] == 0


# ----------------------------------------------------------------------------------------------------------- queen_kill

def test_kill_kind():
    assert kk.kind(1, 12) == "step"
    assert kk.kind(2, 5) == "free" and kk.kind(2, 4) == "paid"     # ceil(5/4) = 2, ceil(4/4) = 1
    assert kk.kind(3, 12) == "free" and kk.kind(4, 12) == "paid"   # ceil(12/4) = 3


def ktrack():
    # queen A (id 0), queen B (id 1), a 9-long B hunter (id 3) and a 2-long A dragon (id 2)
    bodies = {0: [(5, 5), (4, 5)], 1: [(15, 5), (16, 5)], 2: [(8, 8), (8, 9)], 3: [(5, 9)] + [(6 + i, 9) for i in range(8)]}
    return kk.KillTrack(bodies, {0: "A", 1: "B", 2: "A", 3: "B"})


def test_kill_free_sprint_on_enemy_turn():
    t = ktrack()
    t.on_round(200)
    t.on_turn(3)
    t.on_move(3)                              # 9 long: ceil(9/4) = 3 free steps
    t.on_death_why(0, "hitHeadToHead")
    rows = dict(t.kill_rows({"A": "ours", "B": "theirs"}))
    assert rows["ours"] == {"games": 1, "deaths": 1, "enemy turn": 1, "step": 0, "free": 1, "paid": 0, "bucket2": 1}
    assert rows["theirs"]["deaths"] == 0 and rows["theirs"]["bucket2"] == 0


def test_kill_own_turn_and_team_turn_are_not_enemy_turn():
    t = ktrack()
    t.on_turn(0)
    t.on_move(1)
    t.on_death_why(0, "hitHeadToHead")       # she moved into it herself
    assert t.deaths["A"] == 1 and t.enemy_turn["A"] == 0
    u = ktrack()
    u.on_turn(2)                              # her own team's 2-long dragon's turn
    u.on_move(1)
    u.on_death_why(0, "hitHeadToHead")
    assert u.enemy_turn["A"] == 0


def test_kill_paid_and_other_reasons():
    t = ktrack()
    t.on_turn(3)
    t.on_move(5)                              # 5 steps from 9 long: 2 paid
    t.on_death_why(0, "hitHeadToHead")
    assert t.kills["A"]["paid"] == 1
    u = ktrack()
    u.on_turn(3)
    u.on_move(1)
    u.on_death_why(0, "hitBody")              # on their turn, not a head-on: an enemy-turn death, no kill kind
    assert u.enemy_turn["A"] == 1 and sum(u.kills["A"].values()) == 0


def test_kill_mover_length_before_its_move():
    t = ktrack()
    t.on_turn(3)
    t.on_move(3)
    t.on_update(3, (5, 6), (12, 9))           # its body after the move
    t.on_death_why(0, "hitHeadToHead")
    assert t.kills["A"]["free"] == 1          # judged by the 9 it had before moving


def test_kill_table():
    rows = [("x [on]", {"games": 1, "deaths": 1, "enemy turn": 1, "step": 0, "free": 1, "paid": 0, "bucket2": 1}),
            ("x [on]", {"games": 1, "deaths": 0, "enemy turn": 0, "step": 0, "free": 0, "paid": 0, "bucket2": 0})]
    t = kk.table(rows)
    assert t[2] == "| x [on] | 2 | 1 | 1 | 0 | 1 | 0 | 50.0 | 50.0 | 1 |"


def test_kill_on_fixtures(replays):
    rows = dict(kk.job(replays["mirror"]))
    assert rows["abyss"]["deaths"] == 1 and rows["abyss"]["enemy turn"] == 0       # she died on her own move
    assert rows["abyss_b"]["deaths"] == 1 and rows["abyss_b"]["enemy turn"] == 0   # so did hers
    rows = dict(kk.job(replays["vs_starter"]))
    assert rows["abyss"]["enemy turn"] == 0
    assert rows["starter"]["enemy turn"] == 1 and rows["starter"]["paid"] == 1     # struck by a paid sprint


# --------------------------------------------------------------------------------------------------- queen_early_deaths

def open_map(W=12, H=10, walls=()):
    """an open W x H map; walls as ('h' north side | 'v' west side, x, y)"""
    def eidx(k, x, y):
        return (2 * y) * (W + 1) + x if k == "h" else (2 * y + 1) * (W + 1) + x
    lines = [f"MAP {W} {H}", "SYMMETRY xy", "MAP_NAME t", f"TILE_COUNT {W * H}"]
    lines += [f"TILE {x} {y} 0 0" for y in range(H) for x in range(W)]
    edges = sorted((eidx(*w), 1, -1) for w in walls)
    lines.append(f"EDGE_COUNT {len(edges)}")
    lines += [f"EDGE {i} {k} {p}" for i, k, p in edges]
    lines += ["DRAGON_COUNT 2", "DRAGON 0 2 6 6 6 7", "DRAGON 1 2 0 6 0 7", "END"]
    return Board("\n".join(lines) + "\n")


def test_early_rmax():
    # ceil(n/4) free steps + n - 2 paid ones; a 1-long dragon cannot move into anyone
    assert [qd.rmax(n) for n in (1, 2, 3, 4, 5, 8)] == [0, 1, 2, 3, 5, 8]


def test_early_best_landing():
    bd = open_map()
    # her head (5, 5), a 2-long enemy head at (7, 5) (reach 1): N / S / W are 3 away (margin 2), E is 1 away (margin 0)
    assert qd.best_landing(bd, (5, 5), set(), [((7, 5), 2)]) == 2
    assert qd.best_landing(bd, (5, 5), {(5, 4), (5, 6), (4, 5)}, [((7, 5), 2)]) == 0          # only E free
    assert qd.best_landing(bd, (5, 5), {(5, 4), (5, 6), (4, 5), (6, 5)}, [((7, 5), 2)]) is None  # boxed
    # a 3-long enemy reaches 2: W is 3 away - margin 1, out of reach
    assert qd.best_landing(bd, (5, 5), {(5, 4), (5, 6), (6, 5)}, [((7, 5), 3)]) == 1
    # the nearest head decides each landing: 2-long heads 2 away on all four sides leave every landing 1 from one of them
    assert qd.best_landing(bd, (5, 5), set(), [((7, 5), 2), ((3, 5), 2), ((5, 3), 2), ((5, 7), 2)]) == 0
    assert qd.best_landing(bd, (5, 5), set(), [((7, 5), 2), ((3, 5), 2)]) == 2
    # no enemy head at all, or one far away: the cap
    assert qd.best_landing(bd, (5, 5), set(), []) == qd.CAP
    assert qd.best_landing(bd, (5, 5), set(), [((11, 0), 2)]) == qd.CAP
    # a wall on her north side: the landing is not there
    bdw = open_map(walls=[("h", 5, 5)])
    assert qd.best_landing(bdw, (5, 5), {(5, 6), (4, 5), (6, 5)}, [((7, 5), 2)]) is None


def test_early_torus():
    bd = open_map()
    assert qd.tdist((0, 5), (11, 5), 12, 10) == 1
    assert qd.tdist((0, 0), (11, 9), 12, 10) == 2
    # her head (0, 5), a 2-long enemy at (11, 5) next to her across the edge: N is 2 from it (margin 1)
    assert qd.best_landing(bd, (0, 5), {(11, 5), (0, 6), (1, 5)}, [((11, 5), 2)]) == 1


def portal_map():
    """an open 12 x 10 map with one portal: the west side of (3, 5) <-> the west side of (9, 2)"""
    W, H = 12, 10

    def eidx(k, x, y):
        return (2 * y) * (W + 1) + x if k == "h" else (2 * y + 1) * (W + 1) + x
    lines = [f"MAP {W} {H}", "SYMMETRY xy", "MAP_NAME t", f"TILE_COUNT {W * H}"]
    lines += [f"TILE {x} {y} 0 0" for y in range(H) for x in range(W)]
    edges = sorted([(eidx("v", 3, 5), 2, 0), (eidx("v", 9, 2), 2, 0)])
    lines += [f"EDGE_COUNT {len(edges)}"] + [f"EDGE {i} {k} {p}" for i, k, p in edges]
    lines += ["DRAGON_COUNT 2", "DRAGON 0 2 6 6 6 7", "DRAGON 1 2 0 6 0 7", "END"]
    return Board("\n".join(lines) + "\n")


def test_early_portals():
    bd = portal_map()
    # stepping W from (3, 5) lands at (8, 2): an enemy head at (8, 2) is 1 step from (3, 5) through the portal
    assert qd.path_dist(bd, (8, 2), 1).get((3, 5)) == 1 and qd.tdist((8, 2), (3, 5), 12, 10) == 8
    # her head (4, 5), only W free: the landing (3, 5) is in the 2-long enemy's reach through the portal (margin 0)
    assert qd.best_landing(bd, (4, 5), {(4, 4), (4, 6), (5, 5)}, [((8, 2), 2)]) == 0
    # the same without the portal: straight-line 8 - reach 1, capped
    assert qd.best_landing(open_map(), (4, 5), {(4, 4), (4, 6), (5, 5)}, [((8, 2), 2)]) == qd.CAP


def test_early_distinct_and_struck():
    a = dict(round=66, reason="hitHeadToHead", L=2, ours=44, near=[(3, 3)], landing=2, own=False, bot="x")
    b = dict(a, bot="y")                       # the same death in an arm that shares the seed
    c = dict(a, round=75)                      # another round on the same map: another death
    d = dict(a, reason="hitWall", own=True)
    e = dict(a)                                # the same state on another map: another death
    deaths = {("unsw", "x"): [a, c, d], ("unsw", "y"): [b], ("autarky", "x"): [e]}
    out = qd.distinct(deaths)
    assert len(out) == 4 and sorted(o["map"] for o in out) == ["autarky", "unsw", "unsw", "unsw"]
    assert qd.struck(a) and not qd.struck(d) and not qd.struck(dict(a, own=True))


def test_early_on_fixtures(replays):
    name, deaths, bots = qd.job((replays["vs_starter"], 150))
    assert bots == {"A": "abyss", "B": "starter"} and [d["round"] for d in deaths] == [33, 44]
    a, b = deaths
    assert a["own"] and a["reason"] == "hitWall" and a["landing"] is None
    assert not b["own"] and b["reason"] == "hitHeadToHead" and qd.struck(b)
    assert qd.job((replays["vs_starter"], 40))[1][0]["round"] == 33 and len(qd.job((replays["vs_starter"], 40))[1]) == 1


def test_cli_tables_on_fixtures(replays, capsys):
    d = os.path.dirname(replays["mirror"])
    for mod in (qb, qk, kk):
        mod.main([d, "-j", "1"])
    qd.main([d, "-j", "1"])
    out = capsys.readouterr().out
    assert "| abyss [fixtures] | 2 | 2 | 2 | 2 | 2 | 2 | 0 | 0 | 0 |" in out
    assert "distinct: 4 queen-games, 4 queen deaths before r150" in out
