#!/bin/sh
# D-17 — every dependency enters by an exact version.
# A FetchContent/ExternalProject block needs URL_HASH or a version GIT_TAG,
# never a moving branch.
set -eu

status=0

files=$(find . \( -name 'CMakeLists.txt' -o -name '*.cmake' \) 2>/dev/null | grep -v '^\./build/' || true)

if [ -z "$files" ]; then
    exit 0
fi

for file in $files; do
    awk -v file="$file" '
        function flush() {
            if (buf == "") return
            hashed = (buf ~ /URL_HASH/) || (buf ~ /GIT_TAG[[:space:]]+[0-9a-f]{7,40}/) || (buf ~ /GIT_TAG[[:space:]]+v?[0-9]+\.[0-9]+/)
            branch = (buf ~ /GIT_TAG[[:space:]]+(main|master|develop|trunk)/)
            if (!hashed || branch) {
                printf "%s: unpinned dependency: %s\n", file, name
                bad = 1
            }
            buf = ""
        }
        /FetchContent_Declare|ExternalProject_Add/ { name = $0; buf = $0; sub(/[[:space:]]+$/, "", name) }
        buf != "" && !/FetchContent_Declare|ExternalProject_Add/ { buf = buf "\n" $0 }
        buf != "" && /\)/ { flush() }
        END { if (buf != "") flush(); exit bad ? 1 : 0 }
    ' "$file" || status=1
done

if [ "$status" -ne 0 ]; then
    echo "D-17: pin the fetch with URL_HASH or a version GIT_TAG, never a branch." >&2
fi

exit $status
