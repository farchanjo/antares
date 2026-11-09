#!/bin/sh
# D-13 — the domain stays free of I/O.
# src/domain may name no file, stream, socket or Qt transport header.
set -eu

root=${1:-src/domain}
status=0

if [ ! -d "$root" ]; then
    exit 0
fi

pattern='#include[[:space:]]*<(fstream|filesystem|cstdio|sys/socket\.h|arpa/inet\.h|netdb\.h|unistd\.h)>|#include[[:space:]]*<(QtNetwork|QTcpSocket|QNetworkAccessManager)>|std::filesystem'

hits=$(grep -rnE "$pattern" "$root" || true)

if [ -n "$hits" ]; then
    echo "D-13: I/O header inside the domain (move the port, keep the domain pure):" >&2
    echo "$hits" >&2
    status=1
fi

exit $status
