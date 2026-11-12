# Window Inventory

Everything the reference shows, captured on the acceptance platform on
2026-11-12 with `agent-desktop` and `screencapture`. This file is the working
map for the port: the spec states the rules, this states the surface.

## How it was captured

```sh
agent-desktop status                       # accessibility, automation, screen recording: granted
agent-desktop snapshot --app Lens --compact # the tree; the content is a canvas, so it stops at containers
agent-desktop --headed mouse-click --xy X,Y # the tree items are only reachable by coordinate
screencapture -x -o shot.png               # the visual, which is what the labels come from
```

Coordinates are root pixels. The window sits at `216,105` and is `1296x900` on a
`3456x2160` display, so a point measured on a `1000x625` screenshot scales by
`3.456`. `System Events` reports the same map but cannot click: it is denied
assistive access, which is why `agent-desktop` is installed.

## Window

| Property | Value |
|---|---|
| title | `Lens` |
| size | 1296 x 900 |
| minimum | unmeasured; the navigator never goes below its search field |
| menu bar | `Lens`, `File`, `Edit`, `View`, `Help` |

## Toolbar

Left to right, one row, fixed height.

| # | Role | Name | Behaviour |
|---|---|---|---|
| 1 | button | — (arrow left) | navigation history back, disabled at the first entry |
| 2 | button | — (arrow right) | navigation history forward |
| 3 | button | `Configure AI` | opens Preferences on External Tools |
| 4 | combobox | `arrow_drop_down` | toolbar overflow |
| 5 | button | `notifications` | notification centre |
| 6 | button | — (account) | account menu, green dot when signed in |

## Cluster rail

Fixed 48 px, vertical, centred circles.

| # | Content | Notes |
|---|---|---|
| 1 | `SA` | avatar, red |
| 2 | `SE` | avatar, blue |
| 3 | `HO` | avatar, green, selected, ring |
| 4 | `GR` | avatar, grey |
| 5 | `+` | add cluster |
| — | stretch | |
| 6 | `1` | terminal, count badge |
| 7 | gear | settings |

The avatar is a filled circle with the cluster's initials in white, 30 px across.
The selected one carries a 2 px ring in a lighter shade of its own colour.

## Navigator

240 px, resizable by splitter, collapsible.

1. Title `Navigator`, bold.
2. Search field holding the current cluster name, with a filter icon on the left
   of the field and a magnifier on the right.
3. Tree, expanded by default:

```
KUBERNETES CLUSTERS            section, caps, small, not selectable
  AKS
  EKS
  Local Kubeconfigs
    gryffindor
    hood                       green dot, expanded
      Overview
      Applications
      Argo CD                  NEW badge
      Nodes
      Workloads
      Config
      Network
      Storage
      Device Resources         NEW badge
      Namespaces
      Events
      Helm
      Access Control
      Custom Resources
      Security Center
    otherproject
    salazar
    services
    snap
TEAMWORK                       section
```

A connected cluster carries a green dot after its name. A `NEW` badge is a caps
pill at the right edge of the row. Sections carry no chevron.

## Content

A tab strip, then the view.

- A tab is `breadcrumb - cluster` with a close button, for example `Pods - salazar`.
- With no view open the region shows the product watermark, centred, 25 % opacity.
- A view is a table: header row, then rows, then a footer.

## Preferences (a view)

Left rail of sub-pages, content to the right.

Sub-pages: `App`, `Proxy`, `Kubernetes`, `Editor`, `Tables`, `Terminal`,
`Integrations`, `Telemetry`, `Lens Teamwork`, `External Tools`.

The `External Tools` page:

- Heading `External Tools`.
- `AI TOOL` with a `PREMIUM` pill.
- One-line description: `The AI tool Lens launches to eg. "Ask AI".`
- Right-aligned `2 of 5 detected on your system` and a `Refresh` action.
- Rows, each an icon, a name, a description and a right-aligned state:

| Name | Description | State |
|---|---|---|
| Claude Code | The coding assistant by Anthropic | `INSTALLED` |
| GitHub Copilot CLI | `CLI command "copilot" not found in PATH, click "install" for instructions.` | `Install` |
| Gemini CLI | `CLI command "gemini" not found in PATH, …` | `Install` |
| Codex CLI | `CLI command "codex" not found in PATH, …` | `Install` |
| OpenCode | The coding assistant by SST | `INSTALLED` |
| Custom | Runs your own configured command | `CUSTOM` |
| No AI tool | Lens does not launch an AI tool. | radio |

- Footer: `Open AI tools in an external terminal` and a toggle, alone.

## Status bar

Fixed 24 px. Two counters on the left, `Support` with a lifebuoy icon on the
right.

## Paid surface, and what replaces it

