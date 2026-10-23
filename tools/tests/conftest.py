"""pytest configuration: the `replays` fixture (the fixture replays; skipped without pycapnp). See helpers.py."""
import pytest

from helpers import MIRROR, VS_STARTER, need_capnp


@pytest.fixture
def replays():
    need_capnp()
    return {"vs_starter": VS_STARTER, "mirror": MIRROR}
