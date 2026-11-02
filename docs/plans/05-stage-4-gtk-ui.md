# Stage 4 — Native GTK UI Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the v0.1 native GTK3 interface: a window with a character library and avatars, a chat transcript and composer that stream replies, settings, and message actions.

**Architecture:** GTK code lives in `src/ui/` and is linked only into the application binary. All presentation-independent logic lives in `libstcore` and is unit-tested without a display. `ChatSession` combines a card, settings, chat, and stores into prompt/request/action operations. `ChatView` runs generation on a worker thread and marshals chunks through `Glib::Dispatcher`. `make smoke` runs the real window under Xvfb and exits 0.

**Tech Stack:** C++17, gtkmm-3.0 (GTK3), Glib/Gdk/Gtk, Xvfb, GNU Make, existing `libstcore`.

**Spec:** `docs/superpowers/specs/2026-11-01-sillytavern-cpp-port-design.md` (sections 4.2, 5.4, 11, 12)
**Master plan:** `docs/plans/00-v0.1-master-plan.md`
**Previous stage:** `docs/plans/04-stage-3-prompting-generation.md`

## Global Constraints

- `src/core/` must never include GTK headers; `make test` must run without a display.
- UI code lives in `src/ui/`; only `src/main.cpp` and `src/ui/*.cpp` may include gtkmm.
- Worker threads never touch GTK widgets; all delivery goes through `Glib::Dispatcher`.
- One generation at a time per window; Send is disabled while streaming and Cancel is enabled.
- Partial streamed text is kept on error or cancellation.
- Settings are validated and saved with mode 0600.
- D02 becomes `ported` only after avatar rendering works; its log row changes from `partial` to `yes`.
- One feature commit per completed feature, immediately after tests pass.
- `make`, `make test`, and `make smoke` must stay warning-free under `-Wall -Wextra -Wpedantic`.
- All user-facing strings are plain ASCII in v0.1.

## Review Focus

1. Starting the app without a display fails cleanly; `--smoke-test` under Xvfb exits 0.
2. A PNG card imports, appears in the list with its avatar, and survives restart.
3. Sending a message appends it once, streams the reply into the transcript, saves both, and survives restart.
4. Cancel keeps partial text and leaves the app usable; errors appear in the status bar and log with the API key redacted.
5. Edit/delete/regenerate/continue change the stored JSONL correctly and never lose unknown fields.
6. The UI never blocks on network or large file work, and no GTK call happens on a worker thread.
7. `src/core/` remains GTK-free and all core tests pass headlessly.

---

### Task 1: P08 GTK application shell, settings, and smoke harness

**Files:**
- Create: `src/ui/application.h`, `src/ui/application.cpp`
- Create: `src/ui/main_window.h`, `src/ui/main_window.cpp`
- Create: `src/ui/settings_dialog.h`, `src/ui/settings_dialog.cpp`
- Create: `src/ui/smoke.h`, `src/ui/smoke.cpp`
- Modify: `src/main.cpp`
- Modify: `Makefile`
- Create: `tests/smoke_gtk.sh`

**Interfaces:**
- `class st::ui::Application : public Gtk::Application` with `static Glib::RefPtr<Application> create();` and `int run(int argc, char** argv);`
- `st::ui::MainWindow` constructor takes `Paths`, `Settings`, `CharacterStore`, `ChatStore`; it owns a `Gtk::Paned` with a placeholder left pane and a chat placeholder right pane, a header bar with a Settings button, and a status bar.
- `st::ui::SettingsDialog` edits `api_base_url`, `api_key`, `model`, `temperature`, `top_p`, `max_tokens`, `context_size`, `user_name`, `system_prompt`, `stream`, and `request_timeout_seconds`; it validates through `Settings::validate()` and saves through `Settings::save(paths.settings_file)`.
- `int st::ui::run_smoke_test();` creates a temporary data root, constructs the window, shows it, processes events for 300 ms, and returns 0 only if the window was realized.
- `--smoke-test` selects `run_smoke_test()`; `--version` keeps the existing behavior.

**Makefile:**
- Detect gtkmm with `pkg-config --cflags/--libs gtkmm-3.0`.
- Build `src/ui/*.cpp` into `build/ui/*.o` and link them with `src/main.cpp` and `libstcore.a`.
- `make smoke` runs `xvfb-run -a ./build/sillytavern-port --smoke-test`.
- If gtkmm is missing, fail with a clear message naming the package.

