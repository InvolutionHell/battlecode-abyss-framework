"""Shared test helpers (paths of the fixtures, skips for missing tools, the starter template).

The fixture replays in tests/fixtures were recorded with unswbc 1.2.9 on the official Arena map, in the judge's sandbox
(`--sandbox`), from an unswbc workspace holding bots/abyss as `abyss`, a renamed copy of it as `abyss_b` and the toolkit's
C++ starter template as `starter`:

  arena__abyss_vs_starter.replay   unswbc run maps/arena.map abyss starter --sandbox --seed 1    (45 rounds, team A wins)
  arena__abyss_vs_abyss_b.replay   unswbc run maps/arena.map abyss abyss_b --sandbox --seed 2    (179 rounds, team B wins)

In the sandbox the seed fixes the pearls and the bots' random numbers and the metered clock makes the search depth
reproducible, so these commands write the same bytes again; native games (no --sandbox) differ from run to run.

Every test that reads a replay needs pycapnp and is skipped without it; tests that run the engine or a compiler are skipped
when `unswbc` or a C++ compiler is not installed.
"""
import hashlib
import os
import shutil
import subprocess

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, "fixtures")
VS_STARTER = os.path.join(FIXTURES, "arena__abyss_vs_starter.replay")
MIRROR = os.path.join(FIXTURES, "arena__abyss_vs_abyss_b.replay")

# the C++ starter template of unswbc 1.2.9 that played team B of VS_STARTER (sha256 of main.cpp and helper.hpp)
STARTER_SHA = {"main.cpp": "58752fa6fe04ed97672e24eb90a3e6cbd3dbb86158ff80156c2b0311520aa575",  # pragma: allowlist secret
               "helper.hpp": "ca57d2398f309144ce98f6c899d28847672cd9914ff8be03afdbb0379e7365b7"}  # pragma: allowlist secret
QUIET_UNSWBC = {"UNSWBC_NO_UPDATE": "1", "UNSWBC_NO_WARM": "1", "UNSWBC_NO_VSCODE": "1"}


def need_capnp():
    pytest.importorskip("capnp", reason="reading replays needs pycapnp")


def need_unswbc():
    exe = os.environ.get("UNSWBC") or shutil.which("unswbc")
    if not exe:
        pytest.skip("unswbc is not installed")
    return exe


def need_cxx():
    cxx = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not cxx:
        pytest.skip("no C++ compiler")
    return cxx


def unswbc_env():
    return dict(os.environ, **QUIET_UNSWBC)


def make_starter(workspace, name="starter"):
    """the toolkit's C++ starter template in workspace/name (`unswbc init cpp`); skips when it is not the template that
    recorded the fixtures"""
    exe = need_unswbc()
    subprocess.run([exe, "init", "cpp", name], cwd=workspace, env=unswbc_env(), capture_output=True, check=True)
    d = os.path.join(workspace, name)
    for f, want in STARTER_SHA.items():
        with open(os.path.join(d, f), "rb") as fh:
            if hashlib.sha256(fh.read()).hexdigest() != want:
                pytest.skip("this unswbc's starter template differs from the one that recorded the fixtures")
    return d
