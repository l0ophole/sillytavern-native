# SillyTavern C/C++ Port

A native C++17 desktop port of SillyTavern for Linux. The first milestone is a
single-user core-chat MVP; the full feature set is tracked in
`../docs/FEATURES.md` and configured with `make menuconfig`.

## v0.1 scope

- Native GTK3/gtkmm application under Xorg.
- SillyTavern-compatible character card import (PNG V2/V3 and JSON).
- OpenAI-compatible streaming chat completions.
- SillyTavern-compatible JSONL chat storage.
- Message editing, deletion, regeneration, and continuation.
- Feature configuration where unported features are visible but disabled.

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
make smoke           # run the GUI smoke test under Xvfb
./build/sillytavern-port
```

`make` creates a default `.config` if none exists. Features that have not been
ported appear dim and cannot be selected.

## Verification

```sh
./tests/verify_v01.sh   # host build, tests, smoke, integration, and feature counts
./tests/alpine_build.sh # Alpine musl build, tests, and smoke (requires Docker)
```

The Alpine script installs its own test dependencies in the container,
including `xvfb-run`, fonts, bash, and Python for the mock streaming server.

## Data locations

```text
~/.config/sillytavern-port/settings.json
~/.local/share/sillytavern-port/characters/
~/.local/share/sillytavern-port/chats/
~/.local/share/sillytavern-port/logs/
```

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
