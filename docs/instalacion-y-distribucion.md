# Instalar, ejecutar y distribuir

Esta guía describe el despliegue integrado: servidor Go en un equipo de la LAN, cliente portátil C++/Qt en los equipos de usuarios y panel local en el equipo administrador. El nombre de los paquetes depende de versión y arquitectura; compílalos para generar los artefactos vigentes.

## Requisitos

Servidor: Go 1.22+, compilador C para CGO y encabezados de SQLite 3. Cliente y panel: C++20, CMake 3.22+, Ninja, Qt 6.4+ (Core, Network, Widgets y Test), OpenSSL y SQLite 3. Se requiere Linux para los scripts incluidos. Las dependencias Go deben estar descargadas o ser accesibles al preparar el módulo.

## Compilar

Desde la raíz del repositorio:

```bash
cd SERVER_JP
go mod download
go build -o chat-lan-go .
go test ./...

cd ../chat-lan-cpp
./scripts/build.sh
ctest --test-dir build --output-on-failure
```

## Preparar el servidor

```bash
cd SERVER_JP
./chat-lan-go init
./chat-lan-go server
```

La inicialización crea certificado TLS, clave privada y base de datos en el directorio de datos. El valor predeterminado suele ser `~/.local/share/chat-lan-go/server/`. Conserva `server.key` solo en el servidor y resguarda también la base. Consulta [operación](operacion.md) para configurar el servicio.

La red debe permitir TCP 8765 para sesiones y UDP 8764 para descubrimiento. Verifica que el firewall permita esos puertos desde la subred autorizada y que el punto de acceso no aísle a los clientes entre sí. Limita el acceso a la LAN de confianza.

## Preparar clientes

Para ejecutar localmente el código compilado:

```bash
cd chat-lan-cpp
./scripts/run.sh ChatLANCpp
```

El cliente crea su propio perfil y clave Ed25519 privada. En el primer uso debe conocer el servidor y confiar en su certificado público. Para generar un cliente portátil configurado para descubrimiento automático, desde `SERVER_JP`:

```bash
bash packaging/build-client.sh auto /ruta/al/server.pem
```

El script encuentra el proyecto C++ vecino `../chat-lan-cpp`; mantén ambas carpetas juntas. El resultado va a `SERVER_JP/dist/`. Consulta la salida para el nombre de archivo generado. Para fijar una dirección inicial, sustituye `auto` por la IP del servidor.

En cada equipo Linux x86_64, extrae el `.tar.gz` y ejecuta el lanzador incluido (en la entrega actual, `./ChatLAN`). Cada usuario crea su perfil propio; no copies `identity.key` entre personas. El paquete lleva el certificado público, nunca la clave privada del servidor.

## Paquete del servidor

Desde `SERVER_JP`, `bash packaging/build-server-deb.sh [version]` genera un paquete Debian en `dist/`. El proceso compila e integra componentes Qt del repositorio vecino. Los paquetes se excluyen de Git. Tras instalarlo, inicia la unidad de usuario si la instalación la registró. Consulta los comandos en [operación](operacion.md).

## Prueba funcional en dos equipos

1. Inicia el servidor y confirma su estado con el panel o `systemctl --user status chat-lan-go.service`.
2. Conecta un cliente y comprueba que la identidad quede aprobada o pendiente según `auto_approve` y el cupo.
3. Conecta otro cliente desde un equipo distinto y envía un mensaje general.
4. Prueba un mensaje privado mientras el destinatario esté conectado.
5. Desconecta y vuelve a conectar un cliente; revisa presencia e historial.
6. En el panel, revisa aprobación, revocación, desconexión y copia de seguridad.

La prueba en un solo equipo no demuestra conectividad entre computadores. Firewall, enrutamiento y aislamiento Wi-Fi afectan el resultado.

## Datos que no se distribuyen

No incluyas `server.key`, `identity.key`, `clients.sqlite3` ni copias de seguridad en entregas públicas. `server.pem` es público, pero debe llegar solo a quienes deban conectarse a esa instancia.
