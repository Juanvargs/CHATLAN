# Cómo funciona el servidor de Chat LAN

Este documento describe **la implementación actual**: servidor en Go, cliente en C++/Qt y datos en SQLite. Está escrito para entender qué hace cada pieza y seguir una conexión desde el inicio hasta el envío de mensajes.

> **Puertos correctos:** el descubrimiento usa **UDP 8764** y el chat usa **TCP 8765 con TLS**. UDP 8765 no es el valor predeterminado de este proyecto. Ambos puertos se pueden cambiar en la configuración del servidor.

## 1. Mapa rápido

```text
Cliente C++/Qt
   │
   ├── UDP 8764: busca al servidor dentro de la red local
   │
   └── TCP 8765 + TLS: se identifica, envía y recibe mensajes
                              │
                         Servidor Go
                              │
                    SQLite: usuarios y mensajes
```

El servidor escucha en todas las interfaces IPv4 (`0.0.0.0`) de forma predeterminada, pero solo admite conexiones TCP desde la red local que identifica con la ruta de red principal, o desde el propio equipo. Se pueden configurar otras subredes permitidas.

## 2. Conceptos usados

### Red

| Concepto | Explicación sencilla | Uso aquí |
|---|---|---|
| **LAN** | Red local: equipos conectados a la misma red doméstica, escolar o de oficina. | Los clientes y el servidor deben poder comunicarse por esa red. |
| **Dirección IP** | Número que identifica un equipo dentro de la red, por ejemplo `192.168.1.10`. | El cliente se conecta a la IP del computador donde corre Go. |
| **Puerto** | Número que indica a qué programa debe llegar una conexión en un equipo. | El servidor usa 8764 para búsqueda y 8765 para chat, salvo que se configuren otros. |
| **UDP 8764** | Canal de datagramas: se envía una pregunta y se espera una respuesta breve, sin mantener una conexión. | El cliente pregunta dónde está el servidor. El servidor responde con su puerto TCP y la huella de su certificado. |
| **TCP 8765** | Canal que mantiene una conexión y entrega los datos en orden. | Lleva la autenticación, los mensajes, el historial y los avisos de usuarios conectados. |
| **Broadcast** | Envío a la red local para que responda quien reconozca la pregunta. | El cliente busca automáticamente el servidor por UDP; también prueba el propio equipo. |
| **Firewall** | Filtro del sistema operativo para conexiones de red. | Si bloquea UDP 8764, falla la búsqueda automática; si bloquea TCP 8765, falla la conexión de chat. |
| **Subred/CIDR** | Forma de describir un grupo de direcciones IP, por ejemplo `192.168.1.0/24`. | La opción `allow` controla qué direcciones pueden abrir una conexión TCP. |

### Seguridad e identidad

| Concepto | Explicación sencilla | Uso aquí |
|---|---|---|
| **TLS** | Protección de la conexión: cifra los datos durante el viaje y permite verificar al servidor. | Se aplica sobre TCP 8765. El servidor acepta TLS 1.2 o superior. |
| **Certificado TLS** | Documento digital que contiene la clave pública del servidor y permite comprobar su identidad. | Se guarda como `server.pem` y se entrega una copia a los clientes. |
| **Certificado autofirmado** | Certificado creado por el propio servidor, sin una entidad externa que lo firme. | El cliente confía expresamente en el certificado incluido en su paquete; al crearlo se le da una validez de 365 días. |
| **Clave privada TLS** | Secreto con el que el servidor demuestra que el certificado es suyo. | Está en `server.key`; debe permanecer solo en el computador servidor. |
| **ECDSA P-256** | Algoritmo y curva usados para la clave del certificado TLS del servidor. | Esta clave es distinta de las claves Ed25519 de los usuarios. |
| **Huella SHA-256** | Resumen de 64 caracteres hexadecimales calculado a partir del certificado. | El cliente comprueba que el servidor presenta exactamente el certificado esperado. |
| **Ed25519** | Algoritmo de firma digital. Una clave privada firma y su clave pública permite verificar la firma. | Cada cliente tiene su propio par de claves para demostrar qué identidad posee. |
| **`identity.key`** | Archivo que contiene la clave privada Ed25519 del cliente. | Se crea en el perfil del cliente y no se envía al servidor. Si se pierde, el cliente ya no puede demostrar esa misma identidad sin recuperación administrativa. |
| **Reto o *nonce*** | Número aleatorio nuevo para cada intento de conexión. | El servidor lo envía; el cliente lo firma. Así demuestra que tiene la clave privada sin enviarla. |
| **Base64** | Forma de representar bytes como texto. **No cifra** los datos. | Se usa para enviar claves públicas, retos y firmas dentro de JSON. |
| **UUID** | Identificador largo y prácticamente único, como `xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`. | El servidor asigna uno a cada usuario; el cliente genera uno para cada mensaje. |

