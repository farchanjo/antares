#!/bin/sh
# D-07 — the tidy configuration is a gate, not a suggestion.
#
# The binary is optional on purpose: LLVM extras are a separate package on
# every distro, and a checkout without them must still lint. Missing binary
# degrades to a warning; a finding from a present binary fails the run.
#
# The translation units come from the compilation database and not from a
# `find`, so a platform-only source (kqueue on Linux) is tidied with the flags
# its own build would use and never with a default one.
set -eu

builddir=${1:-build/dev}
status=0

tidy=$(command -v clang-tidy || command -v clang-tidy-18 || true)
if [ -z "$tidy" ]; then
    echo "D-07: clang-tidy not on PATH — tidy pass skipped (install clang-tidy to gate)." >&2
    exit 0
fi

database=$builddir/compile_commands.json
if [ ! -f "$database" ]; then
    echo "D-07: $database is missing — configure the tree first." >&2
    exit 1
fi

files=$(sed -n 's/.*"file"[[:space:]]*:[[:space:]]*"\(.*\)".*/\1/p' "$database" | sort -u)
if [ -z "$files" ]; then
    exit 0
fi

# One process per translation unit, bounded by the core count. Sequentially this
# took six minutes on sixteen cores; the units are independent.
units=$(printf '%s\n' "$files" | grep -v -e '/tests/' -e '/build/' || true)
if [ -z "$units" ]; then
    exit 0
fi

jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
reports=$(mktemp)
trap 'rm -f "$reports"' EXIT

printf '%s\n' "$units" | xargs -P "$jobs" -I{} sh -c '
    if ! out=$("$0" -p "$1" --quiet "$2" 2>&1); then
        printf "%s\n" "$out" >> "$3"

    fi
' "$tidy" "$builddir" {} "$reports"

if [ -s "$reports" ]; then
    cat "$reports"
    echo "D-07: clang-tidy reported findings — fix them or record why in .clang-tidy." >&2
    exit 1
fi

exit 0
