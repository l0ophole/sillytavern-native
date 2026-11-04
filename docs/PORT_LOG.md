# Port Progress Log

Source: SillyTavern 1.19.0 (06bde939f)
Design: `docs/superpowers/specs/2026-11-01-sillytavern-cpp-port-design.md`
Master plan: `docs/plans/00-v0.1-master-plan.md`

## Status key

- Plan: written / missing
- Implemented: yes / no / partial
- Tested: yes / no / partial

## Features

| Feature | Plan | Implemented | Tested | Commit | Notes |
|---|---|---|---|---|---|
| P01 Build system and menuconfig | 01-stage-0-foundation.md | yes | yes | 28d5cd7 | Registry-driven ncursesw menuconfig, `.config` + `build/features.h`, planned features dim/locked |
| P02 XDG paths and atomic file I/O | 02-stage-1-core-platform.md | yes | yes | 0ecc7c0 | XDG dirs, fsync+rename atomic writes, 0600 option, `read_file_limited` size caps (`ee06e5b`) |
| P03 JSON serialization layer | 02-stage-1-core-platform.md | yes | yes | ce13874 | nlohmann/json wrapper with contextual errors |
| P04 Logging and diagnostics | 02-stage-1-core-platform.md | yes | yes | 50f417f | Thread-safe 0600 file/stderr log with secret redaction and UI error logging |
| P05 Settings and preferences | 02-stage-1-core-platform.md | yes | yes | 1af61ca | Defaults, clamping, unknown-key preservation, 0600 save + load permission repair (`ee06e5b`) |
| P06 Credential storage | 02-stage-1-core-platform.md | yes | yes | 5c56a5a | 0600 enforcement, secret masking, redaction check |
| P07 HTTP/TLS and SSE transport | 02-stage-1-core-platform.md | yes | yes | 6311899 | libcurl POST with TLS verification, race-free `reset_cancel`, multi-line `finish()` SSE parser (`ee06e5b`) |
| O01 File name sanitization and path safety | 03-stage-2-data-compatibility.md | yes | yes | 88dfa73 | Traversal rejection, control-char removal, UTF-8-safe length cap, safe_join containment |
| D01 Character card parser | 03-stage-2-data-compatibility.md | yes | yes | ed1ad0a | PNG tEXt chara/ccv3 with ccv3 precedence, V2/V3/legacy JSON, strict base64 |
| D02 Character library, import, avatars | 05-stage-4-gtk-ui.md | yes | yes | 9d83f1b | Core scan/import/delete (64 MiB cap) plus searchable GTK list, import/delete dialogs, 48 px avatars (`96d2a75`) |
| C01 Chat session storage and lifecycle | 03-stage-2-data-compatibility.md | yes | yes | bf2a586 | ST JSONL load/save/list/rename/delete (64 MiB cap), stable character ID key + display name (`ee06e5b`, `96d2a75`) |
| C02 Chat transcript UI and composer | 05-stage-4-gtk-ui.md | yes | yes | 3bc88a4 | ChatSession prompt/request, transcript with macros, budget label, chat selector/new/rename/delete, transactional send, partial-reply retention on cancel/error/switch/close (`96d2a75`) |
| C05 Message actions | 05-stage-4-gtk-ui.md | yes | yes | 725405d | Transactional edit/delete/regenerate/continue, context-menu sensitivity guards, continuation spacing (`ee06e5b`, `96d2a75`) |
| C30 User identity | 04-stage-3-prompting-generation.md | yes | yes | e30b3ea | Normalized persistent user name; settings integration |
| C03 Prompt assembly and core macros | 04-stage-3-prompting-generation.md | yes | yes | c59e8ce | Ordered system sections, case-insensitive {{char}}/{{user}}/BOT/USER macros, role mapping, post-history instructions |
| C06 Context budgeting and history trimming | 04-stage-3-prompting-generation.md | yes | yes | ac10cc2 | Deterministic UTF-8 token estimate, newest-first trimming, greeting reservation, budget enforcement |
| C39 Basic sampling controls | 04-stage-3-prompting-generation.md | yes | yes | 0b897b5 | Temperature/top_p/max_tokens/stream clamps, settings mapping, request JSON |
| C04 OpenAI-compatible streaming client | 04-stage-3-prompting-generation.md | yes | yes | f6fcff8 | SSE delta streaming, mid-stream provider error detection (`ee06e5b`), non-streaming fallback, cancellation, mock-server integration tests |
| P08 GTK application shell and navigation | 05-stage-4-gtk-ui.md | yes | yes | afd7c8b | GTK3 window, paned layout, header/settings dialog, startup error handling, status bar, Xvfb smoke harness |
| P09 Single-instance and data locking | 08-v0.1.1-reliability.md | yes | yes | 45f8801 | Advisory `flock` instance lock with holder PID plus per-chat write locks; second writer fails cleanly |
| P10 Crash recovery and autosave journal | 08-v0.1.1-reliability.md | yes | yes | 730bd8a | Atomic per-chunk draft journal under `.journal`, base-message staleness check, recovery on chat load |

## v0.1 completion