**Dos claves que no hay que confundir:** `server.key` es la clave privada TLS del servidor; `identity.key` es la clave privada Ed25519 de un cliente. Copiar `server.pem` al cliente es normal. Copiar cualquiera de esas claves privadas a otros usuarios comprometería la identidad correspondiente.

**Alcance del cifrado:** TLS protege el trayecto entre cada cliente y el servidor. El servidor recibe el texto, lo guarda en SQLite y lo reenvía. Por tanto, **no hay cifrado de extremo a extremo** entre los usuarios.

### Datos y ejecución

| Concepto | Explicación sencilla | Uso aquí |
|---|---|---|
| **Go** | Lenguaje con el que está escrito el servidor. | Atiende las conexiones, comprueba las identidades y distribuye los mensajes. |
| **C++/Qt** | Lenguaje y biblioteca de la aplicación de escritorio del cliente. | Muestra la interfaz y se comunica con el servidor. |
| **SQLite** | Base de datos guardada en un archivo local; no requiere instalar otro proceso de base de datos. | `clients.sqlite3` almacena usuarios y mensajes. |
| **JSON** | Texto estructurado en campos, por ejemplo `{"v":1,"type":"ping"}`. | Es el formato de los eventos del protocolo. |
| **Protocolo versión 1** | Conjunto de reglas que cliente y servidor entienden. | Cada evento lleva `v: 1` y un campo `type`, como `hello`, `message` o `presence`. |
| **Trama** | Paquete completo dentro de la conexión TCP. | Primero se envían 4 bytes con la longitud y luego el JSON; el máximo es 65 536 bytes. |
| **systemd de usuario** | Administrador de servicios de Linux para la sesión del usuario. | Puede iniciar y mantener activo `chat-lan-go.service`. |
| **Copia de seguridad** | Copia de los datos para recuperarlos si falla el equipo. | El comando `backup` guarda una copia de la base de datos SQLite; el certificado y `server.key` se respaldan aparte. |

### 2.1. Qué significa «cliente-servidor» en este proyecto

Hay un programa central, el **servidor Go**, y varias aplicaciones **cliente C++/Qt**. Los clientes no intercambian mensajes directamente entre sí. Cada cliente habla con el servidor; este comprueba quién es, guarda los mensajes y decide a quién enviarlos. SQLite tampoco es un servidor aparte: es un archivo que el programa Go lee y modifica.

Imagina que Ana escribe a Carlos. El camino real es **Ana → servidor → Carlos**. El servidor necesita conocer la identidad de Ana y el destino Carlos para hacer esa entrega. Si el computador servidor se apaga, los clientes dejan de poder hablar por esta aplicación, aunque sigan conectados a la misma red Wi-Fi.

La interfaz gráfica del cliente y el panel de administración cumplen tareas diferentes. El **cliente** permite conversar. El **panel** permite gestionar el servicio y las cuentas desde el equipo servidor. Ninguno reemplaza al proceso Go que atiende la red.

### 2.2. IP, puerto y dirección de escucha

Una **dirección IP** señala un computador dentro de la red. Un **puerto** señala un servicio dentro de ese computador. Si la IP del servidor fuera `192.168.1.10`, `192.168.1.10:8765` significaría «hablar con el servicio de chat en ese equipo». El número 8765 no tiene propiedades especiales: es el valor elegido para esta aplicación y se puede configurar.

El servidor escucha de forma predeterminada en `0.0.0.0`. Eso quiere decir **todas las interfaces IPv4 disponibles en el servidor**; `0.0.0.0` no es una dirección que el usuario deba escribir como destino. El cliente usa la IP concreta del servidor que encontró o que se configuró manualmente.

El servidor aplica además una segunda decisión: acepta por defecto conexiones locales y conexiones de la subred de la interfaz que tiene la ruta principal de salida. Una subred escrita como `192.168.1.0/24` suele cubrir `192.168.1.1` hasta `192.168.1.254`. Se pueden indicar subredes explícitas con `allow`. El firewall del sistema operativo es otro filtro independiente: aunque la aplicación admita una IP, el firewall puede impedir que llegue a su puerto.

### 2.3. UDP 8764: encontrar al servidor

**UDP** permite enviar un datagrama, es decir, un mensaje breve e independiente. No establece una conversación permanente ni garantiza por sí mismo que la respuesta llegue. Es apropiado aquí porque el cliente solo necesita preguntar «¿hay un servidor Chat LAN?».

Cuando no tiene una IP fija, el cliente envía el texto `CHAT_LAN_DISCOVER_V1` al puerto **UDP 8764** mediante broadcast local y también a `localhost`. Si el servidor recibe exactamente esa pregunta, responde con un JSON que contiene el nombre del servicio, el puerto de chat y la huella de su certificado. Por ejemplo:

```json
{"service":"chat-lan","port":8765,"fingerprint":"...huella SHA-256..."}
```

