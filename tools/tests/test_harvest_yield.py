"""abyss_tools.kpi.harvest_yield: small closed rooms and the entries into them."""
import os

from abyss_tools.kpi import harvest_yield as hy
from abyss_tools.replay.state import Board


def walled_map(W=8, H=6, walls=(), portals=()):
    """an open W x H map; walls as ('h' north side | 'v' west side, x, y); portals as pairs of such sides"""
    def eidx(k, x, y):
        return (2 * y) * (W + 1) + x if k == "h" else (2 * y + 1) * (W + 1) + x
    lines = [f"MAP {W} {H}", "SYMMETRY xy", "MAP_NAME t", f"TILE_COUNT {W * H}"]
    lines += [f"TILE {x} {y} 0 0" for y in range(H) for x in range(W)]
    edges = sorted([(eidx(*w), 1, -1) for w in walls] + [(eidx(*e), 2, i) for i, pr in enumerate(portals) for e in pr])
    lines.append(f"EDGE_COUNT {len(edges)}")
    lines += [f"EDGE {i} {k} {p}" for i, k, p in edges]
    lines += ["DRAGON_COUNT 2", "DRAGON 0 2 6 4 6 5", "DRAGON 1 2 1 4 1 5", "END"]
    return Board("\n".join(lines) + "\n")


# a 3-tile room {(1,0), (2,0), (1,1)} closed by walls: north sides of row 0 wrap to row 5, so wall those too
ROOM3 = [("h", 1, 0), ("h", 2, 0), ("v", 1, 0), ("v", 3, 0), ("h", 2, 1), ("v", 1, 1), ("v", 2, 1), ("h", 1, 2)]
# a 1-tile room (5, 2)
ROOM1 = [("h", 5, 2), ("h", 5, 3), ("v", 5, 2), ("v", 6, 2)]


def test_rooms():
    bd = walled_map(walls=ROOM3 + ROOM1)
    r = hy.rooms(bd)
    assert {t for t, (_, n) in r.items() if n == 3} == {(1, 0), (2, 0), (1, 1)}
    assert r[(5, 2)][1] == 1
    assert (0, 0) not in r and (4, 4) not in r  # the rest: one big region
    assert r[(1, 0)][0] == r[(1, 1)][0] != r[(5, 2)][0]
    # the cap: a 3-tile room is not a room under cap 2, and is one under cap 3
    assert (1, 0) not in hy.rooms(bd, cap=2) and (5, 2) in hy.rooms(bd, cap=2)
    assert (1, 0) in hy.rooms(bd, cap=3)
    # one wall less: the room opens onto the big region
    bd2 = walled_map(walls=[w for w in ROOM3 if w != ("h", 1, 2)])
    assert (1, 1) not in hy.rooms(bd2)
    # a portal bounds a room as a wall does: its south wall made a portal to the top of (6, 2) in the big region
    bd3 = walled_map(walls=[w for w in ROOM3 if w != ("h", 1, 2)], portals=[(("h", 1, 2), ("h", 6, 2))])
    assert bd3.step((1, 1), 'S') == (6, 2)
    assert {t for t, (_, n) in hy.rooms(bd3).items() if n == 3} == {(1, 0), (2, 0), (1, 1)}


ROOM = {(1, 0): (0, 3), (2, 0): (0, 3), (1, 1): (0, 3), (5, 2): (1, 1)}


def test_late_split_entry():
    # a 2-long A dragon (1,2)-(1,3) outside steps into the 3-tile room and eats at (1,1), (1,0), (2,0) (the tail stays),
    # splits 3 (the child: its head the old tail, outside) and the parent of 2 dies inside
    team, body = {7: 'A'}, {7: [(1, 2), (1, 3)]}
    ev = [('update', 7, (1, 1), (1, 3)),            # in, eats: length 3
          ('update', 7, (1, 0), (1, 3)),            # eats: 4
          ('update', 7, (2, 0), (1, 3)),            # eats: 5
          ('split', 7, 8, 'A', [(2, 0), (1, 0)], [(1, 3), (1, 2), (1, 1)]),
          ('death', 7)]
    (e,) = hy.track(ev, ROOM, team, 'A', body)
    assert e['ate'] and e['len_in'] == 2 and e['size'] == 3 and e['children'] == [3] and e['died']
    assert e['net_seg'] == 1 and e['net_drg'] == 0       # 3 pearls - 2
    assert hy.harvest_like(e)
    assert not hy.harvest_like(dict(e, ate=False)) and not hy.harvest_like(dict(e, len_in=4))
    assert not hy.harvest_like(dict(e, len_in=1))
    assert team[8] == 'A' and body[8][0] == (1, 3)


