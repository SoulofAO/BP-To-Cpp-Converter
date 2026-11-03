/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#pragma once

#ifdef CPPSOURCEPARSER_API
#include "HAL/Platform.h"
#else
#define CPPSOURCEPARSER_API
#endif

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

struct FCppSourceLocation
{
    std::string File;
    unsigned Line = 0;
    unsigned Column = 0;
    unsigned Offset = 0;
};

struct FCppSourceRange
{
    FCppSourceLocation Begin;
    FCppSourceLocation End;
};

enum class ECppDiagnosticSeverity
{
    Ignored, Note, Warning, Error, Fatal
};

struct FCppDiagnostic
{
    ECppDiagnosticSeverity Severity = ECppDiagnosticSeverity::Error;
    std::string Message;
    FCppSourceLocation Location;
};

enum class ECppNodeCategory
{
    TranslationUnit, Declaration, Reference, Expression, Statement, Attribute, Preprocessing, Other
};

enum class ECppAccess
{
    Unspecified, Public, Protected, Private
};

struct FCppToken
{
    std::string Kind;
    std::string Spelling;
    FCppSourceRange Range;
};

struct FCppType
{
    std::string Kind;
    std::string Spelling;
    std::string CanonicalSpelling;
    std::string DeclarationSymbol;
    bool bConst = false;
    bool bVolatile = false;
    bool bRestrict = false;
    bool bVariadic = false;
    bool bHasUnexposedTemplateArguments = false;
    int PointeeType = -1;
    int ElementType = -1;
    int ResultType = -1;
    int UnderlyingType = -1;
    std::int64_t ArraySize = -1;
    std::vector<int> ArgumentTypes;
    std::vector<int> TemplateArgumentTypes;
};

struct FCppSyntaxNode
{
    std::string Kind;
    ECppNodeCategory Category = ECppNodeCategory::Other;
    std::string Name;
    std::string DisplayName;
    std::string Symbol;
    std::string SemanticScope;
    std::string LexicalScope;
    std::string OwnedScope;
    std::string ReferencedSymbol;
    std::string SpecializedTemplate;
    std::string Operator;
    std::string IncludedFile;
    std::string ConditionalExpression;
    std::string DefaultExpression;
    std::vector<std::string> BaseTypes;
    std::vector<std::string> TemplateParameters;
    std::unordered_map<std::string, std::string> Initializers;
    FCppSourceRange BodyRange;
    bool bRequiresMacroExpansion = false;
    bool bSemanticsResolved = false;
    bool bEnumValueKnown = false;
    std::vector<std::string> OverloadedSymbols;
    std::vector<std::string> OverriddenSymbols;
    FCppSourceRange Range;
    FCppSourceLocation SpellingLocation;
    int Parent = -1;
    int Type = -1;
    int ResultType = -1;
    int UnderlyingType = -1;
    int BitWidth = -1;
    ECppAccess Access = ECppAccess::Unspecified;
    bool bDefinition = false;
    bool bStatic = false;
    bool bConst = false;
    bool bVirtual = false;
    bool bOverride = false;
    bool bPureVirtual = false;
    bool bDeleted = false;
    bool bDefaulted = false;
    bool bAbstract = false;
    bool bExplicit = false;
    bool bVirtualBase = false;
    bool bScopedEnum = false;
    std::int64_t EnumValue = 0;
    std::uint64_t UnsignedEnumValue = 0;
    std::vector<int> Children;
    std::vector<int> Parameters;
    std::vector<FCppToken> Tokens;
};

struct FCppSymbol
{
    std::string Identifier;
    std::string Name;
    std::string QualifiedName;
    std::string Kind;
    std::string Parent;
    std::vector<int> Declarations;
    std::vector<int> Definitions;
};

struct FCppScope
{
    std::string Identifier;
    std::string Parent;
    std::string OwnerSymbol;
    std::vector<int> Members;
};

struct FCppSourceDocument
{
    std::string SourceFile;
    std::string ParserVersion;
    bool bHasUnresolvedPreprocessing = false;
    std::vector<std::string> CompilerArguments;
    std::vector<FCppSyntaxNode> Nodes;
    std::vector<FCppType> Types;
    std::unordered_map<std::string, FCppSymbol> Symbols;
    std::unordered_map<std::string, FCppScope> Scopes;
    std::vector<std::string> IncludedFiles;
    std::vector<FCppDiagnostic> Diagnostics;
    std::unordered_map<std::string, std::string> SourceFiles;
    bool bSucceeded = false;

    CPPSOURCEPARSER_API std::vector<std::string> FindSymbols(const std::string& Name) const;
    CPPSOURCEPARSER_API std::string GetSourceText(const FCppSourceRange& Range) const;
    CPPSOURCEPARSER_API void Visit(int Root, const std::function<bool(int, const FCppSyntaxNode&)>& Visitor) const;
};

struct FCppUnsavedFile
{
    std::string File;
    std::string Contents;
};

struct FCppParseRequest
{
    std::string SourceFile;
    std::string WorkingDirectory;
    std::vector<std::string> Arguments;
    std::vector<FCppUnsavedFile> UnsavedFiles;
    bool bCaptureTokens = true;
    bool bFollowIncludes = true;
    bool bRequireIncludes = false;
};

struct FCppCompileCommand
{
    std::string SourceFile;
    std::string WorkingDirectory;
    std::vector<std::string> Arguments;
};

class CPPSOURCEPARSER_API FCppSourceParser
{
public:
    static FCppSourceDocument Parse(const FCppParseRequest& Request);
    static FCppSourceDocument ParseCommand(const FCppCompileCommand& Command, bool bCaptureTokens = false);
    static std::vector<FCppCompileCommand> LoadCompilationDatabase(
        const std::string& Directory, std::string& Error);
};
