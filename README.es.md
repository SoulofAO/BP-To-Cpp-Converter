# BP to CPP Converter

Genera código fuente C++ a partir de clases Blueprint, interfaces, estructuras y enumeraciones compatibles de Unreal Engine. Revise y compile el resultado antes de migrar los recursos.

Requiere Unreal Engine 5.8, Windows x64 y un entorno de compilación C++ configurado. Es un plugin del editor; el código de juego generado pertenece a un módulo independiente del proyecto.

**Dependencias obligatorias:** EnhancedInput, ControlRig y Niagara, incluidos en Unreal Engine, deben permanecer activados. SemanticSearch es opcional y está desactivado de forma predeterminada.

[Guía completa en inglés](BlueprintNativizationV2/Documentation/UserGuide.md) | [English](README.md) | [Русский](README.ru.md) | [中文](README.zh.md)

## Instalación y uso

Cierre Unreal Editor, copie `BlueprintNativizationV2` en `Plugins` del proyecto, compile el destino Editor y reinicie. Abra **Tools > Blueprint to C++**.

1. **Setup:** prepare el módulo, compile y reinicie; cree las cachés de API del proyecto y del motor y revise el acceso a la API nativa.
2. **Assets:** seleccione los recursos y sus dependencias. Antes de cada generación ejecute Analyze Blueprint Code → TryFix → Analyze Blueprint Code. TryFix puede modificar los recursos originales y guarda copias de seguridad.
3. **Preview:** genere y revise los documentos y diagnósticos.
4. **Export:** guarde el resultado, cierre el editor, compile y reinicie.
5. **Migration:** revise y aplique opcionalmente la caché de migración correspondiente.
6. **Tools:** acceda a diagnósticos, cachés y ajustes.

La migración modifica los recursos. Conserve una copia de seguridad y pruebe el juego, el proceso de cook y la versión empaquetada. Construction Script se traduce actualmente a OnConstruction, que se ejecuta en una fase distinta. No se garantiza equivalencia universal de Blueprints.

Módulos Editor para Win64: `CppSourceParser`, `NativeApiCache`, `BlueprintSourceAnalysis` y `BlueprintNativizationV2`. Las cachés locales se guardan en `Saved/BlueprintNativizationV2/NativeApiCache` del proyecto y no se distribuyen con el plugin.

[Licencia](LICENSE). Soporte: sgsggssg1@gmail.com.
