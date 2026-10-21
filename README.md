# CHATLAN

CHATLAN es una aplicación de mensajería de texto para una red local. La versión integrada usa un servidor Go, un cliente de escritorio C++/Qt y un panel de administración C++/Qt. El servidor acepta conexiones TCP protegidas con TLS, autentica perfiles con claves Ed25519 y persiste usuarios y mensajes en SQLite.

Ofrece conversación general y privada entre usuarios aprobados de la misma LAN, presencia e historial limitado. El descubrimiento inicial usa UDP. No implementa llamadas, envío de archivos, audio, mensajes sin conexión ni confirmación de lectura humana.

## Estructura

```text
CHATLAN/
├── SERVER_JP/       Servidor Go, comandos, empaquetado y guías
├── chat-lan-cpp/    Cliente Qt, panel Qt y servidor C++ alternativo
└── docs/            Arquitectura, protocolo, operación, instalación y equipo
```

La configuración integrada es **servidor Go + cliente y panel C++/Qt**. El servidor C++ alternativo sigue en `chat-lan-cpp`; ambos servidores usan los mismos puertos predeterminados y no deben ejecutarse a la vez en un equipo. Mantén ambas carpetas como hermanas porque los scripts de empaquetado dependen de esa relación.

## Requisitos y compilación

Se requiere Go 1.22+, compilador C y SQLite 3 de desarrollo. Cliente y panel requieren C++20, CMake 3.22+, Ninja, Qt 6.4+ (Core, Network, Widgets, Test), OpenSSL y SQLite 3. Los scripts incluidos están orientados a Linux.

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

## Primera ejecución

En una terminal:

```bash
cd SERVER_JP
./chat-lan-go init
./chat-lan-go server
```

En otra terminal:

```bash
cd chat-lan-cpp
./scripts/run.sh ChatLANCpp
./scripts/run.sh chat-lan-admin-gui-cpp --go
```

El cliente confía en el certificado público `server.pem` de esa instancia. La clave privada `server.key` permanece en el servidor. Para instalar en otros equipos, sigue [Instalación y distribución](docs/instalacion-y-distribucion.md).

## Documentación

Empieza por la [ruta de aprendizaje](docs/README.md) y [arquitectura](docs/arquitectura.md). Consulta el [estudio del servidor](SERVER_JP/docs/servidor-explicado.md), la [referencia del protocolo](docs/protocolo.md), la [operación y solución de problemas](docs/operacion.md) y la [guía del panel](SERVER_JP/docs/interfaz-administracion.md).

## Equipo

- Sebastián Tovar
- Juan Pablo Vargas
- Juan David Romero
- Juan Felipe Pachón

## Licencia

El repositorio no contiene un archivo `LICENSE`, así que no publica una licencia que autorice la reutilización o distribución del software. El equipo debe acordar y añadir una antes de presentar el proyecto como software reutilizable. Paquetes `.deb`, `.tar.gz`, certificados, claves y bases de datos son artefactos/datos locales y no deben versionarse.
