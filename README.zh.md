<p align="center">
  <strong>-------></strong>
  <a href="/README.ru.md">俄文</a> |
  <a href="/README.md">英文</a> |
  <a href="/README.es.md">西班牙文</a> |
  <a href="/README.zh.md">中文</a> |
  <strong><-------</strong>
</p>

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="./media/logo-dark.png">
    <img width="512" height="auto" alt="项目Logo" src="./media/logo-light.png">
  </picture>
</p>

---

<div align="center">

[![GitHub](https://img.shields.io/badge/GitHub-blue?style=flat&logo=github)](https://github.com/SoulofAO)
[![License](https://img.shields.io/badge/License-purple?style=flat&logo=github)](/LICENSE.md)
[![GitHub Stars](https://img.shields.io/github/stars/SoulofAO?style=flat&logo=github&label=Stars&color=orange)](https://github.com/SoulofAO)

</div>

<h1 align="center"> 
BP To CPP Converter——一个实现从蓝图到可读C++无缝转换的插件
</h1>

<h3 align="center"> 
最终代码包括从蓝图节点到C++的函数完全转换。
</h3>

<h2 align="center"> 
    ⚠️免责声明⚠️
</h2> 
<p align="center">
  作者对使用本项目可能产生的任何潜在后果不承担责任。
  使用此存储库的资料即表示您自动同意其相关许可协议的条款。
</p>

<details> 
  <summary align="center">⚠️完整文本⚠️</summary>

1. 您使用存储库资料即表示您自动同意其相关许可协议的条款。

2. 作者不对任何具体目的的准确性、完整性或适用性提供明示或暗示的保证。

3. 作者不对因使用或无法使用此资料或随附文档而产生的任何损失负责，包括但不限于直接、间接、附带、后果性或特殊损害，即使您已被告知可能会导致此类损害。

4. 使用此材料表示您承认并承担与其应用相关的所有风险。此外，您同意作者不对因其使用而产生的任何问题或后果负责。

</details>

* * * * * * * * * * * * * * * * * * 
* * * * * * * * * * * * * * * * * * 

<h1 align="center"> 
引言与警告
</h1>

> ⚠️ **重要警告**
> 
> 该插件目前正在积极开发中。使用当前版本时，生成的代码可能会出现错误。开发过程中已修复了许多此类错误，但某些错误源于Unreal Engine的基本限制，某些元素不支持完整反射。
> 
> 该插件设计用于Unreal Engine 5.6或更高版本。

<h1 align="center"> 
插件概述
</h1>

<details>
  <summary align="center">📖 详细概述</summary>

**BP To CPP Converter** 是一个为Unreal Engine设计的专业插件，旨在将蓝图逻辑自动转换为可读的C++代码。该插件解决了从可视化编程迁移至原生代码的挑战，特别有利于以下场景：

- **性能优化** – 针对性能关键部分，从蓝图迁移到C++
- **项目重构** – 优化代码库结构
- **学习C++** – 理解蓝图构造如何被翻译为原生代码

### 主要功能：
- **无缝转换** – 保留功能性的同时完成转换
- **支持基础结构** – 蓝图、接口、结构、枚举
- **灵活配置** – 可适应特定项目需求
- **编辑器集成** – 方便的用户界面来管理转换过程

</details>


* * * * * * * * * * * * * * * * * * 
* * * * * * * * * * * * * * * * * * 

## 📚 目录

### 🎯 基本信息
1. [引言与警告](#引言与警告)
2. [插件概述](#插件概述)
3. [EU_NativizationTool - 管理界面](#eu_nativizationtool---管理界面)

### 🏗️ 技术方面
4. [内部架构——操作原理](#内部架构——操作原理)
5. [其他有用信息](#其他有用信息)

### 🧩 描述
6. [🧩 插件描述](#-插件描述)

### 🚀 快速开始
7. [初始化](#初始化)
8. [使用示例](#使用示例)

### ⚙️ 设置与配置
9. [运行本地化设置](#运行本地化设置)
10. [其他操作和设置](#其他操作和设置)
11. [本地化设置](#本地化设置)

### 📋 特性与限制
12. [特性与限制](#其他有用信息)

### 📜 许可和文档
13. [📜 许可](#-许可)

---

## 🔗 有用链接
- [Unreal Engine 文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-7-documentation?application_version=5.7)
- [蓝图系统概述](https://docs.unrealengine.com/5.0/en-US/blueprint-system-overview-in-unreal-engine/)
- [C++ 编程指南](https://docs.unrealengine.com/5.0/en-US/cpp-programming-in-unreal-engine/)

* * * * * * * * * * * * * * * * * * 
* * * * * * * * * * * * * * * * * * 


<h1 align="center"> 
内部架构——操作原理
</h1>

<details>
  <summary align="center">⚙️ 展开描述</summary>

一般来说，插件功能如下：
- 首先，执行对所有依赖资产的搜索。这些资产默认列入代码生成列表。
- 接着，为每个资产逐一生成代码。支持总共四种结构——普通蓝图（包括组件等）、接口、结构、枚举。只有生成常规蓝图才需要更详细的考虑。

解析主要基于BaseTranslatorObject的派生对象或通俗来说叫Translator的注册设置。这些偶尔会利用并修改以下算法。

蓝图最初生成EntryNodes。插件不是调用现有的函数，而是分解为一系列节点，这些节点仅部分等同于初始函数。更重要的是，最终的分解确保没有一个Entry Nodes 序列是循环的。Translator修改节点是否应循环，是否应生成临时Entry Nodes，或是否需要生成这些节点等等。

随后，针对CPP和H分别生成Includes。通过解析变量、节点、父类、接口及其他元素来形成Includes。Translator会处理节点并提供包括的信息。所有的对象引用仅在CPP文件中声明以避免循环引用，而其前向声明会放置在Header文件中。Header和CPP includes相互排除。

接着是CS的生成。
任何最终的CS以及应用于模块中文件现有部分的修改仅局限于文件更改而非替换整个文件。

随后分别生成Header和CPP。

Header的生成相对简单：我们遍历主要类元素，尤其是它的属性、函数、委托，然后在类中声明它们。此外，还将生成构造函数和SetupInputComponent的声明，但前提是必要的translator被激活。大函数和变量使用U 宏将标志贴近蓝图。还可以使用Translator在Header中添加新变量。

在生成Header之后，接着生成Cpp代码。Header和Cpp的辅助函数，如构造函数和SetupInputComponent，会被实现。
构造函数通过迭代Actor及其组件中所有的FProperties，识别非等价性，主要对来自其他变量而对蓝图不可访问的变量进行过滤，与Getter-Setter数组进行交叉引用，然后以迭代方式初始化所有不同的变量及其修改值。对于未在本地化设置中注册构造函数的结构, 使用一种特殊的ManyLineInitialization，其基于从默认值递归初始化。

随后进行节点遍历。遍历可直接进行，形成主要的执行序列，也可以逆向进行，这通常针对所有非Exec引脚。生成从对应的EntryNode开始，检查是否缺少translator，并通过递归将其结果添加到当前节点来处理下一个节点。Translator，特别是那些控制流的，操作类似，在其Out Exec引脚处拦截递归。逆向遍历在结构上类似，但稍微复杂一些。总的来说，过程是类似的，但对处理分割引脚进行了调整。

Translator处理除Event节点、Function Entry、Enhance节点之外的所有内容。

</details>

* * * * * * * * * * * * * * * * * * 

<h1 align="center"> 
其他有用信息
</h1>

<details>
  <summary align="center">⚙️ 展开描述</summary>

值得注意的是，在蓝图内部存在两种生成方法，以及蓝图本身如何初始化的一般情况。
在蓝图编译时，其代码被简化为字节码。大部分数据为了优化目的而被压缩。在编译阶段，诸如FProperty和UFunction之类的蓝图组件作为反射对象出现。所有这些都是从原始蓝图及其原始数据实现的。
假设解析原始蓝图更加自由，但也更加复杂。
解析蓝图的编译部分更加精确且不易出错。我假设更先进的专用于纯本地化的插件使用编译部分而不是原始部分。

在我的插件中，使用了组合方法。这是由于解析编译部分的便利性，它也有很多问题。最初，我倾向于解析编译部分，但后来转向解析原始数据。这造成了一定的系统性矛盾，我选择忽视它。

该插件支持宏/Composite，但不支持在翻译器系统内预定义的循环宏。这部分是由于代码处理宏或复合节点的理念。宏在代码解析过程中展开，使用存储的生成代码实现。循环宏虽然理论上不难解析，但不可避免地会导致代码污染，产生许多生成函数，而最终代码中已经有很多这样的函数。此外，大多数循环宏在C++代码中通常是相当简单的构造，因此我假设为您的需求创建一个单独的翻译器比试图包含它们要好得多。

编辑内联对象、实例结构、所有非标准对象以及更改子Actor中的组件不受支持。

</details>

* * * * * * * * * * * * * * * * * * 


## 🧩 插件描述

<div align="center">
  <img style="width: 80%; height: auto;" alt="EU_NativizationTool" src="./media/Tutorial\Article_1/image6.png"/>
</div>

<details>
  <summary align="center">⚙️ 展开描述</summary>

BP To CPP Converter是一个实现将蓝图无缝转换为可读C++的插件。通过简单点击，它允许您将蓝图图表转换为C++代码。转换过程称为本地化，严格来说，这并不完全准确。最终代码包括从蓝图节点到C++的完整函数转换。

项目的核心是编辑器小部件 **EU_NativizationTool**。它作为关键控制组件。让我们深入了解：

三个标签页：
1. **Run Nativization** – 启动本地化的主要标签页。
2. **Apply From Cache Nativization Result** – 将Actor本地化结果移动到实际代码的主要标签页。
3. **Other Actions** – 插件的辅助实用程序。

</details>

* * * * * * * * * * * * * * * * * * 

<h2 align="center">
  <a href="#-目录">⬆️ 返回顶部</a> 
</h2>

<h1 align="center"> 
初始化
</h1>

<div align="center">
  <img style="width: 80%; height: auto;" alt="初始化模块" src="./media/Tutorial\Article_1/image9.png"/>
</div>

<details>
  <summary align="center">⚙️ 展开描述</summary>

在使用插件开始时，强烈建议初始化模块 **BlueprintNativizationModule**。该模块用作保存所有C++代码的空间，以便后续将蓝图资产迁移到它们。生成的代码将被结构化。要初始化，转到Other Action，单击按钮Initialize Blueprint Initialization Module，并重新编译项目。如果一切正确完成，工具栏顶部的模块状态将更改。此操作需要只执行一次，在开始工作之前。

在Other Actions中，除了Initialize Blueprint Initialization Module之外，还有其他有用的功能，例如Reset Names。该项目旨在精心避免名称冲突，如果您在不应用生成的代码结果的情况下进行一系列本地化，可能会发生名称冲突。名称冲突通过专门指定的Unique Name系统解决，虽然重置它不是必需的，但通常很有用，因为它会清除系统中临时分配变量的不必要缓存。尽管如此，建议逐步执行本地化，将生成的代码对象转移到BP对象，或在一个大型系列中。这部分是因为缓存只存在于一个Unreal Engine会话中，不会持续更长时间。

**PrintAllK2Nodes** – 忽略此功能，这是开发者的未来功能。

小部件设置设计为不断调整。

</details>

* * * * * * * * * * * * * * * * * * 

<h2 align="center">
  <a href="#-目录">⬆️ 返回顶部</a> 
</h2>

<h1 align="center"> 
使用示例
</h1>

<div align="center">
  <img style="width: 80%; height: auto;" alt="运行本地化界面" src="./media/Tutorial\Article_1/image5.png"/>
</div>

<details>
  <summary align="center">⚙️ 展开描述</summary>

1. 转到 **Run Nativization** 标签页。
2. 在Blueprints中指定您的任何资产。
   - 确保蓝图已编译并保存。
   - 您可以指定一个或多个资产。
   - 在Blueprints中指定的所有实体，以及依赖于Blueprints的实体，也将进行本地化。
3. 单击 **Apply** 按钮。
   - 在底部，在编辑器中，您将看到生成的代码。

要测试插件，您可以使用自己的 **TestNativizationActor**，或Tests文件夹中的任何其他内容。


</details>


* * * * * * * * * * * * * * * * * * 

<h2 align="center">
  <a href="#-目录">⬆️ 返回顶部</a> 
</h2>

<h1 align="center"> 
运行本地化设置
</h1>

<div align="center">
  <img style="width: 80%; height: auto;" alt="运行本地化设置" src="./media/Tutorial\Article_1/image5.png"/>
</div>

<details>
  <summary align="center">⚙️ 展开描述</summary>

---

### Generate Code One Function

允许仅为选定的一个函数生成代码。勾选True，并在Function Name字段中选择函数名称。如果有多个函数，函数可以作为系列生成。例如，选择Input Action函数的情况，或者在本地化过程中函数被拆分为子函数的情况。

---

### Transform Only One File Code

仅为当前指定的蓝图生成代码，忽略所有依赖递归。

---

### SaveToFile

允许将整个结果保存到文件。如果BlueprintNativizationModule未初始化，此功能及许多后续功能将无法正常工作。

通常认为将所有资产引用保留在蓝图端是良好的做法。

**Left All Asset Ref In Blueprint** – 实现此功能的标志。否则，所有引用将硬编码在C++中。

---

### Visualization

负责编辑器下方将显示内容的标志。您可以禁用Header或CPP代码。

---

### SaveOutputFolder

设置保存生成的C++代码的位置。如果将此字段留空，将保存到初始化的BlueprintNativizationModule，正确分布在文件夹中。

---

### Hot Reload and Replace

一个实验性功能，允许在不重启项目的情况下自动用生成的C++类替换蓝图。它有问题，因此经常被Save Cache替代。

---

### Save Cache

保存有关哪些对象是从什么生成的信息。这允许修复错误并重新编译项目，同时保留在Unreal Engine会话之间将用于生成的蓝图替换为生成的C++代码的能力。

---

### Cache Path

与Save Output Folder类似，它将缓存数据保存到文件夹。否则，它保存到BlueprintNativizationModule的根目录。

要使用缓存，您可以使用标签页 **Apply From Cache Nativization Result**。请注意，应用缓存假设重启Unreal Engine项目，否则CDO类将不会初始化。

将Cache Path留空将默认缓存保存到项目根目录。

---

</details>

* * * * * * * * * * * * * * * * * * 

<h2 align="center">
  <a href="#-目录">⬆️ 返回顶部</a> 
</h2>

<h1 align="center"> 
其他操作和设置
</h1>

<div align="center">
  <img style="width: 80%; height: auto;" alt="编辑器设置" src="./media/Tutorial\Article_1/image3.png"/>
</div>

<details>
  <summary align="center">⚙️ 展开描述</summary>

对于更永久的设置，在Editor Settings中有Blueprint Nativization V2 Editor Settings。

总之，除了像Translator使用的设置之外，这里呈现的数据试图补充由于有限反射而无法访问的数据。
据推测，将来这些数据将自动填充。

</details>

* * * * * * * * * * * * * * * * * * 

<h2 align="center">
  <a href="#-目录">⬆️ 返回顶部</a> 
</h2>

<h1 align="center"> 
本地化设置
</h1>

<div align="center">
  <img style="width: 60%; height: auto;" alt="附加界面" src="./media/Tutorial\Article_1/image4.png"/>
</div>

<details>
  <summary align="center">⚙️ 展开描述</summary>

将蓝图函数转换为C++的主要来源是一组翻译器。**Translator**处理一种或多种类型的K2Node，并将它们转换为C++代码。

---

### Global Variable Name

用于避免名称冲突。

---

### Setup Action Object

将EU_NativizationTool与蓝图端的各种辅助小部件链接。建议不要更改此设置。

---

### Enable Generate Value Suffix

确定C++代码中所有生成的变量是否将具有后缀GeneratedValue。最好禁用此功能以获得更清洁的代码。

---

### Add BP Prefix To Parent Blueprint

指定当重建时，转换为C++的现有蓝图类是否会接收前缀"BP_"。

---

### Function Redirects

蓝图实现函数的列表。这些函数缺乏足够的元数据来确定哪个函数调用它们或在C++中覆盖它们的位置。此数组提供蓝图实现函数名称与原始C++函数之间的关联。

---

### Construction Descriptors

结构构造函数，其中使用构造函数进行初始化是"正确"的选择。在其他情况下，值通过'.'直接设置，这意味着，例如，不是FLinearColor(0.0,0.66,1.0,1.0)，生成将是FLinearColor LinearColor(); LinearColor.R = 1.0; LinearColor.G = 1.0; 等等。也由于缺乏对此的反射而实现。对于在蓝图中生成的结构，默认情况下实现它们的完整构造函数。

---

### Ignore Class to Ref Generate

包括所有不应进行本地化的类。这些通常是UI、Widget等。不要更改此小部件或尝试为UI生成C++，因为这会导致崩溃。

---

### Ignore Assets to Ref Generate

类似。

---

### Getter And Setter Description

FProperties缺乏变量隐私或公共可见性的信息。这导致在访问更高级别的对象时由于缺乏变量访问而崩溃。此数组存在以为变量分配函数以进行诸如Get、Set或完全忽略的操作。

---

### Code Editor

主小部件中Text Editor可视化部分的信息。主要包含指示哪些子字符串应以哪些颜色突出显示。

---

</details>


* * * * * * * * * * * * * * * * * * 
* * * * * * * * * * * * * * * * * * 


<h1 align="center"> 📜 许可</h1>
<h2 align="center">
  <strong>---></strong>
  <strong> 该项目根据 </strong> 
  <a href="./LICENSE">SoulofAO License</a>
  <strong><---</strong>
</h1>

---

<h2 align="center"> 
📚 查看文档 
</h2>

<p align="center">
  <strong>---></strong>
  <a href="/README.ru.md">俄文</a> |
  <a href="/README.md">英文</a> |
  <a href="/README.es.md">西班牙文</a> |
  <a href="/README.zh.md">中文</a>
  <strong><---</strong>
</p>
