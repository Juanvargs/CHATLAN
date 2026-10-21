# Servidor C++ alternativo

Este directorio contiene dos ejecutables de servidor posibles: el servidor Go, recomendado para la configuración integrada, y un servidor C++/Qt autónomo. Ambos implementan el protocolo CHATLAN y los mismos puertos predeterminados, por lo que solo uno puede ejecutarse a la vez en un equipo.

## Componentes

| Función | Archivos C++ | Explicación |
| --- | --- | --- |
| Inicio del servicio | `src/server/main.cpp` | Lee opciones y crea el objeto servidor. |
| Sockets, peers y ciclo de vida | `src/server/server.h`, `src/server/server.cpp` | Acepta TLS, atiende descubrimiento UDP, mantiene sesiones y cierra conexiones. |
| Identidad y base | `src/common/store.h`, `src/common/store.cpp` | Registra cuentas, estados, mensajes, historial y copias SQLite. |
| Configuración de red | `src/common/paths.cpp` y opciones de `server/main.cpp` | Obtiene rutas y opciones locales de ejecución. |
| Consola administrativa | `src/admin/main.cpp` | Permite operar cuentas y copias desde terminal. |
| Panel gráfico | `src/admin/gui.cpp` | Presenta estado, configuración y acciones de administración. |

## Funcionamiento

El servidor C++ abre TCP con TLS y UDP para descubrimiento. Cada cliente completa el intercambio `hello`, `challenge` y `proof`; el servidor valida la firma Ed25519 y consulta el estado de la cuenta en SQLite. Una vez autorizado, el peer entra a la colección de sesiones activas. Los mensajes se guardan antes de distribuirse; presencia, historial y confirmaciones se procesan con las mismas clases de eventos descritas en la [referencia de protocolo](../../docs/protocolo.md).

## Cuándo usarlo

La entrega integrada documentada usa el servidor Go y el cliente/panel C++/Qt. El servidor C++ se conserva como alternativa compilable para ejecutar una instalación completamente C++ o comparar sus componentes. Si se inicia este servidor, no se debe iniciar a la vez el servicio Go en los mismos puertos. La explicación de cada fuente está en el [mapa completo de archivos](../../docs/arquitectura.md).
