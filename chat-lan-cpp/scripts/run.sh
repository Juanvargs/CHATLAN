#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [ -d "$root/.toolchain/usr/lib/x86_64-linux-gnu" ]; then
  export LD_LIBRARY_PATH="$root/.toolchain/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  export QT_PLUGIN_PATH="$root/.toolchain/usr/lib/x86_64-linux-gnu/qt6/plugins"
fi
program=${1:-ChatLANCpp}
if (($#)); then shift; fi
exec "$root/build/$program" "$@"
