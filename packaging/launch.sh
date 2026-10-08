#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# No service/runtime startup, no audio configuration changes, no shell-built commands.
exec "$here/frame-volume-mixer" "$@"
