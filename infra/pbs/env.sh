# Shared prologue of the PBS templates in infra/pbs (sourced, not run).
#
# PBS copies a job script into its spool before running it, so the templates cannot find the repository from their own
# path: submit from the repository root (PBS_O_WORKDIR) or pass REPO=/path/to/checkout with qsub -v. Site settings
# (queue, scratch, Python, the unswbc to use) come from the env file named by LAB_ENV (see infra/config/example.env):
#   qsub -v LAB_ENV=$PWD/infra/config/lab.env,A=abyss,B=starter infra/pbs/arena.pbs
REPO=${REPO:-${PBS_O_WORKDIR:-$PWD}}
cd "$REPO" || { echo "repository $REPO not found"; exit 1; }
if [ -n "${LAB_ENV:-}" ]; then
  if [ -f "$LAB_ENV" ]; then set -a; . "$LAB_ENV"; set +a; else echo "LAB_ENV=$LAB_ENV not found"; exit 1; fi
fi
PY=${LAB_PYTHON:-python3}
SCRATCH=${LAB_SCRATCH:-$REPO/data/pbs}
NCPUS=${NCPUS:-$(nproc 2> /dev/null || echo 4)}
export BC_BOTS_DIR=${BC_BOTS_DIR:-$REPO/bots}

# A job-local unswbc cache: arena.py purges a bot's cached wasm before its run, and jobs sharing one cache race on it
# (a game fails with "No such file" when another job purges the wasm it is loading). The compiled judge toolchain
# (clang / wasm-ld .cwasm files, about a minute to compile) stays shared through symlinks, or every job recompiles it.
job_cache() {
  local shared=${XDG_CACHE_HOME:-$HOME/.cache}/unswbc
  export XDG_CACHE_HOME=${TMPDIR:-/tmp}/xdg-cache-${PBS_JOBID:-$$}
  mkdir -p "$XDG_CACHE_HOME/unswbc"
  [ "$shared" = "$XDG_CACHE_HOME/unswbc" ] && return 0   # called twice: already the job cache
  for f in "$shared"/clang-*.cwasm "$shared"/wasm-ld-*.cwasm; do
    [ -e "$f" ] && ln -sf "$f" "$XDG_CACHE_HOME/unswbc/"
  done
  return 0
}

# Lossless replay compression: uncompressed, a few large censuses fill a scratch quota.
compress_replays() {
  command -v zstd > /dev/null || return 0
  for d in "$@"; do zstd -q -6 --rm "$d"/replays/*.replay 2> /dev/null || true; done
}
