# SillyTavern C/C++ Port

A native C++17 desktop port of SillyTavern for Linux. The first milestone is a
single-user core-chat MVP; the full feature set is tracked in
`../docs/FEATURES.md` and configured with `make menuconfig`.

## v0.1 scope

- Native GTK3/gtkmm application under Xorg.
- SillyTavern-compatible character card import and deletion (PNG V2/V3 and JSON).
- OpenAI-compatible streaming chat completions with partial-reply retention.
- SillyTavern-compatible JSONL chat storage with create, switch, rename, and delete.
- Message editing, deletion, regeneration, and continuation with context-budget display.
- Feature configuration where unported features are visible but disabled.

## v0.1.1 scope

- Single-instance protection through `instance.lock`; the flock path makes a
  second process report the holder PID instead of opening the same data. When a
  session bus is present, a second launch may instead be forwarded to the
  running instance by `Gtk::Application` before the lock is reached.
- Per-chat advisory write locks so two processes cannot write one chat file.
- Crash-recovery journal for streaming drafts, replayed on chat load.
- Character-library scans and chat loads moved to background workers.
- Bounded HTTP response bodies, CR/LF/CRLF SSE framing, and a 16 MiB cap per
  SSE event.
- ASan/UBSan unit tests through `make test-sanitize`.

## v0.1.2 scope

- Draft lifecycle: deleting or renaming a chat clears or moves its pending
  crash-recovery draft.
- Draft write cap: streaming draft writes are bounded and failed saves are
  retried with backoff.
- Recoverable regeneration: a draft captured during regeneration is
  recoverable instead of being discarded on recovery.
- Scan-failure status: character-library scan failures are reported instead
  of silently showing an empty library.
- SSE scan cost: the SSE parser no longer rescans buffered bytes on every
  feed.
- Warning assertion: the host verification gate fails if the build log
  contains any `warning:` line, keeping the build warning-free.

## v0.2.0 scope

- Swipes: keep multiple candidate assistant replies per message in
  SillyTavern-compatible `swipes`/`swipe_id` fields; regenerate adds a
  swipe instead of discarding the previous reply, and recovered
  `replace_last` drafts become two swipes.
- Branching and bookmarks: fork a chat from any message into
  `<stem> - branch <n>.jsonl` without mutating the source, and mark
  bookmarked positions in chat metadata for quick jumps.
- Author's note: inject an author's note at a configurable depth
  (0..100, default 4) into the prompt when enabled and non-empty,
  configured in the settings dialog.
- Markdown: assistant replies render as a safe Markdown subset
  (bold, italic, strike, inline/fenced code, headings, blockquotes,
  lists, links) with all HTML escaped; user and system messages
  render as escaped plain text.
- Backups: manual timestamped chat backups under `chats/.backups/`
  with restore from the application menu; one backup tree is capped
  at 512 MiB and the 10 newest backups are kept.

Chat files stay SillyTavern-compatible JSONL: one metadata object,
then one object per message, with unknown fields preserved. Files
written by this version load in SillyTavern for the fields this
milestone writes (`swipes`/`swipe_id`, bookmarks, and the author's
note settings), and SillyTavern-authored chats load here.

Startup scans and chat loads run on background workers. Explicit import/delete
and new-chat writes remain synchronous on the GTK thread and are size-capped:
1 MiB for `settings.json`, 64 MiB for character cards and chat logs, and 16 MiB
for compact drafts. Streaming draft writes are atomic and throttled to one per
second, with each write fsynced on the GTK thread.

## v0.3.0 scope

- World Info: keyword-triggered lore injection from SillyTavern-compatible
  lorebooks (`worldinfo/*.json`), with a lorebook editor and import/export.
  Triggered entries are deduplicated, sorted by position, and bounded by
  recursion (max 5), entry count (max 32), per-entry content (4096 bytes),
  and total injected content (32768 bytes). Lorebook files are capped at
  16 MiB.
- Personas: multiple user personas stored in `personas.json`; the active
  persona's description is injected once into the prompt and `{{user}}`
  expands to the persona name, with empty/missing personas leaving the
  prompt unchanged.
