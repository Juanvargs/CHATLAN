# Operación, configuración y solución de problemas

## Rutas de datos

| Componente | Ruta predeterminada | Contenido |
| --- | --- | --- |
| Servidor Go | `$XDG_DATA_HOME/chat-lan-go/server` o `~/.local/share/chat-lan-go/server` | Certificado, clave TLS, base SQLite y configuración. |
| Cliente C++ | `~/.local/share/chat-lan-cpp/` | Perfiles, claves privadas, certificado de confianza y preferencias. |
| Paquetes | `SERVER_JP/dist/` o `chat-lan-cpp/dist/` | Artefactos locales excluidos de Git. |

Restringe los permisos de los datos al usuario del servicio. Mantén las copias de seguridad fuera del repositorio y con acceso controlado.

## Configuración del servidor

`settings.json`, en el directorio de datos, admite `host`, `port`, `discovery_port`, `allow` y `auto_approve`. Los argumentos explícitos de línea de comandos prevalecen. Los valores predeterminados son `0.0.0.0`, TCP 8765, UDP 8764 y `allow: auto`. La regla automática limita el acceso a loopback y la subred de la interfaz de ruta predeterminada; `--allow` acepta una o más redes CIDR separadas por comas. `--hosts` solo agrega nombres/IP al SAN del certificado cuando este se crea.

Comandos habituales desde `SERVER_JP`:

```bash
./chat-lan-go init
./chat-lan-go server --init
./chat-lan-go fingerprint
./chat-lan-go list
./chat-lan-go approve UUID
./chat-lan-go revoke UUID
./chat-lan-go disconnect UUID
./chat-lan-go delete UUID
./chat-lan-go recover UUID CLAVE_PUBLICA_BASE64
./chat-lan-go backup /ruta/segura/copia.sqlite3
```

Las órdenes aceptan `--data RUTA` para elegir otro directorio. `server --init` crea certificado y clave si ambos faltan; no los reemplaza. El panel gráfico puede ejecutarse en modo Go con `chat-lan-admin-gui-cpp --go` desde la carpeta C++.

## Servicio de usuario

Si el paquete instalado registró la unidad:

```bash
systemctl --user status chat-lan-go.service
systemctl --user start chat-lan-go.service
systemctl --user stop chat-lan-go.service
systemctl --user restart chat-lan-go.service
```

El nombre de unidad depende del paquete instalado. En desarrollo, el servidor iniciado en primer plano se detiene con Ctrl+C. El panel opera el servicio local y no administra servidores remotos.

## Copias y restauración

El comando `backup` copia SQLite. La identidad del servidor vive en `server.pem` y `server.key`, que se respaldan por separado y con protección adecuada. Usa el comando de copia mientras el servicio opera en vez de copiar manualmente el archivo activo. Para restaurar, detén el servicio; si necesitas conservar la confianza de clientes existentes, recupera el certificado correspondiente junto con la base.

## Solución de problemas

| Síntoma | Comprobaciones |
| --- | --- |
| No aparece el servidor | Revisa UDP 8764, la LAN compartida y el aislamiento de clientes Wi-Fi. Prueba una IP explícita si UDP está filtrado. |
| Conexión fallida o espera larga | Comprueba que el servidor esté activo, TCP 8765 esté permitido y `allow` incluya la red cliente. |
| Certificado inválido | Verifica que se instaló el `server.pem` de esa instancia. Si se regeneró, distribuye su nueva copia pública por un canal confiable. No uses el certificado de otra instancia. |
| Cuenta pendiente/rechazada | Consulta `list` o el panel; aprueba si corresponde y revisa el límite de diez cuentas aprobadas. |
| No llega un mensaje privado | El destinatario debe estar conectado: no hay buzón offline. Verifica que seleccionaste la identidad correcta. |
| TCP funciona, pero no hay descubrimiento | Revisa disponibilidad de UDP 8764 y reglas de firewall; el descubrimiento puede fallar aunque TCP esté activo. |
| El panel no muestra el servicio | Usa el modo Go (`--go`) para `chat-lan-go.service` y revisa el estado de la unidad del usuario de escritorio. |

## Seguridad operativa

No publiques `server.key`, `identity.key` ni bases con conversaciones. Cambiar el certificado TLS requiere actualizar la confianza de clientes. Revocar bloquea el acceso futuro de una cuenta; desconectar termina su sesión actual.
