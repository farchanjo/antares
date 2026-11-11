---
user-invocable: true
name: "speckit"
description: >-
  speckit — closed binary /usr/local/bin/speckit for Spec-Driven Development (binary-only; ~/dev/speckit only when developing it). Loop: brief→status→next→phase (constitution/specify/clarify/plan/tasks/analyze/implement); validate green before commit; guard exit 4. Project ask/search over committed doc/arch (evidence-only). Personal/external knowledge: library validate/add/ask/search/update (machine-local library.db, never touches repo). Semantic tier is API-only (OpenAI-compatible); deep corpus is opt-in. Authority: /usr/local/bin/speckit CMD --help | manual --topic. TRIGGER speckit, SDD, doc/arch, guard, validate, verify, search, ask, library, work-id, semantic, cpp, cmake, FetchContent. SKIP GitHub spec-kit.
metadata:
  agents: ""
  peers: "arch-advisor"
  triggers: "speckit, sdd, spec-driven, ask, semantic, search, library, work-id, cpp, c++, cmake"
  related-agents: "rust-engineer: speckit source lives at ~/dev/speckit (Rust edition 2024, MSRV 1.99.1, hexagonal infrastructure->application->ports/domain, DENY lints incl. print_stdout/print_stderr, no unwrap in prod). Follow its AGENTS.md/CLAUDE.md; cargo run -- validate must stay green before every commit. NEVER run `speckit config set` against the speckit repo itself (rewrites speckit.toml, drops comments) — scratch tempdir projects only.; architect-reviewer: Review doc/arch corpus artifacts speckit validates (MADR ADRs, CUE schemas with DDD-role headers, bound Gherkin, statecharts, threat models). A red validate blocks everything: fix the artifact, never the rule. Divergence from an accepted ADR gets a refinement note appended to the ADR, never a silently over-claiming ADR."
  dsh:
    paths:
      - "doc/arch/speckit.toml"
      - "doc/arch/sdd/**"
      - "doc/.specify/**"
    when-to-use: "Any project with doc/arch or doc/.specify; advancing SDD phases; validate/verify/score; project ask/search over committed corpus; personal/external library ingest and ask/search by work-id; semantic status/reindex (fast or deep); guard scope; exit codes 3-8; legacy /spec or spec-mode requests redirect here — spec-mode is dead."
---

# speckit — agent operating skill (v0.44.0)

Self-contained Spec-Driven Development CLI. **Always invoke** `/usr/local/bin/speckit` — never a bare `speckit` from an ambiguous PATH.

**Authority on drift:** this skill tracks verified `/usr/local/bin/speckit` **v0.44.0** behavior. When syntax, flags, or semantics are in doubt, the live binary wins:

```bash
/usr/local/bin/speckit <cmd> --help
/usr/local/bin/speckit manual --topic <topic>
/usr/local/bin/speckit manual --ask "<question>"
```

Closed manual topics include: `lifecycle`, `exit-codes`, `guard`, `json-contract`, `config`, `commands`, `validators`, `errors`, `recipes`, `library`, `mermaid`, `license`, `verify-steps` (aliases: `validate` → `validators`, `lib` → `library`, `diagram` → `mermaid`, `steps` → `verify-steps`). Do **not** invent flags from memory.

## Quick Start

| Goal | Loop (always `/usr/local/bin/speckit …`) |
|---|---|
| **Project workflow** | `status` → `next` → run the named phase → `validate` |
| **Project knowledge** | `ask` / `search` over **committed** `doc/arch` (evidence pack / hits) |
| **Tier health** | `semantic status` / `semantic deep-status` (route + key presence; never a gate) |
| **Personal / external knowledge** | `library validate` → `add` → `ask`/`search` → `update` / `remove` |
| **Remote model surface** | `model api list` → `model api select <role> <id>` → `model api chat` |
| **Fast test loop** | `nexttest` → edit → `verify --changed` (diff slice, seconds) |
| **Binary path** | `/usr/local/bin/speckit` only |

Session opener when lost:

```bash
/usr/local/bin/speckit brief [--budget-tokens N]
/usr/local/bin/speckit status
/usr/local/bin/speckit next          # run EXACTLY the command it names (queue-aware)
/usr/local/bin/speckit missing [--json] [--limit N]
/usr/local/bin/speckit validate [--focus] [--changed]
/usr/local/bin/speckit nexttest            # what to attack now
```

