/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CppSourceParser.h"

struct FCppTypeResolution
{
    std::string QualifiedName;
    std::vector<std::string> TemplateArguments;
    std::vector<std::string> AliasChain;
    std::vector<std::string> Candidates;
    std::string Error;
    bool bResolved = false;
};

class CPPSOURCEPARSER_API FCppSourceIndex
{
public:
    std::vector<FCppSourceDocument> Documents;
    FCppTypeResolution ResolveType(const std::string& Name, const std::string& Scope = {}) const;
    static std::string SubstituteTemplateParameters(const std::string& Text,
        const std::vector<std::string>& Parameters, const std::vector<std::string>& Arguments);
};
