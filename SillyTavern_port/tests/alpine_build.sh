#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image="${ALPINE_IMAGE:-alpine:3.20}"

if ! command -v docker >/dev/null 2>&1; then
    echo "docker is required for the Alpine build check" >&2
    exit 1
fi

echo "alpine: using $image"

docker run --rm \
    -v "$root:/src:ro" \
    "$image" \
    sh -eu -c '
        apk add --no-cache build-base gtkmm3-dev gtk+3.0-dev curl-dev \
            ncurses-dev pkgconf xvfb xvfb-run xauth font-dejavu bash python3 >/dev/null
        cp -a /src /work
        cd /work
        rm -rf build .config
        make -j"$(nproc)"
        version="$(./build/sillytavern-port --version)"
        if [ "$version" != "SillyTavern Port 0.1.2" ]; then
            echo "alpine: expected version SillyTavern Port 0.1.2, found $version" >&2
            exit 1
        fi
        echo "alpine: build ok"
        make -j"$(nproc)" test
        echo "alpine: tests ok"
        ./tests/integration_openai.sh
        echo "alpine: integration ok"
        make smoke
        echo "alpine: smoke ok"
    '