- Instruct/templates: instruct modes (`plain`, `chatml`, `llama`) format
  history messages, and a macro-expanded context template can replace the
  default leading system text while preserving prior prompt order at
  defaults.
- Prompt manager/presets: ordered, enable/disable prompt sections plus
  per-connection presets stored as plain JSON under `presets/*.json`
  (16 MiB cap), with import/export and an active-preset selector.
- Advanced sampling/regex/variables: top-k, repetition/presence/frequency
  penalties, DRY and mirostat sampling fields, bounded regex rewriting of
  history display copies (pattern max 512 bytes, replacement max 4096
  bytes, at most 16 rules), and `{{var:name}}` variables expanded after
  the core macros.

Chat and lorebook files stay SillyTavern-compatible JSONL/JSON with
unknown fields preserved: SillyTavern-authored chats and lorebooks load
here, and files produced by this version load in SillyTavern for the
fields this milestone writes.

Known v0.1.1 limitations:

- File I/O and JSONL parsing run on a worker, but transcript rendering
  (`refresh_transcript`) and the prompt-budget recomputation still run on the
  GTK thread after a load completes; a very large chat can stall the main loop
  during that render.
- Character scans run on a worker, but avatar decoding and `Gtk::ListBoxRow`
  construction for the results still run on the GTK thread.
- A draft captured during regeneration is not recoverable: the regenerated reply
  is popped in memory before generation, so the draft's base no longer matches
  the on-disk chat and is discarded on recovery. The previously saved reply is
  preserved; no history is lost or duplicated.
- Deleting or renaming a chat does not clear or move its pending draft.

The original JavaScript frontend is not embedded. No Node.js or WebKit is used.

## Build dependencies

### Arch Linux

```sh
sudo pacman -S --needed base-devel gtkmm3 curl ncurses pkgconf
```

### Alpine Linux

```sh
sudo apk add build-base gtkmm3-dev gtk+3.0-dev curl-dev ncurses-dev pkgconf
```

`nlohmann/json` is vendored under `third_party/`, so no distro JSON package is
required.

## Build and run

```sh
make                 # build with the current .config
make menuconfig      # configure the feature set
make defconfig       # reset to all ported features
make test            # run unit tests
make test-sanitize   # run unit tests with ASan/UBSan
make smoke           # run the GUI smoke test under Xvfb
./build/sillytavern-port
```

`make` creates a default `.config` if none exists. Features that have not been
ported appear dim and cannot be selected.

## Verification

```sh
./tests/verify_v01.sh   # sanitizer, host build, tests, smoke, integration, and counts
./tests/alpine_build.sh # Alpine musl build, tests, and smoke (requires Docker)
```

`verify_v01.sh` runs `make test-sanitize`, then a clean normal build, unit and
integration tests, the Xvfb smoke test, the menuconfig selftest, the feature
counts and config flags, `--version`, the GTK-free core check, and the clean
upstream tree check.

The Alpine script installs its own test dependencies in the container,
including `xvfb-run`, fonts, bash, and Python for the mock streaming server.

## Data locations

```text
~/.config/sillytavern-port/settings.json
~/.local/share/sillytavern-port/instance.lock
~/.local/share/sillytavern-port/characters/
~/.local/share/sillytavern-port/chats/
~/.local/share/sillytavern-port/chats/<character>/.locks/<file>.lock
~/.local/share/sillytavern-port/chats/.journal/<character>/<file>.draft
~/.local/share/sillytavern-port/logs/
```

`<character>` and `<file>` are sanitized path components. The journal holds
crash-recovery drafts for in-progress streaming replies; a completed or
discarded reply clears its draft.

Importing a card copies it into the port's own library. The original
SillyTavern installation is never modified.

## Project layout

```text
src/core/       GTK-free data, settings, prompt, HTTP, and generation library
src/ui/         GTK3/gtkmm user interface
tools/          menuconfig and documentation generators
features/       canonical feature registry
tests/          unit and integration tests
third_party/    vendored dependencies
```

## License

The port follows the upstream SillyTavern license terms. Vendored
`nlohmann/json` is MIT-licensed; see `third_party/nlohmann/LICENSE.MIT`.
