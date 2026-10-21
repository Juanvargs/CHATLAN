# Chat LAN C++

Implementación en **C++20** de los componentes de escritorio de CHATLAN: cliente
y panel de administración Qt 6. También contiene un servidor C++ alternativo
con Qt Network. La configuración integrada del repositorio utiliza el servidor
Go que está en `../SERVER_JP`; cliente/panel pueden usarse con él. OpenSSL
gestiona operaciones criptográficas y SQLite persiste datos.

Empieza por la [documentación de CHATLAN](../docs/README.md), la
[arquitectura](../docs/arquitectura.md) y la [referencia del protocolo](../docs/protocolo.md).
El [mapa de arquitectura](../docs/arquitectura.md) explica la responsabilidad
de cada archivo. El protocolo está descrito en [su referencia técnica](../docs/protocolo.md).

## Funciones

- Chat general y mensajes privados entre clientes conectados de la misma LAN.
- Hasta diez identidades aprobadas. Cada perfil conserva su clave Ed25519; no hay
  contraseñas. El servidor puede aprobar automáticamente mientras haya cupo.
- TLS con certificado local y comprobación de huella en el cliente; descubrimiento
  del servidor por UDP cuando cambia la IP.
- Historial de hasta 200 mensajes relevantes al conectar, presencia, acuse de
  recepción por aplicación y reconexión con espera progresiva.
- Panel local y consola para aprobar, revocar, desconectar y eliminar cuentas,
  hacer copias de SQLite y consultar el estado y los registros del servicio.

El protocolo usa JSON con longitud prefijada, versión 1 y prueba Ed25519. Los
perfiles del cliente guardan sus datos por defecto en
`~/.local/share/chat-lan-cpp/`. Se puede indicar otra ruta con `--data` o
`--profile`, según el ejecutable.

## Compilar y probar

En Debian/Ubuntu se necesitan compilador C++20, CMake, Ninja, Qt 6.4 o superior
(Core, Network, Widgets y Test), OpenSSL y los encabezados de SQLite 3. Si esas
bibliotecas ya están instaladas:

```bash
./scripts/build.sh
ctest --test-dir build --output-on-failure
```

`scripts/prepare-local.sh` prepara dependencias extraídas dentro de `.toolchain/`
cuando no se desea instalarlas en el sistema. `scripts/build.sh` las detecta y
las utiliza. La prueba `chat-core` comprueba protocolo, identidad, SQLite y una
conversación TLS entre dos clientes; `chat-ui` comprueba el estado inicial y el
escape de texto en la interfaz.

## Uso local

```bash
# En el equipo servidor: genera certificado y abre TCP 8765 / UDP 8764.
./scripts/run.sh chat-lan-server-cpp --init

# Panel local del servidor (otra terminal).
./scripts/run.sh chat-lan-admin-gui-cpp

# Cliente (en cada equipo).
./scripts/run.sh ChatLANCpp
```

El cliente solicita el certificado público `server.pem` del servidor y su IP la
primera vez que se conecta. Se debe compartir **sólo** `server.pem`; `server.key`
permanece en el equipo servidor. Los perfiles posteriores reutilizan la
configuración local. Si el cliente y el servidor están en el mismo equipo, el
cliente toma el certificado de la carpeta de datos del servidor.

Los puertos predeterminados son 8765 TCP y 8764 UDP. Solo uno de los servidores
(Go o C++) puede ocupar esos puertos en un equipo a la vez.
El servidor acepta `--host`, `--port`, `--discovery-port`, `--allow` y `--data`.
El cliente acepta `--profile` y `--public-key`. El panel permite cambiar puertos,
subredes y aprobación automática en `settings.json`; se aplican al reiniciar.

## Administración por terminal

```bash
./scripts/run.sh chat-lan-admin-cpp init
./scripts/run.sh chat-lan-admin-cpp list
./scripts/run.sh chat-lan-admin-cpp approve UUID
./scripts/run.sh chat-lan-admin-cpp revoke UUID
./scripts/run.sh chat-lan-admin-cpp disconnect UUID
./scripts/run.sh chat-lan-admin-cpp delete UUID
./scripts/run.sh chat-lan-admin-cpp recover UUID CLAVE_PUBLICA_BASE64
./scripts/run.sh chat-lan-admin-cpp backup /ruta/copia.sqlite3
```

`recover` sustituye la clave de una cuenta y bloquea la antigua. El panel gráfico
permite las operaciones habituales de cuentas y servicio; la recuperación se
realiza por terminal.

## Instalación

`cmake --install build --prefix /usr` instala los cuatro ejecutables, las entradas
de menú y la unidad de usuario `chat-lan-server-cpp.service`. En Debian/Ubuntu,
`bash packaging/build-deb.sh` crea un paquete con dependencias Qt 6, OpenSSL y
SQLite en `dist/`. Para distribuir clientes con la IP y el certificado público
ya configurados, use
`bash packaging/build-deb.sh 0.2.0 IP_DEL_SERVIDOR /ruta/server.pem`.
`bash packaging/build-portable.sh [IP_DEL_SERVIDOR /ruta/server.pem]` crea una
carpeta portátil en `dist/ChatLANCpp-portable/` con las bibliotecas Qt y los
plugins necesarios; se inicia con `dist/ChatLANCpp-portable/ChatLANCpp`.
La unidad se activa, si se desea, con
`systemctl --user enable --now chat-lan-server-cpp.service`. El panel usa esa unidad.

## Datos y límites

El servidor conserva `server.key`, `server.pem` y `clients.sqlite3`; cada perfil
conserva `identity.key`, `connection.json` y una copia del certificado público.
La base guarda el texto sin cifrado de extremo a extremo. Los privados requieren
que el destinatario esté conectado; no hay adjuntos, llamadas, buzón fuera de línea
ni confirmación de lectura humana. Al borrar una cuenta se eliminan sus mensajes
asociados, incluidos los generales que envió.