## BINARY-ONLY CONTRACT (hard)

- NEVER read/grep/explore `~/dev/speckit` to answer usage, interpret exit codes, or debug consumer projects.
- Unexpected behavior → report as a **speckit bug** to the operator; do not source-dive.
- **Sole exception:** task is explicitly developing speckit itself (`cwd = ~/dev/speckit`).
- Propagate this into every subagent brief that mentions speckit: binary only.

## Decision tree

```
What knowledge or action?
|
+-- Question about THIS project's committed specs/ADRs/plans
|     → /usr/local/bin/speckit ask "…" [--json]
|     → or /usr/local/bin/speckit search "…" [--kind …] [--json]
|     (project surfaces do NOT consult the personal library)
|
+-- External / personal docs (books, notes, papers, foreign repos)
|     → library path (machine-local ~/.config/speckit/library.db)
|     → ingest without touching the repo:
|         /usr/local/bin/speckit library validate --from <path> [--json]
|         /usr/local/bin/speckit library add --from <path> [--work-id <id>] [--kind <k>]
|     → query:
|         /usr/local/bin/speckit library ask "…" --work <id> [--json]
|         /usr/local/bin/speckit library search "…" --work <id> [--json]
|     → refresh work after source changes:
|         /usr/local/bin/speckit library update <work-id>
|     → remove:
|         /usr/local/bin/speckit library remove <work-id>
|
+-- Semantic tier looks off / deep absent / model unbound
|     → /usr/local/bin/speckit semantic status      # route, key presence, degrade reason
|     → /usr/local/bin/speckit semantic deep-status # role selections + corpus state
|     → /usr/local/bin/speckit model api list       # what the endpoint serves
|     → /usr/local/bin/speckit reindex              # fast mirror
|     → /usr/local/bin/speckit reindex --deep       # explicit deep full build only
|     Deep absent/stale is NOT a failure of the fast tier (exit 0 + hint).
|     The tier is API-only: every embed/rerank/judge call is HTTP; no local weights.
|
+-- Advance SDD work
      → /usr/local/bin/speckit status && /usr/local/bin/speckit next
      → execute the named phase command exactly
      → /usr/local/bin/speckit validate before every commit
```

## search vs ask (formal)

| Surface | Returns | Agent duty |
|---|---|---|
| `search` | Discovery **hits** (paths/sections/scores) | Scan, pick targets, open files |
| `ask` | Budgeted, provenance-tagged **evidence pack** of verbatim sections — **answer-free by construction** | **Synthesize** the answer, **cite** provenance; never treat pack as proof of implementation |

Shared rules:

- **Project** `ask`/`search` → committed `doc/arch` (+ optional machine-local **packs** via `--packs`). They do **not** query `library`.
- **Library** `ask`/`search` → `~/.config/speckit/library.db` only. Library never writes/injects into `doc/arch`, packs, or project corpus.
- Prefer `--json` when piping to tools. `--json` emits exactly one JSON object on stdout.
- Evidence-only ≠ code truth: after a project `ask` about a feature, **verify in code/tests** before claiming implemented.

## Canonical command cheat-sheet

Prefix every line with the absolute `/usr/local/bin/speckit`.

**Retrieval (project)**

```bash
/usr/local/bin/speckit ask "<question>" [--budget-tokens N] [--json] [--lexical-only] [--packs <list>] [--pinpoint] [--module <slug>]
/usr/local/bin/speckit search "<query>" [--kind <kind>] [--limit N] [--json] [--lexical-only] [--packs <list>] [--module <slug>]
```

**Semantic**

```bash
/usr/local/bin/speckit semantic status
/usr/local/bin/speckit semantic deep-status
/usr/local/bin/speckit reindex
/usr/local/bin/speckit reindex --deep
/usr/local/bin/speckit semantic enable          # opt in; installs/verifies the pinned model (network)
/usr/local/bin/speckit semantic enable <dir>    # air-gapped source dir, same pinned-digest check
/usr/local/bin/speckit semantic off             # project-level opt-out
/usr/local/bin/speckit model api list            # models the endpoint serves
/usr/local/bin/speckit model api select <role> <id>
/usr/local/bin/speckit model api chat            # one chat completion to the bound llm role
/usr/local/bin/speckit model api apply           # semantic.deepBackend=openai, machine-local
```

**Library (machine-local; no repository writes)**

