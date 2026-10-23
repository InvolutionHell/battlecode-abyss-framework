"""abyss_tools.maps: the generators make legal, exactly symmetric maps with alternating teams."""
import json
import os

import pytest

from abyss_tools.maps import mapgen, mapgen_cls, pearl_variants
from abyss_tools.replay.state import Board


def dragons(text):
    return [l.split() for l in text.splitlines() if l.startswith("DRAGON ")]


def symmetric(text):
    """beds, kelp and portals map onto themselves under the declared symmetry; team B's dragons mirror team A's"""
    m = mapgen_cls.parse(text)
    assert all(m.beds.get(m.T(t)) == v for t, v in m.beds.items())
    assert all(m.TE(e) in m.kelp for e in m.kelp)
    assert all(m.TE(e) in m.portal for e in m.portal)
    a = [b for tm, b in m.dragons if tm == 0]
    b = [b for tm, b in m.dragons if tm == 1]
    assert sorted(tuple(m.T(t) for t in body) for body in a) == sorted(tuple(body) for body in b)
    return m


def test_mapgen_deterministic_and_valid():
    for seed in range(1, 13):
        text = mapgen.make(seed, f"g{seed:06d}")
        assert text == mapgen.make(seed, f"g{seed:06d}")
        m = symmetric(text)
        teams = [d[1] for d in dragons(text)]
        assert teams == ["0", "1"] * (len(teams) // 2)                    # teams alternate, A first
        bd = Board(text)
        assert len(bd.dragons) == len(teams) and (bd.w, bd.h) == (m.w, m.h)
        for _, body in bd.dragons:                                         # consecutive segments are neighbours (torus)
            assert all(bd.step(body[i + 1], d) == body[i] for i in range(len(body) - 1)
                       for d in [bd.dir_between(body[i + 1], body[i])])
    b_first = [d[1] for d in dragons(mapgen.make(5, "x", b_first=True))]
    assert b_first[:2] == ["1", "0"]


def test_mapgen_cli(tmp_path, capsys):
    assert mapgen.main(["--out", str(tmp_path), "--count", "3", "--seed", "900000", "--prefix", "h"]) == 0
    assert sorted(os.listdir(tmp_path)) == ["h900000.map", "h900001.map", "h900002.map"]


@pytest.mark.parametrize("fam", ["SEP", "SMALL", "SCATTER"])
def test_mapgen_cls_family(fam, tmp_path):
    (tmp_path / fam).mkdir()
    row = mapgen_cls.build((fam, 0, 7, str(tmp_path)))
    assert row["family"] == fam and row["file"] == os.path.join(fam, f"{fam}_00.map")
    text = (tmp_path / fam / f"{fam}_00.map").read_text()
    m = mapgen_cls.parse(text)
    assert mapgen_cls.validate(m) == []
    f = mapgen_cls.feat(m)
    assert mapgen_cls.INTENT[fam](f) and mapgen_cls.classifier_ok(fam, f)
    assert mapgen_cls.build((fam, 0, 7, str(tmp_path)))["seed"] == row["seed"]     # deterministic in (seed, class, index)


def test_mapgen_cls_cli(tmp_path, capsys):
    mapgen_cls.main(["--out", str(tmp_path), "--classes", "SMALL", "--count", "2", "--jobs", "1"])
    man = json.loads((tmp_path / "manifest.json").read_text())
    assert [r["file"] for r in man["maps"]] == [os.path.join("SMALL", "SMALL_00.map"), os.path.join("SMALL", "SMALL_01.map")]
    assert man["generator"] == "abyss_tools.maps.mapgen_cls"
    capsys.readouterr()
    mapgen_cls.show(str(tmp_path / "SMALL" / "SMALL_00.map"))
    assert "'cls2': 'SMALL'" in capsys.readouterr().out


def test_pearl_variants(tmp_path):
    src = tmp_path / "src"
    src.mkdir()
    for seed in (11, 12):
        (src / f"m{seed}.map").write_text(mapgen.make(seed, f"m{seed}"))
    out = tmp_path / "pv"
    paths = [str(src / "m11.map"), str(src / "m12.map")]
    assert pearl_variants.main(paths + ["--out", str(out), "--per-map", "3", "--seed", "4"]) == 0
    assert len(os.listdir(out)) == 6
    for f in sorted(os.listdir(out)):
        text = (out / f).read_text()
        base = (src / (f.split("_pv")[0] + ".map")).read_text()
        keep = lambda t: [l for l in t.splitlines() if not l.startswith(("TILE ", "MAP_NAME"))]  # noqa: E731
        assert keep(text) == keep(base)                                    # only the beds and the name change
        m = mapgen_cls.parse(text)
        assert all(m.beds.get(t) == m.beds.get(m.T(t)) for t in m.beds), f   # symmetric beds
        assert f"MAP_NAME {f[:-4]}" in text
