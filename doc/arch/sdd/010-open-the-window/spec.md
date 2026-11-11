---
id: 01a1cd03-09c6-7273-9ce6-0c5b75e3c1a4
number: 010
slug: open-the-window
status: analyzed
created_at: 2026-11-12T00:50:00.000000000Z
---
# Feature Specification: Open The Window

Feature: 010-open-the-window
Created: 2026-11-12

## User Stories

- As the operator, I want the app to open on my current context, so I see objects
  without choosing anything twice.
- As the operator, I want the app to close cleanly on a window close or a
  `SIGTERM`, so nothing is left half-written.
- As the developer, I want one place that owns the order of construction and
  destruction, so a new component has an obvious slot.

## Scope

The process entry point: creating the objects in dependency order, hosting the
engine loop on the main thread, and taking them down in reverse with a drain.

Out of scope: every widget except the main window, and every component's own
behaviour — this feature only sequences them.

## Functional Requirements

### FR-001 Construction follows dependency order

Objects are created in one function, in this order, and each is handed to the
next:

```
QApplication → engine → engine.attach() → HttpClient → HttpKubeClient
→ ObjectStore → ListModel → MainWindow → QSocketNotifier → show()
```

Every `QObject` takes its parent, so the tree owns it (P-12 as amended). The
engine is the only object with `unique_ptr`, because it is not a `QObject`.

### FR-002 A failure unwinds what was built

If any step fails, the steps already done are undone in reverse before the
process exits non-zero. A failure to bind the engine or to load the kubeconfig
is reported once, on stderr, with its family — never a bare exit code.

### FR-003 The loop is hosted, not run

The engine is attached, not run on its own thread: `attach()` records the loop
thread and arms the wakeup, and a `QSocketNotifier` over `waitHandle()` drives
`runOnce()` with the deadline from `nextDeadline()`.

### FR-004 Destruction is the reverse, after the drain

`app.exec()` returns, then the sequence runs backwards: uninstall the signal
handler, `drain()` the client, destroy the window, the model, the store, the
client, and the engine last — the engine outlives everything that posts to it.

### FR-005 Signals reach the same path as a close

`SIGTERM` and `SIGINT` are installed before the window shows. The handler writes
one byte to a self-pipe; the notifier on that pipe calls `quit()`. `SIGPIPE` is
ignored, so a closed peer is an error and not a death (FR-027 of 002).

### FR-006 The first context comes from the kubeconfig

Startup reads the kubeconfig's current context. If there is none, the window
opens empty with a message naming the missing file — never a dialog the operator
cannot dismiss.

### FR-007 A second instance does not fight

Not required to be single-instance, but two instances must not corrupt the same
kubeconfig; startup opens it read-only.

## Security Requirements

- **Data sensitivity/classification.** The process holds the kubeconfig and its
  bearer tokens for the whole run. The file is read once at startup.
- **Authentication/authorization.** Not applicable — the credential is the
  kubeconfig's, unchanged.
- **Input validation.** The kubeconfig is untrusted input; it is parsed by the
  existing loader, and a malformed file is a reported failure (FR-002), not a
  crash.
- **Cryptography in transit/at rest.** The kubeconfig is read in clear and not
  copied; TLS is the client's concern.
- **Logging/audit.** Startup logs the context name at `info`, never a token. A
  failure logs the family and the path.
- **Error-handling information exposure.** A failure message names the file and
  the rule broken; a token is never printed.

## Acceptance Scenarios

Given a kubeconfig with a current context
When  the process starts
Then  the window opens on that context and the loop is hosted.

Given the window is open with a request in flight
When  `SIGTERM` arrives
Then  the drain runs and the process exits zero within the bound.

Given a kubeconfig that does not exist
When  the process starts
Then  the window opens empty naming the file.

The executable form lives in
`doc/arch/specs/features/open-the-window.feature`.

## Observability

No new metric. Startup logs the context at `info` and the backend name once; the
shutdown logs the drain outcome at `warn` when it expires.

## Clarifications

- **Why a self-pipe and not `signalfd`?** `signalfd` is Linux-only and the
  acceptance target is macOS. A pipe works on both, and the notifier is the same
  mechanism the loop already needs.
- **Why does the engine outlive everything?** Every component posts to it. Freeing
  it first turns a late completion into a crash.
- **Why is a missing context not a dialog?** A dialog before `exec()` needs its
  own event loop; an empty window with a message is the same information without
  a nested loop.
