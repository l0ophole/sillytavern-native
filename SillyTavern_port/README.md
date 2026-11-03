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

- Single-instance protection through `instance.lock`; a second process reports
  the holder PID instead of opening the same data.
- Per-chat advisory write locks so two processes cannot write one chat file.
- Crash-recovery journal for streaming drafts, replayed on chat load.
- Character-library scans and chat loads moved to background workers.
- Bounded HTTP response bodies, CR/LF/CRLF SSE framing, and a 16 MiB cap per
  SSE event.
- ASan/UBSan unit tests through `make test-sanitize`.

Startup scans and chat loads run on background workers. Explicit import/delete
and new-chat writes remain synchronous on the GTK thread and are size-capped:
1 MiB for `settings.json`, 64 MiB for character cards and chat logs, and 16 MiB
for compact drafts. Streaming draft writes are atomic and throttled to one per
second, with each write fsynced on the GTK thread.

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
make defconfig       # reset to all currently ported critical features
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