Esa respuesta **no transporta mensajes de chat ni contraseñas**. Solo permite encontrar la dirección IP de quien respondió y saber a qué puerto TCP intentar conectarse. El cliente descarta las respuestas cuya huella no coincide con el certificado que trae instalado. Después vuelve a comprobar el certificado durante TLS; una respuesta UDP por sí sola no autentica una sesión.

El broadcast normalmente se queda dentro de la red local. Algunas redes lo bloquean o separan unos equipos de otros; por eso el cliente también puede usar una IP inicial configurada. Si UDP 8764 falla, el proceso Go puede seguir atendiendo por TCP 8765, pero la búsqueda automática deja de funcionar.

### 2.4. TCP 8765: mantener la conversación

**TCP** abre una conexión duradera entre cliente y servidor. Entrega los bytes en orden y detecta pérdidas para retransmitirlos. Eso facilita enviar `hello`, el reto de autenticación, mensajes, presencia e historial sin que lleguen mezclados. El servidor escucha en **TCP 8765** de forma predeterminada.

TCP transporta una secuencia de bytes, **no separa automáticamente los mensajes de la aplicación**. Por eso este proyecto añade su propio formato de tramas: delante de cada JSON pone cuatro bytes que indican cuánto mide. El receptor lee primero esa longitud y luego exactamente esa cantidad de bytes. Si no se hiciera, dos mensajes enviados seguidos podrían parecer uno solo, o uno grande podría llegar en varias lecturas.

El servidor admite hasta 30 conexiones TCP simultáneas. Una conexión TCP abierta aún no equivale a un usuario autenticado: primero debe superar TLS y demostrar su identidad Ed25519.

### 2.5. TLS: proteger el trayecto e identificar al servidor

**TLS** es la capa de seguridad que se establece *encima de TCP*. Antes de enviar datos del chat, cliente y servidor negocian una conexión cifrada. Después, alguien que observe la red local no debería poder leer el contenido de esa conexión ni modificarlo sin que se detecte. El código exige TLS 1.2 o una versión posterior.

TLS también ayuda a contestar «¿estoy hablando con el servidor correcto?». Para eso el servidor presenta un **certificado X.509**. Este certificado contiene, entre otras cosas, una clave pública. El servidor guarda la clave privada correspondiente en `server.key` y la usa para demostrar que realmente controla ese certificado. `server.pem` es el certificado codificado en formato PEM, un formato de texto habitual para certificados. La clave del certificado se genera con **ECDSA P-256**, un algoritmo de firma digital basado en curvas elípticas.

El certificado es **autofirmado**: se crea en el propio servidor, sin pedirlo a una autoridad pública. Esto sirve en una LAN, pero obliga a distribuir a los clientes la copia correcta de `server.pem`. El cliente la instala como certificado de confianza, verifica la negociación TLS y comprueba además su huella. El certificado que crea el servidor vale 365 días desde su generación; la fecha y hora del equipo influyen en la validación.

En este cliente, el nombre que se verifica dentro del certificado es `localhost`; la **huella exacta** confirma cuál certificado concreto se aceptó. Esta combinación permite que la IP del servidor cambie sin tener que emitir un certificado distinto por cada IP, siempre que el cliente lleve el mismo certificado. La IP encontrada por UDP sirve para llegar al equipo, mientras que el certificado y su huella sirven para reconocerlo.

La **huella SHA-256** es un resumen calculado sobre los bytes del certificado. Si cambia el certificado, cambia su huella con altísima probabilidad. La huella es útil para comparar, pero no sustituye por sí sola la validación TLS: aquí se usan ambas. Si se regenera `server.pem`, hay que entregar el nuevo certificado a todos los clientes; conservar la huella vieja hará fallar la conexión.

**Límite de esta protección:** TLS cifra el tramo Ana-servidor y, por separado, el tramo servidor-Carlos. El servidor descifra el contenido para procesarlo y lo guarda como texto en SQLite. No existe cifrado de extremo a extremo entre Ana y Carlos en esta versión. Por eso quien tenga acceso autorizado al archivo de base de datos del servidor puede consultar los mensajes.

### 2.6. Ed25519: demostrar quién es un cliente

**Ed25519** es otro algoritmo de firma digital. Cada perfil del cliente tiene un **par de claves**: una privada en `identity.key`, que debe permanecer en ese equipo, y una pública que se puede compartir. Firmar con la privada permite que cualquier servidor que tenga la pública compruebe que la firma corresponde a ese par.

La clave TLS del servidor y la clave Ed25519 del cliente responden a preguntas distintas:

| Pregunta | Mecanismo | Quién conserva el secreto |
|---|---|---|
| «¿Este es el servidor correcto?» | Certificado TLS ECDSA P-256, verificación y huella SHA-256. | El servidor conserva `server.key`. |
| «¿Este cliente posee la identidad registrada?» | Firma Ed25519 de un reto. | Cada cliente conserva su `identity.key`. |

