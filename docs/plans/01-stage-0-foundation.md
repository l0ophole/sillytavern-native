# Stage 0 — Foundation, Build, and menuconfig Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bootstrap the port repository so `make` builds a skeleton binary and `make menuconfig` reads the 105-feature registry, dims unported features, and writes `.config` plus `build/features.h`.

**Architecture:** A standalone ncurses tool parses `features/features.json` with vendored nlohmann/json. GNU Make builds the tool without GTK, generates a default config, and builds a minimal CLI skeleton. No feature code from later stages is present yet.

**Tech Stack:** C++17, GNU Make, ncursesw, vendored nlohmann/json, pkg-config.

**Spec:** `docs/superpowers/specs/2026-11-01-sillytavern-cpp-port-design.md`
**Master plan:** `docs/plans/00-v0.1-master-plan.md`

## Global Constraints

- Only P01 is ported in this stage. All other 104 features are `planned`.
- Planned features must render dim and be unselectable.
- Ported critical features are locked on.
- `make menuconfig` must work before the GTK dependencies are installed.
- `make` must not require the original `SillyTavern/` tree.
- `.config`, `build/`, and runtime data are never committed.
- Commit P01 immediately after verification.

## Review Focus

1. A missing or malformed `features.json` must fail with a clear message, not a crash.
2. A dependency on a missing feature ID must fail registry validation.
3. A planned feature must never appear enabled in `.config`, even if listed as critical.
4. A ported critical feature must be enabled by `--defaults` and locked in the TUI.
5. `make menuconfig` must not require gtkmm or libcurl.

---

### Task 1: Vendor JSON and create the skeleton tree

**Files:**
- Create: `SillyTavern_port/third_party/nlohmann/` (copied headers)
- Create: `SillyTavern_port/third_party/nlohmann/LICENSE.MIT`
- Create: `SillyTavern_port/src/main.cpp`
- Create: `SillyTavern_port/README.md`

**Interfaces:**
- Produces: `src/main.cpp` with `int main(int, char**)` that prints `SillyTavern Port 0.1.0 (skeleton)` and returns 0. `--version` prints the same version and returns 0.

- [ ] **Step 1: Copy the vendored JSON headers**

Run:

```bash
mkdir -p SillyTavern_port/third_party
cp -R /usr/include/nlohmann SillyTavern_port/third_party/nlohmann
cp /usr/share/licenses/nlohmann-json/LICENSE.MIT SillyTavern_port/third_party/nlohmann/LICENSE.MIT
```

Expected: `SillyTavern_port/third_party/nlohmann/json.hpp` exists and includes resolve relative to the copied tree.

- [ ] **Step 2: Write the skeleton entry point**

Create `SillyTavern_port/src/main.cpp`:

```cpp
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    const std::string version = "SillyTavern Port 0.1.0 (skeleton)";
    if (argc > 1 && std::string(argv[1]) == "--version") {
        std::cout << version << '\n';
        return 0;
    }
    std::cout << version << '\n';
    return 0;
}
```

- [ ] **Step 3: Write the README**

Create `SillyTavern_port/README.md` with:

- project purpose and v0.1 scope;
- Arch packages: `base-devel gtkmm3 curl ncurses pkgconf`;
- Alpine packages: `build-base gtkmm3-dev gtk+3.0-dev curl-dev ncurses-dev pkgconf`;
- commands `make`, `make menuconfig`, `make test`, `make smoke`;
- data locations from the spec;
- a note that unported features are intentionally disabled.

- [ ] **Step 4: Verify the skeleton compiles manually**

