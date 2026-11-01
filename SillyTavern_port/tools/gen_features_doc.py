#!/usr/bin/env python3
"""Generate docs/FEATURES.md from features/features.json."""

from __future__ import annotations

import json
import sys
from collections import defaultdict
from pathlib import Path

GROUP_TITLES = {
    "platform": "Platform and build",
    "characters": "Character management",
    "chat": "Chat, prompting, and generation",
    "ui": "User interface and media",
    "backends": "Model backends and interoperability",
    "operations": "Operations and security",
}

GROUP_ORDER = ["platform", "characters", "chat", "ui", "backends", "operations"]


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    registry_path = root / "SillyTavern_port" / "features" / "features.json"
    output_path = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "docs" / "FEATURES.md"

    data = json.loads(registry_path.read_text(encoding="utf-8"))
    features = data["features"]
    by_id = {feature["id"]: feature for feature in features}

    required_by: dict[str, list[str]] = defaultdict(list)
    for feature in features:
        for dependency in feature["depends_on"]:
            required_by[dependency].append(feature["id"])

    critical = [feature for feature in features if feature["critical"]]
    target = [feature for feature in features if feature["target"] == "0.1"]
    ported = [feature for feature in features if feature["status"] == "ported"]

    lines: list[str] = []
    add = lines.append

    add("# SillyTavern Port — Full Feature and Dependency Matrix")
    add("")
    add(f"Source analyzed: SillyTavern `{data['source_version']}`.")
    add("")
    add("This is the full-parity feature set. It is intentionally broader than v0.1.")
    add("Every feature is classified as **critical** or **non-critical**, and every")
    add("dependency edge is listed in both directions.")
    add("")
    add("## Criticality rule")
    add("")
    add(f"> {data['critical_definition']}")
    add("")
    add("## Summary")
    add("")
    add("| Measure | Count |")
    add("|---|---:|")
    add(f"| Features in full-parity set | {len(features)} |")
    add(f"| Critical features | {len(critical)} |")
    add(f"| v0.1 target features | {len(target)} |")
    add(f"| Ported features | {len(ported)} |")
    add("")
    add("Status values in the registry: `planned` means the feature is visible in")
    add("`make menuconfig` but disabled; `ported` means it is implemented, builds,")
    add("and has been committed.")
    add("")
    add("## v0.1 critical dependency graph")
    add("")
    add("```mermaid")
    add("graph TD")
    for feature in critical:
        label = feature["name"].replace('"', "'")
        add(f'    {feature["id"]}["{feature["id"]} {label}"]')
    for feature in critical:
        for dependency in feature["depends_on"]:
            if by_id[dependency]["critical"]:
                add(f"    {dependency} --> {feature['id']}")
    add("```")
    add("")
    add("## Feature tables")
    add("")
    add("`Depends on` lists prerequisites. `Required by` lists dependants. Both use")
    add("feature IDs; names are in the same table.")
    add("")

    for group in GROUP_ORDER:
        group_features = [feature for feature in features if feature["group"] == group]
        if not group_features:
            continue
        add(f"### {GROUP_TITLES.get(group, group.title())}")
        add("")
        add("| ID | Feature | Critical | Plan | Status | Depends on | Required by |")
        add("|---|---|:---:|---|---|---|---|")
        for feature in group_features:
            deps = ", ".join(feature["depends_on"]) or "—"
            dependants = ", ".join(sorted(required_by[feature["id"]])) or "—"
            critical_mark = "**yes**" if feature["critical"] else "no"
            plan = "v0.1" if feature["target"] == "0.1" else "later"
            add(
                f"| {feature['id']} | {feature['name']} | {critical_mark} | {plan} | "
                f"{feature['status']} | {deps} | {dependants} |"
            )
        add("")

    add("## Notes")
    add("")
    add("- Criticality is about the single-user local core loop, not about how")
    add("  commonly a feature is used. World Info is important to many users but is")
    add("  not required to import a card, chat, and persist the result.")
    add("- A feature is only marked `ported` after its implementation, tests, build")
    add("  verification, log update, and commit are complete.")
    add("- `make menuconfig` must render every non-ported feature dimmed and")
    add("  unselectable. Critical v0.1 features are locked on.")
    add("- Feature IDs are stable. New features get new IDs; retired IDs are not")
    add("  reused.")
    add("")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {output_path} ({len(features)} features)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