```bash
/usr/local/bin/speckit library validate --from <path> [--work-id <id>] [--kind <k>] [--json]
/usr/local/bin/speckit library add --from <path> [--work-id <id>] [--kind <k>] [--license-note "…"] [--mermaid-strict] [--json]
# NO --force on validate/add
/usr/local/bin/speckit library update <work-id> [--json]     # positional work-id
/usr/local/bin/speckit library remove <work-id> [--json]     # positional work-id
/usr/local/bin/speckit library ask "<q>" --work <id> [--kind <k>] [--limit N] [--json]
/usr/local/bin/speckit library search "<q>" --work <id> [--kind <k>] [--limit N] [--json]
/usr/local/bin/speckit library list | show <work-id>
/usr/local/bin/speckit library import <file> [--force]       # --force ONLY here (overwrite work id)
/usr/local/bin/speckit library export --work <id> --to <file>
/usr/local/bin/speckit library open --work <id> --asset <path> [--mode auto|kitty|iterm|open|path|source]
/usr/local/bin/speckit library extract --work <id> --asset <path> --to <file>
/usr/local/bin/speckit library serve [--host H] [--port N] [--kind <k>] [--no-open]   # browse = alias
```

Kinds: `book` | `paper` | `study-case` | `coding` | `documentation` | `coding-docs`.
Modalities: text, image, svg. Mermaid fences render via pure-Rust `mermaid-subset/v1`; `--mermaid-strict` makes an unsupported fence fatal.

**SDD lifecycle**

```bash
/usr/local/bin/speckit init --here [--email you@example.com] [--license <id>] [--github-ci] [--force]
/usr/local/bin/speckit constitution
/usr/local/bin/speckit specify "short feature name" [--module <slug>]   # 3–5 words → branch NNN-slug
/usr/local/bin/speckit clarify | plan | tasks | analyze | implement
/usr/local/bin/speckit feature list | select <NNN> | archive | compact | insert | new | renumber | reorder | restore
/usr/local/bin/speckit validate [--focus] [--changed] [--deep] [--list] [--json]   # must be 0 finding(s): ok before commit
/usr/local/bin/speckit check
/usr/local/bin/speckit verify [--changed] [--filter <s>] [--tags <expr>] [--jobs N]
/usr/local/bin/speckit nexttest
/usr/local/bin/speckit commit suggest | commit check
/usr/local/bin/speckit guard check <path>             # verdict only, never mutates
/usr/local/bin/speckit guard hook                     # host PreToolUse adapter, stdin JSON
/usr/local/bin/speckit hook session-start | user-prompt | post-edit | pre-commit
/usr/local/bin/speckit on | off
```

**Generated artifacts (never hand-edit):** `doc/arch/agents/`, `doc/arch/skills/`, the roster and
every projected section of `AGENTS.md`. Their writes bypass the guard scope check under a named
carve-out and always audit as `carve-out` (ADR-0127).

```bash
/usr/local/bin/speckit agent propose | confirm | reject | index | regenerate <slug>
/usr/local/bin/speckit skill propose | confirm | reject | index | regenerate <slug>
/usr/local/bin/speckit generate agents | root [--check] [--adopt] [--mode <m>]   # writes; --check exits 6 on drift
/usr/local/bin/speckit generate commands                                          # regenerates doc/arch/commands/
/usr/local/bin/speckit generate readme                                            # authoring brief only, no write
```

A feature carries a `## Crew` pool and each task narrows it; the guard scope is the intersection with
the bound agents, so a write outside the binding exits 4. Under `crew.enforce = "block"` a commit
needs a fresh attestation: `crew review --agent <review|security> --attest "<note>"` pins the staged
diff hash, and `commit check --message "<header>"` reports `crew.review.missing` /
`crew.review.stale` until it matches. `commit check` always exits 0 — its finding is the stderr line.

**Decision tier (optional):** `decision status` (sites on/off/blocked), `probe`, `probe-rule`, `eval`,
`extract`. Disable a site by hand-editing the `[decision] disabledSites` list in
`doc/arch/speckit.toml` (a list key, so `config set` rejects it). Dormant or unreachable ⇒
deterministic heuristic, exit 0.

**Providers:** `provider list | status | probe` — the declared remote routes behind the deep tier;
`model api` binds a served model to a role.

