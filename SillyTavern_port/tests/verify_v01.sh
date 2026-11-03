#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

make clean
make -j"$(nproc)"
make -j"$(nproc)" test
make smoke
./tests/integration_openai.sh
./build/menuconfig --selftest

python3 - <<'PY'
import json
import sys

with open("features/features.json", encoding="utf-8") as handle:
    registry = json.load(handle)

features = registry["features"]
ported = [feature for feature in features if feature["status"] == "ported"]
planned = [feature for feature in features if feature["status"] == "planned"]
v011 = [feature for feature in features if feature["target"] == "0.1.1"]

errors = []
if len(features) != 105:
    errors.append(f"expected 105 features, found {len(features)}")
if len(ported) != 20:
    errors.append(f"expected 20 ported features, found {len(ported)}")
if len(planned) != 85:
    errors.append(f"expected 85 planned features, found {len(planned)}")
if len(v011) != 2:
    errors.append(f"expected 2 v0.1.1 target features, found {len(v011)}")

with open(".config", encoding="utf-8") as handle:
    config_lines = {line.strip() for line in handle if line.strip() and not line.startswith("#")}
with open("build/features.h", encoding="utf-8") as handle:
    autoconf_text = handle.read()

for feature in ported:
    fid = feature["id"]
    if f"CONFIG_{fid}=y" not in config_lines:
        errors.append(f"missing CONFIG_{fid}=y in .config")
    if f"#define ST_FEATURE_{fid} 1" not in autoconf_text:
        errors.append(f"missing #define ST_FEATURE_{fid} 1 in build/features.h")

for feature in planned:
    fid = feature["id"]
    if f"CONFIG_{fid}=y" in config_lines:
        errors.append(f"planned feature CONFIG_{fid}=y enabled in .config")
    if f"#define ST_FEATURE_{fid} 0" not in autoconf_text:
        errors.append(f"missing #define ST_FEATURE_{fid} 0 in build/features.h")

if errors:
    for error in errors:
        print("verify: " + error, file=sys.stderr)
    sys.exit(1)

print(f"verify: {len(features)} features, {len(ported)} ported, {len(planned)} planned, "
      f"{len(v011)} v0.1.1")
PY

if grep -rIn 'gtk\|gtkmm' src/core; then
    echo "verify: GTK include found in src/core" >&2
    exit 1
fi

if [ -d ../SillyTavern/.git ] && [ -n "$(git -C ../SillyTavern status --porcelain)" ]; then
    echo "verify: upstream SillyTavern worktree is dirty" >&2
    exit 1
fi

echo "v0.1.1 verification: PASS"
