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

for file in $files; do
    case $file in
    */tests/* | */build/*) continue ;;
    esac
    "$tidy" -p "$builddir" --quiet "$file" || status=1
done

if [ "$status" -ne 0 ]; then
    echo "D-07: clang-tidy reported findings — fix them or record why in .clang-tidy." >&2
fi

exit $status