- [ ] **Step 1: Write the failing smoke test**

Create `tests/smoke_gtk.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
if ! command -v xvfb-run >/dev/null 2>&1; then
    echo "xvfb-run is required for the GTK smoke test" >&2
    exit 1
fi
xvfb-run -a ./build/sillytavern-port --smoke-test
```

Add the `smoke` target and run:

```bash
chmod +x tests/smoke_gtk.sh
make smoke
```

Expected before implementation: the current skeleton prints its version and exits 0, but does not create a window. Make the smoke test assert a marker: the app must print `smoke: window realized` and exit 0; the skeleton does not print it, so the script fails.

- [ ] **Step 2: Add GTK build support and the shell**

Add gtkmm detection to the Makefile and implement `Application`, `MainWindow`, and `SettingsDialog`. `MainWindow` should use `Gtk::Paned`, set a default size of 1100x720, show a header bar with `Settings`, and a status bar reading `Ready`.

`Application::on_activate` creates the window, loads `Settings::load(paths.settings_file)`, resolves paths with `Paths::resolve()`, ensures directories, initializes logging to `paths.logs_dir / "app.log"`, and presents the window. Settings changes update the live settings object and the status bar.

- [ ] **Step 3: Implement the smoke harness**

`run_smoke_test()` must:

1. create a unique temporary root under `std::filesystem::temp_directory_path()`;
2. build `Paths{root/"config", root/"data", ...}` and call `ensure()`;
3. create an empty `CharacterStore` and `ChatStore`;
4. construct and show `MainWindow`;
5. iterate the default main context for 300 ms;
6. check `window.get_realized()`, print `smoke: window realized`, destroy the window, and return 0.

Use RAII cleanup for the temporary root. Do not require a real XDG directory.

- [ ] **Step 4: Run and verify pass**

```bash
make
make smoke
make test
```

Expected: the smoke test prints `smoke: window realized` and exits 0; all existing tests still pass; no warnings.

- [ ] **Step 5: Commit P08**

```bash
git add src/main.cpp src/ui Makefile tests/smoke_gtk.sh \
        SillyTavern_port/features/features.json docs/FEATURES.md docs/PORT_LOG.md
git commit -m "feat(ui): add GTK application shell"
```

Set P08 to `ported`, regenerate `docs/FEATURES.md`, and add the log row.

---

### Task 2: D02 character library UI, search, import, and avatars

**Files:**
- Create: `src/core/character_filter.h`, `src/core/character_filter.cpp`
- Test: `tests/test_character_filter.cpp`
- Create: `src/ui/character_list.h`, `src/ui/character_list.cpp`
- Create: `src/ui/avatar.h`, `src/ui/avatar.cpp`
- Modify: `src/ui/main_window.*`, `src/ui/smoke.cpp`
- Modify: `SillyTavern_port/features/features.json` (D02 status)

**Interfaces:**
- `std::vector<CharacterEntry> st::filter_characters(const std::vector<CharacterEntry>& entries, const std::string& query);`
  - case-insensitive substring match against name, description, creator, and tags;
  - empty or whitespace-only query returns all entries;
  - result keeps the input order.
- `Glib::RefPtr<Gdk::Pixbuf> st::ui::load_avatar(const std::filesystem::path& path, int size);`
  - returns an empty pointer for a missing/non-PNG path;
  - scales to fit `size` while preserving aspect ratio.
- `CharacterList` provides a `Gtk::SearchEntry`, an `Import` button, a `Gtk::ListBox` of characters with a 48 px avatar and name, and `sigc::signal<void(CharacterEntry)> signal_selected`.
  - Import opens `Gtk::FileChooserDialog` filtered to `.png` and `.json`, calls `CharacterStore::import_file`, refreshes, and reports failures in the status bar.
  - Selection loads the character into the chat pane.

- [ ] **Step 1: Write failing filter tests**

```cpp
TEST(filter_characters_matches_name_and_description) {
    // build entries with cards Seraphina / Old Man / Dragon
    // empty query returns all; "sera" matches name case-insensitively;
    // "legacy" matches description; "fantasy" matches a tag.
}

TEST(filter_characters_preserves_order_and_rejects_no_match) {
    // "zzz" returns empty; input order preserved for a shared match.
}
```

