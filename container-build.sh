#!/bin/sh
# Build a callbox image with podman or docker.
# The build context is assembled from this repo and the sibling OPAL and PTLib trees.
set -eu

here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
root=$(CDPATH= cd -- "$here/.." && pwd)
tag=${CALLBOX_IMAGE:-callbox}

if command -v podman >/dev/null 2>&1; then
  runtime=podman
elif command -v docker >/dev/null 2>&1; then
  runtime=docker
else
  echo "podman or docker is required" >&2
  exit 1
fi

for tree in opalvoip-ptlib opalvoip-opal callbox; do
  if [ ! -d "$root/$tree" ]; then
    echo "Missing $root/$tree" >&2
    exit 1
  fi
done

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

copy_tree() {
  src=$1
  dest=$2
  mkdir -p "$dest"
  if command -v rsync >/dev/null 2>&1; then
    rsync -a \
      --exclude '/build' \
      --exclude '/build-*' \
      --exclude '/.git' \
      --exclude '/vcpkg_installed' \
      "$src/" "$dest/"
  else
    tar -C "$src" -cf - \
      --exclude build \
      --exclude './build' \
      --exclude .git \
      . | tar -C "$dest" -xf -
  fi
}

copy_tree "$root/opalvoip-ptlib" "$stage/opalvoip-ptlib"
copy_tree "$root/opalvoip-opal" "$stage/opalvoip-opal"
copy_tree "$here" "$stage/callbox"

exec "$runtime" build \
  --build-arg "CALLBOX_H323=${CALLBOX_H323:-ON}" \
  --build-arg "CALLBOX_SIP=${CALLBOX_SIP:-ON}" \
  -f "$stage/callbox/Dockerfile" -t "$tag" "$stage"
