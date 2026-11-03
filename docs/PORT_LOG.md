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
| P09 Single-instance and data locking | 08-v0.1.1-reliability.md | yes | yes | pending | Advisory `flock` instance lock with holder PID plus per-chat write locks; second writer fails cleanly |
| P10 Crash recovery and autosave journal | 08-v0.1.1-reliability.md | yes | yes | pending | Atomic per-chunk draft journal under `.journal`, base-message staleness check, recovery on chat load |

## v0.1 completion

All 19 critical v0.1 features are ported and tested. The remaining 85 features
stay visible but dim and unselectable in `make menuconfig`. Host verification
and the Alpine (musl) container gate both pass, including the Xvfb smoke test
and live mock-server streaming.

Post-review minor fixes: card-file size caps are enforced before reading a
manually placed card, and deleting the last search-filtered character clears
the stale chat view instead of leaving the deleted character selected.

## Known v0.1 limitations

- Local character card and chat JSONL file I/O runs synchronously on the GTK main thread. Because v0.1 targets single-user local storage and enforces strict file-size caps (1 MiB for `settings.json`, 64 MiB for character cards and chat files), synchronous I/O avoids cross-thread session races in v0.1; background file I/O workers are deferred to post-v0.1.

## Stages

| Stage | Plan written | Complete | Evidence |
|---|---|---|---|
| 0 Foundation | yes | yes | `make`, `make test`, menuconfig selftest, Xterm TUI smoke |
| 1 Core platform | yes | yes | `make test` 28 tests, menuconfig selftest, mock SSE server streaming |
| 2 Data compatibility | yes | yes | `make test` 65 tests; PNG ccv3 precedence, path containment, JSONL round-trip; D02 UI completed in stage 4 |
| 3 Prompting/generation | yes | yes | `make test` 101 tests, `tests/integration_openai.sh` 101 tests; prompt order/macros, budget trimming, sampling clamps, SSE streaming/errors/cancellation |
| 4 GTK UI | yes | yes | `make test` 127 tests, `make smoke` (8 markers: window, avatar, chat round-trip, message actions, chat lifecycle, character delete, live UI streaming, partial on switch), `tests/integration_openai.sh` 127 tests, menuconfig selftest; no warnings; `src/core/` GTK-free |
| 5 Verification | yes | yes | `tests/verify_v01.sh`: clean build, 127 tests, 8 smoke markers, integration tests, menuconfig selftest, 105/19/86 counts + all `CONFIG_*`/`ST_FEATURE_*` flags verified, GTK-free core, clean upstream; `tests/alpine_build.sh` on alpine:3.20 (musl): build, 127 unit + integration tests, 8 smoke markers including live UI streaming and partial-on-switch |
