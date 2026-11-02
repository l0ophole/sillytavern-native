# Post-Review Fix Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the release blockers and data-integrity gaps found in the v0.1 final review, then re-run the full host and Alpine gates.

**Architecture:** Keep `libstcore` GTK-free. Harden core parsing/transport first, then make the GTK chat/character lifecycle transactional and complete, then handle startup/logging and verification. Async file I/O is explicitly deferred with a documented v0.1 limitation.

**Tech Stack:** C++17, gtkmm-3.0, libcurl, ncursesw, GNU Make, Xvfb, Docker, Python 3.

**Spec:** `docs/superpowers/specs/2026-11-01-sillytavern-cpp-port-design.md`

## Global Constraints

- v0.1 is single-user, local, Xorg-only; no Node.js/WebKit.
- `src/core/` stays GTK-free.
- Partial streamed text is kept on cancel, error, character switch, and window close.
- Failed saves leave the previous file intact; failed actions do not mutate the visible chat.
- Chats are stored under a stable character id, not the display name.
- Never write into `SillyTavern/`.
- All 19 critical features stay ported; the 86 backlog features stay planned.
- Tests fail before the fix and pass after it; no compiler warnings.

## Review Focus

1. Switching characters or closing the window mid-stream must save the partial reply instead of discarding it.
2. Chat rename/delete and character delete must be reachable from the UI, validate names, confirm destructive actions, and update the session/selector.
3. A malformed `settings.json` must not abort the process; startup shows a clear error and exits cleanly.
4. Mid-stream provider `error` objects must surface as errors, not as successful truncated replies.
5. Failed saves must restore the prior chat state and keep the composer text.

---

### Task 1: Harden core transport, parsing, and session guards

**Files:**
- Modify: `src/core/sse.cpp`, `tests/test_sse.cpp`
- Modify: `src/core/openai_client.cpp`, `tests/test_openai_integration.cpp`, `tests/mock_openai.py`
- Modify: `src/core/http_client.h/.cpp`
- Modify: `src/core/atomic_file.h/.cpp`, `src/core/settings.cpp`, `src/core/chat_store.cpp`, `src/core/character_store.cpp`
- Modify: `src/core/chat_session.h/.cpp`, `tests/test_chat_session.cpp`
- Modify: `tests/test_settings.cpp`, `tests/test_chat_store.cpp`, `tests/test_character_store.cpp`
- Modify: `src/core/chat_store.h`

**Interfaces:**
- `std::string read_file_limited(const std::filesystem::path&, std::size_t max_bytes, const std::string& context);`
- `Chat ChatStore::create(const std::string& key, const std::string& user_name, const std::string& first_mes, const std::string& display_name = {});`
- `bool ChatSession::can_regenerate_at(std::size_t index) const;`
- `bool ChatSession::can_continue_at(std::size_t index) const;`
- `void ChatSession::set_filename(std::string filename);`

- [ ] **Step 1: Write failing tests**

Add tests asserting:

- `sse_finish_handles_multiple_lines`: `feed("data: a\ndata: b")` then `finish()` yields one event with `"a\nb"`.
- `settings_load_repairs_permissions`: write `settings.json` with mode 0644, load it, assert 0600.
- `settings_load_rejects_oversized_file`: a file larger than 1 MiB throws `"settings"` and `"too large"`.
- `chat_load_rejects_oversized_file`: a chat file larger than 64 MiB throws.
- `store_import_rejects_oversized_card`: a sparse card larger than 64 MiB throws before copying.
- `openai_reports_mid_stream_error`: `/stream-error` sends `hello` then `{"error":{"message":"boom"}}`; result text is `hello` and error is `boom`.
- `session_action_guards`: `can_regenerate_at`/`can_continue_at` are true only for the last assistant.
- `session_continuation_adds_separator`: `"Hello"` + `"world"` becomes `"Hello world"`; `"Hello "` + `"world"` does not double the space.
- `chat_create_uses_display_name`: `create("id-1", "User", "Hi", "Seraphina")` stores `character_name == "Seraphina"` and greeting name `Seraphina`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd SillyTavern_port && make test`
Expected: compile failures for the new interfaces or failing assertions.

- [ ] **Step 3: Implement**

- `read_file_limited`: `fs::file_size` check, then `read_file`; throw `context + ": file is too large"`.
- Use 1 MiB for settings, 64 MiB for chats, 64 MiB for cards.
- `Settings::load`: after reading, `fs::permissions(path, owner_read|owner_write, replace)` when group/other bits are set.
- `SseParser::finish`: split the remaining buffer on `\n`, process each line, then dispatch.
- `handle_event`: if the JSON object has an `error` object/string or `detail` string, set `state.error` and return false.
- `HttpClient`: remove the reset from `post()`; add `reset_cancel()` and call it before starting a request.
- `write_file_atomic`: treat `write()` returning 0 as `EIO`.
- `ChatStore::create`: add `display_name` defaulting to the key; use it for metadata and the greeting.
- `ChatSession`: add the guard methods, `set_filename`, and continuation spacing.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cd SillyTavern_port && make test`
Expected: all tests pass, no warnings.

- [ ] **Step 5: Commit**

```bash
git add SillyTavern_port/src/core SillyTavern_port/tests
git commit -m "fix(core): harden transport, parsing, and session guards"
```

---

### Task 2: Complete chat lifecycle and transactional message actions

