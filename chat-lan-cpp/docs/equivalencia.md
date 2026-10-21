# Equivalencia con `chat-server`

Referencia histórica: implementación Python original en `chat-server/src/chat_lan/` (no incluida en este
repositorio). La configuración integrada actual combina el servidor Go de `../SERVER_JP` con el cliente y
panel C++/Qt de `src/`. Este directorio conserva además un servidor C++ alternativo. Para conocer la
arquitectura vigente, consulta [`../../docs/arquitectura.md`](../../docs/arquitectura.md); las verificaciones
indicadas abajo son el registro previo del equipo y no sustituyen la ejecución de pruebas en la versión actual.

| Función del original | Implementación C++ | Verificación |
| --- | --- | --- |
| Perfiles, identidad Ed25519, clave pública | `src/client/window.cpp`, `src/common/crypto.cpp` | `chat-core`, `chat-ui` |
| TCP/TLS, huella de certificado, desafío y firma | `src/client/connection.cpp`, `src/server/server.cpp` | `chat-core` y pruebas de compatibilidad en ambos sentidos con Python |
| Descubrimiento UDP y reconexión | `src/client/connection.cpp`, `src/server/server.cpp` | Revisado en código; falta prueba entre dos equipos físicos |
| Sala general, privados, presencia e historial | `src/server/server.cpp`, `src/common/store.cpp`, `src/client/window.cpp` | `chat-core`, `chat-ui` y compatibilidad con Python |
| Diez cuentas aprobadas, pendientes y revocadas | `src/common/store.cpp`, `src/server/server.cpp` | `chat-core` |
| Acuses `accepted` y `delivered` | `src/server/server.cpp`, `src/client/connection.cpp` | `chat-core` |
| Administración de cuentas y servicio | `src/admin/main.cpp`, `src/admin/gui.cpp` | Compilación; operaciones SQLite verificadas en `chat-core`; panel físico pendiente |
| Base SQLite, eliminación, recuperación y copia | `src/common/store.cpp` | `chat-core`, incluidas recuperación de una clave pendiente, copia y borrado pendiente |
| Cliente y panel de escritorio | `src/client/window.cpp`, `src/admin/gui.cpp` | `chat-ui`; interfaz portátil iniciada sin pantalla |
| Ejecutable cliente distribuible y paquete Debian | `packaging/build-portable.sh`, `packaging/build-deb.sh` | Ambos se generan; el paquete se inspeccionó, sin instalarlo en otro equipo |

Los puertos predeterminados son **8765 TCP** y **8764 UDP**, como en Python. El
formato de las tramas, la prueba Ed25519, el archivo `identity.key` y las tablas
SQLite permiten comunicación entre ambas implementaciones. En pruebas temporales,
un cliente Python se autenticó y envió un mensaje al servidor C++, y un cliente
C++ hizo lo mismo con el servidor Python. El historial quedó guardado en ambos
casos.

La versión C++ conserva sus datos por defecto bajo `~/.local/share/chat-lan-cpp/`,
sin escribir en `~/.local/share/chat-lan/`. Se puede usar `--data` o `--profile`
para elegir rutas concretas. Un servicio Python y uno C++ no pueden escuchar al
mismo tiempo en los mismos puertos.

La interfaz gráfica usa Qt Widgets en lugar de PySide6 y no reproduce cada píxel
del original. El cliente portátil C++ es una **carpeta con ejecutable y bibliotecas**;
el original distribuía un ejecutable PyInstaller de un archivo. El comportamiento
de chat implementado es el mismo; aún corresponde validar descubrimiento real,
notificaciones de escritorio, panel y paquete en los equipos de destino.