**C/C++ source tier (v0.44.0, ADR-0173):** detection splits into `c` and `cpp`
(`CMakeLists.txt` or a C++ source/header under `codeRoot` is `cpp`; a `.c` source is
`c`). No compiler, no CMake cache — the readers are textual like every other
validator.

| Rule | Fires on | Severity |
|---|---|---|
| `fqcn.relative-super` | `#include "../x.hpp"` in production code | error |
| `fqcn.partial-include` | quoted include that only resolves beside its own file | error |
| `pin.cmake.unpinned-fetch` | `FetchContent_Declare`/`ExternalProject_Add` with no `URL_HASH` and no `GIT_TAG` | error |
| `pin.cmake.floating-tag` | `GIT_TAG main`/`master`/`develop`/`HEAD`/`trunk`, even beside a hash | error |
| `pin.cmake.find-package-unversioned` | `find_package(X)` with no version | warning |
| `cmake.link-visibility` | `target_link_libraries(<exe> PUBLIC …)` | error |
| `cmake.subdirectory-order` | `add_subdirectory` above the first `add_library`/`add_executable` | error |

Angle includes (`<openssl/...>`, `<vector>`) and quoted includes naming no file
under `codeRoot` are ignored. The test path (`test/`, `tests/`, `test_*`,
`*_test.*`, `*_unittest.*`) excuses only the `../` form.

`constitution.doctrines.gap` now distinguishes a declared `(manual)` doctrine
from one whose every rule is gated to another language — the second reads
`no checker for the detected stack: <lang>`.

**Project-declared external checker (v0.44.0):** a stack the binary does not
ship a checker for still gates its tree. Declare it in `doc/arch/speckit.toml`:

```toml
[[validate.external]]
id = "clang-tidy"                 # ^[a-z0-9][a-z0-9-]*$, max 32
command = "clang-tidy"
args = ["-p", "build", "--quiet"]
severity = "error"                # error (default) | warning | info
```

`validate` runs each entry in the project root: exit 0 → `<id>.ok` info, non-zero
→ `<id>.failed` at the declared severity, missing command → `<id>.skipped` info,
malformed entry → `external.invalid` warning. Cap 8 entries; the entries appear
in `validate --list`. A list-of-tables key, so `config set` rejects it — edit the
file.

**Other useful leaves:** `brief`, `missing`, `spec score`, `context pack|score`, `dedupe`, `dismiss`, `explain`, `guide`, `stats attributes|compliance|corpus|findings|guard|profile|recommendations`, `crew review|backfill`, `doctrine list|show`, `module add|check|list|remove|retire|show`, `gitlab sync|status`, `pack add|import|export|list|remove|update`, `license check|list|set|show`, `migrate`, `workflow render`, `mermaid render`, `manual`, `version`, `completions zsh`.

Full leaf list: `/usr/local/bin/speckit manual --topic commands`.

## End-to-end examples

### A. Is this project feature implemented? (evidence-only → verify code)

```bash
# 1) Evidence pack from committed corpus — NOT a yes/no oracle
/usr/local/bin/speckit ask "Is feature 042 rate-limit middleware implemented?" --json

# 2) Discovery hits if you need paths first
/usr/local/bin/speckit search "rate limit middleware" --kind spec --json

# 3) YOU synthesize from evidence, then VERIFY in code/tests
#    (rg/tests/CI). Spec text can lag or lead the tree.
# 4) Never claim "implemented" from ask alone.
```

### B. Index external docs without touching the repo

```bash
# Isolated staging (outside the git worktree is fine)
STAGE=/tmp/speckit-lib-stage/lang-lock-notes
mkdir -p "$STAGE" && cp -R ~/Documents/lang-lock/*.md "$STAGE/"

/usr/local/bin/speckit library validate --from "$STAGE" --work-id lang-lock --kind documentation --json
/usr/local/bin/speckit library add --from "$STAGE" --work-id lang-lock --kind documentation --license-note "personal notes"
/usr/local/bin/speckit library ask "What is the lock order for embeddings?" --work lang-lock --json
/usr/local/bin/speckit library search "embedding lock" --work lang-lock --json

# After editing sources on disk:
/usr/local/bin/speckit library update lang-lock

# Cleanup work (still no project tree writes):
/usr/local/bin/speckit library remove lang-lock
```

`library.*` mutates only `~/.config/speckit/library.db` (+ mermaid cache under that store). `git status` in the project must stay unchanged by library ops.

### C. Fast/deep stale or absent — not an error

