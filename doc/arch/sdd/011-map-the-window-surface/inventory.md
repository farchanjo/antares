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

`Help`: `Search`, `Welcome`, `Documentation`, `Release Notes`, then a separator,
`Support`, `Export Support Logs`, `License`.

`Export Support Logs` is the field-diagnostics action: it bundles the logs so a
user can attach them. `License` shows the bundled open-source notices.

## Menu bar, complete

`Lens` (application menu: About, Check for Updates, Preferences ⌘,, Services,
Hide ⌘H, Hide Others ⌥⌘H, Show All, Quit ⌘Q), `File`, `Edit`, `View`, `Help`.

## Table view (Secrets)

Tab `Secrets - hood`. Namespace selector reads `fp-prod`. `20 items`.

Columns: `☐` · `Name` · `Namespace` · `Labels` · `Keys` · `Type` · `ArgoCD` ·
`Age` · `⋮`

- **`Keys`** lists the key *names* of the secret, comma separated and truncated
  (`ca.crt, ca.p12, ca.pas…`). The values are never fetched for the list.
- **`Type`** carries the Kubernetes secret type (`Opaque`,
  `kubernetes.io/dock…`) and is truncated the same way.
- **`Labels`** renders `key=value` pairs, truncated.
- **`ArgoCD`** is a column of its own; empty here.
- A secret row is **not** expanded: the value is behind the detail pane.

## Column vocabulary, collected

Every family reuses a small set of cell types:

| Cell | Families | Rendering |
|---|---|---|
| name | all | text, link-styled |
| namespace | namespaced | text |
| labels | resources with labels | `k=v`, truncated |
| keys | secrets, config maps | key names, comma separated, truncated |
| type | secrets | text, truncated |
| cpu / memory / disk | nodes, pods | meter bar, coloured |
| restarts | pods | integer, dot when non-zero |
| controlled by | pods | owner kind |
| node | pods | node name |
| qos | pods | text |
| status | pods, others | coloured word |
| conditions | nodes | stack of coloured pills |
| webhooks | webhook configs | integer |
| version | nodes | text |
| age | all | compact duration |
| `⋮` | all | row menu |
| `☐` | all | selection checkbox |

## Navigation by keyboard

Coordinate clicks hit the tree unreliably (the list scrolls under the pointer).
Keyboard is deterministic and is the method from here:

```sh
agent-desktop --headed mouse-click --xy X,Y      # focus one tree row
agent-desktop --headed press down --app Lens     # next row
agent-desktop --headed press return --app Lens   # open it
```

`down` moves the selection; `return` opens the selected row. `escape` returns to
the tree from a view.

## Table view (Resource Quotas)

Columns: `☐` · `Name` · `Namespace` · `Age` · `⋮`. `0 items` in this namespace.

### Empty state

A scope with nothing in it shows, centred and grey, **`Item list is empty`**. No
icon, no action, no scope name. The `+` stays available above it.

## Table view (Horizontal Pod Autoscalers)

Columns: `☐` · `Name` · `Namespace` · `Metrics` · `Min Pods` · `Max Pods` ·
`Replicas` · `Age` · `Status` · `⋮`

`Metrics` renders `current / target` (`3% / 80%`). `Status` is a **row of pills**,
one per condition, each its own colour: `AbleToScale` green, `Scaling` blue.

Pills are the general answer for a multi-valued status cell — nodes use the same
shape for `Conditions`. Amber marks a degraded one (`SchedulingDisab`).

Empty families in this namespace: Resource Quotas, Limit Ranges.

## Table view (Pod Disruption Budgets)

Columns: `☐` · `Name` · `Namespace` · `Min Available` · `Max Unavailable` ·
`Current Healthy` · `Desired Healthy` · `Age` · `⋮`

`Max Unavailable` renders `N/A` when the budget states a minimum instead — an
either/or pair, only one of which is set. `8 items`.

## Table view (Priority Classes)

Columns: `☐` · `Name` · `Value` · `Global Default` · `Age` · `⋮`. `2 items`.

`Value` is a raw integer (`2000000000`). `Global Default` is the literal `false`
— a boolean renders as the word, not a tick.

## Table view (Leases)

Columns: `☐` · `Name` · `Namespace` · `Holder` · `Age` · `⋮`. `0 items` here.

`Runtime Classes` sits between Priority Classes and Leases and is cluster-scoped
like a priority class.

## Config family, complete

`Config Maps`, `Secrets`, `Resource Quotas`, `Limit Ranges`,
`Horizontal Pod Autoscalers`, `Pod Disruption Budgets`, `Priority Classes`,
`Runtime Classes`, `Leases`, `Mutating Webhook Configurations`,
`Validating Webhook Configurations`, `Admission Policies`.

## Table view (Jobs)