El servidor **no pide ni almacena la clave privada del cliente**. Guarda su clave pública en SQLite. Cuando el cliente vuelve, presenta esa misma clave pública y demuestra posesión de la privada. El nombre escrito en la interfaz es solo una etiqueta visible; dos personas podrían escribir el mismo nombre, pero sus claves públicas serían diferentes.

Esta autenticación ocurre **dentro de TLS** y forma parte del protocolo de Chat LAN; no es un certificado TLS instalado en cada cliente. Es importante distinguirlo si te preguntan cómo se autentica cada lado.

### 2.7. Reto, *nonce* y firma: por qué no basta con enviar la clave pública

Una clave pública no es secreta. Si el cliente solo enviara su clave pública, otro equipo podría copiarla y afirmar que es la misma persona. Por eso el servidor envía un **reto aleatorio nuevo**, llamado *nonce*, de 32 bytes. El cliente firma una combinación de ese reto, su clave pública y la huella del servidor. El servidor comprueba la firma usando la clave pública que recibió.

El contenido firmado se construye de forma precisa: los bytes `chat-lan/auth/v1`, un byte cero separador, los 32 bytes del reto, los 32 bytes de la clave pública y los 32 bytes de la huella. El prefijo identifica el propósito de la firma. Incluir el reto evita reutilizar una firma capturada en una conexión anterior; incluir la huella la vincula al certificado de este servidor. La firma resultante tiene 64 bytes.

El reto, la clave pública y la firma viajan dentro de JSON representados en **Base64**. Base64 solo transforma bytes en caracteres que se pueden escribir en JSON. Cualquier persona que vea un valor Base64 puede decodificarlo; **Base64 no es cifrado**. La confidencialidad durante el viaje proviene de TLS.

### 2.8. JSON, versión y tramas: cómo se entienden Go y C++

Aunque uno está escrito en Go y otro en C++, ambos pueden comunicarse porque siguen **el mismo protocolo**: reglas sobre los campos, el orden y el significado de los eventos. No intercambian objetos internos de Go o C++; intercambian JSON codificado en UTF-8.

Un evento pequeño tiene esta forma:

```json
{"v":1,"type":"ping"}
```

`v: 1` declara la versión del protocolo; `type` dice qué clase de evento es. Otros tipos son `hello`, `challenge`, `proof`, `authenticated`, `presence`, `send_message`, `message`, `received`, `accepted`, `delivered`, `pong` y `error`. Si llega una versión incompatible o un tipo inválido, el servidor rechaza el paquete.

Cada JSON viaja en una **trama** con este esquema:

```text
[4 bytes: longitud del JSON en orden big-endian][JSON UTF-8]
```

*Big-endian* significa que el byte más significativo va primero. El máximo permitido para el cuerpo JSON es **65 536 bytes**. Este límite no es el mismo que el de **2 000 caracteres** para el texto de un mensaje: el primero protege el protocolo completo; el segundo limita el contenido escrito por un usuario.

### 2.9. SQLite: qué persiste y qué se pierde al cerrar

**SQLite** organiza datos en tablas dentro de `clients.sqlite3`. No hay que levantar un servicio adicional de base de datos. El servidor Go abre ese archivo y ejecuta consultas para buscar usuarios, cambiar estados y guardar mensajes. Si se reinicia el servidor, las cuentas y los mensajes siguen ahí porque están en disco.

Las tablas principales son:

| Tabla | Datos que guarda | Para qué se consultan |
|---|---|---|
| `clients` | Código numérico, UUID, clave pública, nombre, estado, indicador en línea y último cambio de conexión. | Reconocer a un cliente que vuelve y controlar si puede entrar. |
| `messages` | ID del mensaje, remitente, destino, texto y fecha. | Formar el historial de chats. |
| `deleted_clients` | Clave pública y datos de cuentas borradas. | Impedir que la misma clave eliminada se registre de nuevo como si fuera nueva. |

La base conserva mensajes, pero **no conserva una conexión de red**. Después de un reinicio, el servidor marca a todos como desconectados y espera a que sus aplicaciones se conecten otra vez. El historial que envía al entrar se limita a los últimos 200 mensajes relevantes para esa cuenta; no significa que SQLite borre automáticamente los anteriores.

La base de datos contiene el texto de los mensajes sin cifrado de extremo a extremo. El comando `backup` hace una copia de la base, pero para migrar o restaurar completamente la identidad del servidor también hay que conservar `server.pem` y `server.key` de forma segura.

### 2.10. UUID, código numérico, nombre y estado: cuatro cosas distintas

El **UUID del usuario** es un identificador interno de 128 bits generado por el servidor cuando aparece una clave pública nueva. Permite dirigir privados sin depender de nombres repetibles. El **código** (`001`, `002`, etc.) es un número asignado por SQLite para mostrar una referencia corta. El **nombre** lo propone el usuario, sirve para mostrarlo y no prueba su identidad. La **clave pública Ed25519** es el dato que permite encontrar la cuenta existente y comprobar la firma.