Run:

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic SillyTavern_port/src/main.cpp -o /rpf/tmp/st-skeleton
/rpf/tmp/st-skeleton --version
```

Expected: prints `SillyTavern Port 0.1.0 (skeleton)`.

- [ ] **Step 5: Commit the skeleton**

```bash
git add SillyTavern_port/third_party SillyTavern_port/src/main.cpp SillyTavern_port/README.md
git commit -m "chore: vendor JSON headers and add skeleton entry point"
```

---

### Task 2: Implement the menuconfig tool

**Files:**
- Create: `SillyTavern_port/tools/menuconfig.cpp`
- Test: `SillyTavern_port/tools/menuconfig.cpp` (`--selftest` mode)

**Interfaces:**
- Consumes: `features/features.json` with fields `id`, `group`, `name`, `description`, `critical`, `target`, `status`, `depends_on`.
- Produces CLI:
  - `menuconfig --defaults [--config PATH] [--header PATH]`
  - `menuconfig --selftest`
  - `menuconfig` (interactive ncurses)
- Produces `.config` lines `CONFIG_<ID>=y` or `# CONFIG_<ID> is not set`.
- Produces `build/features.h` lines `#define ST_FEATURE_<ID> 1` or `0`.
- Exit codes: 0 success, 2 registry/CLI error.

**Behavior:**
- `--defaults` enables only features whose `status == "ported"` and `critical == true`, plus dependency closure. All other features are off.
- A planned feature is never enabled by defaults.
- Interactive TUI groups features by `group`, shows `[x]`, `[ ]`, or `(not ported)`; planned rows use `A_DIM` and ignore Space/Enter.
- Ported critical rows are locked `[x]`.
- Saving writes config and header.
- Saving also writes/touches `build/.configured` so Make does not regenerate the config on the next build.
- `--selftest` validates:
  - JSON parses and every ID is unique;
  - every dependency ID exists;
  - no dependency cycles;
  - using an internal fixture where P01 is ported and C04 is planned, defaults enable P01 only, leave C04 off, and write `ST_FEATURE_P01 1` / `ST_FEATURE_C04 0`;
  - using the real registry, every enabled feature is ported and critical, and every planned feature is off.

- [ ] **Step 1: Write the failing selftest expectations**

Create `tools/menuconfig.cpp` with the data model, CLI parsing, and a `run_selftest()` that returns failures for the expected behavior. Start with a stub that returns 1 so the test fails.

- [ ] **Step 2: Build and run the failing selftest**

Run:

```bash
cd SillyTavern_port
g++ -std=c++17 -Wall -Wextra -Wpedantic -Ithird_party \
  tools/menuconfig.cpp -o /rpf/tmp/menuconfig -lncursesw
/rpf/tmp/menuconfig --selftest
```

Expected: FAIL with a message that the selftest is not implemented.

- [ ] **Step 3: Implement registry loading and validation**

Use `nlohmann::json` to read `features/features.json`. Resolve the registry path as:

1. `--features PATH` if provided;
2. `$ST_FEATURES` if set;
3. `features/features.json` relative to the current directory.

On parse or validation failure, print `menuconfig: <error>` to stderr and exit 2.

- [ ] **Step 4: Implement defaults, config, and header writing**

`--defaults` builds an enabled set from ported critical features and recursively enables dependencies. Write files atomically: write `PATH.tmp`, then `rename`. Create parent directories. Header begins with `#pragma once`.

- [ ] **Step 5: Implement the ncurses TUI**

Use `initscr`, `cbreak`, `noecho`, `keypad`, `start_color`, `use_default_colors`. Provide a scrollable list, group headers, a help pane showing name/description/dependencies/status, and keys: Up/Down, Space/Enter, `s` save, `q` quit, `?` help. Handle terminal resize with `KEY_RESIZE`. Restore the terminal on every exit path.

- [ ] **Step 6: Implement `--selftest`**

Run the checks listed in Interfaces. Print `menuconfig selftest: PASS` on success. Return 0 only if every check passes.

- [ ] **Step 7: Run the selftest and verify it passes**

Run:

