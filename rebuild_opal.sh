#!/bin/sh
# Rebuild OPAL, then OpenPhone.
#   ./rebuild_opal.sh
#   ./rebuild_opal.sh --regenerate
#
# --regenerate deletes the build directory and creates it again with CMake
# against the sibling PTLib build, with samples enabled so OpenPhone exists,
# then builds. The generator is CMake's default for this platform.
set -eu

here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
source=$(CDPATH= cd -- "$here/../opalvoip-opal" && pwd)
build=$source/build
ptlib_build=$(CDPATH= cd -- "$here/../opalvoip-ptlib" && pwd)/build

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
  if [ ! -f "$ptlib_build/PTLibConfig.cmake" ]; then
    echo "PTLib has not been configured. Run rebuild_ptlib.sh --regenerate first." >&2
    exit 1
  fi
  rm -rf "$build"
  set -- -S "$source" -B "$build" -DOPAL_PTLIB_DIR="$ptlib_build" -DOPAL_BUILD_SAMPLES=ON
  using_vcpkg=0
  if [ -n "${CMAKE_TOOLCHAIN_FILE:-}" ]; then
    set -- "$@" -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE"
    case "$CMAKE_TOOLCHAIN_FILE" in
      *vcpkg*) using_vcpkg=1 ;;
    esac
  elif [ -n "${VCPKG_ROOT:-}" ]; then
    set -- "$@" -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    using_vcpkg=1
  fi
  # WXDIR is a wxWidgets source tree. vcpkg's toolchain finds its own install,
  # and a ROOT_DIR set here hides that.
  if [ -n "${WXDIR:-}" ] && [ "$using_vcpkg" -eq 0 ]; then
    set -- "$@" -DwxWidgets_ROOT_DIR="$WXDIR"
  fi
  cmake "$@"
fi

if [ ! -d "$build" ]; then
  echo "OPAL build directory not found: $build. Pass --regenerate to create it." >&2
  exit 1
fi

cd "$build"
make
if [ ! -e samples/CMakeFiles/openphone.dir/build.make ] && [ ! -e samples/openphone.vcxproj ]; then
  echo "OpenPhone was not generated. Install wxWidgets so wx-config and wxrc are on PATH, or set VCPKG_ROOT and run this script with --regenerate." >&2
  exit 1
fi
exec make openphone
