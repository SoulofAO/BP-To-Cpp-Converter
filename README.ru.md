# BP to CPP Converter

Плагин создаёт исходники C++ из поддерживаемых Blueprint-классов, интерфейсов, структур и перечислений Unreal Engine. Результат нужно проверить и собрать перед миграцией ассетов.

Требования: Unreal Engine 5.8, Windows x64, настроенный компилятор C++ и Windows SDK. Плагин работает в редакторе; игровой код создаётся в отдельном модуле проекта.

**Обязательные зависимости:** EnhancedInput, ControlRig и Niagara входят в Unreal Engine и должны быть включены. SemanticSearch необязателен и по умолчанию отключён.

[Руководство на английском](BlueprintNativizationV2/Documentation/UserGuide.md) | [English](README.md) | [Español](README.es.md) | [中文](README.zh.md)

## Установка

Закройте Unreal Editor, поместите `BlueprintNativizationV2` в `Plugins` проекта и соберите Editor target. После перезапуска откройте **Tools > Blueprint to C++**.

## Порядок работы

1. **Setup:** подготовьте модуль результата, соберите проект и перезапустите редактор. Постройте Project API Cache и Engine API Cache, проверьте доступ к нативному API.
2. **Assets:** выберите ассеты и соберите зависимости. Перед каждой генерацией выполните Analyze Blueprint Code → TryFix → Analyze Blueprint Code. TryFix может менять исходные ассеты и сохраняет резервные копии.
3. **Preview:** создайте результат в памяти, проверьте документы и диагностику.
4. **Export:** сохраните результат, закройте редактор, соберите проект и перезапустите его.
5. **Migration:** при необходимости проверьте и примените соответствующий кеш миграции к существующим ассетам.
6. **Tools:** используйте диагностику, действия с кешем и настройки плагина.

Миграция изменяет ассеты проекта. Сохраняйте резервную копию и отдельно проверяйте игровой сценарий, cook и packaged runtime. Construction Script пока переводится в OnConstruction с другой фазой исполнения; полная эквивалентность произвольных Blueprint не заявляется.

## Исходники

В плагине четыре Editor-модуля Win64: `CppSourceParser`, `NativeApiCache`, `BlueprintSourceAnalysis` и `BlueprintNativizationV2`. Локальные API-кеши находятся в `Saved/BlueprintNativizationV2/NativeApiCache` проекта и не входят в поставку.

[Лицензия](LICENSE). Поддержка: sgsggssg1@gmail.com.
