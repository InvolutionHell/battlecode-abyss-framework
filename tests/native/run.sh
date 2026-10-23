#!/usr/bin/env bash
# Build and run the native C++ tests of the released bot, one build per line of manifest.txt.
#
# usage: tests/native/run.sh [-v] [-k] [PATTERN ...]
#   PATTERN   run only the manifest lines that contain one of these substrings (e.g. fallback95, mapnet)
#   -v        print every test's full output, not only the summary line
#   -k        keep the build folder (its path is printed)
#
# environment:
#   CXX           the C++ compiler (default: c++; any C++20 compiler; with libstdc++ (g++) run.sh adds
#                 -include bits/stdc++.h itself, see below)
#   CXXFLAGS      extra flags for every build (the script always passes -std=c++20 -O2)
#   BOT_DIR       the bot under test (default: <repo>/bots/abyss)
#   BUILD_DIR     where binaries and logs go (default: a fresh temporary folder, removed at the end unless -k)
#   ABYSS_MAPS    a folder with the official .map files; region_test.cpp then also checks its incremental rooms against a
#                 full rebuild on real maps (without it that case is skipped). `unswbc maps DIR` copies the maps to DIR.
#   TEST_TIMEOUT  seconds per test binary (default 300)
#
# The tests reach private members with `#define private public` before they include the bot, which libstdc++'s
# headers do not accept. With libstdc++ (g++, or clang++ on most Linux systems) the script therefore adds
# -include bits/stdc++.h, so every standard header is read before that macro; libc++ (Apple clang) needs nothing.
#
# Exit status: 0 when every line compiled, exited with 0 and ran at least one check; 1 otherwise.
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
CXX=${CXX:-c++}
CXXFLAGS=${CXXFLAGS:-}
BOT_DIR=${BOT_DIR:-$REPO/bots/abyss}
TEST_TIMEOUT=${TEST_TIMEOUT:-300}
MANIFEST="$HERE/manifest.txt"

verbose=0
keep=0
patterns=()
while [ $# -gt 0 ]; do
    case "$1" in
        -v) verbose=1 ;;
        -k) keep=1 ;;
        -h|--help) sed -n '2,23p' "$0"; exit 0 ;;
        -*) echo "unknown option: $1" >&2; exit 2 ;;
        *) patterns+=("$1") ;;
    esac
    shift
done

if [ ! -f "$BOT_DIR/policy.hpp" ]; then
    echo "no bot at $BOT_DIR (set BOT_DIR)" >&2
    exit 2
fi
if ! command -v "$CXX" > /dev/null 2>&1; then
    echo "compiler not found: $CXX (set CXX)" >&2
    exit 2
fi
# libstdc++ rejects `#define private public` in its own headers: include them all first (see the note at the top)
stdlib_note=""
# shellcheck disable=SC2086
if printf '#include <cstddef>\n' | "$CXX" -std=c++20 $CXXFLAGS -x c++ -E -dM - 2> /dev/null | grep -q '__GLIBCXX__'; then
    case " $CXXFLAGS " in
        *"bits/stdc++.h"*) ;;
        *) CXXFLAGS="-include bits/stdc++.h${CXXFLAGS:+ $CXXFLAGS}"; stdlib_note=" (libstdc++: -include bits/stdc++.h added)" ;;
    esac
fi

if [ -n "${BUILD_DIR:-}" ]; then
    mkdir -p "$BUILD_DIR" || exit 2
    BUILD=$(cd "$BUILD_DIR" && pwd)
else
    BUILD=$(mktemp -d "${TMPDIR:-/tmp}/abyss-native-tests.XXXXXX") || exit 2
    if [ "$keep" = 0 ]; then trap 'rm -rf "$BUILD"' EXIT; fi
fi

# a time limit per binary, with whatever the system offers
run_limited() {
    if command -v timeout > /dev/null 2>&1; then
        timeout "$TEST_TIMEOUT" "$@"
    elif command -v gtimeout > /dev/null 2>&1; then
        gtimeout "$TEST_TIMEOUT" "$@"
    elif command -v perl > /dev/null 2>&1; then
        perl -e 'alarm shift @ARGV; exec @ARGV or exit 127' "$TEST_TIMEOUT" "$@"
    else
        "$@"
    fi
}

matches() {
    [ ${#patterns[@]} -eq 0 ] && return 0
    local p
    for p in "${patterns[@]}"; do
        case "$1" in *"$p"*) return 0 ;; esac
    done
    return 1
}

echo "compiler: $("$CXX" --version 2>/dev/null | head -1)$stdlib_note"
echo "bot:      $BOT_DIR"
[ -n "${ABYSS_MAPS:-}" ] && echo "maps:     $ABYSS_MAPS"
echo

n=0
passed=0
failed=()
set -f  # the manifest's words are not globs
while IFS= read -r line || [ -n "$line" ]; do
    line=${line%%#*}
    # shellcheck disable=SC2086
    set -- $line
    [ $# -eq 0 ] && continue
    src=$1
    shift
    flags=("$@")
    matches "$src ${flags[*]:-}" || continue
    n=$((n + 1))
    name="${src%.cpp}"
    label="$src${flags[*]:+ ${flags[*]}}"
    bin="$BUILD/$(printf '%02d' "$n")_$name"
    if [ ! -f "$HERE/$src" ]; then
        printf 'FAIL  %-62s missing source\n' "$label"
        failed+=("$label")
        continue
    fi
    # shellcheck disable=SC2086
    if ! "$CXX" -std=c++20 -O2 $CXXFLAGS ${flags[@]+"${flags[@]}"} -I "$BOT_DIR" "$HERE/$src" -o "$bin" > "$bin.build.log" 2>&1; then
        printf 'FAIL  %-62s does not compile\n' "$label"
        sed 's/^/      /' "$bin.build.log" | grep -m 20 'error' || true
        failed+=("$label")
        continue
    fi
    (cd "$BUILD" && run_limited "$bin") > "$bin.out" 2>&1
    rc=$?
    summary=$(grep -v '^[[:space:]]*$' "$bin.out" | tail -1)
    nochecks=0
    if grep -Eq '(^| )0 / 0 checks|^0 checks' "$bin.out"; then nochecks=1; fi
    if [ "$rc" -eq 0 ] && [ "$nochecks" = 0 ]; then
        printf 'ok    %-62s %s\n' "$label" "$summary"
        passed=$((passed + 1))
    else
        why="exit $rc"
        [ "$nochecks" = 1 ] && why="no checks ran (is the module's switch on?)"
        printf 'FAIL  %-62s %s: %s\n' "$label" "$why" "$summary"
        grep '^FAIL' "$bin.out" | head -20 | sed 's/^/      /'
        failed+=("$label")
    fi
    if [ "$verbose" = 1 ]; then sed 's/^/      | /' "$bin.out"; fi
done < "$MANIFEST"

echo
if [ "$n" -eq 0 ]; then
    echo "no manifest line matched"
    exit 1
fi
echo "$passed / $n builds passed"
[ "$keep" = 1 ] && echo "build folder: $BUILD"
if [ ${#failed[@]} -gt 0 ]; then
    printf 'failed: %s\n' "${failed[@]}"
    exit 1
fi
exit 0
