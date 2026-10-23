"""Reading .replay files.

A replay is a packed Cap'n Proto message (schema: abyss_tools/schema/replay.capnp, vendored from the unswbc engine). Files may
be stored gzip- or zstd-compressed (`.replay.gz`, `.replay.zst`); `load` detects the compression from the magic bytes.

Arena replay names follow `MAP__A_vs_B[_rN].replay`: the map's file stem, the two bot folders in side order, and the repeat
index (see abyss_tools.run.arena). The arena copies each bot under a side-specific name (`<bot>_L`, `<bot>_R`), so the bot
names stored in a replay may carry that suffix; `base_name` strips it.
"""
import glob
import gzip
import os
import shutil
import subprocess

SCHEMA_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "schema")
REPLAY_EXTS = (".replay", ".replay.zst", ".replay.gz")

_schema = None


def schema():
    """the compiled replay schema (pycapnp), loaded once"""
    global _schema
    if _schema is None:
        try:
            import capnp
        except ImportError as e:  # pragma: no cover - depends on the environment
            raise ImportError("reading replays needs pycapnp: pip install pycapnp (or uv run --with pycapnp ...)") from e
        capnp.remove_import_hook()
        _schema = capnp.load(os.path.join(SCHEMA_DIR, "replay.capnp"), imports=[SCHEMA_DIR])
    return _schema


def _unzstd(raw):
    try:
        import zstandard
    except ImportError:
        exe = shutil.which("zstd")
        if not exe:
            raise ImportError("a .zst replay needs the zstandard module or the zstd command line tool")
        return subprocess.run([exe, "-dc"], input=raw, capture_output=True, check=True).stdout
    return zstandard.ZstdDecompressor().decompressobj().decompress(raw)  # works without a content size in the frame


def decompress(raw):
    """plain, gzip or zstd bytes (also gzip inside zstd) -> the packed message"""
    if raw[:4] == b"\x28\xb5\x2f\xfd":
        raw = _unzstd(raw)
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    return raw


def load(path):
    """a replay file -> its Replay message (pycapnp reader)"""
    with open(path, "rb") as f:
        raw = decompress(f.read())
    return schema().Replay.from_bytes_packed(raw, traversal_limit_in_words=2 ** 40, nesting_limit=128)


def initial_teams(map_text):
    """{dragon id: 'A' | 'B'} of the starting dragons: ids follow the map's DRAGON lines in file order, team 0 is A"""
    teams = []
    for line in map_text.splitlines():
        parts = line.split()
        if parts and parts[0] == "DRAGON":
            teams.append("A" if parts[1] == "0" else "B")
    return dict(enumerate(teams))


def map_name(map_text):
    return next((l.split(None, 1)[1] for l in map_text.splitlines() if l.startswith("MAP_NAME")), "?")


def winner_of(rep):
    """'A', 'B' or None (no winner)"""
    res = rep.result
    return None if res.which() == "noWinner" else ("A" if res.winner == "a" else "B")


def side_of_team(team_enum):
    """a replay Team enum value ('a' / 'b') -> 'A' / 'B'"""
    return "A" if str(team_enum) == "a" else "B"


def base_name(name):
    """a bot name without the arena's side suffix (_L / _R)"""
    return name[:-2] if name.endswith(("_L", "_R")) else name


def map_stem(path):
    """the map part of an arena replay name (MAP__A_vs_B...)"""
    return os.path.basename(path).split("__")[0]


def is_replay(path):
    return path.endswith(REPLAY_EXTS)


def replay_files(dirs, prefix=""):
    """the replays directly inside the given folders (plain, .zst or .gz), sorted per folder and extension"""
    if isinstance(dirs, str):
        dirs = [dirs]
    return [f for d in dirs for ext in REPLAY_EXTS
            for f in sorted(glob.glob(os.path.join(os.path.expanduser(d), prefix + "*" + ext)))]


def replay_files_recursive(root):
    """every replay under root, one per game (a compressed copy wins over the plain file)"""
    seen = {}
    for p in sorted(glob.glob(os.path.join(os.path.expanduser(root), "**", "*.replay*"), recursive=True)):
        if not is_replay(p):
            continue
        base = p[:-3] if p.endswith(".gz") else p[:-4] if p.endswith(".zst") else p
        if base not in seen or p != base:
            seen[base] = p
    return sorted(seen.values())


def arm_label(d):
    """a replay folder -> the label of its arm: the folder's name, or its parent's for a folder called `replays`"""
    d = os.path.normpath(os.path.expanduser(d))
    base = os.path.basename(d)
    return os.path.basename(os.path.dirname(d)) if base == "replays" else base
