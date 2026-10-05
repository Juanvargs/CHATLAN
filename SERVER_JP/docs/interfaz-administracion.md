# Guía de la interfaz de administración de Chat LAN Go

Este documento explica la **ventana de administración del servidor**: qué muestra, para qué sirve cada botón y qué sucede después de pulsarlo. Describe la interfaz actual, escrita en C++/Qt y ejecutada en modo Go. El proceso que atiende los mensajes sigue siendo el **servidor Go**.

Para entender TLS, SQLite, los puertos y la identificación de los clientes, consulta también [Cómo funciona el servidor](servidor-explicado.md).

## 1. Abrir el panel y entender su función

En el equipo donde está instalado el paquete del servidor puedes abrir la aplicación **Administrar Chat LAN Go** desde el menú, o ejecutar:

```bash
chat-lan-admin-go
```

Si estás trabajando desde las carpetas de código fuente de este equipo:

```bash
cd /ruta/a/CHATLAN/SERVER_JP
../chat-lan-cpp/build/chat-lan-admin-gui-cpp --go
```

El panel usa dos mecanismos distintos:

1. Consulta y controla `chat-lan-go.service` con **systemd de usuario**. Así sabe si el proceso está activo y puede encenderlo, apagarlo o reiniciarlo.
2. Lee y modifica `clients.sqlite3` para mostrar usuarios y gestionar sus permisos. El servidor Go observa esos cambios y, si hace falta, actúa sobre las conexiones abiertas.

El panel **no es el servidor**. Puedes cerrar la ventana sin apagar el servicio: el chat continúa si `chat-lan-go.service` sigue activo. Para administrar la instalación necesitas abrirlo con el usuario de Linux dueño de ese servicio y de los archivos de datos.

## 2. Vista general de la ventana

La ventana tiene una cabecera, un botón principal y tres pestañas:

```text
Servidor Chat LAN
Servidor: En marcha · En línea: 2 · Autorizados: 3/10
IP local: ...
[Apagar servidor]  Estado y usuarios se actualizan automáticamente.
Mensaje de resultado de la última acción

[Usuarios] [Configuración] [Registros]
```

Los usuarios, el estado del servicio y los registros se consultan automáticamente aproximadamente cada **dos segundos**. No hay un botón «Actualizar» porque normalmente no hace falta. Si acabas de pulsar una acción, puede tardar un instante en reflejarse.

### Qué significa la cabecera

| Dato mostrado | Significado |
|---|---|
| **Servidor: En marcha** | El servicio systemd figura como activo. |
| **Servidor: Apagado** | El servicio está detenido. Ningún cliente puede usar este servidor mientras siga así. |
| **Servidor: Error** | systemd informa un fallo; la pestaña **Registros** puede mostrar la causa. |
| **En línea: N** | Número de cuentas que figuran conectadas cuando el servicio está activo. Si está apagado, el panel muestra cero. |
| **Autorizados: N/10** | Cuentas cuyo acceso está aprobado. El servidor admite hasta diez cuentas aprobadas a la vez. No significa que todas estén conectadas. |
| **IP local** | Direcciones IPv4 de las interfaces de red activas del equipo. Sirven para saber qué dirección podría usar un cliente de la LAN. |
| **Mensaje bajo el botón** | Resultado de la última acción: por ejemplo «Servidor encendido», «Acceso actualizado» o un error. |

La cabecera muestra datos de **este equipo servidor**. Si aparecen varias direcciones IP, por ejemplo una de Wi-Fi y otra de VPN, la dirección útil para un cliente depende de la red a la que pertenezca ese cliente.

## 3. Botón principal: Encender servidor / Apagar servidor

Es **un mismo botón que cambia de texto** según el estado del servicio.

### Encender servidor

Cuando el servidor está apagado, el botón dice **Encender servidor**. Al pulsarlo, el panel pide a systemd que inicie `chat-lan-go.service`. El proceso Go carga su certificado y la base SQLite y comienza a escuchar normalmente en TCP 8765 y UDP 8764, salvo que esos puertos se hayan cambiado.

