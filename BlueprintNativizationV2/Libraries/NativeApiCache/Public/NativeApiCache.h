/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CppSourceIndex.h"
#include <map>

struct FNativeSourceInput
{
    std::string File;
    std::string Module;
    std::string Include;
};

struct FNativeReflectionType
{
    std::string Path;
    std::string NativeName;
    std::map<std::string, std::string> Properties;
};

struct FNativeApiCacheRequest
{
    std::vector<FNativeSourceInput> Sources;
    std::vector<FNativeReflectionType> ReflectedTypes;
    std::vector<std::string> Arguments;
    std::string Configuration;
};

struct FNativeApiEvidence
{
    std::string File;
    unsigned Line = 0;
    std::string Condition;
    std::string Text;
    unsigned BeginOffset = 0;
    unsigned EndOffset = 0;
};

struct FNativeApiParameter
{
    std::string Name;
    std::string Type;
    std::string DefaultExpression;
};

struct FNativeApiForwarding
{
    int RootParameter = -1;
    std::vector<std::string> Members;
    std::string Method;
    std::vector<int> Arguments;
};

struct FNativeApiCallable
{
    std::string Include;
    std::string Module;
    std::string Name;
    std::string Signature;
    std::string ReturnType;
    std::vector<FNativeApiParameter> Parameters;
    std::map<std::string, std::string> Initializers;
    std::vector<FNativeApiEvidence> Evidence;
    std::string Body;
    std::vector<std::string> ForwardingExpression;
    FNativeApiForwarding PublicForwarding;
    std::string ReadMember;
    std::string WriteMember;
    std::string ForwardedCall;
    std::map<std::string, std::vector<std::string>> DirectCalls;
    struct FCallSite
    {
        std::string Name;
        std::string Receiver;
        std::vector<std::string> Arguments;
        size_t TokenPosition = 0;
    };
    std::vector<FCallSite> CallSites;
    ECppAccess Access = ECppAccess::Unspecified;
    bool bConstructor = false;
    bool bDestructor = false;
    bool bAccessVerified = true;
    bool bConst = false;
    bool bStatic = false;
    bool bDeleted = false;
    bool bVirtual = false;
    bool bExported = false;
    bool bInlineDefinition = false;
};

struct FNativeApiMember
{
    std::string Name;
    std::string Type;
    ECppAccess Access = ECppAccess::Unspecified;
    FNativeApiEvidence Evidence;
};

struct FNativeApiType
{
    bool bDefinition = false;
    bool bExported = false;
    std::string Name;
    std::string Module;
    std::string Include;
    std::vector<std::string> BaseTypes;
    std::vector<std::string> TemplateParameters;
    std::vector<FNativeApiMember> Members;
    std::vector<FNativeApiCallable> Callables;
    std::vector<FNativeApiEvidence> Evidence;
    FNativeApiEvidence Declaration;
    FNativeApiEvidence GeneratedBody;
    bool bReflectedClass = false;
    bool bMinimalApi = false;
    bool bCustomConstructor = false;
    bool bLegacyGeneratedBody = false;
    ECppAccess GeneratedBodyAccess = ECppAccess::Unspecified;
};

struct FNativeApiReflectionMatch
{
    std::string Path;
    std::string NativeName;
    FCppTypeResolution Resolution;
    std::vector<std::string> MatchedProperties;
    std::vector<std::string> MissingProperties;
    std::vector<std::string> TypeMismatches;
    std::map<std::string, std::string> ResolvedMemberTypes;
};

struct FNativeApiSourceStatus
{
    FNativeSourceInput Source;
    std::string Fingerprint;
    bool bParsed = false;
    unsigned UnresolvedDeclarations = 0;
    unsigned MacroInvocations = 0;
    std::vector<FCppDiagnostic> Diagnostics;
};

struct FNativeApiConstant
{
    std::string Name;
    std::string Include;
    std::string Module;
    std::string Expression;
    FNativeApiEvidence Evidence;
};

struct FNativeApiCache
{
    std::string Configuration;
    std::vector<std::string> Arguments;
    std::map<std::string, FNativeApiType> Types;
    std::vector<FNativeApiCallable> Functions;
    std::vector<FNativeApiConstant> Constants;
    std::vector<FNativeApiSourceStatus> Sources;
    std::vector<FNativeApiReflectionMatch> ReflectionMatches;
    bool bSucceeded = false;
    std::string ToJson() const;
};

class FNativeApiCacheBuilder
{
public:
    static FNativeApiCache Build(const FNativeApiCacheRequest& Request);
};