```bash
cd SillyTavern_port
g++ -std=c++17 -Wall -Wextra -Wpedantic -Ithird_party \
  tools/menuconfig.cpp -o /rpf/tmp/menuconfig -lncursesw
/rpf/tmp/menuconfig --selftest
```

Expected: `menuconfig selftest: PASS`.

- [ ] **Step 8: Verify defaults output**

Run:

```bash
cd SillyTavern_port
/rpf/tmp/menuconfig --defaults --config /rpf/tmp/st.config --header /rpf/tmp/st-features.h
grep 'CONFIG_P01 is not set' /rpf/tmp/st.config
grep 'CONFIG_C04 is not set' /rpf/tmp/st.config
grep 'ST_FEATURE_P01 0' /rpf/tmp/st-features.h
grep 'ST_FEATURE_C04 0' /rpf/tmp/st-features.h
```

Expected: all four greps match while P01 is still planned. The selftest's
internal fixture already proves a ported P01 is enabled.

- [ ] **Step 9: Commit the menuconfig tool**

```bash
git add SillyTavern_port/tools/menuconfig.cpp
git commit -m "build: add registry-driven ncurses menuconfig"
```

---

### Task 3: Add the Makefile and default configuration flow

**Files:**
- Create: `SillyTavern_port/Makefile`
- Create: `SillyTavern_port/tests/test_main.cpp`
- Create: `SillyTavern_port/tests/test_util.h`
- Modify: `SillyTavern_port/README.md`

**Interfaces:**
- Produces targets: `all`, `menuconfig`, `defconfig`, `test`, `smoke`, `clean`, `help`.
- `build/menuconfig` is built without GTK or libcurl.
- `.config` and `build/features.h` are generated by `build/menuconfig --defaults` when missing.
- `make` builds `build/sillytavern-port`.
- `make test` builds and runs `build/run_tests`.
- `make smoke` is a placeholder that prints `smoke: not available until the GTK UI stage` and exits 0.
- `make clean` removes `build/` and the binary.

- [ ] **Step 1: Write a failing test harness**

Create `tests/test_util.h` with a minimal `TEST(name)` registration macro and `CHECK`/`CHECK_EQ` macros. Create `tests/test_main.cpp` with one test `skeleton_is_alive` that checks `1 + 1 == 2`, and a `main` that runs all registered tests and returns 1 on failure.

- [ ] **Step 2: Write the Makefile**

Include:

```make
CXX ?= g++
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic -Ithird_party -Isrc -MMD -MP
BUILD := build
```

Rules:

```make
$(BUILD)/menuconfig: tools/menuconfig.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $< -o $@ $$(pkg-config --libs ncursesw 2>/dev/null || echo -lncurses)

$(BUILD)/.configured: $(BUILD)/menuconfig features/features.json
	$(BUILD)/menuconfig --defaults --config .config --header $(BUILD)/features.h
	@touch $@

.config $(BUILD)/features.h: $(BUILD)/.configured

$(BUILD)/sillytavern-port: src/main.cpp $(BUILD)/features.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $< -o $@

$(BUILD)/run_tests: tests/test_main.cpp $(BUILD)/features.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $< -o $@

menuconfig: $(BUILD)/menuconfig
	$(BUILD)/menuconfig

defconfig: $(BUILD)/menuconfig
	$(BUILD)/menuconfig --defaults --config .config --header $(BUILD)/features.h
	@touch $(BUILD)/.configured

all: $(BUILD)/sillytavern-port
test: $(BUILD)/run_tests
	$(BUILD)/run_tests
```

`make menuconfig` changes `.config` but not the stamp; after using it, run
`touch build/.configured` or `make defconfig`. Add that behavior to the
interactive tool by having it also write `build/.configured` after saving.

- [ ] **Step 3: Build and run tests**

Run:

```bash
cd SillyTavern_port
make clean
make
make test
./build/sillytavern-port --version
```

Expected:

