# Chat LAN Go

Servidor del proyecto Python original reimplementado en Go. Incluye TLS,
autenticación Ed25519, protocolo JSON, descubrimiento UDP, persistencia SQLite y
administración por terminal. La entrega integrada usa el cliente y el panel
gráfico C++/Qt de `../chat-lan-cpp`.
El [estudio del original](docs/estudio-original.md) documenta el comportamiento.
La [guía de entrega Go+C++](docs/entrega-go-cpp.md) explica los paquetes y la
prueba con otros usuarios.

## Compilar

Requiere Go 1.22 o posterior, compilador C, `libsqlite3-dev` y acceso a la
dependencia `github.com/mattn/go-sqlite3` al preparar el módulo.

```bash
go mod download
go build -o chat-lan-go .
go test ./...
```

## Ejecutar

```bash
./chat-lan-go init
./chat-lan-go server --init
./chat-lan-go list
./chat-lan-go approve UUID
./chat-lan-go revoke UUID
./chat-lan-go disconnect UUID
./chat-lan-go delete UUID
./chat-lan-go recover UUID CLAVE_PUBLICA_BASE64
./chat-lan-go backup /ruta/copia.sqlite3
```

Todas las órdenes aceptan `--data RUTA` antes de los argumentos finales. La
carpeta predeterminada es `~/.local/share/chat-lan-go/server`; el servidor no
modifica los datos Python salvo que se indique explícitamente `--data`.
`--init` crea el certificado la primera vez. El servidor también lee
`settings.json` de esa carpeta (`host`, `port`, `discovery_port`, `allow` y
`auto_approve`); los argumentos explícitos tienen prioridad. El panel C++/Qt
lo modifica con `chat-lan-admin-gui-cpp --go`. Los puertos predeterminados son
TCP 8765 y UDP 8764.
