#!/usr/bin/env bash
# Build SDL3 3.4.10 from upstream source for MinGW-w64 and install it into
# third_party/sdl/install/<win32|win64>. The MinGW frontend makefiles call this
# automatically for Win64 because that frontend uses SDL3 for input only.
set -euo pipefail

ARCH="${1:-win64}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
URL="${SDL3_URL:-https://github.com/libsdl-org/SDL/releases/download/release-3.4.10/SDL3-3.4.10.tar.gz}"
TARBALL="$ROOT/third_party/sdl/SDL3-3.4.10.tar.gz"
SRC="$ROOT/third_party/sdl/SDL3-3.4.10"
BUILD="$ROOT/third_party/sdl/build/$ARCH"
PREFIX="$ROOT/third_party/sdl/install/$ARCH"

case "$ARCH" in
  win64) CC=${CC:-x86_64-w64-mingw32-gcc}; CXX=${CXX:-x86_64-w64-mingw32-g++}; RC=${RC:-x86_64-w64-mingw32-windres} ;;
  win32) CC=${CC:-i686-w64-mingw32-gcc}; CXX=${CXX:-i686-w64-mingw32-g++}; RC=${RC:-i686-w64-mingw32-windres} ;;
  *) echo "usage: $0 win32|win64" >&2; exit 2 ;;
esac

mkdir -p "$ROOT/third_party/sdl"
if [ ! -f "$TARBALL" ]; then
  echo "Downloading SDL3 source: $URL"
  if command -v curl >/dev/null 2>&1; then curl -fL "$URL" -o "$TARBALL"
  elif command -v wget >/dev/null 2>&1; then wget "$URL" -O "$TARBALL"
  else echo "Need curl or wget to fetch SDL3. Place SDL3-3.4.10.tar.gz in third_party/sdl/." >&2; exit 1
  fi
fi

if [ ! -d "$SRC" ]; then
  rm -rf "$SRC.tmp"
  mkdir -p "$SRC.tmp"
  tar -xf "$TARBALL" -C "$SRC.tmp" --strip-components=1
  mv "$SRC.tmp" "$SRC"
fi

command -v cmake >/dev/null 2>&1 || { echo "cmake is required to build SDL3" >&2; exit 1; }
command -v "$CC" >/dev/null 2>&1 || { echo "missing compiler: $CC" >&2; exit 1; }

TOOLCHAIN="$BUILD/mingw-toolchain.cmake"
mkdir -p "$BUILD"
cat > "$TOOLCHAIN" <<TC
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER $CC)
set(CMAKE_CXX_COMPILER $CXX)
set(CMAKE_RC_COMPILER $RC)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
TC

cmake -S "$SRC" -B "$BUILD" \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DSDL_SHARED=OFF \
  -DSDL_STATIC=ON \
  -DSDL_TEST_LIBRARY=OFF \
  -DSDL_TESTS=OFF \
  -DSDL_EXAMPLES=OFF
cmake --build "$BUILD" --parallel "${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"
cmake --install "$BUILD"

echo "SDL3 installed to $PREFIX"
