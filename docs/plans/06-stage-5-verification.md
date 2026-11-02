# Stage 5: v0.1 Verification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove the v0.1 port meets its definition of done on Arch Linux (glibc) and Alpine Linux (musl), then finalize the port log and feature matrix.

**Architecture:** Verification only; no new product features. A reproducible Alpine Docker script builds and runs the same unit, smoke, and mock-streaming tests as the host, while a host verification script codifies the v0.1 gate and feature-count assertions. Documentation records the resulting evidence.

**Tech Stack:** C++17, GNU Make, gtkmm-3.0, libcurl, ncursesw, Xvfb, Docker, Python 3, bash.

**Spec:** `docs/superpowers/specs/2026-11-01-sillytavern-cpp-port-design.md`

## Global Constraints

- Linux only; native Xorg GUI; no Node.js, WebKit, or embedded original frontend.
- Must build on Arch Linux (glibc) and Alpine Linux (musl).
- `make` must build; `make menuconfig` must configure.
- Unported features stay visible but dim and unselectable; ported critical features are locked on.
- Never write into `SillyTavern/`; all runtime writes stay under XDG roots.
- `docs/PORT_LOG.md` is updated for every feature and stage.
- Plans live in `docs/plans/`; never delete them.
- One descriptive git commit per verified unit of work; `git status` clean at stage end.

## Review Focus

1. Alpine's musl libc and BusyBox userland can differ from glibc in locale, filesystem, and process behavior; the container must run the real build and tests, not just compile a subset.
2. The GUI smoke test needs an X server and fonts inside the container; a headless build alone does not prove the app runs on Alpine.
3. `make smoke` must exercise live SSE streaming, not silently skip it because Python is missing in the container.
4. The v0.1 counts can drift silently when a feature status changes; assert 105 total, 19 ported, and 86 planned from the registry.
5. Verification must not modify the upstream `SillyTavern/` checkout; assert its git worktree is still clean.

---

### Task 1: Reproducible Alpine/musl build and runtime verification

**Files:**
- Create: `SillyTavern_port/tests/alpine_build.sh`
- Modify: `SillyTavern_port/README.md` (verification commands)

**Interfaces:**
- Consumes: the existing `Makefile`, `tests/smoke_gtk.sh`, `tests/mock_openai.py`, and `--smoke-test` entry point.
- Produces: `tests/alpine_build.sh`, an executable script that exits 0 only when Alpine builds, unit tests, and the full smoke suite (including `smoke: ui streaming`) pass.

- [ ] **Step 1: Write `tests/alpine_build.sh`**

The script must:

- resolve the port root from `BASH_SOURCE`;
- require `docker`, with image override `ALPINE_IMAGE` defaulting to `alpine:3.20`;
- run the container with the source mounted read-only at `/src`;
- install `build-base gtkmm3-dev gtk+3.0-dev curl-dev ncurses-dev pkgconf xvfb xvfb-run xauth font-dejavu bash python3`;
- copy `/src` to `/work`, remove any host `build/` and `.config`, and run `make`, `make test`, and `make smoke`;
- rely on `make smoke` to start `tests/mock_openai.py` and require `smoke: ui streaming`;
- print `alpine: build ok`, `alpine: tests ok`, and `alpine: smoke ok` on success.

- [ ] **Step 2: Run the script to verify the container gate**

Run: `cd SillyTavern_port && ./tests/alpine_build.sh`

Expected: `alpine: build ok`, `all tests passed (113)`, all five `smoke:` markers including `smoke: ui streaming`, and `alpine: smoke ok`.

- [ ] **Step 3: Document the command in `README.md`**

Add `./tests/alpine_build.sh` under a "Verification" heading, stating that it requires Docker and runs the musl build, unit tests, and Xvfb smoke test.

- [ ] **Step 4: Commit**

```bash
git add SillyTavern_port/tests/alpine_build.sh SillyTavern_port/README.md
git commit -m "test: add Alpine musl build verification"
```

---

### Task 2: Codify the host v0.1 verification gate

**Files:**
- Create: `SillyTavern_port/tests/verify_v01.sh`

**Interfaces:**
- Consumes: `make`, `make test`, `make smoke`, `tests/integration_openai.sh`, `build/menuconfig --selftest`, `features/features.json`, `.config`, and the upstream `SillyTavern/.git`.
- Produces: `tests/verify_v01.sh`, an executable script that exits 0 only when the v0.1 definition of done holds on the host.

- [ ] **Step 1: Write `tests/verify_v01.sh`**

