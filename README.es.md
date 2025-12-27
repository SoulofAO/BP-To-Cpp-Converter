<p align="center">
  <strong>-------></strong>
  <a href="/README.ru.md">Ruso</a> |
  <a href="/README.md">Inglés</a> |
  <a href="/README.es.md">Español</a> |
  <a href="/README.zh.md">Chino</a> |
  <strong><-------</strong>
</p>

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="./media/logo-dark.png">
    <img width="512" height="auto" alt="Logotipo del proyecto" src="./media/logo-light.png">
  </picture>
</p>

---

<div align="center">

[![GitHub](https://img.shields.io/badge/GitHub-blue?style=flat&logo=github)](https://github.com/SoulofAO)
[![License](https://img.shields.io/badge/License-purple?style=flat&logo=github)](/LICENSE.md)
[![GitHub Stars](https://img.shields.io/github/stars/SoulofAO?style=flat&logo=github&label=Stars&color=orange)](https://github.com/SoulofAO)

</div>

<h1 align="center"> 
BP To CPP Converter — un plugin que implementa la conversión fluida de Blueprints a C++ legible
</h1>

<h3 align="center"> 
El código final incluye la transformación completa de funciones desde nodos de Blueprint a C++.
</h3>

<h2 align="center"> 
    ⚠️ Descargo de responsabilidad ⚠️
</h2> 
<p align="center">
  El autor no se hace responsable de las posibles consecuencias que puedan surgir del uso de este proyecto.
  Al utilizar los materiales del repositorio, aceptas automáticamente los términos del acuerdo de licencia asociado con él.
</p>

<details> 
  <summary align="center">⚠️Texto completo⚠️</summary>

1. Al utilizar los materiales del repositorio, aceptas automáticamente los términos del acuerdo de licencia asociado con él.

2. El autor no ofrece garantías, expresas o implícitas, sobre la precisión, integridad o idoneidad de este material para cualquier propósito específico.
3. El autor no será responsable de ninguna pérdida, incluyendo pero no limitado a daños directos, indirectos, incidentales, consecuentes o especiales derivados del uso o la imposibilidad de uso de este material o la documentación acompañante, incluso si se le advierte de la posibilidad de tales daños.

4. Al utilizar este material, reconoces y asumes todos los riesgos asociados a su aplicación. Además, aceptas que el autor no puede ser considerado responsable de ningún problema o consecuencia derivada de su uso.

</details>

* * * * * * * * * * * * * * * * * * 
* * * * * * * * * * * * * * * * * * 

<h1 align="center"> 
Introducción y Advertencia
</h1>

> ⚠️ **ADVERTENCIA IMPORTANTE**
> 
> El plugin está actualmente en desarrollo activo. Pueden ocurrir errores en el código generado al usar la versión actual. Muchos de estos errores están siendo corregidos durante el desarrollo, pero algunos surgen de limitaciones fundamentales del Unreal Engine, que no admite completamente la reflexión de ciertos elementos.
> 
> El plugin está diseñado para la versión 5.6 o posterior de Unreal Engine.

<h1 align="center"> 
Descripción General del Plugin
</h1>

<details>
  <summary align="center">📖 Descripción Detallada</summary>

**BP To CPP Converter** es un plugin especializado para Unreal Engine diseñado para transformar automáticamente la lógica de Blueprint en código C++ legible. El plugin aborda el desafío de migrar programación visual al código nativo, lo cual es especialmente beneficioso para:

- **Optimización del Rendimiento** – transición de Blueprints a C++ para secciones críticas de rendimiento.
- **Refactorización del Proyecto** – simplificación de la estructura del código base.
- **Aprender C++** – entender cómo se traducen las construcciones de Blueprint en código nativo.

### Características Clave:
- **Conversión Sin Fallos** – transformación preservando la funcionalidad.
- **Soporte para Estructuras Esenciales** – Blueprint, Interface, Struct, Enum.
- **Configuración Flexible** – adaptable a las necesidades específicas del proyecto.
- **Integración en el Editor** – interfaz de usuario amigable para la gestión del proceso.

</details>


* * * * * * * * * * * * * * * * * * 
* * * * * * * * * * * * * * * * * * 

## 📚 Tabla de Contenidos

### 🎯 Información General
1. [Introducción y Advertencia](#introducción-y-advertencia)
2. [Descripción General del Plugin](#descripción-general-del-plugin)
3. [EU_NativizationTool - Interfaz de Gestión](#eu_nativizationtool---interfaz-de-gestión)

### 🏗️ Aspectos Técnicos
4. [Arquitectura Interna – Principios de Funcionamiento](#arquitectura-interna---principios-de-funcionamiento)
5. [Otra Información Útil](#otra-información-útil)

### 🧩 Descripción
6. [🧩 Descripción del Plugin](#-descripción-del-plugin)

### 🚀 Comenzando
7. [Inicialización](#inicialización)
8. [Ejemplo de Uso](#ejemplo-de-uso)

### ⚙️ Configuración y Ajustes
9. [Configuraciones de Nativización](#configuraciones-de-nativización)
10. [Otras Acciones y Configuraciones](#otras-acciones-y-configuraciones)
11. [Ajustes de Nativización](#ajustes-de-nativización)

### 📋 Características y Limitaciones
12. [Características y Limitaciones](#otra-información-útil)

### 📜 Licencia y Documentación
13. [📜 Licencia](#-licencia)

---

## 🔗 Enlaces Útiles
- [Documentación de Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-7-documentation?application_version=5.7)
- [Descripción General del Sistema de Blueprint](https://docs.unrealengine.com/5.0/en-US/blueprint-system-overview-in-unreal-engine/)
- [Guía de Programación en C++](https://docs.unrealengine.com/5.0/en-US/cpp-programming-in-unreal-engine/)

* * * * * * * * * * * * * * * * * * 
* * * * * * * * * * * * * * * * * * 


<h1 align="center"> 
Arquitectura Interna – Principios de Funcionamiento
</h1>

<details>
  <summary align="center">⚙️ Expandir Descripción</summary>

En general, el plugin funciona de la siguiente manera.
- Primero, se realiza una búsqueda de todos los activos dependientes. Estos activos se incluyen en la lista para la generación de código por defecto.
- A continuación, se genera el código para cada activo. Se admiten un total de 4 estructuras: Blueprint habitual (incluyendo componentes y más), Interface, Struct, Enum. Se necesita una consideración más detallada solo para generar Blueprints regulares.

El análisis se basa principalmente en configuraciones registradas para derivados de BaseTranslatorObject, o, simplemente, Traductores. Estos ocasionalmente modifican y usan el algoritmo descrito a continuación.

El Blueprint inicialmente genera EntryNodes. En lugar de usar funciones ya hechas, el plugin descompone en una serie de nodos, que solo son parcialmente equivalentes a las funciones originales. Más importante aún, la descomposición final asegura que ninguna de las secuencias de nodos Entry sea cíclica. Los Traductores modifican si el nodo debe ser cíclico, si deben generarse nodos Entry temporales, si deben generarse en absoluto, etc.

Las inclusiones se generan por separado para CPP y H. Se forman analizando variables, nodos, clases padres, interfaces y otros elementos. Los traductores procesan nodos y sugieren sus inclusiones. Todas las referencias a objetos se declaran solo en el archivo CPP para evitar inclusiones cíclicas, mientras que su declaración anticipada se coloca en el archivo Header. Las inclusiones de Header y CPP se excluyen mutuamente.

Posteriormente, se genera el CS.
Cualquier CS eventual, así como modificaciones a archivos existentes dentro del módulo, se aplican exclusivamente a cambios en el archivo en lugar de reemplazar todo el archivo.

Los Traductores también influyen en el CS.

Después, la generación de Header y CPP se realiza por separado.

La generación del Header es bastante sencilla: recorremos los elementos principales de la clase, específicamente sus Propiedades, Funciones, Delegados, y los declaramos dentro de la clase. Además, se generan declaraciones para el constructor y SetupInputComponent, pero solo si el traductor necesario está activado. Las funciones y variables grandes tienen flags de macros U que coinciden con los Blueprints. También se pueden usar Traductores para agregar nuevas variables al Header.

El código Cpp se genera después del Header. Las funciones auxiliares para el Header y el Cpp, como el constructor y SetupInputComponent, se implementan.
El constructor se genera iterando a través de todas las FProperties dentro del Actor y sus componentes, identificando no equivalencias, filtrando principalmente aquellas variables inaccesibles para los Blueprints (y que derivan de otras variables), verificando con el array Getter-Setter e inicializando sucesivamente todas las variables diferenciadas con sus valores modificados. Para estructuras cuyo constructor no está registrado en Nativization Settings, se utiliza un ManyLineInitialization especial, construido sobre la inicialización recursiva por defecto.

El proceso luego pasa al recorrido de nodos. Este puede ser directo, formando la secuencia principal de ejecución, o inverso, característico de todos los pines no Exec. La generación comienza con el EntryNode correspondiente, verifica la ausencia de un traductor y avanza al siguiente nodo, añadiendo su resultado al actual mediante recursión. Los traductores, especialmente aquellos que controlan el flujo, operan de manera similar, interceptando la recursión en sus pines Out Exec. El recorrido inverso, estructuralmente, es similar pero ligeramente más complejo. En general, el proceso es análogo, pero se realizan ajustes para manejar pines divididos.

Los traductores procesan todo excepto los nodos Event, Function Entries, Enhance Nodes.

</details>

* * * * * * * * * * * * * * * * * * 

<h1 align="center"> 
Otra Información Útil
</h1>

<details>
  <summary align="center">⚙️ Expandir Descripción</summary>

Vale la pena señalar que existen dos enfoques para la generación dentro de Blueprints y, en general, para cómo se inicializan los propios Blueprints.
Durante la compilación de Blueprint, su código se simplifica en bytecode. La mayoría de los datos se condensan por propósitos de optimización. En la etapa de compilación, los componentes de Blueprint como FProperty y UFunction emergen como objetos de reflexión. Todo esto se implementa desde el Blueprint original y sus datos en bruto.
Hipotéticamente, analizar el Blueprint original es mucho más libre pero también más complejo.
Analizar la parte compilada del Blueprint es más preciso y menos propenso a errores. Asumo que los plugins más avanzados dedicados a la nativización pura utilizan la parte compilada en lugar del original.

En mi plugin, se utiliza un enfoque combinado. Esto se debe a la conveniencia de analizar la parte compilada, que también tiene numerosos problemas. Inicialmente, me incliné hacia analizar la parte compilada, pero más tarde cambié para analizar los datos en bruto. Esto creó cierta contradicción sistémica que opté por pasar por alto.

El plugin admite macros/Composite pero no macros cíclicos predefinidos dentro del sistema de traductor. Esto se debe parcialmente a la ideología mediante la cual el código procesa macros o nodos compuestos. Las macros se expanden durante el análisis de código, implementadas utilizando el código generado almacenado. Las macros cíclicas, aunque teóricamente no son difíciles de analizar, inevitablemente conducen a una contaminación del código con muchas funciones generadas, de las cuales ya hay bastantes en el código final. Además, la mayoría de las macros cíclicas suelen ser construcciones bastante simples en el código C++, por lo que asumí que crear un traductor separado para tus necesidades sería una mejor sugerencia que intentar incluirlas.

Edit Inline Object, Instance Struct, todos los objetos no estándar y el cambio de componentes en actores secundarios no están soportados.

</details>

* * * * * * * * * * * * * * * * * * 


## 🧩 Descripción del Plugin

<div align="center">
  <img style="width: 80%; height: auto;" alt="EU_NativizationTool" src="./media/Tutorial\Article_1/image6.png"/>
</div>

<details>
  <summary align="center">⚙️ Expandir Descripción</summary>

BP To CPP Converter es un plugin que implementa una transformación fluida de Blueprints en C++ legible. Con un simple clic, permite convertir diagramas de Blueprint en código C++. El proceso de transformación se llama nativización, lo cual, estrictamente hablando, no es del todo correcto. El código final incluye la transformación completa de funciones desde nodos de Blueprint a C++.

El núcleo del proyecto es el Editor Widget **EU_NativizationTool**. Actúa como el componente clave de control. Vamos a profundizar en ello:

Tres pestañas:
1. **Run Nativization** – la pestaña principal para lanzar la nativización.
2. **Apply From Cache Nativization Result** – la pestaña principal para mover los resultados de nativización del Actor al código real.
3. **Other Actions** – utilidades auxiliares para el plugin.

</details>

* * * * * * * * * * * * * * * * * * 

<h2 align="center">
  <a href="#-tabla-de-contenidos">⬆️ Volver al Inicio</a> 
</h2>

<h1 align="center"> 
Inicialización
</h1>

<div align="center">
  <img style="width: 80%; height: auto;" alt="Inicializar Módulo" src="./media/Tutorial\Article_1/image9.png"/>
</div>

<details>
  <summary align="center">⚙️ Expandir Descripción</summary>

Al comenzar a trabajar con el plugin, se recomienda encarecidamente inicializar el módulo **BlueprintNativizationModule**. Este módulo sirve como el espacio donde se guardan todos los códigos C++ para la posterior migración de los activos de Blueprint. El código resultante estará estructurado. Para iniciar, ve a Other Action, haz clic en el botón Initialize Blueprint Initialization Module y recompila el proyecto. Si todo se hace correctamente, el estado del módulo en la parte superior de tu barra de herramientas cambiará. Este acción necesita realizarse SOLO UNA VEZ, ANTES DE EMPEZAR A TRABAJAR.

En Other Actions, se incluyen otras funcionalidades útiles, aparte de Initialize Blueprint Initialization Module, como Reset Names. El proyecto tiene como objetivo evitar meticulosamente conflictos de nombres que pueden ocurrir si realizas la nativización en series sin aplicar los resultados del código generado. Los conflictos de nombres se resuelven utilizando un sistema designado específicamente como Unique Name, y aunque no es necesario resetearlo, a menudo es útil, ya que limpia la caché innecesaria en el sistema para variables asignadas temporalmente. No obstante, se recomienda realizar la nativización paso a paso, transfiriendo los objetos de código generados a los objetos BP, o en una gran serie. Esto se debe en parte a que la caché solo existe dentro de una sesión de Unreal Engine y no persiste más tiempo.

**PrintAllK2Nodes** – ignóralo, es para desarrolladores en el futuro.

Las configuraciones del widget están diseñadas para ajustarse continuamente.
</details>

* * * * * * * * * * * * * * * * * * 

<h2 align="center">
  <a href="#-tabla-de-contenidos">⬆️ Volver al Inicio</a> 
</h2>

<h1 align="center"> 
Ejemplo de Uso
</h1>

<div align="center">
  <img style="width: 80%; height: auto;" alt="Interfaz de Nativización" src="./media/Tutorial\Article_1/image5.png"/>
</div>

<details>
  <summary align="center">⚙️ Expandir Descripción</summary>

1. Ve a la pestaña **Run Nativization**.
2. Especifica cualquiera de tus activos en Blueprints.
   - Asegúrate de que el Blueprint esté compilado y guardado.
   - Puedes especificar uno o varios activos.
   - Todas las entidades especificadas en Blueprints, así como las entidades dependientes de los Blueprints, también estarán sujetas a nativización.
3. Haz clic en el botón **Apply**.
   - En la parte inferior del editor, verás el código resultante.

Para probar el plugin, puedes usar tu propio **TestNativizationActor**, o cualquier otro de la carpeta Tests.


</details>


* * * * * * * * * * * * * * * * * * 

<h2 align="center">
  <a href="#-tabla-de-contenidos">⬆️ Volver al Inicio</a> 
</h2>

<h1 align="center"> 
Configuraciones de Nativización
</h1>

<div align="center">
  <img style="width: 80%; height: auto;" alt="Configuración de Nativización" src="./media/Tutorial\Article_1/image5.png"/>
</div>

<details>
  <summary align="center">⚙️ Expandir Descripción</summary>

---

### Generate Code One Function

Permite generar código solo para una función seleccionada. Marca True y selecciona el nombre de la función en el campo Function Name. Las funciones pueden generarse como una serie si hay múltiples funciones. Por ejemplo, situaciones donde se selecciona una función de Input Action o una función se divide en subfunciones durante la nativización.

---

### Transform Only One File Code

Genera código solo para los Blueprints actualmente especificados, ignorando toda recursión de dependencias.

---

### SaveToFile

Permite guardar el resultado completo en un archivo. Esta función y muchas otras posteriores no funcionarán correctamente si el BlueprintNativizationModule no está inicializado.

A menudo se considera una buena práctica dejar todas las referencias de Activo en el lado de Blueprint.

**Left All Asset Ref In Blueprint** – es un flag para implementar esto. De lo contrario, todas las referencias se codificarán directamente en C++.

---

### Visualization

Flags responsables de lo que se mostrará en el editor abajo. Puedes desactivar el código Header o CPP.

---

### SaveOutputFolder

Establece la ubicación para guardar el código C++ generado. Si dejas este campo vacío, se guardará en BlueprintNativizationModule inicializado, distribuido correctamente en las carpetas.

---

### Hot Reload and Replace

Una función experimental que permite reemplazar automáticamente los Blueprints con la clase C++ generada sin reiniciar el proyecto. Tiene problemas y, por tanto, a menudo se reemplaza por Save Cache.

---

### Save Cache

Guarda información sobre qué objetos se generaron a partir de qué. Esto permite corregir errores y recompilar el proyecto, mientras se conserva la capacidad de reemplazar los Blueprints usados para generación con el código C++ generado entre sesiones de Unreal Engine.

---

### Cache Path

Similar a Save Output Folder, guarda los datos de la caché en una carpeta. De lo contrario, guardará en la raíz de BlueprintNativizationModule.

Para usar la caché, puedes usar la pestaña **Apply From Cache Nativization Result**. Ten en cuenta que aplicar la caché implica reiniciar el proyecto Unreal Engine, de lo contrario, las clases CDO no se inicializarán.

Dejar Cache Path vacío predeterminadamente guardará la caché en la raíz del proyecto.

---

</details>

* * * * * * * * * * * * * * * * * * 

<h2 align="center">
  <a href="#-tabla-de-contenidos">⬆️ Volver al Inicio</a> 
</h2>

<h1 align="center"> 
Otras Acciones y Configuraciones
</h1>

<div align="center">
  <img style="width: 80%; height: auto;" alt="Configuraciones del Editor" src="./media/Tutorial\Article_1/image3.png"/>
</div>

<details>
  <summary align="center">⚙️ Expandir Descripción</summary>

Para configuraciones más permanentes, existe Blueprint Nativization V2 Editor Settings en Editor Settings.

En resumen, además de configuraciones como las utilizadas por Translator, los datos presentados aquí intentan complementar información que es inaccesible debido a la reflexión limitada.
Presumiblemente, en el futuro, estos datos se llenarán automáticamente.
</details>

* * * * * * * * * * * * * * * * * * 

<h2 align="center">
  <a href="#-tabla-de-contenidos">⬆️ Volver al Inicio</a> 
</h2>

<h1 align="center"> 
Ajustes de Nativización
</h1>

<div align="center">
  <img style="width: 60%; height: auto;" alt="UI Adicional" src="./media/Tutorial\Article_1/image4.png"/>
</div>

<details>
  <summary align="center">⚙️ Expandir Descripción</summary>

La fuente principal para convertir funciones de Blueprint a C++ es un conjunto de traductores. **Translator** maneja uno o varios tipos de K2Node y los traduce en código C++.

---

### Global Variable Name

Sirve para evitar conflictos de nombres.

---

### Setup Action Object

Enlaza EU_NativizationTool con varios Widgets auxiliares en el lado del Blueprint. Se aconseja no cambiar esta configuración.

---

### Enable Generate Value Suffix

Determina si todas las variables generadas en el código C++ llevarán el sufijo GeneratedValue. Es mejor desactivarlo para un código más limpio.

---

### Add BP Prefix To Parent Blueprint

Especifica si las clases de Blueprint existentes convertidas a C++ recibirán el prefijo "BP_" cuando se reconstruyan.

---

### Function Redirects

Una lista de funciones Implemented en Blueprint. Estas funciones carecen de metadatos suficientes para determinar qué función las llama o dónde sobrescribirlas en C++. Este array proporciona la correlación entre el nombre de la función Implemented en Blueprint y la función original en C++.

---

### Construction Descriptors

Constructores de estructuras donde usar un constructor para inicialización es la opción "correcta". En otros casos, los valores se establecen directamente mediante ‘.’, lo que significa que en lugar de, por ejemplo, FLinearColor(0.0,0.66,1.0,1.0), la generación será FLinearColor LinearColor(); LinearColor.R = 1.0; LinearColor.G = 1.0; y así sucesivamente. También se implementa debido a la falta de reflexión para esto. Para estructuras generadas en Blueprints, su constructor completo se implementa por defecto.

---

### Ignore Class to Ref Generate

Incluye todas las clases que no deberían someterse a nativización. Estas suelen ser UI, Widgets, etc. No alteres este widget ni intentes generar C++ para UI, ya que esto causará un fallo.

---

### Ignore Assets to Ref Generate

Similar.

---

### Getter And Setter Description

FProperties carecen de información sobre la privacidad de la variable o visibilidad pública. Esto lleva a fallos al acceder a objetos de nivel superior debido a la falta de acceso a variables. Este array existe para asignar funciones a variables para operaciones como Get, Set, o ignorarlo por completo.

---

### Code Editor

Información para la visualización en el Text Editor del widget principal. Principalmente contiene indicaciones para resaltar ciertas subcadenas en colores específicos.

---

</details>


* * * * * * * * * * * * * * * * * * 
* * * * * * * * * * * * * * * * * * 


<h1 align="center"> 📜 Licencia</h1>
<h2 align="center">
  <strong>---></strong>
  <strong> Este proyecto está distribuido bajo la </strong> 
  <a href="./LICENSE">SoulofAO License</a>
  <strong><---</strong>
</h1>

---

<h2 align="center"> 
📚 Revisa la Documentación 
</h2>

<p align="center">
  <strong>---></strong>
  <a href="/README.md">Ruso</a> |
  <a href="/README.en.md">Inglés</a> |
  <a href="/README.es.md">Español</a> |
  <a href="/README.zh.md">Chino</a> |
  <strong><---</strong>
</p>