Run `make test`; expected: compile failure for `core/character_filter.h`.

- [ ] **Step 2: Implement the filter and run tests**

Use ASCII case folding on bytes; UTF-8 text still matches byte substrings. Run `make test`; expected pass.

- [ ] **Step 3: Implement the character list and avatar**

Replace the left placeholder with `CharacterList`. Load avatars with `Gdk::Pixbuf::create_from_file` in a `try/catch`; scale with `scale_simple`. Use `Gtk::Image` from the pixbuf. Show a generic silhouette when no avatar exists. Wire selection to `MainWindow`.

- [ ] **Step 4: Extend the smoke test**

`run_smoke_test()` imports a synthetic PNG card built in `src/ui/smoke.cpp` (a minimal PNG with a `chara` tEXt chunk), selects it, checks that `load_avatar(entry.path, 48)` is non-null, prints `smoke: character loaded`, and exits 0.

- [ ] **Step 5: Run and verify pass**

```bash
make test
make smoke
```

Expected: filter tests pass; smoke prints both markers; no warnings.

- [ ] **Step 6: Commit D02**

```bash
git add src/core/character_filter.* tests/test_character_filter.cpp \
        src/ui/character_list.* src/ui/avatar.* src/ui/main_window.* src/ui/smoke.cpp \
        SillyTavern_port/features/features.json docs/FEATURES.md docs/PORT_LOG.md
git commit -m "feat(ui): add character library, search, import, and avatars"
```

Set D02 to `ported`; change its log row from `partial` to `yes` and record the commit.

---

### Task 3: C02 chat session core, transcript, composer, and streaming

**Files:**
- Modify: `src/core/prompt.h`, `src/core/prompt.cpp` (add `PromptParts`)
- Create: `src/core/chat_session.h`, `src/core/chat_session.cpp`
- Test: `tests/test_chat_session.cpp`
- Create: `src/ui/chat_view.h`, `src/ui/chat_view.cpp`
- Modify: `src/ui/main_window.*`, `src/ui/smoke.cpp`

**Interfaces:**
- `struct st::PromptParts { std::vector<PromptMessage> leading_system; std::vector<PromptMessage> history; std::vector<PromptMessage> trailing_system; };`
- `PromptParts st::build_prompt_parts(const CharacterCard&, const Settings&, const std::vector<ChatMessage>&);`
  - leading system = the assembled card/global system text;
  - history = mapped chat messages in order;
  - trailing system = post-history instructions.
- `st::build_prompt` returns leading + history + trailing, preserving current behavior and tests.
- `class st::ChatSession`:
  - constructor: `ChatSession(CharacterCard card, Settings settings, ChatStore store, std::string character, std::string filename, Chat chat);`
  - `const Chat& chat() const`, `const CharacterCard& character() const`, `const Settings& settings() const`, `Settings& settings()`;
  - `std::vector<PromptMessage> prompt_messages() const` — builds parts, fits history with `settings.context_size`/`max_tokens`, and reassembles leading + trimmed history + trailing;
  - `int estimated_tokens() const`, `bool trimmed() const` — values from the last `prompt_messages()` call;
  - `OpenAiRequest request() const` — base URL, key, model, messages, sampling, timeout;
  - `void append_user(const std::string& text);`
  - `void append_assistant(const std::string& text);`
  - `void save() const;` — `store.save(character, filename, chat)`.
- `ChatView`:
  - transcript: `Gtk::TextView` read-only, monospace, message headers (`User:`/`Character:`/`System:`), macros expanded for display;
  - composer: `Gtk::TextView` with Enter-to-send and Shift+Enter newline;
  - `Send` disabled while streaming, `Cancel` enabled;
  - `Gtk::ComboBoxText` chat selector, `New` button, `ChatStore::create` for the first message;
  - worker thread + `Glib::Dispatcher`; chunks are queued under a mutex and appended on the main thread;
  - on completion, append the assistant message, save, refresh the selector, and show `Ready` or the error in the status bar;
  - on error/cancel, keep partial text and show the error/`Cancelled`.

- [ ] **Step 1: Write failing prompt-parts and session tests**