def test_left_alive_and_no_eat():
    # a 4-long dragon steps in without eating (the tail moves): len_in 4, not harvest-like; it splits 2 (the child
    # reversed, its head the old tail) and the parent leaves alive with 2
    team, body = {3: 'B'}, {3: [(1, 2), (1, 3), (1, 4), (1, 5)]}
    ev = [('update', 3, (1, 1), (1, 4)),
          ('split', 3, 4, 'B', [(1, 1), (1, 2)], [(1, 4), (1, 3)]),
          ('update', 3, (0, 1), (1, 1))]
    (e,) = hy.track(ev, ROOM, team, 'B', body)
    assert not e['ate'] and e['len_in'] == 4 and not hy.harvest_like(e)
    assert not e['died'] and e['len_out'] == 2 and e['children'] == [2]
    assert e['net_seg'] == 0 and e['net_drg'] == 1


def test_side_filter_and_one_tile():
    # side B's entries are not counted for A; a 2-long A dragon eats into the 1-tile room and dies there
    team, body = {1: 'A', 2: 'B'}, {1: [(5, 1), (5, 0)], 2: [(1, 2), (1, 3)]}
    ev = [('update', 2, (1, 1), (1, 3)),
          ('update', 1, (5, 2), (5, 0)),
          ('death', 1)]
    es = hy.track(ev, ROOM, team, 'A', body)
    assert len(es) == 1 and es[0]['id'] == 1 and es[0]['size'] == 1
    assert es[0]['net_seg'] == -2 and es[0]['net_drg'] == -1 and hy.harvest_like(es[0])


def test_open_at_end_and_reentry():
    # leaving and coming back is a second entry; still inside at the game end, len_out is its length
    team, body = {5: 'A'}, {5: [(1, 2), (1, 3)]}
    ev = [('update', 5, (1, 1), (1, 3)),   # in, eats: 3
          ('update', 5, (0, 1), (1, 2)),   # out (the tail moves): the first entry ends alive with 3
          ('update', 5, (0, 0), (1, 1)),
          ('update', 5, (1, 0), (1, 1))]   # in again, eats: 4 - open at the end
    es = hy.track(ev, ROOM, team, 'A', body)
    assert len(es) == 2
    assert es[0]['len_in'] == 2 and es[0]['len_out'] == 3 and es[0]['net_seg'] == 1 and es[0]['net_drg'] == 0
    assert es[1]['len_in'] == 3 and es[1]['ate'] and es[1]['len_out'] == 4 and es[1]['net_seg'] == 1


def test_no_step_no_entry():
    # a head already in a room, an update that does not move it (its tail after a split): no entry
    team, body = {9: 'A'}, {9: [(1, 1), (1, 2), (1, 3)]}
    assert hy.track([('update', 9, (1, 1), (1, 2))], ROOM, team, 'A', body) == []


def test_table():
    e = dict(size=3, net_seg=1, net_drg=0, died=True)
    t = hy.table([('x', [e, e])])
    assert '| x | 3 | 2 | +2 / +1.00 | +0 / +0.00 | 0 | 2 |' in t


def test_replay_files(tmp_path):
    for f in ('stripes__a.replay', 'stripes__b.replay', 'stripes__c.replay.zst', 'trauma__x.replay', 'stripes__d.out',
              'stripes__e.zst'):
        (tmp_path / f).write_bytes(b'')
    assert [os.path.basename(f) for f in hy.replay_files([str(tmp_path)], 'stripes')] == [
        'stripes__a.replay', 'stripes__b.replay', 'stripes__c.replay.zst']


def test_cli_on_fixtures(replays, capsys):
    hy.main(['abyss', os.path.dirname(replays['mirror']), '--all-entries'])
    out = capsys.readouterr().out
    assert out.startswith('2 replays') and '| bot | room tiles |' in out
