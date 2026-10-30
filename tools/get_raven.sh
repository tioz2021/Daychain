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

# Raven hardcodes the radius of the dialog surface; keep the interface square.
if [ -f "$SRC/src/widget/window.c" ]; then
    sed -i \
        -e 's/rvn_paint_surface(painter, root, frame, 26\.0f, RVN_SURFACE_POPUP/rvn_paint_surface(painter, root, frame, 0.0f, RVN_SURFACE_POPUP/' \
        -e 's/rvn_paint_box(painter, frame, 10\.0f, RVN_THEME_COLOR(R_COLOR_POPUP)/rvn_paint_box(painter, frame, 0.0f, RVN_THEME_COLOR(R_COLOR_POPUP)/' \
        "$SRC/src/widget/window.c"
fi

echo "configuring $BUILD"
"$CMAKE" -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
    -DRAVEN_BUILD_EXAMPLES=OFF -DRAVEN_BUILD_TOOLS=OFF -DRAVEN_BUILD_TESTS=OFF

echo "building"
"$CMAKE" --build "$BUILD" -j"$(nproc 2>/dev/null || echo 4)"
echo "done: $BUILD/libraven.a"
