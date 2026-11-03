# BP to CPP Converter

BP to CPP Converter generates C++ source from supported Unreal Engine Blueprint classes, interfaces, structures and enumerations. Review and compile the result before applying asset migration.

Requires Unreal Engine 5.8, Windows x64 and a configured C++ build environment. The converter is an Editor plugin; generated gameplay code belongs to a separate project module.

[User guide](BlueprintNativizationV2/Documentation/UserGuide.md) | [Русский](README.ru.md) | [Español](README.es.md) | [中文](README.zh.md)

## Installation

Close Unreal Editor, place `BlueprintNativizationV2` in the project's `Plugins` directory, and build the project's Editor target. Open **Tools > Blueprint to C++** after restarting.

## Workflow

1. **Setup:** prepare the output module, rebuild and restart, then build the Project and Engine API caches and review native API access.
2. **Assets:** select assets, collect dependencies, and run Analyze Blueprint Code → TryFix → Analyze Blueprint Code before each generation. TryFix can change source assets and saves backups.
3. **Preview:** generate and inspect the documents and diagnostics.
4. **Export:** save the reviewed result, close the editor, build and restart.
5. **Migration:** optionally review and apply the matching migration cache to existing assets.
6. **Tools:** access diagnostics, caches and plugin settings.

Migration changes project assets. Keep a source backup and verify affected gameplay, cooking and packaged execution. Construction Script currently translates to OnConstruction with a different engine phase; universal Blueprint equivalence is not claimed.

## Source layout

The plugin has four Win64 Editor modules: `CppSourceParser`, `NativeApiCache`, `BlueprintSourceAnalysis`, and `BlueprintNativizationV2`. Native API caches are derived project data under `Saved/BlueprintNativizationV2/NativeApiCache`; they are not shipped with the plugin.

[License](LICENSE) · Support: sgsggssg1@gmail.com
