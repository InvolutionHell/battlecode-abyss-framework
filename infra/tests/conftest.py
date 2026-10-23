"""Test setup: the infra modules import from their own folders; every test runs with a private LAB_DATA."""
import importlib
import os
import sys

import pytest

INFRA = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
for sub in ("lab", "sweep", "tuning"):
    sys.path.insert(0, os.path.join(INFRA, sub))


@pytest.fixture
def labenv(tmp_path, monkeypatch):
    """A clean lab environment: LAB_DATA in tmp, a two-node pool file, no env file; returns (tmp_path, modules)."""
    for k in list(os.environ):
        if k.startswith("LAB_") or k in ("BC_BOTS_DIR",):
            monkeypatch.delenv(k, raising=False)
    nodes = tmp_path / "nodes.toml"
    nodes.write_text('[nodes]\n"worker-a" = 30\n"worker-b" = 60\n[scheduler]\nmin_slots = 20\nbig_games = 44\n'
                     'big_min = 20\npush_workers = 2\n')
    monkeypatch.setenv("LAB_ENV", str(tmp_path / "none.env"))
    monkeypatch.setenv("LAB_DATA", str(tmp_path / "data"))
    monkeypatch.setenv("LAB_NODES", str(nodes))
    import labconf
    importlib.reload(labconf)
    import lab
    importlib.reload(lab)
    return tmp_path, lab