**Cuándo usarlo:** al comenzar a prestar el servicio, después de haberlo apagado, o después de guardar configuración con el servidor apagado.

**Qué debes comprobar después:** que la cabecera diga «En marcha». Si no inicia, entra en **Registros**. Entre las causas posibles están un puerto ocupado, configuración inválida o archivos de certificado incompletos.

### Apagar servidor

Cuando está activo, el mismo botón dice **Apagar servidor**. Al pulsarlo, el panel pide a systemd que detenga el proceso Go. Si el panel detecta usuarios conectados, **solicita confirmación** porque sus conexiones se cerrarán.

**Efecto:** se interrumpe el chat hasta que lo vuelvas a encender. Las cuentas, los mensajes ya guardados y el certificado permanecen en los archivos del servidor. Apagarlo no elimina usuarios ni borra el historial.

**Diferencia con cerrar la ventana:** cerrar el panel solo cierra la interfaz. **Apagar servidor** detiene el servicio de red.

## 4. Pestaña Usuarios

La tabla muestra una fila por cuenta registrada. Selecciona una fila antes de usar los botones de esta pestaña. El panel muestra los siguientes datos:

| Columna | Qué indica |
|---|---|
| **Código** | Número corto de la cuenta, presentado con tres cifras, por ejemplo `001`. No es una contraseña. |
| **Usuario** | Nombre guardado para mostrar a esa persona. |
| **Acceso** | `Autorizado`, `Pendiente` o `Revocado`. Es el permiso de esa cuenta para entrar. |
| **Conexión** | `En línea` o `Desconectado` en ese momento. Es distinto del permiso de acceso. |
| **Último cambio** | Fecha local del último cambio registrado de conexión. No es un historial de todas las sesiones ni una fecha de lectura de mensajes. |

Al situar el puntero sobre una celda, la ayuda puede mostrar el **UUID** interno de la cuenta. Ese ID distingue cuentas incluso si dos usuarios comparten el mismo nombre. La tabla es de consulta; no se cambian nombres ni códigos escribiendo en las celdas.

### Aprobar usuario

Cuando seleccionas una cuenta **Pendiente**, el botón de acceso dice **Aprobar usuario**. Al pulsarlo, su estado cambia a `approved`. La persona podrá conectarse al chat cuando vuelva a intentar la conexión.

Una cuenta pendiente puede aparecer cuando está desactivada la aprobación automática o cuando ya se alcanzó el límite de diez autorizados. Si ya hay diez cuentas autorizadas, el intento de aprobar otra muestra un error; primero habría que revocar o eliminar una cuenta que ya no debe ocupar ese cupo.

**Ejemplo:** llega una solicitud de Marta. Ves «Pendiente», seleccionas su fila y pulsas **Aprobar usuario**. Marta vuelve a pulsar **Conectar** en su cliente y entra con su mismo ID y código.

### Revocar acceso

Cuando seleccionas una cuenta **Autorizada**, ese mismo botón cambia a **Revocar acceso**. Al pulsarlo, la cuenta pasa a `revoked`: conserva su registro e identificación, pero ya no puede entrar al chat.

Si esa persona está conectada, el servidor detecta el cambio y cierra su sesión en unos segundos. Revocar es apropiado cuando quieres **retirar el permiso**, no solo terminar la conexión actual. Sus mensajes ya guardados no se borran por esta acción.

### Reactivar acceso

Cuando seleccionas una cuenta **Revocada**, el botón dice **Reactivar acceso**. Al pulsarlo, vuelve a `approved`, siempre que haya espacio dentro del límite de diez cuentas autorizadas. La persona puede volver a conectarse con el mismo perfil y la misma identidad.

Reactivar no crea una cuenta nueva, no cambia el código y no recupera una cuenta que ya fue **eliminada**. Sirve únicamente para volver a permitir una identidad que seguía registrada como revocada.

### Desconectar

