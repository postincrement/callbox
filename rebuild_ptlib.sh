#!/bin/sh
# Rebuild PTLib.
#   ./rebuild_ptlib.sh
#   ./rebuild_ptlib.sh --regenerate
#
# --regenerate deletes the build directory and creates it again with CMake,
# then builds. The generator is CMake's default for this platform.
set -eu

here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
source=$(CDPATH= cd -- "$here/../opalvoip-ptlib" && pwd)
build=$source/build

case "${1:-}" in
  --regenerate|-Regenerate)
    regenerate=1
    ;;
  "")
    regenerate=0
    ;;
  *)
    echo "usage: $(basename "$0") [--regenerate]" >&2
    exit 1
    ;;
esac

if [ "$regenerate" -eq 1 ]; then
  rm -rf "$build"
  set -- -S "$source" -B "$build"
  if [ -n "${CMAKE_TOOLCHAIN_FILE:-}" ]; then
    set -- "$@" -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE"
  elif [ -n "${VCPKG_ROOT:-}" ]; then
    set -- "$@" -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
  fi
  cmake "$@"
fi

if [ ! -d "$build" ]; then
  echo "PTLib build directory not found: $build. Pass --regenerate to create it." >&2
  exit 1
fi

cd "$build"
exec make -j4
