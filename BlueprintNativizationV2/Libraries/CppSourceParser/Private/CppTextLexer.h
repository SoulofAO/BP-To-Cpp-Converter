/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#pragma once
#include "CppSourceParser.h"
#include <filesystem>

struct FCppTextToken
{
    std::string Text;
    std::string Kind;
    FCppSourceRange Range;
    std::string Condition;
    int BraceDepth = 0;
    int ParenthesisDepth = 0;
    int BracketDepth = 0;
};

std::string NormalizeCppPath(const std::string &Path, const std::string &Directory = {});
std::filesystem::path MakeCppSourcePath(const std::string& Text);
std::vector<FCppTextToken> TokenizeCppText(const std::string &File, const std::string &Text,
                                           std::vector<FCppDiagnostic> &Diagnostics);
std::vector<std::string> SplitCppCommand(const std::string &Text);