El botón **Desconectar** se habilita cuando la fila seleccionada figura **En línea**. Solicita al servidor que cierre **la sesión actual** de ese usuario. El servidor revisa esa solicitud y le envía el aviso de desconexión administrativa en unos segundos.

La cuenta **sigue autorizada**. El cliente detenido por esta acción no se reconecta automáticamente en esa sesión, pero la persona puede volver a pulsar **Conectar** más tarde. Usa este botón si quieres terminar una conexión concreta sin quitarle el permiso futuro.

Si el usuario ya está desconectado, el botón se deshabilita: no hay sesión activa que cerrar.

### Eliminar cuenta

El botón **Eliminar cuenta** abre primero una **confirmación**. Si aceptas, elimina la identidad y sus mensajes guardados asociados: tanto los enviados por ella al chat general como los privados que envió o recibió.

Si la cuenta estaba conectada, el panel deja una solicitud de eliminación para que el servidor avise al cliente, cierre la conexión y complete el borrado. Un cliente actualizado que reciba el aviso limpia sus archivos de perfil. Si el usuario estaba fuera de línea, la eliminación de la base puede hacerse directamente. En ambos casos la clave pública antigua queda marcada como eliminada para que no vuelva a registrarse automáticamente.

**Úsalo solo cuando realmente quieras retirar esa cuenta y sus mensajes.** Si solo necesitas cortar su conexión actual, usa **Desconectar**; si necesitas impedirle entrar conservando su registro y mensajes, usa **Revocar acceso**.

### Comparación de las tres acciones que más se confunden

| Acción | Cierra la sesión actual | ¿Puede volver a entrar con esa cuenta? | ¿Borra la cuenta y sus mensajes asociados? |
|---|---|---|---|
| **Desconectar** | Sí, en unos segundos. | Sí, al conectar de nuevo. | No. |
| **Revocar acceso** | Sí, si estaba conectado. | No, hasta que se reactive. | No. |
| **Eliminar cuenta** | Sí, si estaba conectado. | No con esa identidad eliminada. | Sí. |

## 5. Pestaña Configuración

Esta pestaña contiene los campos con los que se configura el proceso Go y dos botones: **Guardar y aplicar** y **Guardar copia de seguridad**. Los cambios escritos en los campos no se aplican hasta pulsar **Guardar y aplicar**.

### Dirección de escucha

Indica en qué dirección IPv4 escucha el servidor. El valor habitual es `0.0.0.0`, que significa «en todas las interfaces IPv4 del equipo». Si pones `127.0.0.1`, solo el propio equipo podrá conectarse; otros computadores de la LAN no podrán hacerlo. Si pones una IP concreta de la LAN, el servidor escuchará en esa dirección.

La interfaz exige una dirección IPv4 válida. Esta dirección **no es el nombre de un usuario** ni el certificado; es la dirección local donde el proceso abre sus puertos.

### Puerto TCP/TLS

Es el puerto de la conexión permanente de chat cifrada con TLS. El valor inicial es **8765**. Si lo cambias, los clientes deben usar el nuevo puerto: el descubrimiento UDP puede anunciarlo, pero los clientes con una configuración fija también deben ser compatibles con el cambio. El firewall debe permitir el puerto nuevo.

### Descubrimiento UDP

Es el puerto al que los clientes envían la búsqueda automática del servidor. El valor inicial es **8764**. Si lo cambias solo en el servidor, los clientes que sigan buscando por 8764 dejarán de encontrarlo. Esto afecta la búsqueda; la conexión TCP puede seguir funcionando si el cliente conoce manualmente la IP y el puerto correctos.

### Subredes permitidas (auto o CIDR)

Este campo determina desde qué direcciones IP acepta el servidor conexiones TCP. Con **`auto`**, admite el propio equipo y la subred de su interfaz de ruta principal. También puedes escribir una o varias redes en formato CIDR, separadas por comas, por ejemplo `192.168.1.0/24`.

Si restringes este campo demasiado, clientes válidos pueden ser rechazados aunque conozcan la IP y tengan el certificado correcto. El firewall del sistema operativo es un filtro aparte que también debe permitir la conexión.

