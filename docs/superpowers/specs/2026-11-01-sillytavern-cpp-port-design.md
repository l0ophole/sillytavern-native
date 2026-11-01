# SillyTavern C/C++ Port — Design Specification

- **Date:** 2026-11-01
- **Source version analyzed:** SillyTavern 1.19.0 (`06bde939f`)
- **Status:** Design approved; implementation plan pending
- **Target:** v0.1 core-chat MVP, then staged full parity

## 1. Purpose

Build a native C/C++ desktop port of SillyTavern for Linux. The port must:

- run under Xorg with a native GUI;
- compile and run on Arch Linux (glibc) and Alpine Linux (musl);
- build with `make`;
- expose its feature set through `make menuconfig`;
- preserve compatibility with SillyTavern character cards and chat files;
- reach full feature parity through staged releases after v0.1.

The original JavaScript application is a reference, not a runtime dependency.
No Node.js, embedded browser, WebKit, or original frontend bundle is used.

## 2. Goals

### 2.1 v0.1 goals

A single-user local application that can:

1. Launch a native GTK3 window under Xorg.
2. Import a SillyTavern character card (PNG V2/V3 or JSON).
3. Show the imported character with its avatar.
4. Configure an OpenAI-compatible endpoint, key, model, and basic sampling.
5. Start, load, rename, delete, and persist chats in SillyTavern JSONL format.
6. Assemble a SillyTavern-compatible prompt from card fields and history.
7. Stream a reply from an OpenAI-compatible `/chat/completions` endpoint.
8. Edit, delete, regenerate, and continue messages.
9. Keep history within a configurable context budget.
10. Survive restart with settings, cards, and chats intact.

### 2.2 Non-goals for v0.1

World Info, groups, persona manager, prompt presets, themes, extensions,
TTS, image generation, translation, vectors, multi-user/server mode, alternate
provider protocols, and all other non-critical features are explicitly deferred.
They remain visible but disabled in `make menuconfig`.

### 2.3 v0.1 feature IDs

Exactly these 19 features are critical and in scope:

| Area | Feature IDs |
|---|---|
| Platform | P01 build/menuconfig, P02 paths/atomic I/O, P03 JSON, P04 logging, P05 settings, P06 credentials, P07 HTTP/SSE, P08 GTK shell |
| Characters | D01 card parser, D02 character library/import/avatars |
| Chat | C01 session storage, C02 transcript UI, C03 prompt assembly, C04 OpenAI-compatible streaming, C05 message actions, C06 context budgeting, C30 user identity, C39 basic sampling |
| Safety | O01 filename/path safety |

The other 86 features are full-parity backlog and must be dim and unselectable
until ported. The complete matrix is in `docs/FEATURES.md` and
`SillyTavern_port/features/features.json`.

## 3. Constraints

- **Language:** C++17.
- **GUI:** GTK3 via gtkmm-3.0, native X11/Xorg rendering.
- **Networking:** libcurl with TLS verification and SSE streaming.
- **Configuration UI:** ncurses-based `menuconfig` tool.
- **JSON:** vendored `nlohmann/json` single header to avoid distro version drift.
- **PNG metadata:** in-house PNG chunk reader; no libpng dependency.
- **Build:** GNU Make; pkg-config; no CMake required.
- **Platforms:** Arch Linux and Alpine Linux, glibc and musl.
- **Data safety:** never write into the original SillyTavern installation.
- **Source control:** git; one descriptive commit per ported feature.
- **Plans:** one plan per stage in `docs/plans/`, never deleted.
- **Progress log:** `docs/PORT_LOG.md`, updated per feature.

## 4. Architecture

