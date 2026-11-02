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
| P01 Build system and menuconfig | 01-stage-0-foundation.md | yes | yes | 3a04dfc | Skeleton CLI; GTK comes in stage 4 |
| P02 XDG paths and atomic file I/O | 02-stage-1-core-platform.md | yes | yes | 0ecc7c0 | XDG dirs, fsync+rename atomic writes, 0600 option |
| P03 JSON serialization layer | 02-stage-1-core-platform.md | yes | yes | ce13874 | nlohmann/json wrapper with contextual errors |
| P04 Logging and diagnostics | 02-stage-1-core-platform.md | yes | yes | 50f417f | Thread-safe file/stderr log with secret redaction |
| P05 Settings and preferences | 02-stage-1-core-platform.md | yes | yes | 1af61ca | Defaults, clamping, unknown-key preservation, 0600 save |
| P06 Credential storage | 02-stage-1-core-platform.md | yes | yes | 5c56a5a | 0600 enforcement, secret masking, redaction check |
| P07 HTTP/TLS and SSE transport | 02-stage-1-core-platform.md | yes | yes | 6311899 | libcurl POST with TLS verification, cancellation, incremental SSE parser |
| O01 File name sanitization and path safety | 03-stage-2-data-compatibility.md | yes | yes | 88dfa73 | Traversal rejection, control-char removal, UTF-8-safe length cap, safe_join containment |
| D01 Character card parser | 03-stage-2-data-compatibility.md | yes | yes | ed1ad0a | PNG tEXt chara/ccv3 with ccv3 precedence, V2/V3/legacy JSON, strict base64 |
| D02 Character library, import, avatars | 03-stage-2-data-compatibility.md | partial | yes | c91f669 | Core scan/import/delete complete; avatar UI in stage 4; status stays planned |
| C01 Chat session storage and lifecycle | 03-stage-2-data-compatibility.md | yes | yes | bf2a586 | ST JSONL load/save/list/rename/delete, unknown-field preservation, traversal rejection |
| C30 User identity | 04-stage-3-prompting-generation.md | yes | yes | e30b3ea | Normalized persistent user name; settings integration |
| C03 Prompt assembly and core macros | 04-stage-3-prompting-generation.md | yes | yes | c59e8ce | Ordered system sections, case-insensitive {{char}}/{{user}}/BOT/USER macros, role mapping, post-history instructions |
| C06 Context budgeting and history trimming | 04-stage-3-prompting-generation.md | yes | yes | ac10cc2 | Deterministic UTF-8 token estimate, newest-first trimming, greeting reservation, budget enforcement |
| C39 Basic sampling controls | 04-stage-3-prompting-generation.md | yes | yes | 0b897b5 | Temperature/top_p/max_tokens/stream clamps, settings mapping, request JSON |
| C04 OpenAI-compatible streaming client | 04-stage-3-prompting-generation.md | yes | yes | f6fcff8 | SSE delta streaming, non-streaming fallback, error mapping, cancellation, mock-server integration tests |
| P08 GTK application shell and navigation | 05-stage-4-gtk-ui.md | yes | yes | pending | GTK3 window, paned layout, header/settings dialog, status bar, Xvfb smoke harness |

## Stages

| Stage | Plan written | Complete | Evidence |
|---|---|---|---|
| 0 Foundation | yes | yes | `make`, `make test`, menuconfig selftest, Xterm TUI smoke |
| 1 Core platform | yes | yes | `make test` 28 tests, menuconfig selftest, mock SSE server streaming |
| 2 Data compatibility | yes | yes | `make test` 65 tests; PNG ccv3 precedence, path containment, JSONL round-trip; D02 UI deferred to stage 4 |
| 3 Prompting/generation | yes | yes | `make test` 101 tests, `tests/integration_openai.sh` 101 tests; prompt order/macros, budget trimming, sampling clamps, SSE streaming/errors/cancellation |
| 4 GTK UI | no | no | — |
| 5 Verification | no | no | — |
