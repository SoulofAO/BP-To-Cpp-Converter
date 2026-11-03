# Technical information

## Features

- C++ source generation from supported Blueprint classes, interfaces, structures and enumerations.
- Native Slate interface with Setup, Assets, Preview, Export, Migration and Tools stages.
- Dependency collection, native API analysis, source preparation, document preview and export.
- Optional review and migration of existing assets using the cache matching a compiled export.

## Code modules

| Module | Type | Platform |
| --- | --- | --- |
| CppSourceParser | Editor | Win64 |
| NativeApiCache | Editor | Win64 |
| BlueprintSourceAnalysis | Editor | Win64 |
| BlueprintNativizationV2 | Editor | Win64 |

- Number of Blueprints in the distribution: 0.
- Number of C++ classes: 106 class definitions in the distributed headers, including internal helper classes; forward declarations and structs are not counted.
- Network Replicated: No. This is an editor utility; generated gameplay replication depends on the source assets and the target project.
- Supported Development Platforms: Windows x64: Yes. macOS: No. Linux: No.
- Supported Target Build Platforms: Win64 Editor. The converter modules are not part of a packaged game. Generated gameplay code must be validated for the target project's platforms separately.
- Supported Engine Version: Unreal Engine 5.8. Other versions are not claimed.
- Distribution Method: Code Plugin.
- Documentation Link: https://github.com/SoulofAO/BP-To-Cpp-Converter/blob/main/BlueprintNativizationV2/Documentation/UserGuide.md
- Example Project: No separate example project is included. Install the plugin into a C++ UE 5.8 project and follow the user guide using a Blueprint Actor created in that project.

## Dependencies and prerequisites

Unreal Engine 5.8 on Windows x64, its compatible C++ compiler and Windows SDK, and the built-in Enhanced Input, Control Rig and Niagara plugins are required. The project must have C++ build targets before preparing the generated module. Engine API analysis requires the corresponding engine C++ sources.

The parser and native cache modules are maintained in this repository. No external parser library, LLVM or libclang is bundled. Local native API caches are rebuilt for the current project and engine and are stored under the project's Saved directory.

## Important notes

Back up source assets before running TryFix or migration. Generation, successful export, compilation, migration and runtime verification are separate steps. Read diagnostics and verify the affected gameplay. Construction Script currently translates to OnConstruction, which has a different engine phase; universal Blueprint equivalence is not claimed.

The source submission archive excludes Blueprint content, local caches, tests, standalone build helpers, Binaries, Intermediate and Saved. This repository's technical information is provided for the listing; changes to the Fab listing and its declarations must be applied in the publisher portal.
