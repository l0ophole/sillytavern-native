#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

if ! command -v xvfb-run >/dev/null 2>&1; then
    echo "xvfb-run is required for the GTK smoke test" >&2
    exit 1
fi

server_pid=""
port_file=""
cleanup() {
    if [ -n "$server_pid" ]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    if [ -n "$port_file" ]; then
        rm -f "$port_file"
    fi
}
trap cleanup EXIT

if command -v python3 >/dev/null 2>&1; then
    port_file="$(mktemp)"
    rm -f "$port_file"
    python3 tests/mock_openai.py "$port_file" &
    server_pid=$!
    for _ in $(seq 1 100); do
        if [ -s "$port_file" ]; then
            break
        fi
        sleep 0.05
    done
    if [ ! -s "$port_file" ]; then
        echo "mock OpenAI server did not start" >&2
        exit 1
    fi
    export ST_MOCK_BASE_URL="http://127.0.0.1:$(cat "$port_file")"
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

if ! grep -q 'smoke: async library scan' <<<"$output"; then
    echo "smoke test failed: async library scan failed" >&2
    exit 1
fi

if ! grep -q 'smoke: scan failure' <<<"$output"; then
    echo "smoke test failed: scan failure not reported" >&2
    exit 1
fi

if ! grep -q 'smoke: chat round-trip' <<<"$output"; then
    echo "smoke test failed: chat round-trip failed" >&2
    exit 1
fi

if ! grep -q 'smoke: message actions' <<<"$output"; then
    echo "smoke test failed: message actions failed" >&2
    exit 1
fi

if ! grep -q 'smoke: chat lifecycle' <<<"$output"; then
    echo "smoke test failed: chat lifecycle failed" >&2
    exit 1
fi

if ! grep -q 'smoke: async chat load' <<<"$output"; then
    echo "smoke test failed: async chat load failed" >&2
    exit 1
fi

if ! grep -q 'smoke: async stale load' <<<"$output"; then
    echo "smoke test failed: async stale load failed" >&2
    exit 1
fi

if ! grep -q 'smoke: malformed draft' <<<"$output"; then
    echo "smoke test failed: malformed draft failed" >&2
    exit 1
fi

if ! grep -q 'smoke: character delete' <<<"$output"; then
    echo "smoke test failed: character delete failed" >&2
    exit 1
fi

if ! grep -q 'smoke: crash recovery' <<<"$output"; then
    echo "smoke test failed: crash recovery failed" >&2
    exit 1
fi

if ! grep -q 'smoke: branch' <<<"$output"; then
    echo "smoke test failed: branch failed" >&2
    exit 1
fi

if ! grep -q 'smoke: authors note' <<<"$output"; then
    echo "smoke test failed: authors note failed" >&2
    exit 1
fi

if ! grep -q 'smoke: markdown render' <<<"$output"; then
    echo "smoke test failed: markdown render failed" >&2
    exit 1
fi

if ! grep -q 'smoke: world info' <<<"$output"; then
    echo "smoke test failed: world info failed" >&2
    exit 1
fi

if ! grep -q 'smoke: backup round trip' <<<"$output"; then
    echo "smoke test failed: backup round trip failed" >&2
    exit 1
fi

if [ -n "${ST_MOCK_BASE_URL:-}" ] && ! grep -q 'smoke: ui streaming' <<<"$output"; then
    echo "smoke test failed: UI streaming failed" >&2
    exit 1
fi

if [ -n "${ST_MOCK_BASE_URL:-}" ] && ! grep -q 'smoke: swipes' <<<"$output"; then
    echo "smoke test failed: swipes failed" >&2
    exit 1
fi

if [ -n "${ST_MOCK_BASE_URL:-}" ] && ! grep -q 'smoke: partial on switch' <<<"$output"; then
    echo "smoke test failed: partial on switch failed" >&2
    exit 1
fi

if [ -n "${ST_MOCK_BASE_URL:-}" ] && ! grep -q 'smoke: autosave journal' <<<"$output"; then
    echo "smoke test failed: autosave journal failed" >&2
    exit 1
fi

if [ -n "${ST_MOCK_BASE_URL:-}" ] && ! grep -q 'smoke: cancel during regenerate' <<<"$output"; then
    echo "smoke test failed: cancel during regenerate failed" >&2
    exit 1
fi

if [ -n "${ST_MOCK_BASE_URL:-}" ] && ! grep -q 'smoke: backup guard while streaming' <<<"$output"; then
    echo "smoke test failed: backup guard while streaming failed" >&2
    exit 1
fi
