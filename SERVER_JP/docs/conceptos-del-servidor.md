# Fundamentos del servidor CHATLAN

Este documento presenta el servidor desde cero: su responsabilidad, sus límites y la manera en que procesa una conexión. La implementación de referencia está escrita en Go y se complementa con un cliente y un panel C++/Qt.

## Qué hace el servidor

El servidor es el punto central de la aplicación. Descubre clientes en la red local, acepta conexiones TCP protegidas con TLS, verifica la identidad de cada perfil mediante Ed25519, aplica permisos de cuenta, guarda mensajes en SQLite y los entrega a usuarios conectados. Los clientes no se comunican directamente entre sí: cada mensaje pasa por este proceso.

## Componentes internos

| Parte | Responsabilidad |
| --- | --- |
| Entrada y configuración | Selecciona el comando, lee `settings.json` y valida opciones de red. |
| Capa TLS | Crea/carga `server.pem` y `server.key`, limita TLS a 1.2 o superior y calcula una huella SHA-256. |
| Protocolo | Convierte eventos JSON en tramas con prefijo de cuatro bytes y verifica tamaño y versión. |
| Autenticación | Emite un nonce nuevo y verifica una firma Ed25519 ligada a la clave y huella del servidor. |
| Control de sesiones | Atiende conexiones, controla acceso por IP, presencia, keepalive y límite de solicitudes. |
| Persistencia | SQLite conserva cuentas, mensajes e identidades eliminadas para impedir su reutilización. |
| Administración | Aprueba, revoca, desconecta, elimina o recupera cuentas; también crea copias de la base. |

La ubicación de cada parte en el código está en [arquitectura y mapa de archivos](../../docs/arquitectura.md).

## Inicio de una sesión

1. El cliente envía `CHAT_LAN_DISCOVER_V1` por UDP 8764 y recibe el puerto TCP y la huella anunciada.
2. El cliente establece TCP y negocia TLS con el certificado público configurado para esa instalación.
3. El cliente envía `hello` con nombre y clave pública. El servidor responde con `challenge` y un nonce aleatorio.
4. El cliente firma el desafío con la clave privada Ed25519 de su perfil y responde con `proof`.
5. El servidor valida la firma y busca o registra la cuenta. Solo una cuenta aprobada entra a la sala.
6. El servidor notifica `authenticated`, envía historial pertinente y publica la lista de presencia.

Cada paso de red se detalla en la [referencia del protocolo](../../docs/protocolo.md).

## Ciclo de un mensaje

El cliente genera un UUID por mensaje y envía `send_message`. El servidor valida destino, texto, formato y frecuencia; comprueba que el destinatario privado esté conectado; guarda el texto y lo distribuye. El emisor recibe `accepted`. El destinatario confirma a nivel de aplicación con `received`; entonces el emisor puede recibir `delivered`. Este último estado indica que el servidor recibió la confirmación, no que una persona haya leído el mensaje.

La sala `general` se distribuye a usuarios conectados. Los privados no quedan en cola cuando el destinatario está desconectado. El servidor mantiene un historial limitado para la siguiente conexión.

## Estados de cuenta

- `pending`: cuenta nueva que necesita aprobación.
- `approved`: puede iniciar una sesión.
- `revoked`: el acceso está bloqueado.
- `deleted`: la cuenta y sus mensajes asociados se eliminaron, y la clave anterior queda registrada para impedir un alta accidental con la misma identidad.

El límite es diez cuentas aprobadas y treinta conexiones simultáneas. El panel o la CLI pueden cambiar estados. Un supervisor revisa cada pocos segundos las solicitudes administrativas y la autorización de sesiones activas.

## Persistencia y protección de datos

`clients.sqlite3` contiene cuentas y mensajes. La clave privada TLS `server.key` prueba la identidad del servidor y no se comparte. Cada cliente protege `identity.key`; el servidor recibe únicamente la clave pública. TLS cifra el trayecto entre cliente y servidor, pero el servidor procesa texto legible y lo guarda en SQLite; este diseño no ofrece cifrado de extremo a extremo.

Los comandos y rutas de datos se describen en la [guía de operación](../../docs/operacion.md).