- `make` creates `build/features.h` and `build/sillytavern-port`;
- `make test` prints a pass line and exits 0;
- `--version` prints `SillyTavern Port 0.1.0 (skeleton)`.

- [ ] **Step 4: Verify `make menuconfig` builds the tool**

Run:

```bash
cd SillyTavern_port
make build/menuconfig
./build/menuconfig --selftest
```

Expected: `menuconfig selftest: PASS`. Do not run the interactive TUI in automated verification; a human smoke run is `make menuconfig` and `q`.

- [ ] **Step 5: Commit the build system**

```bash
git add SillyTavern_port/Makefile SillyTavern_port/tests SillyTavern_port/README.md
git commit -m "build: add make targets and default feature configuration"
```

---

### Task 4: Add the progress log and finish P01

**Files:**
- Create: `docs/PORT_LOG.md`
- Modify: `SillyTavern_port/features/features.json` (P01 status `ported`)
- Regenerate: `docs/FEATURES.md`

**Interfaces:**
- Produces a log table with columns: Feature, Plan, Implemented, Tested, Commit, Notes.
- P01 row records: plan `01-stage-0-foundation.md`, implemented `yes`, tested `yes`, commit hash.

- [ ] **Step 1: Update P01 status**

Change P01 in `features/features.json` from `"status": "planned"` to `"status": "ported"`.

- [ ] **Step 2: Regenerate the feature document**

Run:

```bash
python3 SillyTavern_port/tools/gen_features_doc.py
```

Expected: `docs/FEATURES.md` summary shows `Ported features | 1`.

- [ ] **Step 3: Write the progress log**

Create `docs/PORT_LOG.md`:

```markdown
# Port Progress Log

Source: SillyTavern 1.19.0 (06bde939f)
Design: docs/superpowers/specs/2026-11-01-sillytavern-cpp-port-design.md
Master plan: docs/plans/00-v0.1-master-plan.md

## Status key

- Plan: written / missing
- Implemented: yes / no / partial
- Tested: yes / no / partial

## Features

| Feature | Plan | Implemented | Tested | Commit | Notes |
|---|---|---|---|---|---|
| P01 Build system and menuconfig | 01-stage-0-foundation.md | yes | yes | pending | Skeleton CLI; GTK comes in stage 4 |

## Stages

| Stage | Plan written | Complete | Evidence |
|---|---|---|---|
| 0 Foundation | yes | yes | make, make test, menuconfig selftest |
| 1 Core platform | no | no | — |
| 2 Data compatibility | no | no | — |
| 3 Prompting/generation | no | no | — |
| 4 GTK UI | no | no | — |
| 5 Verification | no | no | — |
```

- [ ] **Step 4: Run the full stage verification**

Run:

```bash
cd SillyTavern_port
make clean
make
make test
./build/menuconfig --selftest
./build/menuconfig --defaults --config /rpf/tmp/st.config --header /rpf/tmp/st.h
grep -q 'CONFIG_P01=y' /rpf/tmp/st.config
grep -q 'CONFIG_C04 is not set' /rpf/tmp/st.config
```

Expected: every command exits 0.

- [ ] **Step 5: Commit P01**

```bash
git add docs/PORT_LOG.md docs/FEATURES.md SillyTavern_port/features/features.json
git commit -m "feat(build): complete P01 make and menuconfig foundation"
```

Record the commit hash in the log in the final stage; the log's `pending` value
is replaced then.

---

## Stage 0 completion checklist

- [ ] `make` builds from a clean tree.
- [ ] `make test` passes.
- [ ] `make build/menuconfig` succeeds without GTK/libcurl.
- [ ] `./build/menuconfig --selftest` passes.
- [ ] `--defaults` enables P01 only and leaves C04 disabled.
- [ ] `docs/PORT_LOG.md` exists and P01 is recorded.
- [ ] `features.json` marks P01 `ported`; `docs/FEATURES.md` regenerated.
- [ ] `git status` is clean.
