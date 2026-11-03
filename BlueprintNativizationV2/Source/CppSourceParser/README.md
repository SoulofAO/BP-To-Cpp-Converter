# C++ source parser

Standalone C++17 structural analysis of original C++ text. No Unreal, LLVM, libclang, UHT or external parser dependency. Build the CppSourceParser static target using CMake 3.24 or newer and a C++17 compiler.

## Input and API

FCppSourceParser::Parse accepts a source path, working directory, optional unsaved files and include/define arguments. No compiler is launched and the process working directory is unchanged. ParseCommand accepts a compilation command; LoadCompilationDatabase reads compile_commands.json and expands response files without executing commands. Compilation databases are optional. Response files must use textual UTF-8; UTF-16 is unsupported.

bFollowIncludes controls recursive source loading. Paths come from the request and the including file directory. bRequireIncludes makes unresolved includes errors. UHT .generated.h and .gen.cpp includes are never loaded, even if present. There is no compiler-provided classification of system headers.

FindSymbols searches exact simple or qualified names. Visit traverses node children with optional pruning. GetSourceText reads retained source ranges.

## Model

FCppSourceDocument owns nodes, symbols, scopes, types, source contents and diagnostics. Indices are document-local; -1 means unavailable. Source ranges use byte offsets with an exclusive end.

Declarations include namespaces, records, enums, aliases, templates, functions, methods, parameters and variables. SemanticScope describes ownership, LexicalScope describes placement and OwnedScope identifies an entity's own scope. Reopened namespaces share scope. Qualified out-of-line methods retain their owner; an unavailable owner is represented by an unresolved scope.

Symbol identifiers are textual owner/name/signature identities, not compiler USRs. They support structural grouping within a snapshot, not semantic equivalence across aliases, configurations or arbitrary redeclarations. Type spelling and recognizable qualifiers are retained. Canonical type resolution, overload resolution, template instantiation and inherited override resolution are unavailable. Unavailable model fields remain empty; bSemanticsResolved stays false.

Function bodies retain ranges and selected expressions and local declarations, not a complete expression AST. Unrecognized declarations become UnresolvedDeclaration nodes with original text.

## Macros and conditions

The lexer preserves comments, escaped and raw literals, directives and source positions. Known function-like macros and uppercase macro invocations at declaration boundaries are recorded separately. GENERATED_BODY and other UE annotations become MacroInvocation nodes, never fields or functions. Macro bodies are not expanded and generated members are not invented. Unknown uppercase function-like names can be conservatively classified as macros.

Only simple known conditions are evaluated, not the full preprocessor expression language. Unknown branches retain ConditionalExpression and set bHasUnresolvedPreprocessing. Conditional definitions remain uncertain for later conditions. Includes within uncertain branches are not recursively loaded. An absent macro in #ifndef is treated as undefined, including ordinary header guards. Alternatives are not separate translation units: arbitrary branch-dependent partial declarations cannot be reconstructed completely.

## Boundaries

bSucceeded means no detected lexical or structural error. It does not prove complete recognition, compilability or semantic correctness. Consumers must inspect diagnostics, unresolved nodes, conditional context and macro expansion requirements.

Constructor/getter/setter inference, reflection matching, conversion policies, module inference and cache serialization belong to a separate consumer.

CppSourceIndex additionally resolves explicit cross-file alias chains to record/template declarations and returns their template arguments, rejecting cycles and conditional/ambiguous aliases. Identifier-aware substitution supports reflected member comparisons. This is a structural resolver, not C++ template instantiation. Function nodes also expose BodyRange, Initializers and parameter DefaultExpression. Record nodes expose BaseTypes and TemplateParameters. Overloaded operators are retained; anonymous union/struct fields preserve lexical containment while using the enclosing semantic owner.
