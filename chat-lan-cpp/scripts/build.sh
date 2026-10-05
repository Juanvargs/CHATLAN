#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
args=()
if [ -d "$root/.toolchain/usr/lib/x86_64-linux-gnu/cmake/Qt6" ]; then
  export LD_LIBRARY_PATH="$root/.toolchain/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  args+=("-DCMAKE_PREFIX_PATH=$root/.toolchain/usr" "-DOPENSSL_INCLUDE_DIR=$root/.toolchain/usr/include" "-DCMAKE_CXX_FLAGS=-I$root/.toolchain/usr/include/x86_64-linux-gnu" "-DOPENSSL_CRYPTO_LIBRARY=/usr/lib/x86_64-linux-gnu/libcrypto.so.3" "-DOPENSSL_SSL_LIBRARY=/usr/lib/x86_64-linux-gnu/libssl.so.3" "-DSQLite3_INCLUDE_DIR=$root/.toolchain/usr/include" "-DSQLite3_LIBRARY=/usr/lib/x86_64-linux-gnu/libsqlite3.so.0")
fi
cmake -S "$root" -B "$root/build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo "${args[@]}" "$@"
cmake --build "$root/build" --parallel 2