```cpp
TEST(prompt_parts_splits_system_and_history) {
    // card with system prompt, description, post_history_instructions;
    // two history messages; expect one leading system, two history, one trailing system.
}

TEST(session_prompt_respects_budget_and_keeps_post_history) {
    // small context_size, long history; expect trimmed == true,
    // estimated_tokens <= context_size - max_tokens,
    // leading system content and trailing post-history content retained,
    // surviving history order preserved.
}

TEST(session_request_uses_settings_and_sampling) {
    // base_url/model/messages/temperature/max_tokens/stream/timeout match.
}

TEST(session_append_edit_delete_regenerate_continue) {
    // append user/assistant set timestamps; edit updates mes and preserves extra;
    // delete removes the right message; regenerate removes the last assistant;
    // continue_prefix returns the last assistant text.
}
```

Run `make test`; expected: compile failure for `core/chat_session.h`.

- [ ] **Step 2: Implement prompt parts and ChatSession**

Refactor `build_prompt` to call `build_prompt_parts` and concatenate. Implement `ChatSession` with `ChatStore` by value. `append_user` sets `is_user = true`, `name = settings.user_name`, `send_date` UTC ISO-8601. `append_assistant` sets `name = character.name`. `save()` delegates to the store.

Run `make test`; expected: all new tests pass and existing prompt tests still pass.

- [ ] **Step 3: Implement ChatView and wire it into MainWindow**

MainWindow selects the first character, lists its chats, loads or creates the latest chat, and constructs a `ChatSession`. The chat pane shows the transcript, composer, chat selector, New, Send, and Cancel. Use `Glib::signal_timeout` only for the smoke test, not for streaming.

Generation worker:

```cpp
auto client = std::make_shared<st::OpenAiClient>();
worker_ = std::thread([this, client, request] {
    const auto result = client->generate(request, [this](const std::string& chunk) {
        {
            std::lock_guard<std::mutex> lock(chunk_mutex_);
            pending_chunks_ += chunk;
        }
        dispatcher_.emit();
    });
    // append assistant text, save, and dispatch the final state
});
```

`ChatView::on_dispatcher` drains `pending_chunks_` into the transcript. The final dispatch carries a result struct with `text`, `error`, and `cancelled`; join the worker before starting another.

- [ ] **Step 4: Extend the smoke test**

Add a second smoke phase that constructs a `ChatSession` from the imported card, appends a user message, checks `prompt_messages()` is non-empty, saves, reloads through `ChatStore`, and prints `smoke: chat round-trip`.

Optionally, when `ST_MOCK_BASE_URL` is set, drive one real stream through `OpenAiClient` and check the accumulated text before exiting.

- [ ] **Step 5: Run and verify pass**

```bash
make test
make smoke
./tests/integration_openai.sh
```

Expected: all tests pass; smoke prints the window, character, and chat markers; no warnings.

- [ ] **Step 6: Commit C02**

```bash
git add src/core/prompt.* src/core/chat_session.* tests/test_chat_session.cpp \
        src/ui/chat_view.* src/ui/main_window.* src/ui/smoke.cpp \
        SillyTavern_port/features/features.json docs/FEATURES.md docs/PORT_LOG.md
git commit -m "feat(ui): add chat transcript and composer"
```

Set C02 to `ported`, regenerate the feature doc, and add the log row.

---

### Task 4: C05 message actions

**Files:**
- Modify: `src/core/chat_session.h`, `src/core/chat_session.cpp`
- Modify: `tests/test_chat_session.cpp`
- Modify: `src/ui/chat_view.h`, `src/ui/chat_view.cpp`
- Modify: `src/ui/main_window.*` (status bar messages)

**Interfaces:**
- `bool st::ChatSession::edit_message(std::size_t index, const std::string& text);`
  - updates `mes`, leaves `send_date`, role flags, and `extra` untouched; false for an invalid index.
- `bool st::ChatSession::delete_message(std::size_t index);`
- `bool st::ChatSession::regenerate();`
  - removes the last assistant message when it is the final message; returns true when removed.
- `bool st::ChatSession::can_continue() const;`
  - true when the last non-empty message is an assistant message.
- `std::string st::ChatSession::continue_prefix() const;`
  - returns the last assistant `mes` when `can_continue()`, otherwise empty.
- `void st::ChatSession::append_to_last_assistant(const std::string& text);`
  - appends text to the last assistant message (used after a continuation stream).