### Aprobar automáticamente hasta diez cuentas

Esta casilla controla qué ocurre cuando aparece una **identidad nueva**. Si está marcada y aún hay menos de diez cuentas aprobadas, el servidor la aprueba al registrarla. Si no está marcada, queda **Pendiente** y debes usar **Aprobar usuario**.

Si el límite de diez ya está completo, una identidad nueva queda pendiente incluso con la casilla marcada. Cambiar esta opción no convierte inmediatamente todas las cuentas pendientes en aprobadas; el servidor puede reconsiderar una pendiente cuando esa identidad vuelve a intentar conectarse y existe cupo.

### Guardar y aplicar

Este botón valida la dirección IPv4 y las subredes, y escribe los valores en `settings.json` dentro de la carpeta de datos del servidor.

- **Si el servicio está encendido**, el panel ordena reiniciarlo para que lea la nueva configuración. Las conexiones actuales se cortan y los clientes normalmente intentan reconectarse.
- **Si está apagado**, solo guarda el archivo. Los ajustes entran en vigor cuando pulses **Encender servidor**.

Si el servicio no logra arrancar después del cambio, consulta **Registros**. Un puerto ocupado o una IP que no pertenece al equipo pueden impedir el inicio. La casilla de aprobación automática y los puertos solo afectan al servidor cuando este ha leído los valores nuevos.

### Guardar copia de seguridad

Abre un diálogo para escoger dónde guardar una **copia nueva de `clients.sqlite3`**. La copia incluye cuentas y mensajes tal como estaban en ese momento. El destino debe ser un archivo nuevo; el panel no sobreescribe una copia existente.

**La copia SQLite no incluye `server.pem` ni `server.key`.** Para restaurar el servidor con la misma identidad TLS y que los clientes sigan confiando en él, conserva también esos dos archivos por separado y protege especialmente `server.key`. El botón no exporta un paquete completo para clientes.

## 6. Pestaña Registros

Muestra la salida reciente de `journalctl` para el servicio `chat-lan-go.service`: aproximadamente las **40 entradas más recientes**. El texto se actualiza automáticamente y es de solo lectura.

Sirve para investigar problemas concretos, por ejemplo si el servicio no arranca, el puerto TCP está ocupado o el descubrimiento UDP no puede abrirse. Esta pestaña muestra registros del **servidor**; no es una lista de mensajes de chat ni un historial completo de todas las conexiones de usuarios.

## 7. Tres usos habituales, paso a paso

### Admitir a un usuario pendiente

1. Abre **Usuarios** y localiza la fila con acceso **Pendiente**.
2. Selecciónala y pulsa **Aprobar usuario**.
3. Comprueba que cambia a **Autorizado**. Pide a la persona que vuelva a pulsar **Conectar**.

### Cortar una sesión sin prohibir el acceso futuro

1. En **Usuarios**, selecciona una fila que diga **En línea**.
2. Pulsa **Desconectar**.
3. Espera unos segundos. La columna **Conexión** pasará a **Desconectado**; la columna **Acceso** seguirá en **Autorizado**.

### Cambiar la red del servidor

1. En **Configuración**, revisa **Dirección de escucha**, **Puerto TCP/TLS**, **Descubrimiento UDP** y **Subredes permitidas**.
2. Pulsa **Guardar y aplicar**. Si el servidor estaba activo, se reiniciará.
3. Comprueba en la cabecera que vuelve a **En marcha**, revisa **Registros** si aparece un error y prueba la conexión desde un cliente de la LAN. Actualiza los clientes y el firewall si cambiaste puertos.

## 8. Dónde está implementada la interfaz

- [Código de la ventana y sus botones](../../chat-lan-cpp/src/admin/gui.cpp).
- [Operaciones de usuarios y copia SQLite](../../chat-lan-cpp/src/common/store.cpp).
- [Servidor Go que aplica cambios de estado y desconexión](../server.go).
- [Guía de instalación y entrega](entrega-go-cpp.md).