```text
+--------------------------------------------------------------+
|                        GTK3 / gtkmm UI                       |
|  MainWindow | CharacterList | ChatView | SettingsDialog      |
+-----------------------------+--------------------------------+
                              | events / Glib::Dispatcher
+-----------------------------v--------------------------------+
|                         libstcore                           |
| paths | json | settings | credentials | logging             |
| character_card | character_store | chat_store               |
| prompt | context_budget | http_client | sse | openai_client |
+-----------------------------+--------------------------------+
                              | files / HTTPS
+-----------------------------v--------------------------------+
| XDG config + data dirs       | OpenAI-compatible endpoint    |
+--------------------------------------------------------------+
```

### 4.1 `libstcore`

No GTK dependency. Owns all business logic and file formats. Built as a static
library so unit tests can link it without a display server.

Modules:

| Module | Responsibility |
|---|---|
| `paths` | XDG config/data/log paths; directory creation |
| `atomic_file` | temp-write, fsync, rename; safe reads |
| `json` | JSON aliases and parse/serialize helpers |
| `logging` | file and stderr logging; levels; redaction hooks |
| `settings` | schema, defaults, load/save, validation |
| `credentials` | API key storage, 0600 permissions, redaction |
| `http_client` | libcurl wrapper, headers, timeouts, cancellation |
| `sse` | incremental SSE line/event parser |
| `base64` | strict base64 decode/encode for card metadata |
| `character_card` | PNG tEXt `chara`/`ccv3` and JSON card parser |
| `character_store` | library scan, import, delete, avatar path |
| `chat_store` | ST-compatible JSONL load/save/list/rename/delete |
| `prompt` | macro expansion and prompt message construction |
| `context_budget` | heuristic token estimate and history trimming |
| `openai_client` | request build, streaming parse, error mapping |
| `names` | filename sanitization and path containment |

### 4.2 GTK UI

- `Application` owns the `Gtk::Application` lifecycle.
- `MainWindow` uses a `Gtk::Paned`: character library on the left, chat on the right.
- `CharacterList` provides search, import, selection, and avatar rendering.
- `ChatView` provides transcript, composer, send/cancel, chat selector, and
  message context actions.
- `SettingsDialog` edits connection, generation, identity, and context settings.
- Worker threads never touch GTK widgets directly. Streaming chunks are queued
  and delivered through `Glib::Dispatcher`.

### 4.3 Build tools

- `tools/menuconfig.cpp`: ncurses feature browser/editor.
- `features/features.json`: canonical registry (105 features).
- `tools/gen_features_doc.py`: generates `docs/FEATURES.md`.
- Generated `.config` and `build/features.h` are build artifacts.

## 5. Data model and compatibility

### 5.1 Directories

```text
$XDG_CONFIG_HOME/sillytavern-port/
  settings.json                  (0600)
$XDG_DATA_HOME/sillytavern-port/
  characters/                    imported PNG/JSON cards
  chats/<character-slug>/        JSONL chat files
  logs/                          application logs
```

Defaults follow XDG (`~/.config`, `~/.local/share`). An explicit override may be
added later; v0.1 uses XDG only.

### 5.2 Character cards

Supported inputs:

- PNG with `ccv3` tEXt chunk (takes precedence);
- PNG with `chara` tEXt chunk;
- V2/V3 JSON (`spec` + `data`);
- legacy JSON with fields at the root.

Normalized fields: `name`, `description`, `personality`, `scenario`,
`first_mes`, `mes_example`, `system_prompt`, `post_history_instructions`,
`alternate_greetings`, `tags`, `creator`, `character_version`, `spec_version`.

Import copies the source file into the library. The original file is never
modified. Name collisions get a numeric suffix. JSON-only cards use a
placeholder avatar.

### 5.3 Chat files

SillyTavern-compatible JSONL:

```json
{"user_name":"User","character_name":"Seraphina","create_date":"...","chat_metadata":{}}
{"name":"Seraphina","is_user":false,"is_system":false,"send_date":"...","mes":"...","extra":{}}
{"name":"User","is_user":true,"is_system":false,"send_date":"...","mes":"...","extra":{}}
```

- First line is metadata.
- Subsequent lines are messages.
- Unknown fields are preserved when loading and rewriting.
- Saves are atomic (temp file + rename).
- A new chat contains the character's first message.