The script must run, in order:

```bash
make clean
make
make test
make smoke
./tests/integration_openai.sh
./build/menuconfig --selftest
```

Then assert, using Python 3 on `features/features.json`:

- 105 total features;
- exactly 19 with status `ported`;
- exactly 86 with status `planned`;
- every ported feature is critical.

Assert with grep:

- `src/core/` contains no `gtk` or `gtkmm` include;
- `.config` has `CONFIG_P08=y`, `CONFIG_D02=y`, `CONFIG_C02=y`, and `CONFIG_C05=y`.

Assert with git:

- if `../SillyTavern/.git` exists, `git -C ../SillyTavern status --porcelain` is empty.

Print `v0.1 verification: PASS` on success.

- [ ] **Step 2: Run the script to verify the host gate**

Run: `cd SillyTavern_port && ./tests/verify_v01.sh`

Expected: clean build with no warnings, `all tests passed (113)`, all five `smoke:` markers, menuconfig selftest PASS, and `v0.1 verification: PASS`.

- [ ] **Step 3: Commit**

```bash
git add SillyTavern_port/tests/verify_v01.sh
git commit -m "test: codify the v0.1 verification gate"
```

---

### Task 3: Final port log, README, and feature matrix

**Files:**
- Modify: `docs/PORT_LOG.md`
- Modify: `SillyTavern_port/README.md`
- Regenerate: `docs/FEATURES.md`

**Interfaces:**
- Consumes: evidence from Tasks 1 and 2, and the Stage 4 commits through `37eb782`.
- Produces: a v0.1 log with real evidence and a README that matches the implemented layout.

- [ ] **Step 1: Update `docs/PORT_LOG.md`**

Add a Stage 5 row with plan `yes`, complete `yes`, and evidence naming:

- `tests/verify_v01.sh` host gate;
- `tests/alpine_build.sh` on `alpine:3.20` (musl);
- 113 unit/integration tests;
- five smoke markers including live UI streaming;
- menuconfig selftest;
- 19 ported / 86 planned.

Add a short "v0.1 completion" note above the stages table stating that all 19 critical features are ported, the remaining 86 stay visible but disabled, and the original `SillyTavern/` worktree is clean.

- [ ] **Step 2: Fix the README layout and verification section**

Change `src/app/` to `src/ui/` in the project layout. List `./tests/verify_v01.sh` and `./tests/alpine_build.sh` under Verification, and state that the Alpine check requires Docker.

- [ ] **Step 3: Regenerate the feature matrix**

Run: `python3 SillyTavern_port/tools/gen_features_doc.py`

Expected: `docs/FEATURES.md` still reports 19 ported and 86 planned.

- [ ] **Step 4: Commit**

```bash
git add docs/PORT_LOG.md docs/FEATURES.md SillyTavern_port/README.md
git commit -m "docs: finalize v0.1 port log and feature matrix"
```

---

### Task 4: Final clean-tree verification

**Files:** none.

**Interfaces:**
- Consumes: all prior tasks.
- Produces: final evidence and a clean worktree.

- [ ] **Step 1: Run the host gate from a clean tree**

Run: `cd SillyTavern_port && ./tests/verify_v01.sh`

Expected: `v0.1 verification: PASS`.

- [ ] **Step 2: Run the Alpine gate**

Run: `cd SillyTavern_port && ./tests/alpine_build.sh`

Expected: `alpine: smoke ok`.

- [ ] **Step 3: Confirm the worktree and upstream are clean**

Run:

```bash
git status --short
git -C SillyTavern status --short
```

Expected: both empty.

- [ ] **Step 4: Confirm plans and log completeness**

Run:

```bash
ls docs/plans/00-v0.1-master-plan.md docs/plans/0{1,2,3,4,5}-stage-*.md
grep -c '^| [PDCO][0-9]' docs/PORT_LOG.md
```

Expected: all six plan files exist and 19 feature rows are present.

---

## Stage 5 completion checklist

- [ ] Alpine `make`, `make test`, and `make smoke` pass in `alpine:3.20`.
- [ ] Host `make`, `make test`, `make smoke`, integration, and menuconfig selftest pass with no warnings.
- [ ] `src/core/` remains GTK-free.
- [ ] 105 features: 19 ported, 86 planned; ported critical flags are on.
- [ ] Upstream `SillyTavern/` worktree is clean.
- [ ] `docs/PORT_LOG.md` and `docs/FEATURES.md` are final; `git status` is clean.
