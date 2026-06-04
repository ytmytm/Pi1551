# Agent instructions (Pi1541)

## Code knowledge graph (graphify / Cursor)

This repo indexes **`src/`** into `graphify-out/` via [graphify](https://github.com/safishamsi/graphify) (`graphifyy` on PyPI).

- **Query first:** `.venv/bin/graphify query "<question>"` from the repo root.
- **Rebuild:** `.venv/bin/graphify update .` after `src/` changes (git post-commit hook also refreshes the graph).
- **Cursor:** `.cursor/rules/graphify.mdc` (always on) and `.cursor/skills/graphify/SKILL.md`.

Install or upgrade: `uv venv .venv && uv pip install --python .venv/bin/python graphifyy && .venv/bin/graphify update . && .venv/bin/graphify cursor install --project && .venv/bin/graphify hook install`

Do not run `graphify install --project` (Claude Code); use `graphify cursor install --project` instead.
