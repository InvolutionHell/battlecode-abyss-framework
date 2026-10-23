"""abyss_tools.kpi.opening: the opening KPIs from synthetic event streams, and the tables over the fixtures."""
import os

from abyss_tools.kpi import opening as ok
from abyss_tools.replay.io import base_name, replay_files


def start():
    # A's queen 0 and B's queen 1 (2 long each), A's 1-long dragon 2
    return {0: 'A', 1: 'B', 2: 'A'}, {0: [(5, 5), (5, 6)], 1: [(1, 1), (1, 2)], 2: [(8, 8)]}


def test_first_eat_and_lengths():
    team, body = start()
    ev = [('round', 1), ('update', 0, (5, 4), (5, 5)),          # A moves, no eat
          ('round', 2), ('update', 1, (1, 0), (1, 2)),          # B eats at r2 (the tail stays)
          ('round', 3), ('update', 2, (8, 7), (8, 8)),          # A eats at r3 (a 1-long grows)
          ('round', 4), ('update', 0, (5, 3), (5, 5)),          # A eats again at r4: the first eat stays r3
          ('round', 40)]
    k = ok.kpis(ev, team, body, None, {'A': 2, 'B': 1})
    assert k['A']['first'] == 3 and k['B']['first'] == 2
    assert k['A']['l40'] == 3 + 2 and k['B']['l40'] == 3     # A: 0 (3 long) + 2 (2 long); B: 1 (3 long)
    assert not k['A']['won'] and not k['B']['won'] and not k['A']['wiped']


def test_never_eats():
    team, body = start()
    k = ok.kpis([('round', 1), ('update', 0, (5, 4), (5, 5)), ('round', 40)], team, body, None, {'A': 2, 'B': 1})
    assert k['A']['first'] == 999 and k['B']['first'] == 999


def test_units_share_queen_and_wipe():
    team, body = start()
    ev = [('round', 5), ('update', 0, (5, 4), (5, 6)), ('update', 0, (5, 3), (5, 6)),   # A's queen grows to 4
          ('split', 0, 3, 'A', [(5, 3), (5, 4)], [(5, 6), (5, 5)]),                    # and splits: 3 A units
          ('death', 1),                                                                # B's queen dies
          ('round', 100)]
    k = ok.kpis(ev, team, body, 'A', {'A': 3, 'B': 0})
    assert k['A']['u100'] == 3 and k['B']['u100'] == 0
    assert k['A']['share'] == 1.0 and k['B']['share'] == 0.0   # the opponent has none: 1 if any, else 0
    assert k['A']['q100'] and not k['B']['q100']
    assert k['A']['won'] and not k['B']['won'] and k['B']['wiped'] and not k['A']['wiped']
    # a loss with dragons left is not a wipe
    team, body = start()
    k = ok.kpis([('round', 100)], team, body, 'A', {'A': 2, 'B': 1})
    assert not k['B']['wiped'] and k['A']['share'] == 2.0 and k['B']['share'] == 0.5


def test_opening_judge_kpis():
    # units at r50, the r100 unit difference, eats before r100 only, and the round a wiped side lost its last dragon
    team, body = start()
    ev = [('round', 10), ('update', 0, (5, 4), (5, 6)),                                 # A eats (r10)
          ('round', 50), ('update', 1, (1, 0), (1, 2)),                                 # B eats (r50)
          ('round', 60), ('death', 1),                                                  # B's only dragon dies at r60
          ('round', 100), ('update', 0, (5, 3), (5, 6))]                                # A eats at r100: not before r100
    k = ok.kpis(ev, team, body, 'A', {'A': 2, 'B': 0})
    assert k['A']['u50'] == 2 and k['B']['u50'] == 1
    assert k['A']['du100'] == 2 and k['B']['du100'] == -2
    assert k['A']['eat100'] == 1 and k['B']['eat100'] == 1
    assert k['B']['wround'] == 60 and k['A']['wround'] is None
    r = dict(k['B'], bot='x')
    assert ok.row_open('trophy', 'x', [r, dict(r, wround=None, u50=3)]) == '| trophy | x | 2 | 2.0 | 0.0 | -2.0 | 1.0 | 1 | 60 |'


def test_split_child_counts_after():
    # a child born after r40 is not in the r40 length, but is a unit at r100
    team, body = start()
    ev = [('round', 40), ('round', 50), ('update', 1, (1, 0), (1, 2)), ('update', 1, (2, 0), (1, 2)),
          ('split', 1, 4, 'B', [(2, 0), (1, 0)], [(1, 2), (1, 1)]), ('round', 100)]
    k = ok.kpis(ev, team, body, None, {'A': 2, 'B': 2})
    assert k['B']['l40'] == 2 and k['B']['u100'] == 2 and k['B']['first'] == 50


def test_name_and_row():
    assert base_name('cand_h1_L') == 'cand_h1' and base_name('cand') == 'cand'
    r = dict(won=True, wiped=False, first=30, l40=7, u100=8, share=2.0, q100=True)
    assert ok.row('stripes', 'x', [r, dict(r, won=False, first=40)]) == '| stripes | x | 2 | 1 | 0 | 35.0 | 1 | 7.0 | 8.0 | 2.00 | 2 |'


def test_replay_files(tmp_path):
    # plain and compressed replays, nothing else
    for f in ('stripes__a.replay', 'stripes__b.replay', 'stripes__c.replay.zst', 'trauma__x.replay', 'stripes__d.out',
              'stripes__e.zst'):
        (tmp_path / f).write_bytes(b'')
    assert [os.path.basename(f) for f in replay_files([str(tmp_path)])] == [
        'stripes__a.replay', 'stripes__b.replay', 'trauma__x.replay', 'stripes__c.replay.zst']


def test_job_on_fixtures(replays):
    mp, rows = ok.job(replays['vs_starter'])
    assert mp == 'arena' and [r['bot'] for r in rows] == ['abyss', 'starter']
    a, b = rows
    assert a['won'] and not b['won'] and b['wiped'] and b['wround'] == 44
    assert (a['first'], a['l40'], a['eat100']) == (29, 10, 21) and (b['first'], b['l40'], b['eat100']) == (7, 25, 24)
    mp, rows = ok.job(replays['mirror'])
    assert rows[0]['wiped'] and rows[0]['wround'] == 178 and rows[1]['won'] and not rows[1]['wiped']
    assert (rows[0]['first'], rows[1]['first']) == (14, 10) and (rows[0]['l40'], rows[1]['l40']) == (6, 5)


def test_cli_on_fixtures(replays, capsys):
    ok.main([os.path.dirname(replays['vs_starter']), '-j', '1'])
    out = capsys.readouterr().out
    assert '| arena | abyss | 2 |' in out and '| all | starter | 1 |' in out
