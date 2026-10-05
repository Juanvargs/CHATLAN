#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
version=${1:-0.2.0}
host=${2:-}
certificate=${3:-}
if [[ -n "$host" || -n "$certificate" ]]; then
  if [[ ! "$host" =~ ^([0-9]{1,3}\.){3}[0-9]{1,3}$ || ! -f "$certificate" ]]; then
    printf 'Uso: %s [VERSIÓN [IP_SERVIDOR CERTIFICADO_PUBLICO_PEM]]\n' "$0" >&2
    exit 2
  fi
  openssl x509 -in "$certificate" -noout >/dev/null
fi
arch=$(dpkg --print-architecture)
stage=$(mktemp -d "$root/build/chat-lan-cpp-package.XXXXXX")
trap 'rm -rf -- "$stage"' EXIT
DESTDIR="$stage" cmake --install "$root/build" --prefix /usr
if [[ -n "$host" ]]; then
  mkdir -p "$stage/usr/share/chat-lan-cpp"
  cp "$certificate" "$stage/usr/share/chat-lan-cpp/server.pem"
  printf '{"host":"%s","port":8765,"discovery_port":8764,"certificate":"server.pem"}\n' "$host" > "$stage/usr/share/chat-lan-cpp/client-default.json"
  chmod 644 "$stage/usr/share/chat-lan-cpp/server.pem" "$stage/usr/share/chat-lan-cpp/client-default.json"
fi
mkdir -p "$stage/DEBIAN" "$root/dist"
find "$stage" -type d -exec chmod 755 {} +
cat > "$stage/DEBIAN/control" <<CONTROL
Package: chat-lan-cpp
Version: $version
Section: net
Priority: optional
Architecture: $arch
Maintainer: Chat LAN
Depends: libqt6core6t64 | libqt6core6, libqt6network6t64 | libqt6network6, libqt6widgets6t64 | libqt6widgets6, libssl3t64 | libssl3, libsqlite3-0
Description: Chat de red local implementado en C++
 Cliente, servidor y administración Qt 6 con TLS y SQLite.
CONTROL
dpkg-deb --root-owner-group --build "$stage" "$root/dist/chat-lan-cpp_${version}_${arch}.deb" >/dev/null
printf 'Paquete creado: %s/dist/chat-lan-cpp_%s_%s.deb\n' "$root" "$version" "$arch"