Un ejemplo útil para recordar la diferencia:

```text
Nombre visible: Ana
Código mostrado: 001
UUID interno:   123e4567-e89b-42d3-a456-426614174000  (ejemplo ilustrativo)
Clave pública:  valor Ed25519 en Base64 guardado por el servidor
```

Si Ana cambia de computador y crea otra `identity.key`, aunque escriba el mismo nombre, el servidor ve una clave nueva y abre otra solicitud de registro. Para conservar la identidad necesita conservar su clave privada original o realizar un procedimiento administrativo de recuperación. Si una cuenta está `pending`, aún no puede chatear; si está `approved`, puede entrar; si está `revoked`, se rechaza. Una cuenta eliminada se registra aparte como `deleted` para bloquear su antigua clave pública.

Cada **mensaje** también tiene su propio UUID, pero ese ID lo crea el cliente para distinguir un envío de otro. Por tanto, «ID del usuario» e «ID del mensaje» tienen el mismo formato general, aunque representan cosas diferentes y los genera un componente distinto.

### 2.11. Presencia, confirmaciones y conexión viva

**Presencia** significa saber quién está conectado ahora. Después de autenticar a alguien o detectar su salida, el servidor envía una lista `presence` actualizada. Eso permite a la interfaz mostrar usuarios disponibles y al servidor decidir si puede entregar un privado en ese momento.

Hay tres señales de mensaje que conviene explicar con cuidado. `accepted` indica que el **servidor** aceptó y guardó el mensaje. `received` lo envía la **aplicación destinataria** cuando recibe el evento. `delivered` lo envía después el servidor al remitente para avisarle de ese `received`. Ninguna de las tres demuestra que un ser humano haya leído el texto. Tampoco hay una cola de privados para destinatarios desconectados: el servidor rechaza ese envío.

Para saber que la conexión sigue viva, el cliente manda `ping` aproximadamente cada 20 segundos y el servidor responde `pong`. Si el cliente deja de recibir datos o se corta la red, intenta conectarse de nuevo con pausas crecientes. El servidor, por su parte, cierra sesiones inactivas según sus tiempos de espera y vuelve a comprobar periódicamente si el administrador revocó o desconectó una cuenta.

### 2.12. Servicio systemd, configuración y separación de archivos

**systemd** puede ejecutar el servidor como servicio de usuario llamado `chat-lan-go.service`. Esto permite iniciarlo, detenerlo, reiniciarlo y consultar si está activo sin abrir manualmente una terminal que quede ocupada. El servicio es quien pone el programa Go a escuchar en la red; la ventana de administración es una interfaz para controlarlo y gestionar cuentas.

El archivo `settings.json` contiene ajustes opcionales: IP donde escuchar, puerto TCP, puerto UDP, subredes permitidas y aprobación automática. Si no se indican, se usan los valores predeterminados. Las opciones de línea de comandos que se pasen explícitamente tienen prioridad sobre esos ajustes. Cambiar un puerto en el servidor también exige que los clientes busquen o se conecten al puerto correcto.

La carpeta `SERVER_JP` contiene el **proyecto y el ejecutable**. Los datos de ejecución viven normalmente en `~/.local/share/chat-lan-go/server/`: base SQLite, certificado, clave y ajustes. Esta separación explica por qué cambiar el nombre de la carpeta del proyecto no cambia los usuarios ni sus mensajes guardados.

## 3. Qué guarda cada archivo

La carpeta del **código fuente** es `SERVER_JP`. De forma predeterminada, los **datos en funcionamiento** se guardan aparte, en `~/.local/share/chat-lan-go/server/` (o en `$XDG_DATA_HOME/chat-lan-go/server/` si esa variable está definida). Cambiar el nombre de `SERVER_JP` no mueve esos datos.

| Archivo | Contenido |
|---|---|
| `server.pem` | Certificado público TLS. El cliente necesita una copia de este mismo certificado. |
| `server.key` | Clave privada TLS. Solo debe estar en el servidor. |
| `clients.sqlite3` | Usuarios registrados, estados, mensajes e información de conexión. |
| `settings.json` | Ajustes opcionales, como dirección de escucha, puertos, subredes permitidas y aprobación automática. |

Cada cliente guarda por separado su perfil y su `identity.key`. La clave privada del cliente no está en `clients.sqlite3`; allí se guarda su **clave pública**.

## 4. Funcionamiento completo, paso a paso

### 4.1. Arranque del servidor

