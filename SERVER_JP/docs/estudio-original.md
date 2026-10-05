# Estudio del servidor Python

Fuente: proyecto Python original `chat-server/src/chat_lan/` (no incluido en este repositorio), especialmente `common/protocol.py`,
`server/main.py`, `server/service.py`, `server/storage.py` y `server/tls.py`.

## Protocolo y seguridad

- TCP cifrado con TLS 1.2 o posterior. El certificado autofirmado ECDSA P-256
  está en `server.pem`, la clave en `server.key`; la huella es SHA-256 del DER.
- Cada trama empieza con cuatro bytes big-endian que indican la longitud del
  JSON UTF-8. El cuerpo debe medir entre 1 y 65536 bytes, ser un objeto y
  contener `v:1` y un `type` de texto.
- Autenticación: `hello` con `name` y clave pública Ed25519 en Base64; el
  servidor envía `challenge` con nonce aleatorio de 32 bytes en Base64 y
  `fingerprint`; el cliente firma la concatenación de
  `chat-lan/auth/v1\0`, nonce, clave pública y huella binaria, y devuelve
  `proof` con la firma en Base64. El servidor verifica antes de registrar.
- UDP 8764: ante `CHAT_LAN_DISCOVER_V1` responde con JSON que contiene
  `service:"chat-lan"`, el puerto TCP y la huella. El cliente usa la huella
  para reconocer un servidor aunque cambie su dirección IP.

## Estados y mensajes

- Máximo 30 conexiones TCP y 10 cuentas aprobadas; la aprobación automática
  deja hasta 30 solicitudes pendientes. Estado posible: `pending`,
  `approved`, `revoked`; una clave borrada conserva una lápida `deleted`.
- Al autenticarse se envían `authenticated`, hasta 200 mensajes de historial
  relevantes y `presence`. Un inicio de sesión con la misma identidad
  reemplaza el anterior.
- `send_message` admite UUID, destino `general` o ID de otro usuario conectado
  y texto de 1 a 2000 caracteres tras `strip`. El emisor recibe `accepted`;
  el destinatario responde `received` y entonces el emisor recibe `delivered`.
  Un ID repetido con idéntico contenido recibe `accepted` con `duplicate:true`;
  con distinto contenido se rechaza. Los privados exigen destinatario en línea.
- `ping` responde `pong`. La sesión limita 50 solicitudes en 5 segundos.
  Un monitor cada dos segundos aplica revocación, desconexión y borrado.

## Persistencia

- SQLite `clients.sqlite3` contiene `clients`, `messages` y `deleted_clients`.
  El servidor Python usa una conexión por operación y transacciones para
  cambios de estado. Historial en orden de secuencia, con los últimos 200
  mensajes generales o privados propios. Al eliminar una cuenta se borran
  los mensajes enviados por ella o dirigidos a ella y se bloquea su clave.
- Los datos Python se guardan por defecto en
  `$XDG_DATA_HOME/chat-lan/server` o `~/.local/share/chat-lan/server`.
  Esta implementación Go usa `chat-lan-go` como carpeta predeterminada para
  no alterar el original; `--data` permite apuntar a una copia compatible.

## Alcance de esta etapa

Servidor, protocolo, TLS, Ed25519, SQLite, descubrimiento UDP y administración
por terminal. La interfaz gráfica se abordará después.