### 5.4 Settings

`settings.json` fields for v0.1:

```json
{
  "api_base_url": "https://api.openai.com/v1",
  "api_key": "",
  "model": "gpt-4o-mini",
  "temperature": 1.0,
  "top_p": 1.0,
  "max_tokens": 300,
  "context_size": 4095,
  "user_name": "User",
  "system_prompt": "Write {{char}}'s next reply in a fictional chat between {{char}} and {{user}}.",
  "stream": true,
  "request_timeout_seconds": 300
}
```

Settings are validated and clamped on load. Unknown keys are preserved for
forward compatibility. The file is written with mode 0600 because it contains
the API key in v0.1; a dedicated secret store is a later hardening step.

## 6. Prompt assembly

v0.1 builds a single system message followed by history:

1. Character `system_prompt`, else global `system_prompt`.
2. Character description.
3. `{{char}}'s personality: <personality>` when present.
4. `Scenario: <scenario>` when present.
5. `Example dialogues:` + `mes_example` when present.
6. Post-history instructions are appended as a final system message after history.

Core macros replaced in all prompt text and message display:

- `{{char}}` / `<BOT>` → character name
- `{{user}}` / `<USER>` → user name

History mapping: `is_user` → `user`, otherwise `assistant`; `is_system` messages
map to `system`. Empty messages are skipped.

## 7. Generation and streaming

`OpenAIClient` sends:

```http
POST <base>/chat/completions
Authorization: Bearer <key>        (omitted when key is empty)
Content-Type: application/json

{"model":"...","messages":[...],"temperature":1.0,"top_p":1.0,"max_tokens":300,"stream":true}
```

- Base URL may already end in `/chat/completions`.
- SSE events are parsed incrementally; `data: [DONE]` terminates.
- `choices[0].delta.content` chunks are emitted to the UI.
- Non-streaming JSON responses are also accepted.
- HTTP errors surface `error.message`, `detail`, or status text.
- Cancellation aborts the curl transfer and keeps any partial text.
- TLS verification is always enabled in v0.1.

## 8. Context budgeting

v0.1 uses a deterministic heuristic, not an exact tokenizer:

- Estimate tokens as `ceil(UTF-8 code points / 4)` plus a small per-message
  overhead.
- Budget = `context_size - max_tokens - system prompt estimate`.
- Keep the newest messages that fit; always keep the first assistant greeting
  when possible.
- The UI shows the estimated prompt tokens and the configured limit.

Exact tokenizer integration is feature C33, deferred.

## 9. Feature configuration

### 9.1 Registry

`features/features.json` is the single source of truth. Each entry has:

```json
{
  "id": "C04",
  "group": "chat",
  "name": "OpenAI-compatible streaming generation",
  "description": "...",
  "critical": true,
  "target": "0.1",
  "status": "planned",
  "depends_on": ["P07", "C03", "C39"]
}
```

`status` is `planned` until the feature is implemented, tested, and committed,
then becomes `ported`.

### 9.2 `make menuconfig`

- ncurses UI with groups, feature list, and a help pane.
- Planned features are rendered dim and cannot be selected.
- Critical v0.1 features are locked on.
- Ported optional features (future) can be toggled.
- Dependencies are enforced; selecting a feature selects its prerequisites.
- Saves `.config` and regenerates `build/features.h`.

`make` without `.config` generates a default configuration with all critical
v0.1 features enabled. `make menuconfig` builds and runs the config tool before
the main application is built.

## 10. Build and platform support

### 10.1 Arch Linux

```sh
sudo pacman -S --needed base-devel gtkmm3 curl ncurses pkgconf
make
```

### 10.2 Alpine Linux

```sh
sudo apk add build-base gtkmm3-dev gtk+3.0-dev curl-dev ncurses-dev pkgconf
make
```

`nlohmann/json` is vendored, so no distro JSON package is required.

### 10.3 Make targets

