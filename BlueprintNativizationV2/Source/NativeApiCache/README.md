# Native API source cache

NativeApiCache is a standalone C++17 library consuming CppSourceParser. It builds a source-backed catalog for Blueprint-to-C++ conversion without depending on Unreal headers or invoking a compiler. The BlueprintSourceAnalysis editor module supplies live reflection and user-facing actions.

FNativeApiCacheBuilder::Build accepts original source paths, module/include provenance, optional define arguments, a configuration identity and optional reflection snapshots. It returns type/member/callable records, constructor initializer expressions, parameter defaults, simple getter/setter relationships, direct forwarding candidates and explicit resolution failures. ToJson writes the versioned snapshot. Source fingerprints cover file bytes. This catalog is separate from Blueprint migration caches.

Each callable retains declaration and definition evidence, source locations and conditions. Direct forwarding is a candidate, not a verified function redirect: overload resolution and side effects require a consumer-specific check. Constructor initializers are source expressions, not proof of value reconstruction. Public access, conditions, missing members and type mismatches must be checked before generation.

Cross-file aliases and template owners use CppSourceIndex. Template arguments are substituted at identifier boundaries for reflected member comparison. Anonymous union/struct members keep their lexical container and their enclosing semantic owner. Reflected property matches identify both missing names and textual type mismatches; this is not universal canonical type equivalence.

The editor API BuildSourceCache saves JSON and reports parse failures. ValidateSourceCache checks schema, engine version, successful parse status and every recorded source fingerprint. It does not certify semantic completeness. Rebuild is explicit, not automatic. Project generation scans Project/Source; editor generation asks for a source directory, preferably a module, and scans its original .h/.cpp files. No UHT output is accepted.

The current library does not replace generator Settings tables or choose structure Translators. Those are consumers of the catalog. A future consumer must select either a complete Translator or cache for a type and must never supplement a selected Translator with cache data. Inherited lookup, general overload resolution, indirect getters, arbitrary constructor dataflow and macro-generated APIs remain unresolved.
