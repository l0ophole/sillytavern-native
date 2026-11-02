#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

make clean
make
make test
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

errors = []
if len(features) != 105:
    errors.append(f"expected 105 features, found {len(features)}")
if len(ported) != 19:
    errors.append(f"expected 19 ported features, found {len(ported)}")
if len(planned) != 86:
    errors.append(f"expected 86 planned features, found {len(planned)}")

non_critical = [feature["id"] for feature in ported if not feature.get("critical", False)]
if non_critical:
    errors.append("ported non-critical features: " + ", ".join(non_critical))

if errors:
    for error in errors:
        print("verify: " + error, file=sys.stderr)
    sys.exit(1)

print(f"verify: {len(features)} features, {len(ported)} ported, {len(planned)} planned")
PY

if grep -rIn 'gtk\|gtkmm' src/core; then
    echo "verify: GTK include found in src/core" >&2
    exit 1
fi

for flag in CONFIG_P08 CONFIG_D02 CONFIG_C02 CONFIG_C05; do
    if ! grep -q "^${flag}=y$" .config; then
        echo "verify: ${flag} is not enabled" >&2
        exit 1
    fi
done

if [ -d ../SillyTavern/.git ] && [ -n "$(git -C ../SillyTavern status --porcelain)" ]; then
    echo "verify: upstream SillyTavern worktree is dirty" >&2
    exit 1
fi

echo "v0.1 verification: PASS"
