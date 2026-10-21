# Servidor CHATLAN en Go

Servidor principal de la versión integrada de CHATLAN. Incluye TLS,
autenticación Ed25519, protocolo JSON, descubrimiento UDP, SQLite y comandos de
administración. El cliente y el panel gráficos están en `../chat-lan-cpp`.
Consulta la [guía general](../docs/README.md), la [arquitectura](../docs/arquitectura.md),
la [referencia de red](../docs/protocolo.md), la [operación](../docs/operacion.md)
y la [instalación](../docs/instalacion-y-distribucion.md).

## Compilar

Requiere Go 1.22 o posterior, un compilador C para CGO, SQLite 3 de desarrollo
(por ejemplo, `libsqlite3-dev` en Debian/Ubuntu) y acceso a la dependencia
`github.com/mattn/go-sqlite3` al preparar el módulo.

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

Las órdenes admiten `--data RUTA` para elegir otra carpeta. La ruta por defecto
es `~/.local/share/chat-lan-go/server`. El archivo `settings.json` admite
`host`, `port`, `discovery_port`, `allow` y `auto_approve`; los argumentos
explícitos prevalecen. `server --init` crea certificado/clave si faltan ambos.
El panel usa `chat-lan-admin-gui-cpp --go`. Los puertos predeterminados son
TCP 8765 y UDP 8764. Para explicación de estados, firmas, recibos y límites,
consulta la [documentación del servidor](docs/servidor-explicado.md) y la
[referencia de protocolo](../docs/protocolo.md).