1. El servidor lee `settings.json` si existe. De forma predeterminada usa TCP 8765, UDP 8764 y aprobación automática. Las opciones dadas al ejecutar el programa tienen prioridad sobre la configuración del archivo.
2. Carga `server.pem` y `server.key`. Con `server --init`, los crea si faltan **ambos**; no sustituye automáticamente un par existente.
3. Abre `clients.sqlite3` y marca a todos los usuarios como desconectados, porque las conexiones anteriores no sobreviven a un reinicio.
4. Abre TCP 8765. También intenta abrir UDP 8764. Si UDP no está disponible, el chat por TCP puede seguir funcionando, aunque falle la búsqueda automática.
5. Si el servidor está gestionado por systemd, el servicio `chat-lan-go.service` ejecuta el proceso y permite ver su estado y registros.

### 4.2. Primera apertura del cliente y búsqueda

1. Al usar por primera vez un perfil para conectarse, el cliente genera su propio par de claves Ed25519. Conserva la clave privada en `identity.key` y obtiene la clave pública correspondiente.
2. Si no tiene una dirección de servidor fijada, envía `CHAT_LAN_DISCOVER_V1` por UDP al puerto 8764 de la red local y del propio equipo.
3. El servidor responde con su puerto TCP y la huella SHA-256 de su certificado. El cliente acepta una respuesta solo si la huella coincide con la del certificado que trae instalado.
4. El cliente abre la conexión TCP al servidor en el puerto indicado, normalmente 8765. Si la búsqueda automática no atraviesa la red, se puede configurar la IP del servidor manualmente.

### 4.3. TLS: comprobar al servidor

1. Antes de intercambiar mensajes del chat, cliente y servidor establecen TLS.
2. El servidor presenta `server.pem` y demuestra que posee `server.key`.
3. El cliente verifica el certificado y su huella frente a su copia local. Si el certificado no corresponde, detiene la conexión y muestra un error de certificado o huella.

Por eso, al crear o sustituir el certificado del servidor hay que entregar a los clientes un paquete actualizado con el **nuevo `server.pem`**. El archivo `server.key` nunca va en el paquete del cliente.

### 4.4. Identificación del cliente

1. Dentro de la conexión TLS, el cliente envía `hello` con su nombre visible y su **clave pública Ed25519**.
2. El servidor devuelve un `challenge` con un reto aleatorio y la huella del certificado.
3. El cliente firma con su clave privada una combinación del reto, su clave pública y la huella; responde con `proof` y la firma.
4. El servidor verifica la firma con la clave pública recibida. Una firma válida demuestra que el cliente posee la clave privada, sin transmitirla.
5. El servidor busca esa clave pública en SQLite. Si ya existe, recupera el mismo usuario. Si es nueva, crea un registro. Una clave de una cuenta eliminada queda bloqueada hasta que se haga la gestión administrativa correspondiente.

El **nombre visible no es una contraseña ni el identificador único**. Cambiar el nombre enviado por un cliente con una clave pública ya registrada no crea otra cuenta ni cambia automáticamente el nombre guardado.

### 4.5. Cómo se asignan los identificadores

En el primer registro, el servidor asigna al usuario:

- Un **ID UUID**, que identifica internamente la cuenta y se usa para dirigir mensajes privados.
- Un **código numérico**, como `001`, generado de forma incremental por SQLite para mostrarlo de manera más sencilla. Puede haber saltos si se han eliminado cuentas. El código no sirve como contraseña.
- Un **estado**: `approved` (aprobado), `pending` (pendiente), `revoked` (revocado) o, tras eliminación, `deleted` (eliminado).

Por ejemplo, si Ana se registra por primera vez, podría recibir código `001` y un UUID. Al cerrar y volver a abrir el mismo perfil, presenta la **misma clave Ed25519** y recupera esos identificadores. Si usa otro perfil con otra clave, el servidor lo ve como una identidad nueva, aunque escriba «Ana» otra vez.

La aprobación automática está activada de forma predeterminada mientras haya menos de **10 cuentas aprobadas**. Si se desactiva o se alcanza ese límite, una cuenta nueva queda pendiente y un administrador debe aprobarla cuando haya cupo. También existe un límite de **30 solicitudes pendientes**.

### 4.6. Entrada al chat

1. Si la cuenta está pendiente, revocada o eliminada, el servidor responde con ese estado y no abre una sesión de chat.
2. Si está aprobada, responde `authenticated` con el ID, el nombre y el código.
3. Marca al usuario como conectado y le envía hasta **200 mensajes anteriores** relevantes: los del chat general y los privados que envió o recibió.
4. Envía a los clientes conectados una lista `presence` con los usuarios actualmente en línea.
5. Si la misma identidad abre otra sesión, la nueva sustituye a la anterior.

La lista de presencia representa las conexiones actuales; SQLite conserva además un indicador de conexión y la fecha del último cambio de ese indicador. El servidor reinicia esos indicadores al arrancar.

### 4.7. Envío de mensajes