**Files:**
- Modify: `src/ui/chat_view.h/.cpp`
- Modify: `src/ui/character_list.h/.cpp`
- Modify: `src/ui/main_window.h/.cpp`
- Modify: `src/ui/smoke.cpp`, `tests/smoke_gtk.sh`
- Modify: `tests/test_chat_session.cpp`

**Interfaces:**
- `bool ChatView::set_character(const st::CharacterEntry& entry);`
- `void ChatView::clear();`
- `CharacterList(..., std::function<void(const CharacterEntry&)> on_selected, std::function<void()> on_changed);`
- `bool CharacterList::empty() const;`
- `ChatSession::snapshot()` / `restore(Chat)` or equivalent rollback helper.

- [ ] **Step 1: Write failing tests**

- Core: `session_snapshot_restores_after_failed_action` (snapshot, edit/delete/append, restore, assert original).
- Smoke: add markers for `smoke: chat lifecycle` (rename, delete, reload), `smoke: character delete`, and `smoke: partial on switch` (start a slow stream, switch characters, verify the partial assistant message is saved).

- [ ] **Step 2: Run to verify failure**

Run: `cd SillyTavern_port && make test`
Expected: missing interface or failing smoke marker.

- [ ] **Step 3: Implement**

- Store chats under `entry.id`; keep `entry.card.name` only for display/metadata.
- `stop_streaming()` saves `final_result_.text` (or `streaming_text_`) into the current session before clearing; all stop paths use it.
- `on_send`: append/save first, clear the composer only on success; restore the snapshot on failure.
- Edit/delete: snapshot, mutate, save, restore on failure.
- Regenerate: keep the old reply on disk and in a backup; restore it when the new generation fails with no text.
- Chat rename/delete buttons with confirmation and filename validation; update `filename_`, session filename, selector, and transcript.
- Character delete button with confirmation; refresh and clear the chat view when the library becomes empty.
- Failed `load_chat` resets the session/filename and refreshes the selector.
- Context menu: disable Regenerate/Continue unless the clicked message is the last assistant.
- Add a `budget_label_` showing `~N / M tokens` after transcript/settings updates.
- MainWindow no longer overwrites a ChatView error status.

- [ ] **Step 4: Run the gates**

Run: `cd SillyTavern_port && make test && make smoke`
Expected: 113+ tests pass and all smoke markers, including the new lifecycle/partial markers.

- [ ] **Step 5: Commit**

```bash
git add SillyTavern_port/src/ui SillyTavern_port/tests
git commit -m "fix(ui): complete chat lifecycle and transactional actions"
```

---

### Task 3: Startup safety, logging, and verification hardening

**Files:**
- Modify: `src/ui/application.cpp`
- Modify: `src/ui/main_window.cpp`, `src/ui/settings_dialog.cpp`, `src/ui/chat_view.cpp`
- Modify: `src/core/log.cpp`, `tests/test_log.cpp`
- Modify: `tests/verify_v01.sh`, `tests/alpine_build.sh`
- Modify: `docs/PORT_LOG.md`, `SillyTavern_port/README.md`

**Interfaces:**
- Startup catches `std::exception`, logs, shows an error dialog, and returns exit code 1 instead of aborting.
- Log file is created with mode 0600; error paths call `log_write`.
- `log_set_redactions` refreshes after settings save; status errors pass through `redact_secrets`.

- [ ] **Step 1: Write failing tests**

- `log_file_is_private`: after `log_init`, the log file mode is 0600.
- `log_redacts_new_secret_after_update`: set redactions, change them, and assert the new secret is redacted.
- Extend `verify_v01.sh` to check every ported feature's CONFIG flag.
- Extend `alpine_build.sh` to run `./tests/integration_openai.sh`.

- [ ] **Step 2: Run to verify failure**

Run: `cd SillyTavern_port && make test` and inspect the scripts.
Expected: the new log tests fail or the scripts lack the new assertions.

- [ ] **Step 3: Implement**

- Wrap `Application::on_activate` setup in `try/catch`; show a `Gtk::MessageDialog` and log; return 1.
- `log_init` chmods the log file to 0600.
- Log save/load/generation/import/delete/startup errors; redact status text.
- Settings dialog edits a copy and assigns only after a successful save.
- Verification scripts check all ported flags and run Alpine integration.

- [ ] **Step 4: Run the full gates**

Run: `cd SillyTavern_port && ./tests/verify_v01.sh && ./tests/alpine_build.sh`
Expected: `v0.1 verification: PASS` and `alpine: smoke ok`.

- [ ] **Step 5: Commit**

```bash
git add SillyTavern_port/src SillyTavern_port/tests docs
git commit -m "fix: harden startup, logging, and verification"
```

---

### Task 4: Final re-review and documentation

- [ ] **Step 1: Re-run the final review** on the new HEAD.
- [ ] **Step 2: Fix any new Critical/Important findings** with a focused RED→GREEN test.
- [ ] **Step 3: Record the deferred async file I/O limitation** in `docs/PORT_LOG.md` (local single-user v0.1, capped file sizes, post-v0.1 worker).
- [ ] **Step 4: Regenerate `docs/FEATURES.md`**, confirm 19 ported / 86 planned, and ensure `git status` is clean.

## Deferred with ruling

- Async character/chat I/O on the GTK main thread is deferred post-v0.1: the v0.1 app is single-user and local, file sizes are capped, and moving all lifecycle operations to workers would add substantial threading risk without changing the critical feature set. Documented in `PORT_LOG.md`.
- Symlink canonicalization, O(n²) streaming re-render, and build-config overwrite behavior remain backlog polish.
