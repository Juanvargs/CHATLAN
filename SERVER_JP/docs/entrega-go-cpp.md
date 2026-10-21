# Guía de entrega heredada (referencia histórica)

> **Guía vigente:** consulta [`docs/instalacion-y-distribucion.md`](../../docs/instalacion-y-distribucion.md) y [`docs/operacion.md`](../../docs/operacion.md). Los nombres de paquete, versión, pruebas y estado del servicio que aparecen más abajo describen una entrega local anterior y pueden haber cambiado. No uses esos nombres como archivos actuales sin regenerar los paquetes.

## Contenido histórico

Los nombres de paquete de esta guía son **archivos generados localmente** con
los scripts de `packaging/`. Los ejecutables y paquetes no se versionan en Git.

La entrega actual está preparada para Linux x86_64, especialmente Ubuntu
24.04 o un sistema compatible. El servidor Go se ejecuta en un equipo; los
usuarios ejecutan el cliente portátil C++/Qt en sus equipos de la misma LAN.
El panel de administración Qt se ejecuta solo en el equipo servidor.

## Archivos generados

- `dist/chat-lan-go-server_0.2.0_amd64.deb`: servidor Go, panel Qt, entrada de
  menú y servicio systemd de usuario.
- `dist/ChatLAN-Go-cliente-linux-amd64-v3.tar.gz`: cliente Qt portátil, bibliotecas
  y certificado público; busca el servidor automáticamente en la LAN.
- `dist/SHA256SUMS`: sumas para comprobar las descargas.

## Servidor de este equipo

El servicio local `chat-lan-go.service` está activado y usa el ejecutable de
este repositorio. La identidad TLS y la base están en
`~/.local/share/chat-lan-go/server/`. Para consultar o reiniciar:

```bash
systemctl --user status chat-lan-go.service
systemctl --user restart chat-lan-go.service
./chat-lan-go list
../chat-lan-cpp/build/chat-lan-admin-gui-cpp --go
```

El panel muestra el estado del servidor y los usuarios cada dos segundos. El
botón principal enciende o apaga el servicio; si hay usuarios conectados pide
confirmación antes de apagarlo. Al seleccionar una cuenta aparecen las acciones
apropiadas para aprobarla, revocar o reactivar su acceso, desconectarla cuando
está en línea o eliminarla con confirmación. **Guardar y aplicar** reinicia el
servicio si está encendido. La columna «Último cambio» muestra el último cambio
de conexión registrado, no un historial completo de sesiones.

TCP 8765 y UDP 8764 deben estar disponibles en la LAN. En este equipo UFW
está activo; un administrador debe comprobar sus reglas y, si faltan, permitir
la subred local. Consulta la IP y máscara actuales con `ip -4 addr show`.
Por ejemplo, para la red `192.168.80.0/24`:

```bash
sudo ufw allow from 192.168.80.0/24 to any port 8765 proto tcp
sudo ufw allow from 192.168.80.0/24 to any port 8764 proto udp
```

## Usuarios de la LAN

Comparte únicamente `ChatLAN-Go-cliente-linux-amd64-v3.tar.gz` con cada usuario.
En cada equipo Linux x86_64:

```bash
tar -xzf ChatLAN-Go-cliente-linux-amd64-v3.tar.gz
cd ChatLANCpp-portable
./ChatLAN
```

Cada usuario crea su propio perfil y pulsa **Conectar**. El cliente busca por
UDP un servidor cuyo certificado coincida con el incluido en el paquete. La
dirección IP puede cambiar sin regenerar el cliente mientras ambos estén en
la misma LAN y UDP 8764 sea accesible. Si esa red bloquea el descubrimiento,
puedes generar otro paquete con una IP inicial explícita.

Prueba recomendada: abre dos clientes, comprueba sala general, privado,
presencia y reconexión con historial. En el panel puedes aprobar, revocar,
desconectar, eliminar y copiar la base. Solo se aprueban diez cuentas a la vez.

## Otro equipo como servidor

En Ubuntu 24.04 amd64 instala el `.deb` y activa el servicio:

```bash
sudo apt install ./chat-lan-go-server_0.2.0_amd64.deb
systemctl --user enable --now chat-lan-go.service
chat-lan-admin-go
```

El primer arranque crea `server.pem`, `server.key` y `clients.sqlite3` en
`~/.local/share/chat-lan-go/server/`. Genera un paquete cliente para la IP y
el `server.pem` **de ese nuevo servidor**:

```bash
bash packaging/build-client.sh auto ~/.local/share/chat-lan-go/server/server.pem
# O, si la red bloquea el descubrimiento:
bash packaging/build-client.sh IP_DEL_SERVIDOR ~/.local/share/chat-lan-go/server/server.pem
```

Este comando requiere las fuentes C++ y Qt del repositorio vecino
`../chat-lan-cpp`. `server.key` es privado y no debe compartirse. Si los
usuarios usan otro sistema operativo, hace falta compilar un cliente para él.

## Pruebas realizadas

- Pruebas Go con detector de carreras y pruebas C++/Qt.
- Dos ventanas C++ conectadas al servidor Go en Linux, con mensajes general y
  privado; una se conectó por descubrimiento UDP sin IP inicial.
- Cliente Python original autenticado en Go, con mensaje e historial.
- Panel Qt en modo Go abierto contra la base SQLite del servidor; una cuenta
  temporal revocada desde C++ apareció revocada al consultarla desde Go.
- Paquetes inspeccionados y cliente portátil ejecutado en modo sin ventana.

La conexión entre computadores físicos aún debe comprobarse en la red de los
usuarios. La disponibilidad desde otros equipos depende también del firewall
y de que la red permita comunicación entre clientes.
