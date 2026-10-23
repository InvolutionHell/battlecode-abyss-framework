"""Configuration of the batch evaluation platform (lab.py, autosweep.py).

Everything site-specific comes from the environment, optionally pre-filled from an env file, and from two TOML files:

  env file   $LAB_ENV, default infra/config/lab.env (copy infra/config/example.env). KEY=VALUE lines; variables already
             set in the environment win over the file; single-quoted values are taken literally (no $VAR expansion),
             so remote-side variables such as $HOME stay for the node's shell.
  node pool  $LAB_NODES, default infra/config/nodes.toml (copy infra/config/nodes.example.toml): ssh alias -> slots,
             plus the scheduler thresholds.
  map sets   $LAB_MAPSETS, default infra/config/mapsets.toml, falling back to infra/config/mapsets.example.toml.

Relative paths in the settings are relative to the repository root. Nothing here talks to the network.
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
INFRA = os.path.dirname(HERE)
CONFIG = os.path.join(INFRA, "config")

DEFAULT_SSH_OPTS = "-o BatchMode=yes -o ConnectTimeout=20 -o ServerAliveInterval=30"
DEFAULT_SCHEDULER = {"min_slots": 20, "big_min": 20, "big_games": 44, "push_workers": 8}


def load_env(path=None):
    """Pre-fill os.environ from a KEY=VALUE file (no override of variables already set). Returns the path used."""
    path = path or os.environ.get("LAB_ENV") or os.path.join(CONFIG, "lab.env")
    if not os.path.isabs(path) and not os.path.exists(path):
        path = os.path.join(os.path.dirname(INFRA), path)
    if not os.path.exists(path):
        return None
    with open(path) as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("export "):
                line = line[len("export "):].strip()
            key, sep, value = line.partition("=")
            key = key.strip()
            if not sep or not re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", key):
                continue
            value = value.strip()
            if len(value) >= 2 and value[0] == value[-1] == "'":
                value = value[1:-1]                       # single quotes: literal, as in a shell
            elif len(value) >= 2 and value[0] == value[-1] == '"':
                value = os.path.expandvars(value[1:-1])
            else:
                value = os.path.expandvars(value.split(" #", 1)[0].strip())
            os.environ.setdefault(key, value)
    return path


def _abs(path):
    """A configured path: absolute as given, relative to the repository root otherwise."""
    path = os.path.expanduser(path)
    return path if os.path.isabs(path) else os.path.join(repo_root(), path)


def repo_root():
    return os.path.abspath(os.environ.get("LAB_REPO") or os.path.dirname(INFRA))


def bots_dir():
    return os.path.abspath(_abs(os.environ.get("BC_BOTS_DIR") or "bots"))


def data_dir():
    """Local registry and collected results."""
    return os.path.abspath(_abs(os.environ.get("LAB_DATA") or os.path.join("data", "lab")))


def remote_root():
    """The checkout on every worker node. A relative value is relative to the remote home directory. It is used
    unquoted in remote shell commands, so it must not contain spaces."""
    r = os.environ.get("LAB_REMOTE_ROOT") or "abyss"
    if r.startswith("/") or r.startswith("~"):
        return r
    return "~/" + r


def ssh_opts():
    return (os.environ.get("LAB_SSH_OPTS") or DEFAULT_SSH_OPTS).split()


def push_paths():
    """Repository paths that `lab.py push` copies to the nodes (relative to the repository root)."""
    return (os.environ.get("LAB_PUSH") or "bots infra").split()


# ---------------------------------------------------------------- TOML
def read_toml(path):
    try:
        import tomllib  # Python 3.11+
        with open(path, "rb") as f:
            return tomllib.load(f)
    except ImportError:
        pass
    try:
        import tomli
        with open(path, "rb") as f:
            return tomli.load(f)
    except ImportError:
        with open(path) as f:
            return parse_toml_subset(f.read())


def _strip_comment(line):
    out, quote = [], None
    for ch in line:
        if quote:
            if ch == quote:
                quote = None
        elif ch in "'\"":
            quote = ch
        elif ch == "#":
            break
        out.append(ch)
    return "".join(out).strip()


def _value(text):
    text = text.strip()
    if text.startswith("["):
        inner = text[1:-1].strip()
        items, cur, quote = [], [], None
        for ch in inner:
            if quote:
                cur.append(ch)
                if ch == quote:
                    quote = None
            elif ch in "'\"":
                quote = ch
                cur.append(ch)
            elif ch == ",":
                items.append("".join(cur))
                cur = []
            else:
                cur.append(ch)
        items.append("".join(cur))
        return [_value(i) for i in items if i.strip()]
    if len(text) >= 2 and text[0] == text[-1] and text[0] in "'\"":
        return text[1:-1]
    if text in ("true", "false"):
        return text == "true"
    try:
        return int(text)
    except ValueError:
        return float(text)


def _key(k):
    k = k.strip()
    return k[1:-1] if len(k) >= 2 and k[0] == k[-1] and k[0] in "'\"" else k


def parse_toml_subset(text):
    """The TOML subset the example files use (for Python < 3.11 without tomli): [table] and [a.b] headers, bare or
    quoted keys, strings, integers, floats, booleans and (possibly multi-line) arrays of those."""
    root = {}
    table = root
    pending = None
    for raw in text.splitlines():
        line = _strip_comment(raw)
        if pending is not None:
            pending[1].append(line)
            if line.endswith("]"):
                key, parts = pending
                table[key] = _value(" ".join(parts))
                pending = None
            continue
        if not line:
            continue
        if line.startswith("[") and line.endswith("]"):
            table = root
            for part in re.findall(r'"[^"]*"|\'[^\']*\'|[^.]+', line[1:-1].strip()):
                table = table.setdefault(_key(part), {})
            continue
        key, sep, value = line.partition("=")
        if not sep:
            raise ValueError(f"cannot parse TOML line: {raw!r}")
        key, value = _key(key), value.strip()
        if value.startswith("[") and not value.endswith("]"):
            pending = (key, [value])
            continue
        table[key] = _value(value)
    return root


# ---------------------------------------------------------------- node pool and map sets
def nodes_path():
    return _abs(os.environ["LAB_NODES"]) if os.environ.get("LAB_NODES") else os.path.join(CONFIG, "nodes.toml")


def load_pool(path=None):
    """({ssh alias: slots}, scheduler settings). Slots = games a node may run at once (see nodes.example.toml)."""
    path = path or nodes_path()
    if not os.path.exists(path):
        raise SystemExit(f"node pool file {path} not found: copy infra/config/nodes.example.toml to "
                         f"infra/config/nodes.toml (or set LAB_NODES) and list your ssh aliases")
    conf = read_toml(path)
    nodes = {str(k): int(v) for k, v in (conf.get("nodes") or {}).items()}
    sched = dict(DEFAULT_SCHEDULER)
    sched.update({k: int(v) for k, v in (conf.get("scheduler") or {}).items()})
    return nodes, sched


def mapsets_path():
    p = _abs(os.environ["LAB_MAPSETS"]) if os.environ.get("LAB_MAPSETS") else os.path.join(CONFIG, "mapsets.toml")
    return p if os.path.exists(p) else os.path.join(CONFIG, "mapsets.example.toml")


def load_mapsets(path=None):
    """({name: (maps string, pv count, pv dir)}, default set name)."""
    conf = read_toml(path or mapsets_path())
    sets = {}
    for name, s in (conf.get("sets") or {}).items():
        sets[name] = (" ".join(s.get("maps", [])), int(s.get("pv", 0)), s.get("pv_dir") or "pv")
    default = conf.get("default_set") or (sorted(sets)[0] if sets else None)
    return sets, default
