#!/bin/sh
# Fetch and build Raven (the native GUI library) into build/raven-build.
# Requires: git, CMake 3.16+, a C11 compiler and libX11-devel.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC="$ROOT/build/raven"
BUILD="$ROOT/build/raven-build"
CMAKE=${CMAKE:-cmake}

if [ ! -d "$SRC" ]; then
    echo "cloning Raven into $SRC"
    git clone --depth 1 https://github.com/oscar-soirey/Raven.git "$SRC"
fi

echo "configuring $BUILD"
"$CMAKE" -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
    -DRAVEN_BUILD_EXAMPLES=OFF -DRAVEN_BUILD_TOOLS=OFF -DRAVEN_BUILD_TESTS=OFF

echo "building"
"$CMAKE" --build "$BUILD" -j"$(nproc 2>/dev/null || echo 4)"
echo "done: $BUILD/libraven.a"