```bash
/usr/local/bin/speckit semantic status
# e.g. route reported, key absent, or embeddings stale — rebuilt on the next semantic command → OK, exit 0

/usr/local/bin/speckit semantic deep-status
# e.g. corpus: absent | freshness: stale  → still OK for fast-tier ask/search

/usr/local/bin/speckit ask "guard exit 4 meaning" --json   # already answered via lexical floor
# When you want deep quality later (never kill -9):
/usr/local/bin/speckit reindex --deep
```

### D. Lang Lock scope split (project ask vs library ask)

```bash
# Project corpus only (committed doc/arch of the current repo)
/usr/local/bin/speckit ask "How does Lang Lock integrate with the guard?" --json

# Personal/external Lang Lock notes ingested earlier as work-id lang-lock
/usr/local/bin/speckit library ask "How does Lang Lock integrate with the guard?" --work lang-lock --json
```

Same question, **disjoint corpora**. Project ask never reads `library.db`; library ask never reads `doc/arch`. Cite provenance tags (`[work:…]` vs project paths) so the answer does not blend scopes.

### E. Fast test loop (feature 087)

```bash
/usr/local/bin/speckit nexttest --json          # FR without scenario → failing → orphan, each with its command
# edit the artifact or the code
/usr/local/bin/speckit verify --changed         # only the diff's scenarios; reuse the rest
/usr/local/bin/speckit validate                 # green before commit
```

A full `verify` is minutes; the diff slice is seconds. Never lower `--jobs` to "isolate" a failure
without re-running the whole corpus afterwards.

## Semantic verification checklist

Before trusting retrieval or closing a task:

1. **Provenance** — every claim cites pack paths / work tags from the tool output.
2. **JSON contract** — `--json` → single object; parse `ok` / findings; usage errors are exit **2** and write nothing.
3. **Work scope** — library queries use `--work <id>` or `--kind` when the answer must not bleed across works.
4. **Code verification** — project `ask` about implementation → confirm in source/tests.
5. **Git unchanged by library** — after library add/update/remove, project `git status` clean of unexpected edits.
6. **Validate before commit** — `/usr/local/bin/speckit validate` → `0 finding(s): ok`.
7. **Stale/absent deep** — note in status, do not fail the turn; reindex when convenient.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `unknown flag --force` on `library add` / `validate` | `--force` is **only** on `library import` and `init`. Drop it. |
| Wrong `library update` / `remove` shape | Positional: `library update <work-id>`, `library remove <work-id>` — not `--work-id` on update/remove. |
| Used `ask` but meant personal notes | Use `library ask` / `library search` with `--work`. Project ask never sees library. |
| Deep unavailable / corpus absent | Fast tier already answered. `reindex --deep` when ready; not a hard error. |
| Exit **2** | Usage (bad flag/kind/budget/topic). Re-read `--help` / manual; nothing written. |
| Re-add same work | Idempotent re-ingest path: prefer `library update <work-id>` from recorded `source_path`; re-`add` only when intentionally re-binding source. |
| Hung deep build | Send SIGTERM/SIGINT — **never `kill -9`** (can orphan GPU memory). Build resumes on re-run. |
| Guard deny exit **4** | Stay in active feature scope or `feature select <NNN>`; never automate `--allow-out-of-spec`. |
| Exit **8** state corruption | Front matter hand-edited — restore from git; never hand-fix forward. |
| `guard hook` / `hook *` "fail" | They exit **0** on every normal path, deny included; the verdict is the `permissionDecision` JSON on stdout. |
| Local weights expected | The tier is API-only: embed/rerank/judge are HTTP calls; there is no device, no `model fetch`. |
| `model list` / `model fetch` unknown | Use `model api list` / `model api select` / `model api chat` / `model api apply`. |
| `generate claude` / `grok` / `llm-context` | Retired by ADR-0111; the target is `agents` (exit 2 names it). |
| `generate agents` on a hand-written file | Refuses until `--adopt`; `--check` renders without writing and exits 6 on drift. |
| `commit check` "failed" | Always exits 0; the finding line on stderr is the verdict. Fix the message or the crew attestation. |

## Safety (non-negotiable)