| Lens | Paid | Port |
|---|---|---|
| Ask AI / Configure AI | yes, Lens Pro | keep the button, open a local settings page |
| Lens Teamwork | yes | keep the section, empty state |
| External Tools catalogue | **no** | port whole, it is local |
| Proxy, Editor, Tables, Terminal | no | port whole |
| Kubernetes, Telemetry, Integrations | no | port whole |

## Correction: the click scale

The first clicks did nothing because `--xy` takes **points**, not pixels. The
display is 3456 x 2160 pixels at 2x, so the point surface is 1728 x 1080 and a
point measured on a `1000x625` screenshot scales by **1.728**. Every coordinate
below is in points.

## Content tabs

The content region is a tab bar. Tabs observed: `Home`, `Preferences`,
`Pods - hood`. Each carries a close button, and the active one is highlighted.
`Home` is the start page. A tab is created by choosing a family in the navigator;
the same family twice reuses its tab.

## Table view (Pods)

Breadcrumb above the table: `Pods - hood`.

A toolbar row of its own:

| Control | Behaviour |
|---|---|
| namespace dropdown | `fp-prod`; narrows the table to one namespace |
| Ask AI field | placeholder `As…`; natural-language filter |
| `Search Pods…` | substring filter, per family |
| refresh | re-runs the list |
| `20 items` | right-aligned count of what is shown |

Columns, in order, all sortable by clicking the header:

`☐` · `Name` · `Namespace` · `CPU` · `Memory` · `Container` · `Restarts` ·
`Controlled By` · `Node` · `QoS` · `Status` · `Age`

Sample rows, verbatim:

```
otel-coll  fp-prod  0.003  120.9MiB  ⏸  0  CloneSet     cpu-optimiz  Burstable  Running  31d
otel-coll  fp-prod  0.004  129.5MiB  ⏸  0  CloneSet     cpu-optimiz  Burstable  Running  31d
kafka-er   fp-prod  0.006  674.7MiB  ⏸  0  ReslicaSet   shardet.am   BestEffort Running  31d
kafka-cc   fp-prod  0.013  903.4MiB  ⏸  0  StrimziPodSet cpu-optimiz Burstable  Running  31d
```

The first column is a selection checkbox. `Restarts` is a small numeric cell with
a dot when non-zero. `Status` is a coloured word. The row action cluster appears
on hover.

Below the rows: a `+` to add a column, and a floating round `+` at the bottom
right.

## Status bar

Left: a green dot and `hood (v1.34.12)` — the connected cluster and its server
version. Right: `Support` with a lifebuoy icon.

## What this changes in the port

- The window needs a **tab bar**, not a single view.
- The table needs a **toolbar of its own**: namespace, AI, search, refresh, count.
- The columns are per family, not the four of the first capture. Twelve for pods.
- The status bar names the cluster and its version, not a row count.
- A row carries a **selection checkbox** and a hover action cluster.

## Table view (Nodes)

Tab `Nodes - hood`, count `16 items`, search `Search Nodes…`.

Columns: `☐` · `Name` · `CPU` · `Memory` · `Disk` · `Taints` · `Roles` ·
`Version` · `Age` · `Conditions` · `⋮`

Differences from the pods table, and they matter:

- **CPU, Memory and Disk are bars**, not numbers. Each is a thin horizontal
  meter with its own colour (CPU blue, memory magenta, disk amber), drawn in the
  cell with the value on hover.
- **`Conditions` is a stack of coloured pills**. `Ready docs.kube` reads green;
  `SchedulingDisab` reads amber. Two conditions show as two pills.
- **`Taints` is a count**, `0` when none.
- **`Roles` is empty** for a worker; the control-plane node would carry one.
- The last column is a `⋮` row menu, always present.
- There is no namespace column: a node is cluster-scoped.

## Family column sets, as observed

| Family | Columns |
|---|---|
| Pods | `☐`, Name, Namespace, CPU, Memory, Container, Restarts, Controlled By, Node, QoS, Status, Age |
| Nodes | `☐`, Name, CPU, Memory, Disk, Taints, Roles, Version, Age, Conditions, `⋮` |

The two share only `Name` and `Age`. A single fixed column set will not do: the
table is described per family.

## Family sub-trees

Families expand, and the expansion is the resource list of that family.

`Workloads`: Overview, Pods, Deployments, Daemon Sets, Stateful Sets,
Replica Sets, Replication Controllers, Jobs, Cron Jobs.

`Network`: Services, Endpoint Slices, Endpoints, Ingresses, Ingress Classes,
Network Policies, Gateway API (`NEW`), Port Forwarding.

A family without a sub-tree is a leaf that opens its view directly (`Nodes`,
`Namespaces`, `Events`).

The nested labels are the Kubernetes kinds in the plural, title-cased, with the
space of the kind name kept (`Daemon Sets`, not `DaemonSets`).

