#!/usr/bin/env bash
set -e

# On macOS pick the arch to match the Nuke build: Nuke <15 is Intel-only
# (Rosetta), 15+ is native arm64. REZ_NUKE_VERSION is set by the resolved
# nuke package (e.g. "16.1.5").
ARCHFLAG=""
if [ "$(uname)" = "Darwin" ]; then
    major="${REZ_NUKE_VERSION%%.*}"
    if [ "${major:-0}" -ge 15 ]; then
        ARCHFLAG="-DCMAKE_OSX_ARCHITECTURES=arm64"
    else
        ARCHFLAG="-DCMAKE_OSX_ARCHITECTURES=x86_64"
    fi
fi

cmake "$REZ_BUILD_SOURCE_PATH" $ARCHFLAG \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$REZ_BUILD_INSTALL_PATH"
cmake --build . --config Release
if [ "$1" = "install" ]; then
    cmake --install . --config Release
fi
