---
id: 01a1ce19-0000-7000-8000-000000000011
number: 011
slug: map-the-window-surface
status: analyzed
created_at: 2026-11-12T04:30:00.000000000Z
---
# Feature Specification: Map The Window Surface

Feature: 011-map-the-window-surface
Created: 2026-11-12

## Why

The window exists and opens (feature 010), but its surface was never specified.
It has been observed with the accessibility API on the acceptance platform and it
is thinner than it looks: a list with no rows, three window buttons the system
draws, and a label carrying the title.

This feature writes the surface down — what is there, what it must become, what
each action does and how the whole thing behaves at another window size — so the
next change has a target instead of an impression.

## Observed State

Read with `osascript` over `System Events` on macOS 27.0.1, arm64, with the app
open on `gryffindor`, 2026-11-12. Roles, descriptions, sizes and actions are
verbatim from the tree.

| Depth | Role | Description | Size | Actions |
|---|---|---|---|---|
| 0 | `AXWindow` | `antares — gryffindor` at (504, 299) | 720 × 512 | — |
| 1 | `AXList` | — | 696 × 456 | `AXRaise` |
| 1 | `AXButton` | close button | 16 × 16 | `AXPress` |
| 1 | `AXButton` | full screen button | 16 × 16 | `AXPress`, `AXZoomWindow`, `AXShowMenu` |
| 1 | `AXButton` | minimize button | 16 × 16 | `AXPress` |
| 1 | `AXStaticText` | text | 138 × 16 | — |

Three findings, in severity order:

- **The list is empty.** `value` is `missing value` and it carries no children.
  `Application::build` calls `fetchMore` once, but no row survives to the view:
  either the page never arrives or the inserted range is not seen. The window is
  therefore correct in shape and useless in content.
- **The label duplicates the title.** `AXStaticText` holds the same string as the
  window title, so a title bar plus a label says one thing twice.
- **Nothing but the list exists.** No namespace or context selector, no filter,
  no refresh, no column headers — the `AXColumn` seen under the list is the
  empty table header, not a set of columns.

## User Stories

- As the operator, I want the list to show the objects of the current context, so
  the window is worth opening.
- As the operator, I want to switch namespace and resource kind, so one window
  serves the whole cluster.
- As the operator, I want to filter the rows I am looking at, so a large
  namespace is still usable.
- As the operator who tabs through the window, I want a focus order and a
  shortcut for each action, so nothing needs a mouse.
- As the operator who resizes the window, I want the list to take the space and
  the controls to keep their size, so the layout holds at 400 px and at 2000 px.
- As an assistive-technology user, I want every control to carry a label and a
  role a screen reader can announce.

## Functional Requirements

- **FR-001** The list shows one row per live entry of the selected scope, in the
  order the store returns them. `rowCount` equals the live entry count.
- **FR-002** A row renders name, namespace, status and age. A row with no status
  renders an empty status, not the word "unknown" (see FR-009).
- **FR-003** The window carries a scope selector: context, namespace and resource
  kind. Changing any of the three closes the old scope and opens the new one.
- **FR-004** A filter box narrows the rows by name. The filter is applied by the
  store, not by the view, so a filtered scan does not walk the whole scope
  (feature 008).
- **FR-005** Actions: refresh, filter, next page on scroll, open the detail of
  the selected row, and quit. Each has a keyboard shortcut and a menu item.
- **FR-006** The list grows with the window and never below 320 × 240; the
  controls keep their height. At 2000 px wide the name column takes the slack.
- **FR-007** Every control carries an accessibility label and, where the role is
  not obvious, a description. The list announces its row count on change.
- **FR-008** Tab moves forward through the controls, Shift+Tab back, and focus is
  visible. The list takes focus on open.
- **FR-009** A scope that refused admission shows a footer saying so, with the
  dropped count. Truncation is never silent.
- **FR-010** An empty scope shows an empty state naming the scope and offering
  refresh, not a blank area.

## Non-Functional Requirements

- **NFR-001** A window resize repaints without a stall: the p99 turn stays under
  the 8 ms budget proved by the spike (feature 007).
- **NFR-002** Opening the window paints the first page inside one second on a
  four-hundred-object namespace.
- **NFR-003** Scrolling a five-thousand-object namespace holds 60 fps.
- **NFR-004** No control is reachable by mouse only.

## Observability

Signals, per the strategy in `doc/arch/observability/observability.md`:

- **`ui.rows`** (gauge, by scope kind) — rows the model holds. Bounded: the scope
  kind, not the scope name.
- **`ui.page`** (counter, by outcome: `ok`, `refused`, `transport`) — pages
  asked against pages landed. The two drifting apart names a stuck fetch loop.
- **`ui.truncated`** (counter) — a scope that hit an admission ceiling (FR-009).
- Logs, all at `info`, all carrying the `ui` category: scope opened, scope
  closed, page landed with its row count. Never the credential (P-10).

## Out Of Scope

The detail pane, editing verbs, and any second window. Logs and exec get their
own features.

## Open Questions

- The empty list (finding 1) may be a defect in feature 010 rather than a gap in
  this feature. It is recorded here because this feature is what proves the
  surface works; if it turns out to be a defect, the fix lands under 010's
  header and this one keeps only the shortfall it names.

## Clarifications