- NEVER edit `doc/.specify/*.db` — disposable mirrors; text under `doc/arch/` wins.
- NEVER open `~/dev/speckit` unless the task is developing speckit.
- NEVER expose secrets (`GITLAB_TOKEN`, provider API keys) in argv, logs, or commits.
- Library: **no repository writes**; store is machine-local `~/.config/speckit/library.db`.
- NEVER commit machine-local selections (`semantic.deepBackend`, `model.selected*`, `semantic.openai.*` under `~/.config/speckit/config.toml`).
- NEVER commit with red `validate` / `check`; no AI attribution trailers; subjects ≤72 chars.
- NEVER `kill -9` a `reindex --deep`.
- Threat entries: `### TM-NNN` (the `: Title` is optional) or a five-column `| TM-NNN | … |` row.
  `privacy-threat-model.md` keeps LINDDUN `PT-NN` ids and is exempt from the STRIDE sweep.
- Every dotted token in `AGENTS.md` must resolve to a registered config key or a catalogued rule id
  (`llm-context.config-key.unknown`); `explain <id>` documents a rule id.

## GOLDEN RULES

1. Source of truth = committed text under `doc/arch/`. Spec > code > assumption. Ask before assume.
2. `validate` green before every commit; fix the **artifact**, never the rule.
3. Feature status lives in spec.md YAML front matter — only phase commands change it.
4. Active feature = git branch `NNN-slug`; `specify` creates; `feature select` switches.
5. Guard denies out-of-scope writes (exit 4).
6. `specify` descriptions become the slug — keep short (3–5 words).
7. Closed flags/keys/kinds only; unknown → exit 2, no write.
8. **Absence is a plus, never an error:** missing model/token/deep/map → graceful degrade (exit 0 + hint).
9. Advisory `Info` (spec-lint, scores, dedupe) informs; never auto-dismiss; never treats as gate.
10. The semantic tier is remote: absence of a key or route degrades to lexical, never blocks.
11. Binary help/manual overrides this skill on drift.

## Feature lifecycle (project)

```bash
/usr/local/bin/speckit specify "short feature name"
# author spec.md body (NOT front matter)
/usr/local/bin/speckit clarify    # then author clarifications
/usr/local/bin/speckit plan       # content-safe if plan.md missing; `plan setup` re-scaffolds
/usr/local/bin/speckit tasks      # same; `tasks setup` re-scaffolds
/usr/local/bin/speckit analyze
/usr/local/bin/speckit implement  # then write code; tick tasks.md
```

Between steps and before every commit: `/usr/local/bin/speckit validate`.

`next` is **queue-aware**: multiple pending features → lowest-numbered pending; no active feature + queue head → recommends `feature select <NNN>`, not `specify`. During implement, `next` descends to next unchecked `T-NNN`.

## Slash command `/speckit [args]`

| Invocation | Run | Report |
|---|---|---|
| `/speckit on` | `/usr/local/bin/speckit on` | lock on |
| `/speckit off` | `/usr/local/bin/speckit off` | lock off |
| `/speckit` or `/speckit status` | `status` then `next` | position + one step |
| `/speckit <rest>` | `/usr/local/bin/speckit <rest>` verbatim | output + hint |

PreToolUse hook may call `speckit guard hook` (stdin JSON → allow/deny). `doc/.specify/**` always denied.

## Exit codes (stable 0–8)

| Exit | Meaning | Action |
|---|---|---|
| 0 | ok (incl. guided no-ops / deep-not-built hints) | continue |
| 1 | general / partial gitlab failures | read stderr; retry safe when idempotent |
| 2 | usage | fix flags; nothing written |
| 3 | prerequisite missing | run what stderr names |
| 4 | guard deny | work in scope / `feature select` |
| 5 | path traversal | project-relative path |
| 6 | validation failed | fix each finding |
| 7 | template render | report speckit bug |
| 8 | state corruption | restore front matter from git |

Library subcommands: exit **0** or **2** only.

## Environment (common)

- `SPECKIT_LOG` = `off|error|warn|info|debug|trace` (default `warn`)
- `SPECKIT_MODEL_DIR`, `SPECKIT_MODEL_SHA256` (test seam)
- `SPECKIT_DEEP_DIAG_CEILING_GB` — ops/investigation only
- `GITLAB_TOKEN` — never argv/log
- `SPECKIT_GUARD_MODE` = `enforce|advisory|off` (ignored under lock)
- `SPECKIT_PROJECT_ROOT` — root override

## Progressive disclosure

Deep library + semantic operator detail: **`references/library-semantic.md`**.

When this skill and the binary disagree: **binary `--help` / `manual` win**. Do not invent surface area.
