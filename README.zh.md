<p align="center">
  <strong>-------></strong>
  <a href="/README.md">俄文</a> |
  <a href="/README.en.md">英文</a> |
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

随后进行节点遍历。遍历可直接进行，形成主要的执行序列，也可以逆向进行，这通常针对所有非Exec引脚。生成从对应的EntryNode开始，检查是否缺少translator，并通过递归将其结果添加到当前节点来处理下一个节点。Translator特别是那些控制流的  สูตรบาคาร่า
