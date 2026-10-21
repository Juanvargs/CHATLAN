# Documentación de CHATLAN

La documentación se separa por propósito: aprender paso a paso, resolver una tarea, consultar un contrato técnico o entender las decisiones del sistema. Sigue el marco [Diátaxis](https://diataxis.fr/), para que una explicación extensa no sustituya a una guía práctica.

## Ruta recomendada para aprender

1. Lee [la arquitectura](arquitectura.md) para ubicar procesos, responsabilidades y conexiones.
2. Revisa [el estudio del servidor](../SERVER_JP/docs/servidor-explicado.md) para comprender TLS, Ed25519, SQLite, descubrimiento, cuentas y mensajes.
3. Sigue [el protocolo de red](protocolo.md) desde el descubrimiento UDP hasta la autenticación y el chat.
4. Lee [la guía de administración](../SERVER_JP/docs/interfaz-administracion.md) para conocer las acciones del panel.
5. Usa el mapa de módulos de arquitectura para recorrer el código y los README de cada componente para compilarlo.

## Guías de uso

- [Instalar, ejecutar y distribuir](instalacion-y-distribucion.md).
- [Operación, configuración y solución de problemas](operacion.md).
- [Interfaz de administración](../SERVER_JP/docs/interfaz-administracion.md).

## Referencias técnicas

- [Arquitectura y mapa del código](arquitectura.md).
- [Protocolo de red](protocolo.md).
- [Servidor Go](../SERVER_JP/README.md).
- [Cliente C++/Qt y servidor alternativo](../chat-lan-cpp/README.md).
- [Equivalencia con el proyecto Python original](../chat-lan-cpp/docs/equivalencia.md).

## Explicaciones y contexto

- [Conceptos y funcionamiento del servidor](../SERVER_JP/docs/servidor-explicado.md).
- [Estudio del proyecto original](../SERVER_JP/docs/estudio-original.md).
- [Equipo](equipo.md).

## Criterios editoriales

El README principal resume propósito, requisitos, inicio y enlaces siguiendo las [recomendaciones de GitHub para README](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/about-readmes). Los diagramas usan el vocabulario del [modelo C4](https://c4model.com/). La documentación describe el comportamiento observado en el código; al cambiar el protocolo o sus límites, actualiza esta referencia y las guías dependientes.

### Fuentes sobre documentación

- [Diátaxis: marco de documentación técnica](https://diataxis.fr/).
- [GitHub Docs: sobre los README](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/about-readmes).
- [C4 model](https://c4model.com/).
- [Go: documentación de código](https://go.dev/doc/comment).
- [GitHub Docs: licenciar un repositorio](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/licensing-a-repository).
