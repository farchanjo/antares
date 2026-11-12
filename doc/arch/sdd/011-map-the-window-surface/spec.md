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

## Reference: Lens

Observed on the acceptance platform, 2026-11-12, screenshot and accessibility
tree. Lens is the shape this window grows into; the regions are named here so a
later feature can point at one instead of describing it again.

| Region | What it holds |
|---|---|
| title bar | menus (File, Edit, View, Help) |
| toolbar | one action cluster on the right: AI configure, chat, account |
| icon rail | one icon per resource family, narrow, fixed |
| navigator | search box, then a tree: cluster, Overview, Applications, Node, Workloads, Config, Network, Storage, Device Resources, Namespaces, Events, Helm, Access Control, Custom Resources, Security Center |
| main | the table for the selected node |
| status bar | Support, on the right |

Three things worth copying, in this order: the navigator is a tree and not a
flat selector; the search box sits at the top of the navigator and narrows it;
the status bar is a fixed strip that never scrolls.

Two things worth not copying: the icon rail duplicates the navigator for the
common families, and there is no empty state — an unconnected cluster shows a
watermark and nothing else.

### Exact tree, captured

Read with `agent-desktop snapshot` on the acceptance platform, 2026-11-12. The
content of the navigator is drawn on a canvas, so the tree stops at the
containers; the labels below come from the screenshot beside it.

| Order | Role | Name | Notes |
|---|---|---|---|
| 1 | button | — | back, icon only, disabled at the first entry |
| 2 | button | — | forward, icon only |
| 3 | button | Configure AI | label plus icon, right-aligned group |
| 4 | combobox | arrow_drop_down | overflow of the toolbar |
| 5 | button | notifications | icon plus label |
| 6 | button | — | account, icon only |
| 7 | main | — | the content region |
| 8 | button | SA | cluster avatar, initials, red |
| 9 | button | SE | cluster avatar, initials, blue |
| 10 | button | HO | cluster avatar, initials, green, selected |
| 11 | button | GR | cluster avatar, initials, grey |
| 12 | button | — | add cluster |
| 13 | button | 1 | terminal, with a count badge |
| 14 | button | — | settings |
| 15 | group | support / Support | status bar, right-aligned |

The navigator itself, top to bottom: the title `Navigator`; a search field
holding the current cluster name with a filter icon and a magnifier; the tree,
whose root is the cluster with a green connected dot, expanded to `Overview`,
`Applications`, `Argo CD`, `Nodes`, `Workloads`, `Config`, `Network`, `Storage`,
`Device Resources`, `Namespaces`, `Events`, `Helm`, `Access Control`, `Custom
Resources`, `Security Center`. `Argo CD` and `Device Resources` carry a `NEW`
badge. The content region shows the product watermark while no view is open. The
status bar carries two counters on the left and `Support` on the right.

### Region map, ported

Each region below is a requirement of this feature, in the order the port lands.

- **R-1 icon rail** — fixed 48 px, one icon per family, the current one carries
  the selection colour. It is a shortcut into the navigator, not a second tree.
- **R-2 navigator** — 240 px, resizable by splitter, never below 180. Search box
  at the top, tree below, cluster as the root. Each family expands to its
  resources; a resource without a view yet is present but insensitive.
- **R-3 toolbar** — context selector, namespace selector, resource kind. On the
  right, the connection state and the refresh action.
- **R-4 main** — the table for the selected node. Empty state names the scope and
  offers refresh (FR-010).
- **R-5 detail** — right pane, 360 px, collapsible. Shows the selected row's
  metadata; a second click on the same row collapses it.
- **R-6 status bar** — fixed 24 px strip: row count, truncation notice (FR-009),
  and the connection state on the right.

The window opens at 1100 x 680 and remembers its geometry across runs. Widths are
the reference's; the splitter lets them move and the geometry file keeps what the
operator chose.

## Layout Rules

Taken from the Qt layout documentation
(`https://doc.qt.io/qt-6/layout.html`), not invented here.

- **LR-001** Every child sits in a `QLayout`; nothing is positioned by hand. A
  layout owns positioning, default size, minimum size and resize, and the
  reparenting that follows (Tips for Using Layouts).
- **LR-002** The list is the only `Expanding` widget. Everything else is
  `Preferred` or `Fixed`, so slack goes to the rows and never to a toolbar.
- **LR-003** The minimum window size is derived from the layout, not set by hand:
  the controls' `minimumSizeHint` plus margins. The window may not shrink below
  what the layout needs (Adding Widgets to a Layout, step 4).
- **LR-004** A custom widget with no children reimplements `sizeHint` and
  `minimumSizeHint` and calls `updateGeometry` when either changes (Custom
  Widgets in Layouts).
- **LR-005** Nesting stops at three levels. A fourth is extracted into a widget
  (D-05 nesting depth).
- **LR-006** A label that wraps sets `heightForWidth` on its size policy and
  reimplements `heightForWidth`, or the layout gives it one line and clips
  (Layout Issues — rich text and FreeResize).
- **LR-007** Spacing and margins come from the style, never from a literal, so a
  platform that wants tighter spacing gets it.

## Internationalization Rules

Taken from the Qt internationalization documentation
(`https://doc.qt.io/qt-6/internationalization.html`) and the source-translation
page, not invented here.

- **I18N-001** Every user-visible string goes through `tr()`. A literal that
  reaches a widget is a defect.
- **I18N-002** Sources are UTF-8 and carry no `CODE` pragma; the string is
  written as text, not as an escape sequence.
- **I18N-003** Translations live in `translations/antares_<locale>.ts`, one file
  per locale, and are built by `qt_add_translations`. `.ts` files are committed;
  `.qm` files are not (Commit no artifacts).
- **I18N-004** At startup the app installs the catalogue for the system locale,
  falling back to the source locale when none matches. Installing a catalogue is
  not an error path — a missing one is normal.
- **I18N-005** A plural uses `tr(..., n)` with the `%n` placeholder, never a
  hand-built `n == 1 ?`.
- **I18N-006** A translatable string names its context; a string shared by two
  widgets gets two contexts when the translations could differ.
- **I18N-007** Widths are not tuned to English. A control sizes to its
  `sizeHint` and the layout absorbs the difference; German is the reference for
  length, Japanese for height.
- **I18N-008** Numbers, dates and durations format through `QLocale`, not
  `printf`. The age column is the first case.
- **I18N-009** Right-to-left is a layout concern, not a string one: no
  `left`/`right` in a stylesheet where `start`/`end` exists, and the row order
  follows the layout direction.
- **I18N-010** `lupdate` runs over `src/adapters/ui` in the `check` target, so a
  new `tr()` with no catalogue entry fails the build.

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
