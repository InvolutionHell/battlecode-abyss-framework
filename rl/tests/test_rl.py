"""Tests of the learning pipeline: featurizer identity, runtime headers, records, training / export, consistency.

  rl/tests/run_tests.sh            # or: uv run --with pytest --with numpy --with pycapnp --with torch pytest rl/tests

Needs a C++17 compiler. Tests that read replays need pycapnp and replay files: by default the small fixture replays of
tools/tests/fixtures, or the files listed in RL_TEST_REPLAYS (colon separated). Tests that train need torch. Missing
pieces skip the tests that need them.
"""
import glob
import json
import os
import shutil
import subprocess
import sys

import numpy as np
import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
RL = os.path.dirname(HERE)
ROOT = os.path.dirname(RL)
BOT = os.path.join(ROOT, "bots", "abyss")
PY = sys.executable
CXX = os.environ.get("CXX") or shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
CXXFLAGS = ["-std=c++17", "-O2", "-ffp-contract=off"]
sys.path[:0] = [os.path.join(RL, "eval", "consistency"), os.path.join(RL, "data"), os.path.join(RL, "train")]

needs_cxx = pytest.mark.skipif(not CXX, reason="no C++ compiler")


def replays():
    env = os.environ.get("RL_TEST_REPLAYS")
    if env:
        return [p for p in env.split(":") if p]
    return sorted(glob.glob(os.path.join(ROOT, "tools", "tests", "fixtures", "*.replay*")))


def needs_replays():
    pytest.importorskip("capnp")
    if not replays():
        pytest.skip("no replay files (set RL_TEST_REPLAYS)")
    return replays()


def run(cmd, **kw):
    p = subprocess.run(cmd, capture_output=True, text=True, **kw)
    assert p.returncode == 0, f"{' '.join(map(str, cmd))}\n{p.stdout}\n{p.stderr}"
    return p.stdout + p.stderr


def compile_cpp(src, out, *flags):
    run([CXX] + CXXFLAGS + list(flags) + [src, "-o", out], cwd=ROOT)
    return out


@pytest.fixture(scope="session")
def work(tmp_path_factory):
    return tmp_path_factory.mktemp("rl")


@pytest.fixture(scope="session")
def featurizers(work):
    if not CXX:
        pytest.skip("no C++ compiler")
    out = work / "bin"
    run([os.path.join(RL, "featurize", "build.sh"), str(out)], env={**os.environ, "CXX": CXX})
    return out


@pytest.fixture(scope="session")
def dummy(work):
    out = work / "dummy"
    run([PY, os.path.join(HERE, "dummy_weights.py"), str(out)])
    return out


# --- the featurizer used for training is the bot's featurizer ---------------------------------------------------------

def test_features_identical_to_bot():
    out = run([PY, os.path.join(RL, "featurize", "check_features.py")])
    assert "PASS" in out


def test_features_check_catches_a_change(tmp_path):
    for f in ("rl_features.hpp", "io.hpp"):
        shutil.copy(os.path.join(RL, "featurize", f), tmp_path / f)
    src = (tmp_path / "rl_features.hpp").read_text()
    (tmp_path / "rl_features.hpp").write_text(src.replace("std::exp(-v.pearlIn / 8.0f)", "std::exp(-v.pearlIn / 9.0f)"))
    p = subprocess.run([PY, os.path.join(RL, "featurize", "check_features.py"), "--featurize", str(tmp_path)],
                       capture_output=True, text=True)
    assert p.returncode == 1 and "MISMATCH" in p.stdout


# --- runtime headers ---------------------------------------------------------------------------------------------------

@needs_cxx
def test_runtime_smoke(work, dummy):
    src = os.path.join(HERE, "runtime_smoke.cpp")
    inc = ["-I", os.path.join(RL, "runtime"), "-I", os.path.join(RL, "featurize"), "-I", str(dummy)]
    exe = compile_cpp(src, str(work / "smoke"), "-DRL_KEEP_SCALAR", *inc)
    assert "PASS" in run([exe])
    exe = compile_cpp(src, str(work / "smoke_f2"), "-DSMOKE_F2", '-DF1_WEIGHTS_HEADER="f2_weights.hpp"', *inc)
    assert "PASS" in run([exe])


# --- records -----------------------------------------------------------------------------------------------------------

