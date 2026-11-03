# BP to CPP Converter

BP to CPP Converter generates C++ source from supported Unreal Engine Blueprint classes, interfaces, structures and enumerations. Review and compile the result before using it. Applying migration is a separate, optional operation that changes existing assets.

## Requirements and installation

- Unreal Engine 5.8 on Windows x64.
- A C++ project with the compiler and Windows SDK required by that engine installation. For a Blueprint-only project, add an empty C++ class and build its Editor target first.
- The built-in Enhanced Input, Control Rig and Niagara plugins. These dependencies are declared in the plugin descriptor.

Close Unreal Editor. Place the `BlueprintNativizationV2` folder in your project's `Plugins` directory, regenerate project files if needed, and build the project's Editor target. Reopen the project and enable the plugin if prompted. Open **Tools > Blueprint to C++**.

The plugin contains four Editor modules, all restricted to Win64:

| Module | Purpose |
| --- | --- |
| CppSourceParser | Structural analysis of C++ source text. |
| NativeApiCache | Catalog generation from parsed C++ declarations. |
| BlueprintSourceAnalysis | Integration with the project's reflected and native API. |
| BlueprintNativizationV2 | Conversion, source preview, export and asset migration. |

The generated gameplay module is separate from these Editor modules. Its dependencies and supported target platforms must be checked in the project where it is used. The converter does not add network replication to gameplay.

## Setup

1. Back up the source project or commit its current state to source control.
2. Choose the generated module name and review the output settings. Select **Prepare module** if it has not been registered in the project.
3. Close the editor, build the project's Editor target, and reopen the project so the prepared module is loaded.
4. Run **Build Project API Cache** and **Build Engine API Cache**. The engine cache needs the corresponding C++ engine source files. Wait for each operation to finish and inspect its result.
5. Run **Analyze native API**. Review proposed source changes before applying **Try Fix project API**. That repair can change project C++ declarations and saves originals under `Saved/BlueprintNativization/SourceRepairs`. Rebuild, restart and rebuild the Project API cache after source repairs.

Native API caches are local derived data under `Saved/BlueprintNativizationV2/NativeApiCache` in the current project. They are not included in the distributed plugin. After upgrading from versions that stored caches in the plugin directory, rebuild them using the Setup actions. Do not copy caches from another engine installation or project.

## Assets

Select supported assets in the Content Browser and add them to the Generation List. Use **Get Dependencies** and review the resulting list, including excluded or unsupported dependencies.

Before every generation, run **Analyze Blueprint Code**, then **TryFix**, then **Analyze Blueprint Code** again. Read the repair report and remaining diagnostics. TryFix is a source preparation operation: it can modify or remove invalid source assets and backs up each affected file under `Saved/BlueprintTryFix`. A partial or failed repair is not a successful preparation. Generation itself reads the prepared source graphs.

## Preview

Generate a preview after preparation succeeds. Select each generated document to review its header, implementation and build rules. Check diagnostics as well as the displayed C++ text. Preview does not compile the result.

Changing generation options or inputs can make the preview stale. Generate it again before export. Changing stages preserves the current preview; closing the tool or generating again may require a decision about unsaved results.

## Export

Review the export overview and output location. Export the current result and, if migration will be needed, enable saving its migration cache. Resolve any reported file conflicts before proceeding.

Close Unreal Editor and build the project's Editor target. Compilation failure must be resolved before migration. Restart the editor after a successful build. Export success alone does not establish that the code compiled or that its behavior matches the source Blueprint.

## Migration

Migration is optional. Use it only when existing project assets should be updated to the generated native types.

Choose the `.bpncache` produced for the matching export and loaded binaries. Analyze the mappings and affected assets, review the result, and then apply migration. Keep the source backup available. Reopen affected maps and assets and verify the project in a fresh editor process after migration.

Test the affected gameplay, construction behavior, components, inheritance, delegates and asynchronous operations. Validate cooking and packaged execution separately when these are required by the project.

## Tools and troubleshooting

Tools contains cache operations, function refresh, name reset, node diagnostics and plugin settings. Switching to Tools does not run a conversion.

- If setup is not ready, read the readiness panel and confirm that the prepared module has been built and loaded after restarting.
- If a source or module cache is stale, rebuild the corresponding cache after rebuilding the project.
- If the selected node, native API or type is unsupported, inspect the reported asset and node instead of exporting an incomplete result as working code.
- If source repair is partial, inspect the report and backup before repeating Analyze and TryFix.
- The interface is implemented in native Slate. No Blueprint assets or plugin Content folder are required. The entry point is **Tools > Blueprint to C++**.

Construction Script currently translates to `OnConstruction`, which runs in a different engine phase from `UserConstructionScript`. Validate attachment, mobility, inheritance and instance overrides for the converted assets. This release does not claim universal equivalence for every Blueprint or node.

## Distribution and support

The source distribution includes the four modules, resources, this guide and the unchanged license. Local caches, host tests, standalone build helpers and locally generated build products are excluded from the source submission archive.

The C++ parser and native cache implementation are maintained as part of this project; they do not require LLVM, libclang or an external parser library. Unreal Engine and its built-in plugins remain prerequisites.

Support: sgsggssg1@gmail.com. Include the Unreal Engine version, plugin version, relevant diagnostic output and a reproducible asset or minimal project when reporting a problem.
