---
name: graphify
description: >-
  Cursor project skill: query the Pi1541 src/ knowledge graph (graphify-out/).
  Use for architecture, IEC/TCBM, drives, tape, emulation, and cross-file links
  in src/ — run graphify query before grep or mass file reads.
---

# Graphify — Pi1541 `src/` graph (Cursor)

This skill is loaded automatically in **Cursor** from `.cursor/skills/graphify/`. The always-on rule is `.cursor/rules/graphify.mdc`.

This repo keeps a **local AST knowledge graph** of firmware under `src/` only (see `.graphifyignore`). Artifacts live in `graphify-out/`. Graphify runs from the project venv at `.venv/bin/graphify`.

## When to use this skill

- Questions about how `src/` modules connect (Pi1541, Pi1551, Pi1581, IEC, TCBM, disk, tape, UI, etc.)
- Tracing symbols, includes, or communities across C/C++ files
- After you edit files under `src/` in this session (rebuild the graph)

Do **not** use the graph for `uspi/`, `docs/`, or root Makefiles — they are outside the indexed corpus.

## Fast path (graph already built)

If `graphify-out/graph.json` exists and the user asks a **natural-language question** about the codebase:

1. Run from the **repository root**:
   ```bash
   .venv/bin/graphify query "<question>"
   ```
2. Use narrower tools if needed:
   ```bash
   .venv/bin/graphify path "SymbolA" "SymbolB"
   .venv/bin/graphify explain "SymbolName"
   ```
3. Read `graphify-out/GRAPH_REPORT.md` only for broad architecture review or when query/path/explain are insufficient.
4. Open `graphify-out/graph.html` in a browser for interactive exploration.

Prefer `graphify query` over grepping `src/` when the graph can answer the question.

## Rebuild (no API cost)

After changing code under `src/`:

```bash
.venv/bin/graphify update .
```

Git **post-commit** and **post-checkout** hooks refresh the graph in the background when `src/` files change (see `.git/hooks/post-commit`). Skip manually with `GRAPHIFY_SKIP_HOOK=1 git commit`.

## Full rebuild / extra flags

For `--cluster-only`, `--force`, docs/PDF ingestion, or other CLI flags, see the [graphify README](https://github.com/safishamsi/graphify). Use `.venv/bin/graphify` from the repo root (do not run `graphify install --project` — that targets Claude Code, not Cursor).

## Install / upgrade (maintainers)

```bash
uv venv .venv
uv pip install --python .venv/bin/python graphifyy
.venv/bin/graphify update .
.venv/bin/graphify cursor install --project   # Cursor rule only
.venv/bin/graphify hook install
```

Package: [graphifyy](https://pypi.org/project/graphifyy/) (CLI `graphify`), upstream [safishamsi/graphify](https://github.com/safishamsi/graphify).