1. El cliente crea un UUID para el mensaje y envía `send_message` con texto y destino. El destino es `general` para el chat común o el UUID de otro usuario para un privado.
2. El servidor verifica el formato y el contenido. Acepta texto de **1 a 2 000 caracteres** después de quitar espacios al principio y al final.
3. Un mensaje privado requiere que el destinatario esté conectado; si no, el servidor responde «Destinatario desconectado». No hay cola de entrega privada para usuarios sin conexión.
4. El servidor guarda el mensaje en SQLite y lo envía a los destinatarios conectados. En el chat general lo reciben todos los conectados, incluido el remitente; en uno privado lo reciben el destinatario y el remitente.
5. El remitente recibe `accepted` cuando el servidor aceptó el mensaje. Los clientes que reciben un mensaje responden `received`, y el servidor avisa al remitente con `delivered`.

**`accepted` significa que el servidor lo guardó y procesó. `delivered` significa que la aplicación destinataria confirmó recepción; no demuestra que la persona lo leyó.** El cliente puede reutilizar un ID de mensaje para reintentar el mismo contenido sin que el servidor lo procese dos veces durante la ventana de control de duplicados. No debe reutilizarlo con otro contenido.

### 4.8. Desconexión y administración

- El cliente envía señales `ping` periódicas; el servidor responde `pong`. Si se pierde la conexión, el cliente intenta reconectar.
- Al desconectarse un cliente, el servidor lo marca fuera de línea y actualiza la presencia para los demás.
- El administrador puede listar cuentas, aprobarlas, revocarlas, desconectarlas, eliminarlas, recuperar una identidad con otra clave pública y hacer copias de seguridad. La interfaz administrativa simplificada usa estas operaciones del servidor.
- Al eliminar una cuenta se retiran sus mensajes guardados y queda registrado que esa clave pública fue eliminada para evitar que reaparezca como cuenta nueva de forma automática.

### 4.9. Ejemplo completo: Ana se conecta y escribe a Carlos

Supongamos que el servidor está en `192.168.1.10`, Ana y Carlos están en esa LAN y ambos tienen instalado el cliente con el certificado público correcto. Los identificadores y el texto siguientes son **ilustrativos**; no son credenciales reales.

```text
Ana (C++/Qt)                  Servidor (Go + SQLite)              Carlos (C++/Qt)
     |                                |                                 |
     |--- búsqueda UDP 8764 -------->|                                 |
     |<-- puerto 8765 + huella -------|                                 |
     |=== TCP 8765 + negociación TLS =|                                 |
     |--- hello: nombre + clave ----->|                                 |
     |<-- challenge: reto ------------|                                 |
     |--- proof: firma del reto ----->|                                 |
     |<-- authenticated: ID/código ---|                                 |
     |<-- historial + presence -------|                                 |
     |                                |<==== Carlos ya está conectado ===|
     |--- send_message: a Carlos ---->|                                 |
     |                                |--- guarda el texto en SQLite    |
     |<-- message + accepted ---------|--- message -------------------->|
     |                                |<-- received --------------------|
     |<-- delivered ------------------|                                 |
```

**Qué ocurrió en cada fase:**

1. Ana preguntó por UDP quién ofrecía Chat LAN. La respuesta le dio una dirección de origen, un puerto de chat y una huella. El cliente solo la aceptó porque la huella coincidía con su certificado.
2. Ana abrió TCP 8765 y estableció TLS. Aquí comprobó el certificado presentado por el servidor; todavía no había entrado al chat.
3. Ana envió su nombre y clave pública Ed25519. El servidor creó un reto aleatorio. Ana lo firmó con `identity.key`; el servidor verificó la firma.
4. SQLite indicó que la clave de Ana ya estaba aprobada, o creó una cuenta nueva y la aprobó si había cupo y la aprobación automática estaba activada. El servidor le devolvió su UUID y código.
5. Ana recibió historial y presencia. Si Carlos aparecía conectado, pudo seleccionarlo para un privado. El destino del mensaje fue el **UUID de Carlos**, no el texto «Carlos» ni su código corto.
6. El cliente de Ana asignó un UUID a ese mensaje y lo envió. El servidor comprobó que Carlos estaba conectado, guardó el texto y lo distribuyó.
7. Ana vio `accepted` porque el servidor lo aceptó. Cuando el programa de Carlos recibió el evento, envió `received` y Ana recibió `delivered`. Esto prueba recepción por la aplicación, no lectura humana.

Si Carlos no hubiera estado conectado, el paso 6 terminaría con **«Destinatario desconectado»** y el servidor no guardaría ese privado como mensaje pendiente para entregarlo después.

## 5. Cómo explicarlo en una exposición

Puedes presentar el funcionamiento en este orden, sin empezar por los nombres de los algoritmos:

