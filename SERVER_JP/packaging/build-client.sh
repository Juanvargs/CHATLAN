#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cpp="$root/../chat-lan-cpp"
if (( $# != 2 )); then
  printf 'Uso: %s auto|IP_SERVIDOR /ruta/server.pem\n' "$0" >&2
  exit 2
fi
host=$1
certificate=$2
"$cpp/scripts/build.sh"
bash "$cpp/packaging/build-portable.sh" "$host" "$certificate"
stage="$cpp/dist/ChatLANCpp-portable"
cat > "$stage/LEEME.txt" <<'INSTRUCTIONS'
CHAT LAN · CLIENTE C++/Qt

1. Extrae la carpeta completa en un equipo Linux x86_64.
2. Ejecuta ./ChatLAN desde esta carpeta.
3. Crea un usuario y pulsa Conectar. El servidor ya está configurado.
4. Si aparece "Alta pendiente", el administrador debe aprobar tu usuario.

Este paquete contiene el certificado PUBLICO del servidor. Nunca distribuyas
server.key ni la base clients.sqlite3. El servidor debe estar encendido en la
misma LAN y aceptar TCP 8765 y UDP 8764.
INSTRUCTIONS
mkdir -p "$root/dist"
tar -C "$cpp/dist" -czf "$root/dist/ChatLAN-Go-cliente-linux-amd64-v3.tar.gz" ChatLANCpp-portable
printf 'Cliente portátil: %s/dist/ChatLAN-Go-cliente-linux-amd64-v3.tar.gz\n' "$root"
