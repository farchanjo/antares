# Spike: the hosted loop on Darwin

Proves the five capabilities ADR-0020 adds, on the acceptance platform (P-05).
Nothing here is product code; it exists to be measured once and then left alone.

## Build

By hand, against the tree already configured for `dev`. The Qt headers live in
the framework bundles of the Homebrew prefix, which is why they are spelled out:

```sh
O=$(brew --prefix openssl@3)
clang++ -std=c++26 -stdlib=libc++ -O2 -Isrc -I"$O/include" \
    -I/opt/homebrew/lib/QtWidgets.framework/Headers \
    -I/opt/homebrew/lib/QtGui.framework/Headers \
    -I/opt/homebrew/lib/QtCore.framework/Headers -F/opt/homebrew/lib \
    -x objective-c++ doc/arch/spikes/hosted-loop/main.cpp -x none build/dev/libantares_io.a \
    -framework QtWidgets -framework QtGui -framework QtCore \
    -framework AppKit -framework Foundation -L"$O/lib" -lssl -lcrypto -o /tmp/spike
```

No `QT_QPA_PLATFORM`: the run needs the real Cocoa platform, because a nested
tracking loop (FR-004) and App Nap (FR-007) only exist there.

`-x objective-c++` covers FR-007 only, which needs `NSProcessInfo beginActivity`.
`-x none` before the archive is what stops the language flag from being applied to
it as well.

## Run

```sh
caffeinate -dimsu /tmp/spike fr-001     # one FR
caffeinate -dimsu /tmp/spike all        # every FR, ~4 minutes
```

One `RESULT` line per measurement on stdout, `PASS`/`FAIL` per threshold. Exit 0
means every threshold in the spec held. Run one FR per process: the numbers above
were taken that way, and a single `all` run is the case that can stall (see the
last paragraph).

## Measured on this host

macOS 27.0.1, arm64, 16 cores, `-O2`, 2026-11-12, with a real Cocoa window
(no `offscreen`), frontmost, under `caffeinate -dimsu` and one
`NSActivityUserInitiated` per run except FR-007.

| Ref | Number |
|---|---|
| FR-001 | 5 wakes, 192 µs / 569 µs / 1 049 µs / 1 211 µs over four runs |
| FR-002 | 62.2 idle fps against 62.5 saturated — ratio 1.004; 121 idle turns against 104 282 saturated, p99 turn 283 µs, 0 overruns, 853 MB |
| FR-003 | modal opens; 60.2 MB delivered underneath it |
| FR-004 | 260.8 MB through a live resize, 281.9 MB through a held menu, 1 237 frames |
| FR-005 | 274 ms against a 200 ms request, fired on the main thread |
| FR-006 | p99 50.0 ms, 40 overruns in 49 turns |
| FR-007 | occluded p99 253.2 ms, max 254.1 ms; with the activity assertion p99 253.2 ms, max 253.8 ms |
| FR-008 | 32 watches at 63.3 fps, 926 MB, p99 turn 8.5 ms |
| `leaks` | 5 147 leaks / 694 KB, every root in AppKit, HIToolbox or Qt; none in the engine, the harness or a connection |

Measured one FR per process. Left unattended as a single `all` run, the process
stalls after about two minutes: with the window occluded, macOS defers the phase
timers themselves, so the run never advances even though the engine is idle. The
activity assertion above is what keeps it moving, and FR-007 is the one case that
measures with the assertion released.

A background app is throttled hard: without the `activateIgnoringOtherApps` in
`main`, FR-001 measures ~70 ms per wake instead of ~1 ms. The record reads the
frontmost numbers.