Columns: `☐` · `Name` · `Namespace` · `Completions` · `Age` · `Conditions` ·
`ArgoCD` · `⋮`. `0 items` here.

`Completions` is a bare count. `Conditions` is the pill stack again; `ArgoCD` is
the sync column and is empty without an Argo application.

## Table view (Cron Jobs)

Columns: `☐` · `Name` · `Namespace` · `Schedule` · `Suspend` · `Active` ·
`Last schedule` · `Next execution` · `ArgoCD` · `Time zone` · `Age` · `⋮`.
`0 items` here.

The widest column set seen: a cron job is the most structured workload. `Schedule`
is the raw cron expression, `Time zone` its zone, `Suspend` a boolean word.

## Workloads family, complete

`Overview`, `Pods`, `Deployments`, `Daemon Sets`, `Stateful Sets`, `Replica Sets`,
`Replication Controllers`, `Jobs`, `Cron Jobs`.

## Table view (Services)

Columns: `☐` · `Name` · `Namespace` · `Type` · `Cluster IP` · `Ports` ·
`External IP` · `Selector` · `ArgoCD` · `Age` · `Status` · `⋮`. `18 items`.

- `Type` is the service type (`ClusterIP`), plain text.
- `Ports` concatenates `port/name/protocol`, comma separated and truncated
  (`80,443/TCP, 47…`).
- `Cluster IP` shows `None` for a headless service.
- `Selector` renders the label selector, truncated.
- `External IP` is empty for a cluster-internal service.
- `Status` is the green `Active` pill.

## Family sub-tree: Storage

`Persistent Volume Claims`, `Persistent Volumes`, `Storage Classes`.

## Row detail

A row click opens the object in the right pane, beside the list. This is the
richest surface in the application and it is not a modal.

Header: a kind icon, then the kind and the name (`Deployment kafka-entity-operator`),
right-aligned actions: expand, edit, delete, close. The pane is resizable and
closable.

Below the header, **collapsible sections that behave as tabs**:

### Metrics

- Heading `Metrics`, an interval selector (`1h`), then expand, refresh, fullscreen.
- A provenance line: `Displaying metrics from Prometheus conntest-ue /
  prometheus-operator:9090`.
- A line chart, Y from `0.000` to `1.000`, X over the interval (`04:07` … `05:01`).
- A spinner while the series loads.

### Properties

A definition list, label left, value right:

| Field | Rendered as |
|---|---|
| `Created` | relative then absolute: `81d 25m ago (Aug 23, 2026, 4:40:46 AM GMT-3)` |
| `Name` | plain |
| `Namespace` | link |
| `Labels` | count, `8 Labels`, expanding to the pairs |
| `Annotations` | `key=value`, comma separated |
| `Replicas` | `1 desired, 1 updated, 1 total, 1 available, 0 unavailable` |
| `Controlled By` | `kind name`, both linked |
| `Selector` | the pairs, space separated, in a monospace face |
| `Strategy Type` | plain |
| `Status` | the status word |
| `Conditions` | pills: `Progressing` blue, `Available` green |

### Deploy Revisions

Collapsed by default. Columns `#`, `Summary`, `Pods`, `Age`.

## What this changes in the port

- The detail pane is a **sectioned panel with internal tabs**, not a label.
- Every value is a **labelled row**, so the pane needs a definition-list widget.
- `Labels` shows a **count** and expands; it is not dumped inline.
- Metrics is a **chart with a provenance line and an interval selector**.
- The header carries **edit, delete and close**, so the pane is where writing starts.

### The chart, once loaded

A filled outline in the accent blue, no grid, a legend below it with a matching
swatch (`■ CPU Usage`). The Y axis rescales to the series (`0.000`…`0.010`). The
spinner is replaced by the plot; a failure would put the provenance line in red.

The interval selector offers `1h` by default; the refresh and fullscreen icons sit
beside it.

## What the detail pane proves

The pane is the product's centre of gravity: it is where an object is read
(Properties), watched (Metrics) and rolled (Deploy Revisions). The list is a way to
choose one. A port that renders only the list has the smaller half.

## Preferences, in full

A tab of its own, titled `Preferences`. A left rail of sub-pages in the order
`App`, `Proxy`, `Kubernetes`, `Editor`, `Tables`, `Terminal`, `Integrations`,
`Telemetry`, `Lens Teamwork`, `External Tools`; the selected one is filled.

### External Tools

- Title `External Tools`.
- `AI TOOL` followed by a `PREMIUM` pill.
- Description: `The AI tool Lens launches to eg. "Ask AI".`
- Right-aligned `2 of 5 detected on your system` and a `Refresh` link.
- Rows, icon and name then description, with the state at the right edge:

| Name | Description | State |
|---|---|---|
| Claude Code | The coding assistant by Anthropic | ✓ `INSTALLED` |
| GitHub Copilot CLI | `CLI command "copilot" not found in PATH, click "install" for instructions.` | `Install` |
| Gemini CLI | the same, for `gemini` | `Install` |
| Codex CLI | the same, for `codex` | `Install` |
| OpenCode | The coding assistant by SST | ✓ `INSTALLED` |
| Custom | Runs your own configured command | `CUSTOM` |
| No AI tool | Lens does not launch an AI tool. | radio, selected |

- Footer: `Open AI tools in an external terminal`, a toggle, and the line `Launch
  the selected AI tool in a new terminal window instead of a Lens terminal tab.`

`INSTALLED` carries a green tick. A row is a radio group: choosing one sets the
launcher.

### App

A settings page is a stack of sections, not a form:

- Section title in small caps.
- A grey one-line description under it.
- The control either right-aligned on the title row (a toggle) or full width
  below (a dropdown).

Sections, in order: `THEME` (dropdown, `Sync with computer`), `LENS CLI` (toggle
off; `Install 'lens' command to /usr/local/bin on macOS/Linux. On Windows 'lens'
command is automatically available.`), `COLORIZED CLUSTERS` (toggle **on**;
`Differentiate Kubernetes clusters by color`, `A specific color can be set in the
cluster settings.`), `START-UP` (toggle off; `Automatically start Lens on login`),
`UPDATE CHANNEL` (dropdown, `Stable`), `LOCALE TIMEZONE` (dropdown,
`America/Sao_Paulo`).

A toggle is a pill switch, green when on. A dropdown fills the content width.

### Proxy

`HTTP PROXY` — text field, placeholder `Type HTTP proxy url (example:
http://proxy.acme.org:8080)`, description `Proxy is used only for non-cluster
communication.`

`LENS INTERNAL PROXY` — toggle on. `Bypass Lens internal KubeApi Proxy` / `This
will make commands like kubectl and helm skip Lens internal KubeApi proxy.`

`CERTIFICATE TRUST` — toggle on. `Allow untrusted Certificate Authorities` / `This
will make Lens to trust ANY certificate authority without any validations. Needed
with some corporate proxies that do certificate re-writing. Does not affect
cluster communications!`

### Kubernetes

`KUBECTL BINARY DOWNLOAD` — toggle on. `Download kubectl binaries matching the
Kubernetes cluster version`.

`DOWNLOAD MIRROR` — dropdown, `Default (Google)`.

`DIRECTORY FOR BINARIES` — path field, `The directory to download binaries into.`

`PATH TO KUBECTL BINARY` — path field.

`MANAGE KUBECONFIGS` — button `Sync Files and Folders`.

`SYNCED ITEMS` — the synced paths, each with a bin icon to drop it
(`/Users/farchanjo/.kube/config`).

A `Helm` heading follows with a `CHARTS` section, so one page can carry more than
one heading.

### Editor

`MINIMAP` — toggle on, label `Show minimap`, and a right-aligned `Position`
dropdown (`right`).

`LINE NUMBERS` — dropdown, `On`.

`TAB SIZE` — numeric field, `2`.

`FONT SIZE` — numeric field, `12`.

`FONT FAMILY` — text field, `RobotoMono`.

The editor settings are a two-column row when the control is small (`MINIMAP` and
`Position` share a line) and full width otherwise.

### Terminal

`TERMINAL SHELL PATH` — text field, `/bin/zsh`.

`TERMINAL COPY & PASTE` — toggle on. `Copy on select and paste on right-click`.

`TERMINAL THEME` — dropdown, `Match Lens Theme`.

`FONT SIZE` — numeric field, `12`.

`FONT FAMILY` — dropdown, `RobotoMono`. The editor's font is free text, the
terminal's is a list.

### Integrations

Two headings, each a group of connector cards.

`Kubernetes Environments`:

| Card | Description | State | Actions |
|---|---|---|---|
| `AWS EKS` | Automatically discover and connect to Kubernetes clusters across your AWS accounts. | `⌁ Not Connected` | `Add AWS Account` |
| `AZURE AKS` | the same, for Azure | `⌁ Not Connected` | `Add Azure Account` |
| `LOCAL KUBECONFIGS` | Discover and manage Kubernetes clusters from kubeconfig files on your local filesystem. | `⌁ Connected  1 source  ^` | `Watch files & folders`, `Manually add kubeconfig` |

`AI & Intelligence Integrations`:

`ENABLE LENS MCP SERVER` — toggle on. `Expose Lens capabilities to AI agents and
tools via the Model Context Protocol (MCP). Works with Claude Code, Cursor, GitHub
Copilot, and other MCP-compatible clients.` Under it, a disclosure `Configure your
MCP client` with `Add this configuration to your MCP client. For example, add it
to your project's .mcp.json.`

A connected card shows its source count and a collapse chevron; a disconnected one
shows `Not Connected` in grey with a single primary action.