@pytest.fixture(scope="session")
def records(work, featurizers):
    reps = needs_replays()
    data = work / "data"
    data.mkdir(exist_ok=True)
    rdir = data / "replays"
    rdir.mkdir(exist_ok=True)
    for p in reps:
        shutil.copy(p, rdir / os.path.basename(p))
    run([PY, os.path.join(RL, "data", "index_replays.py"), str(data), "--out", str(data / "index.jsonl")])
    for kind, extra in (("rl", ["--sample", "1", "--aux", "--max-round", "500"]), ("f1", ["--move1-rate", "1"]),
                        ("f2", ["--move1-rate", "1", "--aux", "--returns", "--lineage", "--team-labels"])):
        run([PY, os.path.join(RL, "data", f"build_{kind}.py"), "--data", str(data), "--featurize",
             str(featurizers / f"featurize_{kind}"), "--shard", "0", "--nshards", "1", "--out", str(data / f"{kind}_0.bin")]
            + extra)
    return data


def test_records_are_well_formed(records):
    rows = [json.loads(l) for l in open(records / "index.jsonl")]
    assert rows and all({"game", "path", "team", "side", "won"} <= set(r) for r in rows)
    rl = np.fromfile(records / "rl_0.bin", np.uint8).reshape(-1, 6 + 843)
    f1 = np.fromfile(records / "f1_0.bin", np.uint8).reshape(-1, 16 + 843)
    f2 = np.fromfile(records / "f2_0.bin", np.uint8).reshape(-1, 16 + 843 + 24)
    assert len(rl) and len(f1) and len(f2)
    assert rl[:, 0].max() <= 8 and f1[:, 0].max() <= 15 and f2[:, 0].max() <= 15
    assert ((rl[:, 4] >> 7) & 1).all(), "build_rl.py --aux marks every record as labelled"
    feats = f1[:, 16:16 + 843]
    assert feats.max() <= 255 and feats[:, 3 * 49:4 * 49].sum() > 0, "our own body is in view"
    assert ((f1[:, 15] >> 1) == 100).all(), "every turn kept at rate 1: the keep-rate field says 100%"
    # every turn kept by both builders: the same turns, the same features and labels (f2 adds the memory inputs)
    cols = [0, 1, 2, 3, 5, 6, 7, 8]  # byte 4 differs: f2 carries the consequence labels (--aux)
    assert np.array_equal(f1[:, cols], f2[:, cols]) and np.array_equal(f1[:, 16:], f2[:, 16:16 + 843])
    assert ((f2[:, 4] >> 7) & 1).all() and not ((f1[:, 4] >> 7) & 1).any()
    assert len(rl) == len(f1) - (f1[:, 0] == 15).sum(), "the 9-class records skip only the no-action turns"
    assert (f2[:, 16 + 843:] > 0).any(), "the memory inputs are filled"


# --- training and export: the int16 export matches the torch model ------------------------------------------------------

def test_pretrain_export_matches_torch(work, records):
    torch = pytest.importorskip("torch")
    import pretrain
    from ref_forward import RLRef
    run_dir = work / "run_rl"
    run([PY, os.path.join(RL, "train", "pretrain.py"), str(records / "rl_0.bin"), "--out", str(run_dir), "--channels", "8",
         "--hidden", "16", "--epochs", "1", "--batch", "256", "--min-samples", "50", "--aux-weight", "0.5"])
    run([PY, os.path.join(RL, "train", "export_bot.py"), str(run_dir / "weights_rl.hpp"), str(run_dir / "rl_weights.hpp")])
    first = open(run_dir / "rl_weights.hpp").readline()
    assert first.startswith("// GENERATED by rl/train/export_bot.py from weights_rl.hpp") and str(ROOT) not in first
    rec = np.fromfile(records / "rl_0.bin", np.uint8).reshape(-1, 6 + 843)
    m = pretrain.Net(8, 16, aux=True)
    m.load_state_dict(torch.load(run_dir / "model.pt", map_location="cpu"))
    m.eval()
    with torch.no_grad():
        pol, val, _ = m(torch.from_numpy(rec[:, 6:].astype(np.float32) / 100),
                        torch.from_numpy(rec[:, 5].astype(np.float32) * 2 / 500).unsqueeze(1), with_aux=True)
    ref = RLRef(str(run_dir / "rl_weights.hpp")).forward(rec[:, 6:], rec[:, 5])
    p_torch = torch.softmax(pol[:, :9], 1).numpy()
    assert np.abs(ref["prob"] - p_torch).max() < 2e-3
    assert (ref["prob"].argmax(1) == p_torch.argmax(1)).mean() > 0.98
    assert np.abs(ref["value"] - val.numpy()).max() < 2e-2


