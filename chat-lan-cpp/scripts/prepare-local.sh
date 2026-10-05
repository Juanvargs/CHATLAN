#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "$root/.toolchain/packages"
cd "$root/.toolchain/packages"
mapfile -t packages < <(apt-get -s --no-install-recommends install qt6-base-dev libssl-dev libsqlite3-dev | awk '/^Inst / && $2 != "libsqlite3-dev" {print $2}')
if ((${#packages[@]})); then
  apt-get download "${packages[@]}"
fi
apt-get download libsqlite3-dev || apt-get download libsqlite3-dev=3.45.1-1ubuntu2
for package in ./*.deb; do
  [ -f "$package" ] && dpkg-deb -x "$package" "$root/.toolchain"
done
printf 'Bibliotecas extraídas en %s/.toolchain (sin instalación del sistema).\n' "$root"
