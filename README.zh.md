# BP to CPP Converter

将 Unreal Engine 中受支持的 Blueprint 类、接口、结构体和枚举转换为 C++ 源代码。迁移资源前，请检查并编译生成结果。

要求：Unreal Engine 5.8、Windows x64，以及已配置的 C++ 编译器和 Windows SDK。转换器是编辑器插件，生成的游戏代码位于独立的项目模块中。

**必需依赖项：** EnhancedInput、ControlRig 和 Niagara 随 Unreal Engine 提供，必须保持启用。SemanticSearch 为可选插件，默认禁用。

[完整英文指南](BlueprintNativizationV2/Documentation/UserGuide.md) | [English](README.md) | [Русский](README.ru.md) | [Español](README.es.md)

## 安装与使用

关闭 Unreal Editor，将 `BlueprintNativizationV2` 放入项目的 `Plugins` 目录，编译 Editor target 后重启。打开 **Tools > Blueprint to C++**。

1. **Setup**：准备输出模块，编译并重启；建立项目和引擎 API 缓存，检查原生 API 访问权限。
2. **Assets**：选择资源并收集依赖。每次生成前执行 Analyze Blueprint Code → TryFix → Analyze Blueprint Code。TryFix 可能修改源资源，并会保存备份。
3. **Preview**：生成并检查文档和诊断信息。
4. **Export**：保存结果，关闭编辑器，编译并重启。
5. **Migration**：按需检查并应用对应的迁移缓存。
6. **Tools**：访问诊断、缓存和插件设置。

迁移会修改项目资源。请保留备份，并分别测试游戏行为、cook 和打包运行。Construction Script 目前转换为 OnConstruction，两者执行阶段不同；本版本不承诺任意 Blueprint 的完全等价性。

插件包含四个 Win64 Editor 模块：`CppSourceParser`、`NativeApiCache`、`BlueprintSourceAnalysis`、`BlueprintNativizationV2`。本地 API 缓存保存在项目的 `Saved/BlueprintNativizationV2/NativeApiCache`，不随插件分发。

[许可证](LICENSE)。支持邮箱：sgsggssg1@gmail.com。
