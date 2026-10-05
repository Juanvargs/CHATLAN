#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cpp="$root/../chat-lan-cpp"
version=${1:-0.2.0}
arch=$(dpkg --print-architecture)
go_bin=${GO_BIN:-go}
cd "$root"
"$cpp/scripts/build.sh"
mkdir -p "$root/dist"
stage=$(mktemp -d)
trap 'rm -rf -- "$stage"' EXIT
mkdir -p "$stage/usr/bin" "$stage/usr/lib/systemd/user" "$stage/usr/share/applications" "$stage/usr/share/icons/hicolor/256x256/apps" "$stage/DEBIAN"
if command -v "$go_bin" >/dev/null 2>&1; then
  "$go_bin" build -o "$stage/usr/bin/chat-lan-go" .
elif [[ -x "$root/chat-lan-go" ]]; then
  printf 'Go no está instalado; se empaqueta el servidor compilado existente.\n' >&2
  install -m 755 "$root/chat-lan-go" "$stage/usr/bin/chat-lan-go"
else
  printf 'Falta Go y no existe un servidor compilado en %s/chat-lan-go\n' "$root" >&2
  exit 1
fi
install -m 755 "$cpp/build/chat-lan-admin-gui-cpp" "$stage/usr/bin/chat-lan-admin-go-qt"
cat > "$stage/usr/bin/chat-lan-admin-go" <<'LAUNCHER'
#!/usr/bin/env bash
exec /usr/bin/chat-lan-admin-go-qt --go "$@"
LAUNCHER
chmod 755 "$stage/usr/bin/chat-lan-admin-go"
install -m 644 "$root/packaging/chat-lan-go.service" "$stage/usr/lib/systemd/user/chat-lan-go.service"
install -m 644 "$root/packaging/chat-lan-go-admin.desktop" "$stage/usr/share/applications/chat-lan-go-admin.desktop"
install -m 644 "$cpp/assets/chat-lan-logo.png" "$stage/usr/share/icons/hicolor/256x256/apps/chat-lan-go.png"
cat > "$stage/DEBIAN/control" <<CONTROL
Package: chat-lan-go-server
Version: $version
Section: net
Priority: optional
Architecture: $arch
Maintainer: Chat LAN
Depends: libc6, libqt6core6t64 | libqt6core6, libqt6network6t64 | libqt6network6, libqt6widgets6t64 | libqt6widgets6, libssl3t64 | libssl3, libsqlite3-0
Description: Servidor Go y panel Qt de Chat LAN
 Servidor LAN con TLS, Ed25519 y SQLite; panel local de administracion.
CONTROL
find "$stage" -type d -exec chmod 755 {} +
dpkg-deb --root-owner-group --build "$stage" "$root/dist/chat-lan-go-server_${version}_${arch}.deb" >/dev/null
printf 'Paquete servidor: %s/dist/chat-lan-go-server_%s_%s.deb\n' "$root" "$version" "$arch"