1. **Arquitectura:** «Tenemos varios clientes C++/Qt y un servidor central en Go. Los mensajes siempre pasan por el servidor, que usa SQLite para guardar cuentas e historial».
2. **Descubrimiento:** «El cliente pregunta por UDP 8764 dónde está el servidor. Recibe su dirección de origen, el puerto de chat y la huella del certificado».
3. **Conexión segura:** «Abre TCP 8765 y negocia TLS. Comprueba el certificado del servidor para no entregar mensajes a un equipo desconocido».
4. **Identidad:** «Cada cliente guarda una clave privada Ed25519. El servidor le manda un reto, el cliente lo firma y el servidor verifica la firma con la clave pública que tiene registrada».
5. **Cuenta:** «La primera vez, el servidor asigna un UUID y un código corto. En conexiones posteriores reconoce la misma clave pública y reutiliza la cuenta».
6. **Chat:** «El servidor envía historial y presencia. Al recibir un texto, verifica destino y tamaño, lo guarda y lo reenvía. Las confirmaciones distinguen aceptación del servidor y recepción de la aplicación».

### Preguntas que podrían hacerte

**¿Por qué hay dos puertos?** Porque el cliente usa un intercambio UDP breve para encontrar al servidor y una conexión TCP estable para hablar con él. En la configuración inicial son UDP 8764 y TCP 8765.

**¿Por qué se necesitan TLS y Ed25519?** TLS protege la conexión y permite verificar al servidor. Ed25519 prueba qué identidad de cliente posee la clave privada registrada. Resuelven problemas distintos.

**¿Por qué se usa un reto aleatorio?** Para que una firma anterior no sirva como prueba en una conexión nueva. Solo quien tenga la clave privada puede firmar el reto recién generado.

**¿Qué diferencia hay entre certificado, clave y huella?** El certificado contiene información pública del servidor; la clave privada permite demostrar que le pertenece; la huella es un resumen para comprobar que es exactamente el certificado esperado.

**¿Por qué no se utiliza el nombre como identificador?** Porque dos personas pueden escribir el mismo nombre y una persona puede cambiarlo. La cuenta se vincula a su clave pública; el servidor le asigna un UUID estable.

**¿Qué pasa al apagar el servidor?** Se cortan las conexiones. SQLite conserva usuarios y mensajes; al reiniciar, las sesiones se vuelven a crear y los clientes reciben el historial permitido.

**¿Los mensajes están cifrados de extremo a extremo?** No. TLS cifra cada tramo de red, pero el servidor necesita leer el contenido para guardarlo y reenviarlo. La base contiene el texto.

**¿«Entregado» significa «leído»?** No. Significa que la aplicación del destinatario respondió que recibió el evento. No hay comprobación de lectura humana.

**¿Por qué un cliente puede mostrar «certificado inválido»?** Entre las causas están llevar otro `server.pem`, una fecha u hora incorrecta, un certificado vencido o presentar el servidor un certificado distinto. Un cambio de IP, por sí solo, no exige cambiar el certificado en esta implementación.

## 6. Límites y funciones disponibles hoy

| Elemento | Situación actual |
|---|---|
| Conexiones TCP simultáneas | Hasta 30. |
| Cuentas aprobadas | Hasta 10. |
| Solicitudes pendientes | Hasta 30. |
| Historial enviado al conectar | Hasta 200 mensajes relevantes. |
| Tamaño del texto de un mensaje | 1 a 2 000 caracteres. |
| Tamaño de una trama del protocolo | Hasta 65 536 bytes de JSON. |
| Ritmo de peticiones por sesión | Hasta 50 en una ventana de 5 segundos. |
| Contenido del chat | Mensajes de texto; no hay transferencia de archivos, audio ni video en este protocolo. |
| Seguridad de mensajes | TLS cliente-servidor; el servidor puede acceder al texto almacenado. |

## 7. Comprobaciones útiles

Desde el equipo servidor:

```bash
systemctl --user status chat-lan-go.service
cd /ruta/a/CHATLAN/SERVER_JP
./chat-lan-go fingerprint
./chat-lan-go list
```

La huella mostrada por `fingerprint` debe corresponder al certificado incluido en el cliente. Si un cliente informa «certificado inválido», comprueba primero que está usando el `server.pem` del **servidor actual**, que la fecha y hora de ambos equipos son correctas y que el servicio está activo. Si no descubre el servidor, comprueba UDP 8764; si lo descubre pero no conecta, comprueba TCP 8765, el firewall y las subredes permitidas.

Para instalar y probar el cliente en otros equipos, consulta [la guía de entrega y prueba](entrega-go-cpp.md).

## 8. Dónde está implementado

- [Inicio, configuración y comandos](../main.go).
- [Certificado y TLS](../tls.go).
- [Formato de mensajes y firma de autenticación](../protocol.go).
- [Conexiones, presencia y entrega de mensajes](../server.go).
- [Registro de usuarios, SQLite e historial](../store.go).
- [Conexión del cliente C++](../../chat-lan-cpp/src/client/connection.cpp).
