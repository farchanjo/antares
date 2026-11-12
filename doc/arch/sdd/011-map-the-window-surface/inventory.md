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
