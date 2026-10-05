#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
stage="$root/dist/ChatLANCpp-portable"
mkdir -p "$stage/bin" "$stage/lib" "$stage/plugins/platforms" "$stage/plugins/tls"
rm -f "$stage/bin/client-default.json" "$stage/bin/server.pem"
cp "$root/build/ChatLANCpp" "$stage/bin/ChatLANCpp.bin"
plugin_root="$root/.toolchain/usr/lib/x86_64-linux-gnu/qt6/plugins"
if [[ ! -d "$plugin_root" ]]; then plugin_root=/usr/lib/x86_64-linux-gnu/qt6/plugins; fi
for plugin in platforms/libqxcb.so platforms/libqoffscreen.so tls/libqopensslbackend.so; do
  if [[ ! -f "$plugin_root/$plugin" ]]; then
    printf 'Falta el plugin Qt requerido: %s\n' "$plugin_root/$plugin" >&2
    exit 1
  fi
  cp -L "$plugin_root/$plugin" "$stage/plugins/$plugin"
done

# Qt's OpenSSL backend loads libssl at runtime, so ldd does not report it.
ssl_library=$(ldconfig -p | awk '/libssl\.so\.3 \(libc6,x86-64\)/ {print $NF; exit}')
if [[ -z "$ssl_library" || ! -f "$ssl_library" ]]; then
  printf 'No se encontró libssl.so.3 para el cliente portátil\n' >&2
  exit 1
fi
cp -L "$ssl_library" "$stage/lib/libssl.so.3"

# Keep Qt from searching the other computer's incompatible system plugins.
cat > "$stage/bin/qt.conf" <<'QT_CONFIG'
[Paths]
Plugins = ../plugins
QT_CONFIG

# Copy direct and indirect dynamic dependencies under the names expected by ld.so.
mapfile -t pending < <(find "$stage/bin" "$stage/plugins" -type f)
declare -A visited=()
for ((index=0; index<${#pending[@]}; index++)); do
  file=${pending[index]}
  while read -r library; do
    [[ -f "$library" ]] || continue
    name=$(basename "$library")
    case "$name" in
      linux-vdso*|ld-linux*|libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*) continue ;;
    esac
    [[ -v visited[$name] ]] && continue
    visited[$name]=1
    cp -L "$library" "$stage/lib/$name"
    pending+=("$stage/lib/$name")
  done < <(ldd "$file" | awk '/=> \// {print $3}')
done
cat > "$stage/ChatLANCpp" <<'LAUNCHER'
#!/usr/bin/env bash
set -euo pipefail
bundle=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
export LD_LIBRARY_PATH="$bundle/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$bundle/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$bundle/plugins/platforms"
if [[ -z "${QT_QPA_PLATFORM:-}" && "${XDG_SESSION_TYPE:-}" == wayland ]]; then
  export QT_QPA_PLATFORM=xcb
fi
exec "$bundle/bin/ChatLANCpp.bin" "$@"
LAUNCHER
chmod 755 "$stage/ChatLANCpp"
cp "$stage/ChatLANCpp" "$stage/ChatLAN"

# Optional server identity for a client bundle ready to connect on first launch.
host=${1:-}
certificate=${2:-}
if [[ -n "$host" || -n "$certificate" ]]; then
  if [[ "$host" != auto && ! "$host" =~ ^([0-9]{1,3}\.){3}[0-9]{1,3}$ || ! -f "$certificate" ]]; then
    printf 'Uso: %s [auto|IP_SERVIDOR CERTIFICADO_PUBLICO_PEM]\n' "$0" >&2
    exit 2
  fi
  openssl x509 -in "$certificate" -noout >/dev/null
  cp "$certificate" "$stage/bin/server.pem"
  chmod 644 "$stage/bin/server.pem"
  config_host=$host
  if [[ "$host" == auto ]]; then config_host=''; fi
  printf '{"host":"%s","port":8765,"discovery_port":8764,"certificate":"server.pem"}\n' "$config_host" > "$stage/bin/client-default.json"
fi
printf 'Cliente portátil creado: %s/ChatLANCpp\n' "$stage"