def test_train_f1_export_matches_torch(work, records):
    torch = pytest.importorskip("torch")
    import train_f1
    from ref_forward import F1Ref
    for name, extra, npriv, nq in (("run_f1", [], 0, 0), ("run_f2", ["--mem", "24", "--q-team"], 24, 6)):
        kind = "f2" if npriv else "f1"
        run_dir = work / name
        run([PY, os.path.join(RL, "train", "train_f1.py"), str(records / f"{kind}_0.bin"), "--out", str(run_dir),
             "--channels", "8", "--hidden", "16", "--epochs", "1", "--batch", "256", "--min-samples", "50"] + extra)
        rec = np.fromfile(records / f"{kind}_0.bin", np.uint8).reshape(-1, 16 + 843 + npriv)
        m = train_f1.F1Net(8, 16, npriv=npriv, q=nq)
        m.load_state_dict(torch.load(run_dir / "model.pt", map_location="cpu"))
        m.eval()
        with torch.no_grad():
            pol, _, _, _ = m(torch.from_numpy(rec[:, 16:859].astype(np.float32) / 100),
                             torch.from_numpy(rec[:, 5].astype(np.float32) * 2 / 500).unsqueeze(1),
                             torch.from_numpy(rec[:, 859:].astype(np.float32) / 255) if npriv else None)
        ref = F1Ref(str(run_dir / "f1_weights.hpp")).forward(rec[:, 16:859], rec[:, 5], rec[:, 859:])
        p_torch = torch.softmax(pol, 1).numpy()
        assert np.abs(ref["prob"] - p_torch).max() < 2e-3, name
        assert (ref["prob"].argmax(1) == p_torch.argmax(1)).mean() > 0.98, name
        assert ref.get("qDie") is None or ref["qDie"].shape == (len(rec), 16)


# --- train / serve consistency: the C++ forward on the bot's own inputs equals the numpy reference ---------------------

def vectors(work, featurizers, name, weights):
    out = work / name
    run([PY, os.path.join(RL, "eval", "consistency", "make_vectors.py")] + needs_replays()
        + ["--featurize", str(featurizers / "featurize_f2"), "--out", str(out), "--dragons", "2"]
        + [f"--weights={k}={v}" for k, v in weights.items()])
    return out


@needs_cxx
def test_consistency_dummy_nets(work, featurizers, dummy, tmp_path):
    vec = vectors(work, featurizers, "vec_dummy", {"f1": dummy / "f1_weights.hpp", "f2": dummy / "f2_weights.hpp",
                                                   "rl": dummy / "rl_weights.hpp"})
    src = os.path.join(RL, "eval", "consistency", "check.cpp")
    inc = ["-I", os.path.join(RL, "featurize"), "-I", os.path.join(RL, "runtime"), "-I", str(dummy)]
    for name, flags in (("f1", []), ("f2", ['-DF1_WEIGHTS_HEADER="f2_weights.hpp"']), ("rl", ["-DCHECK_RLNET"])):
        exe = compile_cpp(src, str(work / f"check_{name}"), *flags, *inc)
        assert "PASS" in run([exe, str(vec / "turns.txt"), str(vec / f"expect_{name}.txt")]), name
    # negative control: other weights of the same shape must fail
    other = tmp_path / "other"
    run([PY, os.path.join(HERE, "dummy_weights.py"), str(other), "--seed", "1"])
    neg = vectors(work, featurizers, "vec_other", {"f1": other / "f1_weights.hpp"})
    p = subprocess.run([str(work / "check_f1"), str(neg / "turns.txt"), str(neg / "expect_f1.txt")], capture_output=True,
                       text=True)
    assert p.returncode == 1 and "FAIL" in p.stdout


@needs_cxx
def test_consistency_released_bot(work, featurizers):
    if not os.path.exists(os.path.join(BOT, "r4_weights.hpp")):
        pytest.skip("bots/abyss not present")
    vec = vectors(work, featurizers, "vec_bot", {"r4": os.path.join(BOT, "r4_weights.hpp"),
                                                 "rl": os.path.join(BOT, "rl_weights.hpp")})
    src = os.path.join(RL, "eval", "consistency", "check.cpp")
    exe = compile_cpp(src, str(work / "check_bot_r4"), '-DF1_NET_HEADER="r4net.hpp"', "-I", BOT, "-I",
                      os.path.join(RL, "runtime"))
    assert "PASS" in run([exe, str(vec / "turns.txt"), str(vec / "expect_r4.txt")])
    exe = compile_cpp(src, str(work / "check_bot_rl"), "-DCHECK_RLNET", "-I", BOT)
    assert "PASS" in run([exe, str(vec / "turns.txt"), str(vec / "expect_rl.txt")])
