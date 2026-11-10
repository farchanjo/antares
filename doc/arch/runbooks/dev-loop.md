# Dev Loop Runbook — antares

Operational procedure for the inner development loop. Code is authored on the
Linux workstation and validated on the macOS host, because macOS is the first
delivery target (P-05) and the only place a build is evidence.

## Purpose

Keep the edit-compile-test loop short and make every claim about behaviour
checkable. Development uses the `dev` preset (P-08); the optimized
configuration runs only on release.

The macOS host is `farchanjo@192.168.202.241`, checkout at `~/dev/antares`.
AppleScript through `osascript` is the evidence source for anything the exit
code cannot prove.

The workstation is the single hub. It is the only publisher: the host never
talks to GitHub, and GitHub is only reached from here.

```mermaid
flowchart LR
    W[Workstation] -->|git push mac| H[macOS host]
    W -->|git push origin| G[GitHub]
    H -->|build, test, osascript evidence| W
```

The host carries **no** `speckit` and **no** remote of its own. Corpus gates run
on the workstation; the host only builds and tests. The `mac` push lands in the
host checkout through `receive.denyCurrentBranch=updateInstead`.

## Trigger

Run this procedure when:

- A change touches `src/`, `CMakeLists.txt`, `CMakePresets.json` or `tools/`.
- A review asks for proof that a behaviour happens, not only that it compiles.
- The local `speckit validate` is green and the change is ready for evidence.

## Preconditions

- `speckit validate` exits `0` on the workstation.
- The `lint` target exits `0` on the workstation.
- The change is committed; the host receives it by push, never by patch.
- The host holds no remote and no upstream branch; it is a sink.
- The host answers and its toolchain resolves: `clang`, `cmake`, `ninja`,
  `sccache`, `ld64.lld`, `osascript`.
- The active feature is selected, so the guard accepts the write.

## Steps

1. Validate the corpus locally: `speckit validate` — must exit `0`.
2. Run the rule checkers locally: `cmake --build --preset dev --target lint` —
   must exit `0`.
3. Commit the change.
4. Push it to the host as `main`: `git push mac HEAD:main` — the checkout
   updates in place. Pushing the bare branch name creates a branch the host
   does not check out, because `updateInstead` only acts on the checked-out ref.
5. Over SSH into `~/dev/antares`, configure the dev preset:
   `cmake --preset dev`.
6. Build: `cmake --build --preset dev`.
7. Run the tests: `cmake --build --preset dev --target check`. It passes `--parallel $(JOBS)` and a CTest entry
   per case, so the suite fans across every core; measured on the 16-core host,
   10 cases run in 0.05 s parallel against 0.26 s serial. Keep per-case entries —
   a single binary per backend loses the failure name and the parallelism both.
   `JOBS=n` in the environment caps the width at configure time; unset means
   every core. A bare `ctest --preset dev` is serial, and so is `--parallel`
   with no value, so the `check` target is the entry point. The Makefile is
   gone: CMake presets and custom targets carry the whole loop.
8. Prove the behaviour with AppleScript, for example
   `osascript -e 'return 1 + 1'` for the interpreter and
   `osascript -e 'display notification "…" with title "antares"'` for the
   notification path.
9. Read the sccache counters with `sccache --show-stats` when build time moves
   without a source change.
10. Publish from the workstation only, and only when the loop is green:
    `git push origin HEAD` for the branch, `git push origin main` after merge.

## Verification

- Steps 5, 6 and 7 exit `0`.
- `git push mac HEAD:main` fast-forwards; a rejection means the host tree diverged.
- The AppleScript assertion prints the expected value; a notification appears.
- `sccache --show-stats` reports a hit rate that does not collapse to zero
  after a no-op rebuild.
- No file under `src/domain` names an I/O header after the change.

## Rollback

If a step fails:

1. Stop; do not push a fix on top of a red host.
2. Read the failing step's output in full — the host output, not a summary.
3. Reproduce locally with `cmake --preset dev`.
4. Revert the commit when the cause is the change, or fix the host when the
   cause is the environment.
5. Re-run the procedure from step 1; a run is only valid from a clean start.

## The live read-only smoke

`antares_k8s_live` exercises the Kubernetes client against a real API server
through the project's own HTTP client. It never writes: the type it holds is
`ports::k8s::Reader`.

```
cmake --build --preset dev --target antares_k8s_live
./build/dev/antares_k8s_live --context hood
```

It lists the contexts of the kubeconfig, resolves one, then runs `GET /version`,
`GET /api/v1/namespaces` and a five-second `WATCH` of a namespace. Redirect its
output to a file and read the file: a stuck resolve is the failure that matters
most here.

`--kubeconfig PATH` may be repeated. Files merge with the first entry winning, so
the one named first decides.

### When a context hangs before any request

The symptom is the binding line followed by nothing: the loop thread is inside
`getaddrinfo`, and there is no timeout there to fire.

Check the split-DNS view first. `dig` reads the plain resolver file and ignores
per-domain routing, so it answers while the system resolver hangs:

```
scutil --dns | grep -B1 -A3 <domain>   # a scoped resolver with its nameservers
dig +time=3 @<nameserver> <host> A     # empty or timed out means that server is dead
```

A scoped resolver whose nameservers do not answer is the usual cause. It is a
machine problem, not a code one: `kubectl` against the same context reports
`Client.Timeout exceeded while awaiting headers`. Fixing it means repairing the
DNS configuration; only a resolver of our own would let the client outlive it.