| Target | Purpose |
|---|---|
| `make` | Build the application with current `.config` |
| `make menuconfig` | Build and run the ncurses feature editor |
| `make defconfig` | Reset to default critical configuration |
| `make test` | Build and run unit tests |
| `make smoke` | Run the GUI under Xvfb and exit |
| `make clean` | Remove build artifacts |
| `make install` | Install binary and desktop entry (later) |

## 11. Threading and error handling

- All file I/O and network work that can block runs off the GTK main thread.
- One generation worker at a time per window.
- The UI disables Send while streaming and exposes Cancel.
- Errors are shown in a dialog/status bar and logged with secrets redacted.
- Partial streamed text is kept when a request fails.
- Failed saves report an error and leave the previous file intact.

## 12. Testing and verification

### 12.1 Unit tests

- base64 round-trip and invalid input;
- synthetic PNG tEXt parsing, `ccv3` precedence;
- V2/V3/legacy JSON card parsing;
- chat JSONL load/save round-trip and unknown-field preservation;
- filename sanitization and path containment;
- prompt assembly order and macro replacement;
- context trimming boundaries;
- SSE chunk/event parsing;
- URL joining and error extraction;
- settings validation and clamping.

### 12.2 Integration

- A mock OpenAI-compatible server streams a known reply.
- A CLI/integration mode drives the client and asserts the accumulated text.
- An Xvfb smoke run opens the GTK window, processes events, and exits 0.

### 12.3 Platform verification

- `make` and `make test` on Arch Linux.
- Alpine build in a Docker container with the documented packages.
- Record exact commands and results in `docs/PORT_LOG.md`.

## 13. Staging, plans, and commits

Stages:

| Stage | Scope |
|---|---|
| 0 | Repository, spec, feature registry, skeleton, menuconfig |
| 1 | Core platform primitives |
| 2 | Character and chat data compatibility |
| 3 | Prompting, context budget, HTTP/SSE, OpenAI client |
| 4 | GTK shell, character library, chat UI, settings |
| 5 | Integration, Xvfb smoke test, Alpine build, docs |

Before each stage, a plan is written to `docs/plans/NN-stage-name.md`. Plans are
never deleted. Each ported feature gets its own commit immediately after it is
implemented and verified. `docs/PORT_LOG.md` records plan/implementation/test/
commit status for every feature.

## 14. Acceptance criteria for v0.1

1. `make menuconfig` lists all 105 features; 19 critical ones are enabled;
   86 planned features are dim and unselectable.
2. `make` builds from a clean tree on Arch and Alpine.
3. `make test` passes all unit and integration tests.
4. The app opens under Xorg and passes the Xvfb smoke test.
5. A real or mock OpenAI-compatible endpoint can stream a reply into the UI.
6. A SillyTavern PNG card imports and displays correctly.
7. A chat survives app restart with messages, names, and timestamps intact.
8. The original `SillyTavern/` tree is untouched.
9. Each v0.1 feature has a commit, a log entry, and a written stage plan.

## 15. Risks and mitigations

| Risk | Mitigation |
|---|---|
| GTK3/gtkmm availability on Alpine | Verify early in Docker; document exact packages |
| libcurl TLS backend differences | Use system libcurl; test HTTPS and error paths |
| SillyTavern format drift | Parse leniently, preserve unknown JSON fields |
| Large scope causing shallow port | v0.1 limited to 19 critical features |
| UI/network races | Single worker, dispatcher marshalling, cancel token |
| menuconfig complexity | Registry-driven UI; no hand-maintained feature list |
| Data loss | Atomic writes; import copies; never write original tree |

## 16. Future direction

After v0.1, each deferred feature moves from `planned` to `ported` only when its
plan, implementation, tests, menuconfig entry, log update, and commit are
complete. Recommended next stages: World Info, persona manager, prompt presets,
character editor, themes/backgrounds, additional backends, then extensions and
media features. Server/multi-user mode comes last because it changes the
security model.
