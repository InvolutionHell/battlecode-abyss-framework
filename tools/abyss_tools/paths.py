"""Where things live: the repository root, the bots folder (an unswbc workspace), the maps, the toolkit and the caches.

Every default can be overridden, in this order: a command-line flag, an environment variable, the repository layout.

  ABYSS_ROOT    the repository root (default: the first parent of this package, then of the working directory, that holds
                a `bots/` folder; else the working directory)
  ABYSS_BOTS    the unswbc workspace with the bot folders (default: <root>/bots)
  ABYSS_MAPS    the folder of .map files (default: <bots>/maps)
  ABYSS_CACHE   cache of the tools (distance matrices, per-game lead curves; default: ~/.cache/abyss_tools)
  UNSWBC        the unswbc executable (default: `unswbc` on PATH)
"""
import os
import shutil

PKG = os.path.dirname(os.path.abspath(__file__))


def _has_bots(d):
    return os.path.isdir(os.path.join(d, "bots"))


def repo_root():
    env = os.environ.get("ABYSS_ROOT")
    if env:
        return os.path.abspath(os.path.expanduser(env))
    for start in (PKG, os.getcwd()):
        d = start
        while True:
            if _has_bots(d):
                return d
            parent = os.path.dirname(d)
            if parent == d:
                break
            d = parent
    return os.getcwd()


def bots_dir(arg=None):
    """the unswbc workspace holding the bot folders"""
    if arg:
        return os.path.abspath(os.path.expanduser(arg))
    env = os.environ.get("ABYSS_BOTS")
    if env:
        return os.path.abspath(os.path.expanduser(env))
    return os.path.join(repo_root(), "bots")


def maps_dir(arg=None, bots=None):
    """the folder of .map files"""
    if arg:
        return os.path.abspath(os.path.expanduser(arg))
    env = os.environ.get("ABYSS_MAPS")
    if env:
        return os.path.abspath(os.path.expanduser(env))
    return os.path.join(bots or bots_dir(), "maps")


def cache_dir(sub=""):
    base = os.environ.get("ABYSS_CACHE") or os.path.join(os.path.expanduser("~"), ".cache", "abyss_tools")
    return os.path.join(base, sub) if sub else base


def unswbc(arg=None):
    """the unswbc executable"""
    return arg or os.environ.get("UNSWBC") or shutil.which("unswbc") or "unswbc"


def wasm_cache():
    """unswbc's cache of sandbox builds (keyed on the bot's .c / .cc / .cpp contents only, not its headers)"""
    home = os.environ.get("XDG_CACHE_HOME") or os.environ.get("LOCALAPPDATA")
    return os.path.join(home or os.path.join(os.path.expanduser("~"), ".cache"), "unswbc", "wasmbots")


def purge_wasm(name, cache=None):
    """Drop every cached sandbox build of bot `name` (and of the `_L` / `_R` copies the arena makes). unswbc hashes only the
    .c / .cc / .cpp files, so after editing a header it would silently reuse the old wasm. Returns the number removed."""
    cache = cache or wasm_cache()
    if not os.path.isdir(cache):
        return 0
    n = 0
    for f in os.listdir(cache):
        if f.split("-")[0] in (name, name + "_L", name + "_R"):
            try:
                os.remove(os.path.join(cache, f))
                n += 1
            except FileNotFoundError:  # another job purged it first
                pass
    return n
