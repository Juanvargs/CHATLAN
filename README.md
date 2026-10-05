# CHATLAN

Chat de texto para una red local. El **servidor principal está escrito en Go** y
el **cliente de escritorio y el panel de administración están escritos en C++/Qt**.
Los clientes descubren el servidor por UDP 8764 y se comunican con él por
TCP 8765 con TLS. Los usuarios se identifican mediante claves Ed25519 y el
servidor guarda cuentas y mensajes en SQLite.

Este repositorio reúne los dos proyectos:

```text
CHATLAN/
├── SERVER_JP/     Servidor Go, scripts de empaquetado y documentación
└── chat-lan-cpp/  Cliente de escritorio C++/Qt y panel Qt
```

Ambas carpetas son **hermanas**. Los scripts de empaquetado del servidor encuentran
el cliente usando esa relación, por lo que conviene mantenerlas juntas.

## Compilar desde el código

Consulta [los requisitos de Go](SERVER_JP/README.md) y
[los requisitos de C++/Qt](chat-lan-cpp/README.md). Desde la raíz del repositorio:

```bash
cd SERVER_JP
go mod download
go build -o chat-lan-go .
go test ./...

cd ../chat-lan-cpp
./scripts/build.sh
ctest --test-dir build --output-on-failure
```

Las carpetas `build/`, `dist/`, `.toolchain/` y los ejecutables generados se
excluyen de Git. Cada servidor crea su propio certificado `server.pem` y su
clave privada `server.key`; los clientes deben recibir el certificado público
de **ese mismo servidor**. No publiques la clave privada ni la base SQLite.

## Uso en este equipo

En el equipo donde se configuró el servicio `chat-lan-go.service`, desde una
terminal situada en la raíz del repositorio:

```bash
systemctl --user status chat-lan-go.service
cd chat-lan-cpp
./scripts/run.sh ChatLANCpp
./scripts/run.sh chat-lan-admin-gui-cpp --go
```

El servicio de este equipo ejecuta el servidor desde `CHATLAN/SERVER_JP`. La
base de datos, la configuración y el certificado están normalmente en
`~/.local/share/chat-lan-go/server/`, fuera del repositorio. Al instalarlo en
otro equipo hay que iniciar allí su propio servicio y generar su certificado.

Para crear un cliente portátil vinculado al certificado del servidor, revisa
[la guía de entrega](SERVER_JP/docs/entrega-go-cpp.md). Los paquetes `.deb` y
`.tar.gz` son artefactos de compilación y no forman parte del código versionado.

## Documentación

- [Estudio de los conceptos y del funcionamiento del servidor](SERVER_JP/docs/servidor-explicado.md).
- [Guía de la interfaz de administración](SERVER_JP/docs/interfaz-administracion.md).
- [Instalación y entrega a otros computadores](SERVER_JP/docs/entrega-go-cpp.md).