## Row selection

Clicking a row selects it: the whole row takes an accent background. The
selection is single and survives a re-sort. `⌘`-click and `⇧`-click extend it;
the leading checkboxes are the persistent form of the same idea, so a row can be
acted on after the view has moved.

The table has no alternating stripe; the selected row is the only fill.

## Window states

The window maximises to the full display and remembers that across a restart. The
navigator, the rail and the status bar keep their widths; only the table grows.

## Flexibility

Nothing in the table is fixed. Every column is resizable by dragging its header
divider; the header shows the resize cursor on hover. The table fills whatever
the splitter gives it, and the columns keep their widths when the window grows —
the surplus goes to the last column, which is the action column.

The navigator and the rail are resizable by the splitter between them; the status
bar and the toolbars are fixed height. A layout that hard-codes a column width,
or that refuses to shrink, is wrong.

The same holds for the detail pane: it collapses to nothing and reopens.

## Mapping status

| Area | State |
|---|---|
| window, toolbar, rail, navigator, status bar | captured |
| Pods view | captured, twelve columns |
| Nodes view | captured, eleven columns, meters and pills |
| Workloads sub-tree | captured, nine entries |
| Network sub-tree | captured, eight entries |
| Preferences shell and External Tools page | captured |
| tab bar, close button, ⌘W | captured |
| row selection | captured |
| Config, Storage, Device Resources, Events, Helm, Access Control, Custom Resources, Security Center | **not yet opened** |
| row detail, row menu, column add, sort, namespace dropdown | **not yet opened** |
| File, Edit, View, Help menus | **not yet opened** |
| remaining Preferences sub-pages | **not yet opened** |

Known shortcuts: `⌘W` closes the active tab.

## Next captures

1. `Config` (`Secrets`, `Config Maps`, `Resource Quotas`, `Limit Ranges`, `HPA`, `Pod Disruption Budgets`) — expect secrets to mask values.
2. `Storage` (`Persistent Volume Claims`, `Persistent Volumes`, `Storage Classes`) — expect a capacity meter.
3. `Events` — expect a time-ordered log with a type column.
4. A row click, for the detail pane.
5. The `⋮` row menu.
6. The `+` under the last column, for the column picker.
7. The four menus.

## Family sub-tree: Config

`Config Maps`, `Secrets`, `Resource Quotas`, `Limit Ranges`,
`Horizontal Pod Autoscalers`, `Pod Disruption Budgets`, `Priority Classes`,
`Runtime Classes`, `Leases`, `Mutating Webhook Configurations`,
`Validating Webhook Configurations`, `Admission Policies` (`NEW`).

### Truncation

The navigator truncates a long label with an ellipsis (`Horizontal Pod Autoscal…`,
`Mutating Webhook Con…`) rather than widening. The full text is the tooltip. This
is the flexibility rule applied to the tree: the column is the splitter's, and a
label that does not fit loses its tail, not the layout.

## Table view (Validating Webhook Configurations)

Tab `Validating Webhook Configurati…` — the tab label truncates too.

Columns: `☐` · `Name` · `Webhooks` · `Age` · `⋮`

`Webhooks` is a plain count. No namespace column: the resource is cluster-scoped.
A page opens with a hero band carrying the title and the cluster, then the table.

## The blue `+`

Every table carries a round accent `+` pinned to the bottom right, above the
status bar. It is the create action for the family.

## `Age`

A compact duration: `3y198d`, `2y232d`, `685d`, `25d`, `53d`. Years and days are
concatenated without a separator and without a space.

## Menu bar

The menu bar is the platform's: `Lens`, `File`, `Edit`, `View`, `Help`.

`File`:

| Item | Shortcut |
|---|---|
| About Lens | |
| Check for Updates… | |
| Preferences | ⌘, |
| Services | submenu |
| Hide Lens | ⌘H |
| Hide Others | ⌥⌘H |
| Show All | |
| Quit | ⌘Q |

The first two are the application menu's; `Preferences` carries ⌘,.

`Edit`, with nothing focused in the table: `Close Window` ⇧⌘W. The menu is
context-sensitive — a focused table adds the clipboard items.

`View` follows.

`View`:

| Item | Shortcut |
|---|---|
| Toggle Toolbar | |
| Command Palette… | ⇧⌘P |
| Back | ⌘[ |
| Forward | ⌘] |
| Reload | ⌘R |
| Actual Size | ⌘0 |
| Zoom In | ⌘+ |
| Zoom Out | ⌘- |
| Toggle Full Screen | ⌃⌘F |

`Command Palette` is the keyboard-first surface: every action is reachable by
name without the tree. `Back` and `Forward` are the toolbar arrows, and `Reload`
is the table refresh.
