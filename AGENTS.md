# AGENTS.md — SillyTavern Native Port

This file is the handoff point for a new session. Read it first, then the plan
you are about to execute.

## What this repo is

A native C++17 / gtkmm-3.0 Linux port of SillyTavern 1.19.0. No WebKit, no
Node.js, no embedded original frontend. The application lives in
`SillyTavern_port/`; the untouched upstream source is in `SillyTavern/`.

- `SillyTavern_port/src/core/` — GTK-free `libstcore` (formats, settings,
  prompt, HTTP/SSE, OpenAI client, chat storage, locking, journal, backups).
- `SillyTavern_port/src/ui/` — gtkmm windows, dispatchers, smoke harness.
- `SillyTavern_port/tests/` — unit, property, integration, and Xvfb smoke tests.
- `SillyTavern_port/features/features.json` — 105-feature registry; drives
  `make menuconfig` and `docs/FEATURES.md`.
- `docs/plans/` — every implementation plan ever written. Never delete one.
- `docs/PORT_LOG.md` — per-feature status and release notes.
- `docs/superpowers/specs/2026-11-01-sillytavern-cpp-port-design.md` — the design spec.
- `.superpowers/sdd/<plan>/` — task briefs, reports, review packages, and the
  `progress.md` ledger for subagent-driven runs.

## Current state (as of 2026-11-03)

- `v0.1.1` released and tagged at `e1f9096` (`fix(core): bound aggregate
  streamed response size`). Host and Alpine gates pass; 155 unit tests; 12
  smoke markers.
- `main` and `port/v0.1` are kept equal (fast-forwarded); tag `v0.1.1` is
  the release commit `e1f9096`.
- Remote `origin` = `https://github.com/l0ophole/sillytavern-native.git`.
- Registry: 105 features, 21 ported, 84 planned, targets 19 `0.1` + 2 `0.1.1`
  (`P09`, `P10`). All 19 critical v0.1 features are ported.
- The v0.1.1 whole-branch review verdict was "Ready to merge: Yes" with no
  Critical issues. Its deferred items are listed in
  `.superpowers/sdd/08-v0.1.1-reliability/progress.md` and drive the next plan.

## Next actions, in order

1. **Execute `docs/plans/09-v0.1.2-reliability-cleanup.md`.** Seven tasks that
   close the v0.1.1 review's non-blocking findings: draft write cap and
   backoff, draft lifecycle on delete/rename, recoverable regeneration drafts,
   scan-failure reporting, incremental SSE scanning, regression tests, and a
   warning-free-build assertion in the gate. Release as `v0.1.2`.
2. **Execute `docs/plans/10-v0.2-chat-depth.md`.** Ten tasks: swipes,
   branching and bookmarks, author's note, Markdown, and chat backups, then
   release as `v0.2.0`.
3. **Push after each release.** `git push origin main port/v0.1 --tags`.
   Adjust the branch list if work moved to a new branch.

Neither plan has been executed yet. Do not mark a feature `ported` before the
release task that owns it.

## Build and verify

All commands run from `SillyTavern_port/`:

```bash
make -j"$(nproc)"          # build (must be warning-free)
make test                  # unit + property tests
make smoke                 # Xvfb GTK smoke, prints `smoke: ...` markers
make test-sanitize         # ASan/UBSan (cleans first)
./tests/integration_openai.sh   # mock OpenAI server
./tests/verify_v01.sh      # full host gate
./tests/alpine_build.sh    # Alpine 3.20 musl gate (Docker)
make menuconfig            # ncurses feature tree
```

Dependencies (Arch): `gtkmm3`, `libcurl`, `ncurses`, `python`, `xvfb`,
`docker` for the Alpine gate.

## Working conventions

- One descriptive commit per task, immediately after its tests pass.
- Every plan task is TDD: failing test first, then implementation, then the
  covering suite, then the commit.
- Core stays GTK-free; workers never touch widgets — only `Glib::Dispatcher`
  handlers do.
- Never write inside `SillyTavern/`; all data lives under the XDG roots.
- Size caps are load-bearing: `settings.json` 1 MiB, cards/chats 64 MiB,
  drafts 16 MiB, one backup tree 512 MiB.
- Keep `docs/PORT_LOG.md` current and regenerate `docs/FEATURES.md` with
  `python3 tools/gen_features_doc.py` when the registry changes.
- Unported features stay visible but dim and unselectable in `make menuconfig`.
- For multi-task work, prefer the `superpowers:subagent-driven-development`
  flow: a fresh implementer per task, a task review, a fix loop, then a
  whole-branch review. Keep the ledger in `.superpowers/sdd/<plan>/progress.md`.

## Known deferred work (not scheduled)

From the v0.1.1 review, beyond plan 09: `StreamState`/UI delta accumulation is
bounded per response now but the transcript still re-renders the whole chat per
streaming chunk; UI-level tests for autosave throttling and async stale paths
are still thin; transcript rendering and avatar decoding still run on the GTK
thread; `make menuconfig` still lists ported features in a flat list.
