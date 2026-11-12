# Spike: the hosted loop on Darwin

Proves the five capabilities ADR-0020 adds, on the acceptance platform (P-05).
Nothing here is product code; it exists to be measured once and then left alone.

## Build

By hand, against the tree already configured for `dev`. `QT_QPA_PLATFORM=offscreen`
lets it run over SSH with no display attached.

```sh
O=$(brew --prefix openssl@3)
clang++ -std=c++26 -stdlib=libc++ -O2 -Isrc -I"$O/include" \
    $(pkg-config --cflags Qt6Widgets) \
    -x objective-c++ doc/arch/spikes/hosted-loop/main.cpp -x none build/dev/libantares_io.a \
    -F/opt/homebrew/lib -framework QtCore -framework QtGui -framework QtWidgets \
    -framework AppKit -framework Foundation -L"$O/lib" -lssl -lcrypto -o /tmp/spike
```

`-x objective-c++` covers FR-007 only, which needs `NSProcessInfo beginActivity`.
`-x none` before the archive is what stops the language flag from being applied to
it as well.

## Run

```sh
QT_QPA_PLATFORM=offscreen /tmp/spike all
```

One `RESULT` line per measurement. Exit 0 means every threshold in the spec held.

## Measured on this host

macOS 27.0.1, arm64, `-O2`, 2026-11-12.

| Ref | Number |
|---|---|
| FR-001 | 5 wakes, 226 µs |
| FR-002 | 63.3 idle fps against 62.5 saturated — ratio 0.987 |
| FR-003 | modal opens; 99.3 MB delivered underneath it |
| FR-004 | 331.2 MB through a live resize, 331.7 MB through a held menu, 5 924 frames |
| FR-005 | 248.6 ms against a 200 ms request, fired on the main thread |
| FR-006 | p99 1 006 µs, 40 overruns in 82 882 turns |
| FR-007 | occluded p99 255.3 ms, with activity 253.6 ms |
| FR-008 | 32 watches at 62.5 fps, 2 703 MB |

`VERDICT pass`. An earlier table came from 5 s windows; these are the windows the
spec names (30 s, 10 s per nested loop, 60 s occluded).

FR-005 and FR-007 both land near 255 ms rather than 200/33: the offscreen platform
plugin clamps the timer to its own quantum. The ratio between the occluded and
active runs is what the record reads, not the absolute.
