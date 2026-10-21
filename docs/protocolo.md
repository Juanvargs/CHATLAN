# Referencia del protocolo de red

Describe la versión integrada. Los nombres y límites deben mantenerse sincronizados con `SERVER_JP/protocol.go` y `SERVER_JP/server.go`. El servidor C++ alternativo implementa compatibilidad con el protocolo de chat.

## Transporte

| Uso | Transporte | Puerto | Propósito |
| --- | --- | --- | --- |
| Descubrimiento | UDP | 8764 | Encontrar servidor, puerto TCP y huella. |
| Sesión | TCP con TLS | 8765 | Autenticación, presencia, mensajes y acuses. |

El datagrama de búsqueda es `CHAT_LAN_DISCOVER_V1`. El servidor responde JSON con servicio, puerto TCP y huella del certificado. UDP solo sirve para descubrimiento, nunca transporta chat.

## Encuadre TCP

Cada mensaje de aplicación es un objeto JSON UTF-8 precedido por cuatro bytes de longitud en orden de red (big-endian). La longitud cuenta el cuerpo, no el prefijo. El cuerpo permitido mide de 1 a 65 536 bytes e incluye `v: 1` y `type`. TCP es un flujo: una lectura puede contener solo parte del prefijo, varios cuerpos o fragmentos; el receptor debe leer exactamente los bytes indicados.

El prefijo permite separar objetos en una conexión persistente y limitar su tamaño; no cifra. TLS aporta confidencialidad e integridad del canal.

## Autenticación

1. El cliente abre TCP y valida el certificado TLS fijado para el servidor.
2. Envía `hello` con versión, nombre visible y clave pública Ed25519.
3. El servidor envía un desafío aleatorio de 32 bytes y datos de huella.
4. El cliente firma el contenido de autenticación con su clave privada y envía la prueba.
5. El servidor verifica la firma y aplica el estado de cuenta: pendiente, aprobada, revocada o eliminada.
6. Si autoriza, responde `authenticated` con la identidad asignada y comienza presencia e historial.

La firma cubre el dominio `chat-lan/auth/v1\0`, el nonce, la clave pública y la huella binaria del servidor. El dominio separa esta firma de otros usos; el nonce fresco dificulta repetir pruebas antiguas. El nombre visible no es un secreto.

La instancia permite hasta diez cuentas aprobadas. `auto_approve` puede aprobar perfiles nuevos cuando hay cupo; si no, quedan pendientes. La administración puede aprobar, revocar, desconectar, eliminar o recuperar una identidad usando otra clave pública.

## Eventos principales

Los paquetes son JSON. La tabla resume eventos; campos opcionales y errores exactos deben consultarse en los handlers del servidor al implementar otro cliente.

| Dirección | Evento | Función |
| --- | --- | --- |
| Cliente → servidor | `ping` | Mantener activa la sesión; recibe `pong`. |
| Cliente → servidor | `send_message` | Solicita texto general o privado e incluye ID, destino y texto. |
| Servidor → emisor | `accepted` | Confirma que el servidor procesó/registró la solicitud; puede señalar duplicado. |
| Servidor → destinatario | `message` | Entrega el mensaje a destinatarios conectados. |
| Cliente → servidor | `received` | Acuse de recepción a nivel de aplicación. |
| Servidor → emisor | `delivered` | El servidor recibió acuse del destinatario; no significa lectura humana. |
| Servidor → clientes | `presence` | Actualiza el conjunto de usuarios conectados. |
| Servidor → cliente | `error` y estados de autenticación | Informan error, versión no compatible o estado de cuenta. |

Los mensajes generales llegan a clientes conectados, incluido el emisor. Los privados solo se entregan si el destinatario está conectado. El servidor guarda antes de confirmar aceptación y no mantiene buzón para usuarios offline.

## Límites y semántica de entrega

- Versión: 1.
- Tamaño de cuerpo: 1–65 536 bytes.
- Texto: hasta 2 000 caracteres Unicode tras recortar espacios externos.
- Frecuencia: hasta 50 solicitudes por cliente cada cinco segundos.
- Historial: hasta 200 mensajes pertinentes al conectar.
- Keepalive: `ping` aproximadamente cada 20 segundos.

El ID del mensaje permite reconocer reintentos. `accepted` describe el procesamiento del servidor; `received` informa recepción por el cliente destinatario; `delivered` confirma que el servidor observó esa señal. Ninguno confirma que una persona haya leído el mensaje.

## Seguridad y privacidad

TLS mínimo 1.2 protege el canal. El servidor usa certificado local autofirmado basado en ECDSA P-256; el cliente verifica la identidad configurada mediante certificado/huella. Ed25519 autentica el perfil, pero no cifra el texto. El servidor puede leer y persiste mensajes en SQLite: no hay cifrado de extremo a extremo.

Comparte solo el certificado público `server.pem`. Protege `server.key`, las claves de perfil y las bases de datos. Si cambia el certificado, los clientes necesitan recibir su nueva copia mediante un canal confiable.

## Compatibilidad

Cambiar nombres de eventos, campos requeridos, orden de autenticación, encuadre, límites o contenido firmado puede romper clientes existentes. Al cambiar el contrato, actualiza la versión, ambos extremos, esta referencia y las pruebas de compatibilidad antes de distribuirlo.
