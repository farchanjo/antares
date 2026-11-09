#!/bin/sh
# D-06 — every first-party include names the full path from src/.
# A "../" climb is the violation; the include must start at the layer directory.
set -eu

root=${1:-src}
status=0

if [ ! -d "$root" ]; then
    exit 0
fi

hits=$(grep -rn --include='*.hpp' --include='*.cpp' --include='*.cc' --include='*.h' \
    -E '^[[:space:]]*#[[:space:]]*include[[:space:]]*"\.\./' "$root" || true)

if [ -n "$hits" ]; then
    echo "D-06: include climbs out of its directory (use a path from src/):" >&2
    echo "$hits" >&2
    status=1
fi

exit $status
