<p align="center">
  <strong>-------></strong>
  <a href="/README.md">Russian</a> |
  <a href="/README.en.md">English</a> |
  <a href="/README.es.md">Spanish</a> |
  <a href="/README.zh.md">Chinese</a> |
  <strong><-------</strong>
</p>

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="./media/logo-dark.png">
    <img width="512" height="auto" alt="Project Logo" src="./media/logo-light.png">
  </picture>
</p>

---

<div align="center">

[![GitHub](https://img.shields.io/badge/GitHub-blue?style=flat&logo=github)](https://github.com/SoulofAO)
[![License](https://img.shields.io/badge/License-purple?style=flat&logo=github)](/LICENSE.md)
[![GitHub Stars](https://img.shields.io/github/stars/SoulofAO?style=flat&logo=github&label=Stars&color=orange)](https://github.com/SoulofAO)

</div>

<h1 align="center"> 
BP To CPP Converter — a plugin implementing seamless conversion of Blueprints to readable C++
</h1>

<h3 align="center"> 
The final code includes full transformation of functions from Blueprint nodes to C++.
</h3>

<h2 align="center"> 
    ⚠️ Disclaimer ⚠️
</h2> 
<p align="center">
  The author is not responsible for any potential consequences that may arise from using this project.
  By using the repository materials, you automatically agree to the terms of the license agreement associated with it.
</p>

<details> 
  <summary align="center">⚠️Full text⚠️</summary>

1. By using the repository materials, you automatically agree to the terms of the license agreement associated with it.

2. The author provides no guarantees, express or implied, regarding the accuracy, completeness, or suitability of this material for any specific purpose.
3. The author shall not be liable for any losses, including but not limited to direct, indirect, incidental, consequential, or special damages arising from the use or inability to use this material or the accompanying documentation, even if advised of the possibility of such damages.

4. By using this material, you acknowledge and assume all risks associated with its application. Furthermore, you agree that the author cannot be held liable for any issues or consequences arising from its use.

</details>

* * * * * * * * * * * * * * * * * * 
* * * * * * * * * * * * * * * * * * 

<h1 align="center"> 
Introduction and Warning
</h1>

> ⚠️ **IMPORTANT WARNING**
> 
> The plugin is currently in active development. Errors in the generated code may occur when using the current version. Many of these errors are being fixed during development, but some arise from fundamental limitations of the Unreal Engine, where certain elements do not support full reflection.

<h1 align="center"> 
Plugin Overview
</h1>

<details>
  <summary align="center">📖 Detailed Overview</summary>

**BP To CPP Converter** is a specialized plugin for Unreal Engine designed to automatically transform Blueprint logic into readable C++ code. The plugin addresses the challenge of migrating visual programming to native code, which is especially beneficial for:

- **Performance Optimization** – transitioning from Blueprints to C++ for performance-critical sections
- **Project Refactoring** – streamlining codebase structure
- **Learning C++** – understanding how Blueprint constructs are translated into native code

### Key Features:
- **Seamless Conversion** – transformation while preserving functionality
- **Support for Essential Structures** – Blueprint, Interface, Struct, Enum
- **Flexible Configuration** – adaptable to specific project needs
- **Editor Integration** – user-friendly UI for process management

</details>


* * * * * * * * * * * * * * * * * * 
* * * * * * * * * * * * * * * * * * 

## 📚 Table of Contents

### 🎯 General Information
1. [Introduction and Warning](#introduction-and-warning)
2. [Plugin Overview](#plugin-overview)
3. [EU_NativizationTool - Management Interface](#eu_nativizationtool---management-interface)

### 🏗️ Technical Aspects
4. [Internal Architecture – Principles of Operation](#internal-architecture---principles-of-operation)
5. [Other Useful Information](#other-useful-information)

### 🧩 Description
6. [🧩 Plugin Description](#-plugin-description)

### 🚀 Getting Started
7. [Initialization](#initialization)
8. [Usage Example](#usage-example)

### ⚙️ Settings and Configuration
9. [Run Nativization Settings](#run-nativization-settings)
10. [Other Actions and Settings](#other-actions-and-settings)
11. [Nativization Settings](#nativization-settings)

### 📋 Features and Limitations
12. [Features and Limitations](#other-useful-information)

### 📜 License and Documentation
13. [📜 License](#-license)

---

## 🔗 Useful Links
- [Unreal Engine Documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-7-documentation?application_version=5.7)
- [Blueprint System Overview](https://docs.unrealengine.com/5.0/en-US/blueprint-system-overview-in-unreal-engine/)
- [C++ Programming Guide](https://docs.unrealengine.com/5.0/en-US/cpp-programming-in-unreal-engine/)

* * * * * * * * * * * * * * * * * * 
* * * * * * * * * * * * * * * * * * 


<h1 align="center"> 
Internal Architecture – Principles of Operation
</h1>

<details>
  <summary align="center">⚙️ Expand Description</summary>

In general, the plugin functions as follows.
- First, a search for all dependent assets is performed. These assets are included in the list for code generation by default.
- Next, code generation is carried out for each asset. A total of 4 structures are supported – regular Blueprint (including components and more), Interface, Struct, Enum. More detailed consideration is needed only for generating regular Blueprints.

Parsing is primarily based on registered settings for derivatives of BaseTranslatorObject, or, simply put, Translators. These occasionally utilize and modify the algorithm below.

The Blueprint initially generates EntryNodes. Instead of using ready-made functions, the plugin breaks down into a series of nodes, which are only partially equivalent to the original functions. More importantly, the final decomposition ensures that none of the Entry Nodes sequences are cyclic. Translators modify whether the node should be cycled, whether temporary Entry Nodes should be generated, and whether they should be generated at all, etc.

Includes are then generated separately for CPP and H. The Includes are formed by parsing variables, nodes, parent classes, interfaces, and other elements. Translators process nodes and suggest their includes. All object references are declared only in the CPP file to avoid cyclic includes, whereas their forward declaration is placed in the Header file. The Header and CPP includes exclude each other.

Subsequently, CS is generated.
Any eventual CS, as well as modifications to existing files within the module, apply exclusively to file changes rather than replacing the entire file.

Translators also influence the CS.

Next, Header and CPP generation takes place separately.

Header generation is fairly simple: we traverse through the main class elements, specifically its Properties, Functions, Delegates, and declare them within the class. Additionally, declarations for the constructor and SetupInputComponent are generated but only if the necessary translator is activated. Large functions and variables have U macros flags that closely match Blueprints. Translators can also be used to add new variables to the Header.

Cpp code is generated after the Header. Helper functions for the Header and Cpp, such as constructor and SetupInputComponent, are implemented.
The constructor is generated by iterating through all FProperties within the Actor and its components, identifying non-equivalence, filtering mostly those variables inaccessible to Blueprints (consequently, derived from other variables), cross-referencing with the Getter-Setter array, and successively initializing all differing variables with their modified values. For structures where the constructor is not registered in Nativization Settings, a special ManyLineInitialization is used, built on recursive initialization from the default.

The process then moves to Node traversal. The traversal can be direct, forming the main execution sequence, or inverse, characteristic of all non-Exec pins. Generation starts with the corresponding EntryNode, checks for the absence of a translator, and moves to the next node, adding its result to the current one via recursion. Translators, especially those controlling flow, operate similarly, intercepting recursion at their Out Exec pins. Reverse traversal, structurally, is similar but slightly more complex. Overall, the process is analogous, but adjustments are made for handling split pins.

Translators process everything except Event Nodes, Function Entries, Enhance Nodes.

</details>

* * * * * * * * * * * * * * * * * * 

<h1 align="center"> 
Other Useful Information
</h1>

<details>
  <summary align="center">⚙️ Expand Description</summary>

It is worth noting that there are two approaches to generation within Blueprints and, in general, how Blueprints themselves are initialized.
During Blueprint compilation, its code is simplified into bytecode. Most data is condensed for optimization purposes. At the compilation stage, Blueprint components such as FProperty and UFunction emerge as reflection objects. All of this is implemented from the original Blueprint and its raw data.
Hypothetically, parsing the original Blueprint is much freer but also more complex.
Parsing the compiled part of the Blueprint is more precise and less prone to errors. I assume more advanced plugins dedicated to pure nativization utilize the compiled part instead of the original.

In my plugin, a combinatorial approach is used. This is due to the convenience of parsing the compiled part, which also has numerous problems. Initially, I leaned towards parsing the compiled portion, but later shifted towards parsing raw data. This created a certain systemic contradiction that I chose to overlook.

The plugin supports macros/Composite but not cyclic macros pre-defined within the translator system. This is partially due to the ideology by which code processes macros or composite nodes. Macros are expanded during code parsing, implemented using stored Generated Code. Cyclic macros, while theoretically not difficult to parse, inevitably lead to code pollution with many generated functions, which there are already plenty of in the final code. Furthermore, most cyclic macros are usually quite simple constructions in C++ code, so I assumed that creating a separate translator for your needs would be a much better suggestion than attempting to include them.

Edit Inline Object, Instance Struct, all non-standard objects, and changing components in Child Actors are not supported.

</details>

* * * * * * * * * * * * * * * * * * 


## 🧩 Plugin Description

<div align="center">
  <img style="width: 80%; height: auto;" alt="EU_NativizationTool" src="./media/Tutorial\Article_1/image6.png"/>
</div>

<details>
  <summary align="center">⚙️ Expand Description</summary>

BP To CPP Converter is a plugin implementing seamless transformation of Blueprints into readable C++. With a simple click, it allows you to convert Blueprint diagrams into C++ code. The transformation process is called nativization, which, strictly speaking, is not quite accurate. The final code includes full transformation of functions from Blueprint nodes into C++.

The core of the project is the Editor Widget **EU_NativizationTool**. It serves as the key control component. Let’s dive into it:

Three tabs:
1. **Run Nativization** – the primary tab for launching nativization.
2. **Apply From Cache Nativization Result** – the primary tab for moving Actor nativization results to real code.
3. **Other Actions** – auxiliary utilities for the plugin.

</details>

* * * * * * * * * * * * * * * * * * 

<h2 align="center">
  <a href="#-table-of-contents">⬆️ Back to Top</a> 
</h2>

<h1 align="center"> 
Initialization
</h1>

<div align="center">
  <img style="width: 80%; height: auto;" alt="Initialize Module" src="./media/Tutorial\Article_1/image9.png"/>
</div>

<details>
  <summary align="center">⚙️ Expand Description</summary>

At the beginning of working with the plugin, it is strongly recommended to initialize the module **BlueprintNativizationModule**. This module serves as the space where all C++ codes are saved for subsequent migration of Blueprint assets to them. The resulting code will be structured. To initialize, go to Other Action, click the button Initialize Blueprint Initialization Module, and recompile the project. If everything is done correctly, the module status at the top of your toolbar will change. This action needs to be performed ONLY ONCE, BEFORE STARTING WORK.

</details>

* * * * * * * * * * * * * * * * * * 

<h2 align="center">
  <a href="#-table-of-contents">⬆️ Back to Top</a> 
</h2>

<h1 align="center"> 
Usage Example
</h1>

<div align="center">
  <img style="width: 80%; height: auto;" alt="Run Nativization UI" src="./media/Tutorial\Article_1/image5.png"/>
</div>

<details>
  <summary align="center">⚙️ Expand Description</summary>

1. Go to the **Run Nativization** tab.
2. Specify any of your assets in Blueprints.
   - Make sure the Blueprint is compiled and saved.
   - You can specify one or multiple assets.
   - All entities specified in Blueprints, as well as entities dependent on the Blueprints, will also be subject to nativization.
3. Click the **Apply** button.
   - At the bottom, in the editor, you will see the resulting code.

To test the plugin, you can use your own, **TestNativizationActor**, or any other from the Tests folder.

</details>


* * * * * * * * * * * * * * * * * * 

<h2 align="center">
  <a href="#-table-of-contents">⬆️ Back to Top</a> 
</h2>

<h1 align="center"> 
Run Nativization Settings
</h1>

<div align="center">
  <img style="width: 80%; height: auto;" alt="Run Nativization Settings" src="./media/Tutorial\Article_1/image5.png"/>
</div>

<details>
  <summary align="center">⚙️ Expand Description</summary>

---

### Generate Code One Function

Allows generating code for only one selected function. Tick True, and select the function name in the Function Name field. Functions can be generated as a series if there are multiple functions. For instance, situations where an Input Action function is selected, or a function is split into subfunctions during nativization.

---

### Transform Only One File Code

Generates code only for the currently specified Blueprints, ignoring all dependency recursion.

---

### SaveToFile

Allows saving the entire result to a file. This feature and many subsequent ones will not work correctly if the BlueprintNativizationModule is not initialized.

It is often considered good practice to leave all Asset references on the Blueprint side.

**Left All Asset Ref In Blueprint** – a flag for implementing this. Otherwise, all references will be hard-coded in C++.

---

### Visualization

Flags responsible for what will be displayed in the editor below. You can disable Header or CPP code.

---

### SaveOutputFolder

Sets the location to save the generated C++ code. Leaving this field empty will save it to the initialized BlueprintNativizationModule, correctly distributed among folders.

---

### Hot Reload and Replace

An experimental feature that allows automatic replacement of Blueprints with the generated C++ class without restarting the project. It has issues and is therefore often replaced by Save Cache.

---

### Save Cache

Saves information about which objects were generated from what. This allows errors to be fixed and the project recompiled, while retaining the ability to replace the Blueprints used for generation with the generated C++ code between Unreal Engine sessions.

---

### Cache Path

Similar to Save Output Folder, it saves cache data to a folder. Otherwise, it saves to the root of the BlueprintNativizationModule.

To use the cache, you can use the tab **Apply From Cache Nativization Result**. Note that applying the cache assumes restarting the Unreal Engine project, otherwise the CDO classes will not be initialized.

Leaving Cache Path empty will default the cache to the project root.

---

</details>

* * * * * * * * * * * * * * * * * * 

<h2 align="center">
  <a href="#-table-of-contents">⬆️ Back to Top</a> 
</h2>

<h1 align="center"> 
Other Actions and Settings
</h1>

<div align="center">
  <img style="width: 80%; height: auto;" alt="Editor Settings" src="./media/Tutorial\Article_1/image3.png"/>
</div>

<details>
  <summary align="center">⚙️ Expand Description</summary>

In Other Actions, there are other useful features found, aside from Initialize Blueprint Initialization Module, such as Reset Names. The project aims to meticulously avoid name conflicts that can occur if you conduct nativization in series without applying the generated code results. Name conflicts are resolved using a specifically designated Unique Name system, and while resetting it is not necessary, it is often useful as it clears unnecessary cache in the system for temporarily allocated variables. Nevertheless, it is recommended to perform nativization step-by-step, transferring generated code objects to BP objects, or in one large series. This is partly because the cache only exists within one Unreal Engine session and does not persist longer.

**PrintAllK2Nodes** – ignore this.

The widget settings are designed to be constantly adjusted. For more permanent settings, there is Blueprint Nativization V2 Editor Settings in the Editor Settings.

</details>

* * * * * * * * * * * * * * * * * * 

<h2 align="center">
  <a href="#-table-of-contents">⬆️ Back to Top</a> 
</h2>

<h1 align="center"> 
Nativization Settings
</h1>

<div align="center">
  <img style="width: 60%; height: auto;" alt="Additional UI" src="./media/Tutorial\Article_1/image4.png"/>
</div>

<details>
  <summary align="center">⚙️ Expand Description</summary>

The main source for converting Blueprint functions to C++ is a set of translators. **Translator** handles one or several types of K2Node and translates them into C++ code.

---

### Global Variable Name

Serves to avoid name conflicts.

---

### Setup Action Object

Links EU_NativizationTool with various auxiliary Widgets on the Blueprint side. It is advised not to change this setting.

---

### Enable Generate Value Suffix

Determines whether all generated variables in the C++ code will have the suffix GeneratedValue. It's better to disable this for cleaner code.

---

### Add BP Prefix To Parent Blueprint

Specifies whether existing Blueprint classes converted into C++ will receive the prefix "BP_" when rebuilt.

---

### Function Redirects

A list of Blueprint Implemented functions. These functions lack sufficient metadata to determine which function calls them or where to override them in C++. This array provides the correlation between the Blueprint Implemented function name and the original C++ function.

---

### Construction Descriptors

Structure constructors where using a constructor for initialization is the "correct" option. In other cases, values are set directly via ‘.’, meaning instead of, for example, FLinearColor(0.0,0.66,1.0,1.0), generation will be FLinearColor LinearColor(); LinearColor.R = 1.0; LinearColor.G = 1.0; and so on. Also implemented due to the lack of reflection for this. For structures generated in Blueprints, their complete constructor is implemented by default.

---

### Ignore Class to Ref Generate

Includes all classes that should not undergo nativization. These are typically UI, Widgets, etc. Do not alter this widget or attempt to generate C++ for UI, as this will cause a crash.

---

### Ignore Assets to Ref Generate

Similar.

---

### Getter And Setter Description

FProperties lack information on variable privacy or public visibility. This leads to crashing when accessing higher-level objects due to the lack of variable access. This array exists to assign functions to variables for operations like Get, Set, or complete ignoring.

---

### Code Editor

Information for the visual part of Text Editor in the main widget. Mainly contains indications for which substrings to highlight in which colors.

---

</details>


* * * * * * * * * * * * * * * * * * 
* * * * * * * * * * * * * * * * * * 


<h1 align="center"> 📜 License</h1>
<h2 align="center">
  <strong>---></strong>
  <strong> This project is distributed under the </strong> 
  <a href="./LICENSE">SoulofAO License</a>
  <strong><---</strong>
</h1>

---

<h2 align="center"> 
📚 Check out the Documentation 
</h2>

<p align="center">
  <strong>---></strong>
  <a href="/README.md">Russian</a> |
  <a href="/README.en.md">English</a> |
  <a href="/README.es.md">Spanish</a> |
  <a href="/README.zh.md">Chinese</a> |
  <strong><---</strong>
</p>