**UI:**
- Right-click a transcript message opens a `Gtk::Menu` with `Edit`, `Delete`, `Regenerate`, `Continue`.
- Edit opens a dialog prefilled with the message; Save calls `edit_message`, saves, and refreshes.
- Delete asks for confirmation, calls `delete_message`, saves, and refreshes.
- Regenerate calls `regenerate()`, saves, then sends the same prompt again (disabled while streaming).
- Continue builds the prompt with an extra system message `Continue the last assistant message without repeating it.`, streams, and appends the result to the last assistant message.
- The status bar reports each action; errors leave the previous file intact.

- [ ] **Step 1: Write failing action tests**

```cpp
TEST(session_edit_preserves_extra_and_timestamp) {
    // message with extra and send_date; edit; expect new mes, same extra/send_date.
}

TEST(session_delete_and_regenerate_boundaries) {
    // delete middle/last; invalid index returns false and leaves chat unchanged;
    // regenerate removes only a trailing assistant message.
}

TEST(session_continue_requires_assistant_tail) {
    // user tail -> can_continue false, prefix empty;
    // assistant tail -> prefix equals its text; append_to_last_assistant extends it.
}
```

Run `make test`; expected failure for the new methods.

- [ ] **Step 2: Implement the actions and run tests**

Implement bounds checks, preserve `extra`, save only through `ChatSession::save()`, and never mutate the chat on a failed action. Run `make test`; expected pass.

- [ ] **Step 3: Implement the context menu and dialogs**

Build the popup menu on the message row under the pointer. Use `Gtk::Dialog` for edit and `Gtk::MessageDialog` for delete confirmation. Refresh the transcript from the session after every action. While streaming, disable Regenerate/Continue/Delete/Edit.

- [ ] **Step 4: Extend the smoke test**

Create a chat with user/assistant messages, edit the user message, delete the assistant message, regenerate a fresh assistant reply, and print `smoke: message actions`. Assert the stored JSONL matches the in-memory chat after reload.

- [ ] **Step 5: Run and verify pass**

```bash
make test
make smoke
```

Expected: action tests pass; smoke prints all markers; no warnings.

- [ ] **Step 6: Commit C05**

```bash
git add src/core/chat_session.* tests/test_chat_session.cpp \
        src/ui/chat_view.* src/ui/main_window.* src/ui/smoke.cpp \
        SillyTavern_port/features/features.json docs/FEATURES.md docs/PORT_LOG.md
git commit -m "feat(ui): add message actions"
```

Set C05 to `ported`, regenerate the feature doc, and add the log row.

---

### Task 5: Stage 4 verification and wrap-up

- [ ] **Step 1: Run the full gate**

```bash
cd SillyTavern_port
make clean
make
make test
make smoke
./tests/integration_openai.sh
./build/menuconfig --selftest
```

Expected: build, unit tests, smoke, integration, and selftest all pass with no warnings.

- [ ] **Step 2: Verify GTK containment and feature status**

```bash
grep -rIn 'gtk\|gtkmm' src/core && exit 1 || true
grep -n 'CONFIG_P08\|CONFIG_D02\|CONFIG_C02\|CONFIG_C05' .config
```

Expected: no GTK includes in `src/core/`; all four are `=y`.

- [ ] **Step 3: Record hashes and commit**

Update `docs/PORT_LOG.md` with real hashes, change D02 to `yes`/`ported`, mark Stage 4 complete with the smoke and test evidence, regenerate `docs/FEATURES.md`, and commit:

```bash
git add docs/PORT_LOG.md docs/FEATURES.md
git commit -m "docs: record stage 4 completion"
```

---

## Stage 4 completion checklist

- [ ] P08, D02, C02, C05 are `ported`; D02's log row is no longer `partial`.
- [ ] The window opens under Xvfb; `make smoke` exits 0.
- [ ] A PNG card imports and its avatar renders.
- [ ] A chat streams into the transcript, saves, and reloads with order/roles intact.
- [ ] Cancel keeps partial text; errors show in the status bar and are logged redacted.
- [ ] Edit, delete, regenerate, and continue update the JSONL correctly.
- [ ] `make test` and `make smoke` pass with no warnings; `src/core/` has no GTK.
- [ ] `docs/FEATURES.md` regenerated; `docs/PORT_LOG.md` updated; `git status` clean.