All 19 critical v0.1 features are ported and tested. The remaining 84 features
stay visible but dim and unselectable in `make menuconfig`. Host verification
and the Alpine (musl) container gate both pass, including the Xvfb smoke test
and live mock-server streaming.

Post-review minor fixes: card-file size caps are enforced before reading a
manually placed card, and deleting the last search-filtered character clears
the stale chat view instead of leaving the deleted character selected.

## v0.1.1 completion

v0.1.1 is a reliability release; it adds no v0.2 chat features.

- P09 (`45f8801`): single-instance `instance.lock` with the holder PID, plus
  per-chat advisory `flock` write locks so a second writer fails cleanly.
- P10 (`730bd8a`): atomic crash-recovery drafts under `chats/.journal`,
  staleness-checked against the on-disk chat and replayed on chat load.
- Character-library scans and chat loads run on background workers
  (`75561cb`, `a8ee9d2`, `0c2d938`).
- HTTP response bodies are bounded, SSE framing accepts CR/LF/CRLF, and a
  single SSE event is capped at 16 MiB (`bf69325`, `de26a31`).
- Parser property tests and the `make test-sanitize` ASan/UBSan target
  (`5f05955`).
- Both gates pass: `tests/verify_v01.sh` runs sanitizer tests, a clean normal
  build, 155 unit tests, integration tests, 12 smoke markers, the menuconfig
  selftest, the 105/21/84/2 feature counts with the P09/P10 membership check,
  the `0.1.1` version string, the GTK-free core, and a clean upstream tree.
  `tests/alpine_build.sh` repeats build, unit, integration, and smoke checks on
  Alpine 3.20 (musl).

## v0.1.2 completion

v0.1.2 is a reliability cleanup release; it adds no new features and the
feature registry is unchanged (105 features, 21 ported, 84 planned; targets
19 `0.1` + 2 `0.1.1`).

- Draft write cap and backoff (`56639e8`): streaming draft writes are bounded
  and failed saves are retried with backoff.
- Draft lifecycle on delete/rename (`1d57fec`): deleting or renaming a chat
  clears or moves its pending draft.
- Recoverable regeneration drafts (`39e1c21`): a draft captured during
  regeneration is recoverable instead of being discarded on recovery.
- Scan-failure reporting (`12b8176`): character-library scan failures are
  reported instead of silently showing an empty library.
- Incremental SSE scanning (`0700dad`): the SSE parser no longer rescans
  buffered bytes on every feed.
- Regression tests (`f88f860`): coverage for failed saves, stale loads, and
  malformed drafts.
- Warning-free build assertion (this release): `tests/verify_v01.sh` fails
  when the build log contains any `warning:` line.

## Known v0.1.1 limitations

- Startup character-library scans and chat loads run on background workers.
  Explicit import/delete and new-chat writes remain synchronous on the GTK
  thread and are size-capped: 1 MiB for `settings.json`, 64 MiB for character
  cards and chat JSONL, and 16 MiB for compact drafts. Streaming draft writes
  are atomic and throttled to one per second, with each write fsynced on the
  GTK thread.
- File I/O and JSONL parsing run on a worker, but transcript rendering
  (`refresh_transcript`) and the prompt-budget recomputation still run on the
  GTK thread after a load completes; a very large chat can stall the main loop
  during that render.
- Character scans run on a worker, but avatar decoding and `Gtk::ListBoxRow`
  construction for the results still run on the GTK thread.
- A draft captured during regeneration is not recoverable: the regenerated
  reply is popped in memory before generation, so the draft's base no longer
  matches the on-disk chat and is discarded on recovery. The previously saved
  reply is preserved; no history is lost or duplicated.
- Deleting or renaming a chat does not clear or move its pending draft.

## Stages

| Stage | Plan written | Complete | Evidence |
|---|---|---|---|
| 0 Foundation | yes | yes | `make`, `make test`, menuconfig selftest, Xterm TUI smoke |
| 1 Core platform | yes | yes | `make test` 28 tests, menuconfig selftest, mock SSE server streaming |
| 2 Data compatibility | yes | yes | `make test` 65 tests; PNG ccv3 precedence, path containment, JSONL round-trip; D02 UI completed in stage 4 |
| 3 Prompting/generation | yes | yes | `make test` 101 tests, `tests/integration_openai.sh` 101 tests; prompt order/macros, budget trimming, sampling clamps, SSE streaming/errors/cancellation |
| 4 GTK UI | yes | yes | `make test` 127 tests, `make smoke` (8 markers: window, avatar, chat round-trip, message actions, chat lifecycle, character delete, live UI streaming, partial on switch), `tests/integration_openai.sh` 127 tests, menuconfig selftest; no warnings; `src/core/` GTK-free |
| 5 Verification | yes | yes | `tests/verify_v01.sh`: sanitizer tests, clean build, 155 unit tests, integration tests, 12 smoke markers, menuconfig selftest, 105/21/84/2 counts with P09/P10 membership + all `CONFIG_*`/`ST_FEATURE_*` flags verified, `0.1.1` version string, GTK-free core, clean upstream; `tests/alpine_build.sh` on alpine:3.20 (musl): build, 155 unit + integration tests, 12 smoke markers |
