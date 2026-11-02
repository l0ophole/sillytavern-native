#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

if ! command -v xvfb-run >/dev/null 2>&1; then
    echo "xvfb-run is required for the GTK smoke test" >&2
    exit 1
fi

output="$(xvfb-run -a ./build/sillytavern-port --smoke-test 2>&1)"
printf '%s\n' "$output"

if ! grep -q 'smoke: window realized' <<<"$output"; then
    echo "smoke test failed: window was not realized" >&2
    exit 1
fi

if ! grep -q 'smoke: character loaded' <<<"$output"; then
    echo "smoke test failed: character or avatar was not loaded" >&2
    exit 1
fi

if ! grep -q 'smoke: chat round-trip' <<<"$output"; then
    echo "smoke test failed: chat round-trip failed" >&2
    exit 1
fi
