#!/usr/bin/env bash
# Run the learning pipeline's tests (rl/tests/test_rl.py) in a throw-away environment with every optional dependency.
#   rl/tests/run_tests.sh [pytest args]       e.g. rl/tests/run_tests.sh -k consistency -x
# Without uv: pip install pytest numpy pycapnp torch, then python -m pytest rl/tests
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
cd "$HERE/../.."
# no .pytest_cache / __pycache__ left in the checkout
export PYTHONDONTWRITEBYTECODE=1
exec uv run -q --with pytest --with numpy --with pycapnp --with torch python -m pytest -q -p no:cacheprovider rl/tests "$@"
