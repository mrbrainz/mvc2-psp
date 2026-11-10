# MvC2 PSP

A PSP build of the Marvel vs. Capcom 2 decompilation, as a new platform backend for the existing port (`port/`).

## Layout
- `docs/backlog.js` — every to-do, as tickets. Board: `docs/backlog.html`.
- `tools/backlog.mjs` — backlog CLI (Node, no deps): `list | show ID | sync | check`.
- `AGENTS.md` / `DECOMP.md` — upstream PS2 decomp rules (matching, `make hybrid`). Follow them when touching `src/`, `config/`, `tools/`.
- `port/psp/` — the PSP build (own CMake project, `cmake -S port/psp ...`; see `port/docs/PSP.md`). Upstream files are unchanged.
- `port/` — upstream native/browser port. It already plays end to end via SDL2 + GL/WebGL2 (`port/platform/`, `port/render/`, `port/audio/`). PSP work adds a backend beside it; see `port/docs/PORT_PLAN.md`, `PLATFORM_SURFACE.md`, `RENDER_PATH.md`, `BOOT_NOTES.md` via ticket `read` refs only.

## Rules
- Every to-do lives in `docs/backlog.js`, nowhere else (not in code comments or this file). New work = new ticket.
- Ticket fields, in order: `id, order, phase, size, status, depends, read, title, description, done_when` (+ optional `note`).
  Status: Backlog, Ready, In progress, Blocked, Done, Won't fix. Size: XS <1h, S half day, M 1–2 days, L several days, XL split it.
- Completing a ticket: set Done + `note`, then `node tools/backlog.mjs sync`.
- Limit prose and non-relevant current work context specifics when completing tickets
- Don't read `docs/archive/` unless you need a closed ticket's detail.
- Keep this file short: rules and current state only. History belongs in ticket `note`s and git log, not here.
- Upstream rules still apply: nothing copyrighted committed (ISOs, ROMs, extracted assets, PSPSDK/compiler binaries stay out of git); player-facing behaviour follows the arcade; never add an option that makes the game worse.
- Never change upstream code (anything not PSP-specific: `src/`, `config/`, `tools/`, shared `port/` files) without asking first. Prefer a separate PSP-side approach (new files under `port/psp/`, `port/platform/*_psp.c`, `port/render/r_gu*.c`) even if it duplicates logic.

## Working lean with tokens
- Read only what the ticket needs: its `read` refs and the files you'll change. No exploratory sweeps of the repo.
- Read large files with offset/limit or grep for the part you need; never re-read a file you just wrote or edited.
- Keep command output small: `| tail -n 5`, `grep -c`, `--quiet`/`-q` flags. Never dump whole logs, JSON blobs or HTML responses; print status codes or one matching line.
- Batch independent tool calls in one message; chain related shell steps in one command.
- Prefer small targeted edits over rewriting whole files.
- No subagents unless the user asks.
- Keep replies short: what changed, how it was verified, what's next. No restating the plan or the code.
- Ticket `note`s: 1–3 sentences (what changed, how verified, limits). No narration.

## Status (max 3 lines; replace, never append)
- P24 done; P25 renders logo, title, char select and fights but fights run at 43.6 fps (P26). Builds need EMSDK=~/Sites/emsdk-brew.
