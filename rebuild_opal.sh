#!/bin/sh
# Rebuild OPAL, then OpenPhone.
#   ./rebuild_opal.sh
#   ./rebuild_opal.sh --regenerate
#   ./rebuild_opal.sh --no-obsolete-codecs
#   ./rebuild_opal.sh --no-t38
#   ./rebuild_opal.sh --opalmin
#
# --regenerate deletes the build directory and creates it again with CMake
# against the sibling PTLib build, with samples enabled so OpenPhone exists,
# then builds. The generator is CMake's default for this platform.
# --no-obsolete-codecs omits Speex, iLBC, LPC-10, H.261, G.722, G.722.1,
# G.722.2, G.726, G.728, GSM 06.10, Theora, G.723.1, and VoiceAge G.729.
# --no-t38 omits T.38 and fax.
# --opalmin omits those obsolete codecs, plus T.38, fax, RFC 4175, T.120,
# Skinny, and line interface devices. Any of these can be combined with
# --regenerate.
set -eu

here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
source=$(CDPATH= cd -- "$here/../opalvoip-opal" && pwd)
build=$source/build
ptlib_build=$(CDPATH= cd -- "$here/../opalvoip-ptlib" && pwd)/build

regenerate=0
no_obsolete=0
no_t38=0
opalmin=0
while [ $# -gt 0 ]; do
  case "$1" in
    --regenerate|-Regenerate)
      regenerate=1
      ;;
    --no-obsolete-codecs|-NoObsoleteCodecs)
      no_obsolete=1
      ;;
    --no-t38|-NoT38)
      no_t38=1
      ;;
    --opalmin|-OpalMin)
      opalmin=1
      ;;
    *)
      echo "usage: $(basename "$0") [--regenerate] [--no-obsolete-codecs] [--no-t38] [--opalmin]" >&2
      exit 1
      ;;
  esac
  shift
done

if [ "$regenerate" -eq 1 ]; then
  if [ ! -f "$ptlib_build/PTLibConfig.cmake" ]; then
    echo "PTLib has not been configured. Run rebuild_ptlib.sh --regenerate first." >&2
    exit 1
  fi
  rm -rf "$build"
  set -- -S "$source" -B "$build" -DOPAL_PTLIB_DIR="$ptlib_build" -DOPAL_BUILD_SAMPLES=ON
  if [ "$no_obsolete" -eq 1 ]; then
    set -- "$@" -DOPAL_OBSOLETE_CODECS=OFF
  fi
  if [ "$no_t38" -eq 1 ]; then
    set -- "$@" -DOPAL_T38_CAPABILITY=OFF
  fi
  if [ "$opalmin" -eq 1 ]; then
    set -- "$@" -DOPAL_OPALMIN=ON
  fi
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
elif [ -d "$build" ] && [ "$no_obsolete" -eq 1 -o "$no_t38" -eq 1 -o "$opalmin" -eq 1 ]; then
  set -- -S "$source" -B "$build"
  if [ "$no_obsolete" -eq 1 ]; then
    set -- "$@" -DOPAL_OBSOLETE_CODECS=OFF
  fi
  if [ "$no_t38" -eq 1 ]; then
    set -- "$@" -DOPAL_T38_CAPABILITY=OFF
  fi
  if [ "$opalmin" -eq 1 ]; then
    set -- "$@" -DOPAL_OPALMIN=ON
  fi
  cmake "$@"
fi

if [ ! -d "$build" ]; then
  echo "OPAL build directory not found: $build. Pass --regenerate to create it." >&2
  exit 1
fi

cd "$build"
make -j4
if [ ! -e samples/CMakeFiles/openphone.dir/build.make ] && [ ! -e samples/openphone.vcxproj ]; then
  echo "OpenPhone was not generated. Install wxWidgets so wx-config and wxrc are on PATH, or set VCPKG_ROOT and run this script with --regenerate." >&2
  exit 1
fi
exec make -j4 openphone
