#!/bin/sh
# Run the callbox image with the JSON file and SQLite directory on the host.
#
#   ./container-run.sh [config.json] [data-directory]
#
# The JSON file is mounted read-only. The database is created in the data
# directory, which is the second argument, defaulting to ./data.
set -eu

here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
tag=${CALLBOX_IMAGE:-callbox}
config=${1:-"$here/callbox.json"}
data=${2:-"$here/data"}

if command -v podman >/dev/null 2>&1; then
  runtime=podman
elif command -v docker >/dev/null 2>&1; then
  runtime=docker
else
  echo "podman or docker is required" >&2
  exit 1
fi

if [ ! -f "$config" ]; then
  echo "Configuration file not found: $config" >&2
  exit 1
fi

mkdir -p "$data"
config_dir=$(CDPATH= cd -- "$(dirname "$config")" && pwd)
config_abs=$config_dir/$(basename "$config")
data_abs=$(CDPATH= cd -- "$data" && pwd)

exec "$runtime" run --rm \
  --network host \
  -v "$config_abs:/config/callbox.json:ro" \
  -v "$data_abs:/var/lib/callbox" \
  -e CALLBOX_DATABASE=/var/lib/callbox/callbox.db \
  "$tag" /config/callbox.json
