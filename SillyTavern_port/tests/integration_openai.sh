#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

port_file="$(mktemp)"
rm -f "$port_file"

python3 tests/mock_openai.py "$port_file" &
server_pid=$!

cleanup() {
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
    rm -f "$port_file"
}
trap cleanup EXIT

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
./build/run_tests
