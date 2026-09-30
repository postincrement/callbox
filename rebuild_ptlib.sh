#!/bin/sh
# Rebuild PTLib.
#   ./rebuild_ptlib.sh
#   ./rebuild_ptlib.sh --regenerate
#   ./rebuild_ptlib.sh --minsize
#   ./rebuild_ptlib.sh --opalmin
#   ./rebuild_ptlib.sh --regenerate --opalmin
#
# --regenerate deletes the build directory and creates it again with CMake,
# then builds. The generator is CMake's default for this platform.
# --minsize matches configure --enable-minsize: the lesser-used modules are
# left out. --opalmin omits FTP, Telnet, ILS, LDAP, SOAP, XML-RPC, SNMP,
# RFC1155, vCard, XMPP, SASL, serial, modem, and internet mail. Either can be
# combined with --regenerate.
set -eu

here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
source=$(CDPATH= cd -- "$here/../opalvoip-ptlib" && pwd)
build=$source/build

regenerate=0
minsize=0
opalmin=0
while [ $# -gt 0 ]; do
  case "$1" in
    --regenerate|-Regenerate)
      regenerate=1
      ;;
    --minsize|-MinSize)
      minsize=1
      ;;
    --opalmin|-OpalMin)
      opalmin=1
      ;;
    *)
      echo "usage: $(basename "$0") [--regenerate] [--minsize] [--opalmin]" >&2
      exit 1
      ;;
  esac
  shift
done

if [ "$regenerate" -eq 1 ]; then
  rm -rf "$build"
  set -- -S "$source" -B "$build"
  if [ "$minsize" -eq 1 ]; then
    set -- "$@" -DPTLIB_MINSIZE=ON
  fi
  if [ "$opalmin" -eq 1 ]; then
    set -- "$@" -DPTLIB_OPALMIN=ON
  fi
  if [ -n "${CMAKE_TOOLCHAIN_FILE:-}" ]; then
    set -- "$@" -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE"
  elif [ -n "${VCPKG_ROOT:-}" ]; then
    set -- "$@" -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
  fi
  cmake "$@"
elif [ -d "$build" ] && [ "$minsize" -eq 1 -o "$opalmin" -eq 1 ]; then
  set -- -S "$source" -B "$build"
  if [ "$minsize" -eq 1 ]; then
    set -- "$@" -DPTLIB_MINSIZE=ON
  fi
  if [ "$opalmin" -eq 1 ]; then
    set -- "$@" -DPTLIB_OPALMIN=ON
  fi
  cmake "$@"
fi

if [ ! -d "$build" ]; then
  echo "PTLib build directory not found: $build. Pass --regenerate to create it." >&2
  exit 1
fi

cd "$build"
exec make -j4
