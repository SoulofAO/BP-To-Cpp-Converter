/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TestParseCppCodeLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Logging/LogMacros.h"
#include <regex>
#include <set>
#include "BlueprintNativizationSettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogFunctionRedirectMapper, Log, All);

namespace
{
    // Helpers for conversions
    static std::string ToStd(const FString& In)
    {
        FTCHARToUTF8 Conv(*In);
        return std::string((const char*)Conv.Get(), Conv.Length());
    }

    static FString ToFString(const std::string& In)
    {
        return FString(UTF8_TO_TCHAR(In.c_str()));
    }

    // Remove comments and string literals from C++ source (simple state machine)
    static void RemoveCommentsAndStrings(std::string& S)
    {
        enum State { Normal, MaybeSlash, CppComment, CComment, DQuote, SQuote, Escape } state = Normal;
        std::string Out;
        Out.reserve(S.size());

        for (size_t i = 0; i < S.size(); ++i)
        {
            char c = S[i];
            switch (state)
            {
            case Normal:
                if (c == '/') state = MaybeSlash;
                else if (c == '"') { Out += '\"'; state = DQuote; }
                else if (c == '\'') { Out += '\''; state = SQuote; }
                else Out += c;
                break;
            case MaybeSlash:
                if (c == '/') state = CppComment;
                else if (c == '*') state = CComment;
                else { Out += '/'; Out += c; state = Normal; }
                break;
            case CppComment:
                if (c == '\n') { Out += '\n'; state = Normal; }
                break;
            case CComment:
                if (c == '*' && i + 1 < S.size() && S[i + 1] == '/') { ++i; state = Normal; }
                break;
            case DQuote:
                if (c == '\\') state = Escape;
                else if (c == '"') { Out += '"'; state = Normal; }
                // else skip characters inside string (we keep opening/closing only)
                break;
            case SQuote:
                if (c == '\\') state = Escape;
                else if (c == '\'') { Out += '\''; state = Normal; }
                break;
            case Escape:
                // skip escaped char, go back to whichever quote we're in (approx)
                // We cannot know easily which quote: just return to Normal to be robust.
                state = Normal;
                break;
            }
        }
        S.swap(Out);
    }

    // Find function definitions Class::Func(...) { ... }
    // Returns tuples (ClassName, FuncName, headerPos, bodyOpenPos, bodyText)
    static std::vector<std::tuple<std::string, std::string, size_t, size_t, std::string>> FindCppFunctionDefs(const std::string& Text)
    {
        std::vector<std::tuple<std::string, std::string, size_t, size_t, std::string>> Results;

        // Regex for Class::Func(...) {   (rough)
        std::regex HeaderRegex(R"(([_A-Za-z][_A-Za-z0-9:]*?)::([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)\s*(?:const\s*)?(?:noexcept\s*)?(?:->[^\\{]+)?\{)",
            std::regex::ECMAScript | std::regex::icase);

        auto it = std::sregex_iterator(Text.begin(), Text.end(), HeaderRegex);
        auto end = std::sregex_iterator();

        for (; it != end; ++it)
        {
            std::smatch m = *it;
            size_t headerPos = m.position(0);
            // find opening brace '{' position (m.length includes up to '{', but be robust)
            size_t bracePos = Text.find('{', headerPos + m.length(0) - 1);
            if (bracePos == std::string::npos) continue;

            // find matching brace (simple depth counting)
            size_t idx = bracePos;
            int depth = 0;
            for (; idx < Text.size(); ++idx)
            {
                if (Text[idx] == '{') ++depth;
                else if (Text[idx] == '}') { --depth; if (depth == 0) break; }
            }
            if (idx >= Text.size()) continue;

            size_t bodyClose = idx;
            std::string body = Text.substr(bracePos, bodyClose - bracePos + 1);
            std::string className = m[1].str();
            std::string funcName = m[2].str();
            Results.emplace_back(className, funcName, headerPos, bracePos, body);
        }

        return Results;
    }

    // Find member-style calls inside a body (this->Foo(   or Something::Foo(   or Foo( )
    static std::vector<std::pair<std::string, size_t>> FindMemberCalls(const std::string& Body)
    {
        std::vector<std::pair<std::string, size_t>> Calls;
        std::regex CallRegex(R"((?:this->|[_A-Za-z][_A-Za-z0-9:]*::)?([A-Za-z_][A-Za-z0-9_]*)\s*\()",
            std::regex::ECMAScript);
        auto it = std::sregex_iterator(Body.begin(), Body.end(), CallRegex);
        auto end = std::sregex_iterator();

        static std::set<std::string> Blacklist = { "if","for","while","switch","return","sizeof","new","delete","catch","static_assert" };

        for (; it != end; ++it)
        {
            std::smatch m = *it;
            std::string name = m[1].str();
            if (Blacklist.find(name) != Blacklist.end()) continue;
            Calls.emplace_back(name, m.position(1));
        }
        return Calls;
    }

    // Count occurrences of "if (" or "if(" between indices (after removing comments/strings)
    static int CountIfsBetween(const std::string& Text, size_t BeginIdx, size_t CallIdx)
    {
        if (CallIdx <= BeginIdx) return 0;
        std::string Sub = Text.substr(BeginIdx, CallIdx - BeginIdx);
        RemoveCommentsAndStrings(Sub);
        std::regex IfRegex(R"(\bif\s*\()", std::regex::ECMAScript | std::regex::icase);
        int count = 0;
        auto it = std::sregex_iterator(Sub.begin(), Sub.end(), IfRegex);
        auto end = std::sregex_iterator();
        for (; it != end; ++it) ++count;
        return count;
    }

    // Try recover parameter list from an array of file texts by searching for Class::Func(...) pattern
    static TArray<FString> ExtractParamListFromDecls(const TArray<FString>& AllTexts, const FString& ClassName, const FString& FuncName)
    {
        TArray<FString> Result;
        std::string Pattern = "\\b" + ToStd(ClassName) + "::" + ToStd(FuncName) + "\\s*\\(([^)]*)\\)";
        std::regex R(Pattern, std::regex::ECMAScript);

        for (const FString& TxtF : AllTexts)
        {
            std::string Txt = ToStd(TxtF);
            auto it = std::sregex_iterator(Txt.begin(), Txt.end(), R);
            auto end = std::sregex_iterator();
            for (; it != end; ++it)
            {
                std::smatch m = *it;
                std::string paramsRaw = m[1].str();
                // split by commas at top level (consider <> () [])
                std::vector<std::string> parts;
                std::string cur;
                int depth = 0;
                for (char c : paramsRaw)
                {
                    if (c == '<' || c == '(' || c == '[') { ++depth; cur.push_back(c); }
                    else if (c == '>' || c == ')' || c == ']') { if (depth > 0) --depth; cur.push_back(c); }
                    else if (c == ',' && depth == 0) { if (!cur.empty()) { parts.push_back(cur); cur.clear(); } }
                    else cur.push_back(c);
                }
                if (!cur.empty()) parts.push_back(cur);

                for (auto& p : parts)
                {
                    // trim
                    size_t a = 0, b = p.size();
                    while (a < b && isspace((unsigned char)p[a])) ++a;
                    while (b > a && isspace((unsigned char)p[b - 1])) --b;
                    if (b > a)
                    {
                        Result.Add(ToFString(p.substr(a, b - a)));
                    }
                }
                return Result; // return first found
            }
        }
        return Result;
    }

    // Get list of files under SourceRoot with chosen extensions
    static void CollectFiles(const FString& SourceRoot, const TArray<FString>& Extensions, TArray<FString>& OutFiles)
    {
        IFileManager& FM = IFileManager::Get();
        TArray<FString> AllFiles;
        FM.FindFilesRecursive(AllFiles, *SourceRoot, TEXT("*.*"), true, false);
        for (const FString& P : AllFiles)
        {
            for (const FString& Ext : Extensions)
            {
                if (P.EndsWith(Ext, ESearchCase::IgnoreCase))
                {
                    OutFiles.Add(P);
                    break;
                }
            }
        }
    }
} // anonymous namespace

TArray<FFunctionBinding> UEFunctionRedirectMapper::FindBindings(const FString& SourceRoot, int32 MaxIfs, const TArray<FString>& Extensions)
{
    TArray<FFunctionBinding> Results;
    if (SourceRoot.IsEmpty())
    {
        UE_LOG(LogFunctionRedirectMapper, Error, TEXT("SourceRoot is empty."));
        return Results;
    }

    TArray<FString> Files;
    CollectFiles(SourceRoot, Extensions, Files);
    UE_LOG(LogFunctionRedirectMapper, Log, TEXT("Scanning %d candidate files under %s"), Files.Num(), *SourceRoot);

    // Read all files
    TArray<FString> FileTexts;
    FileTexts.Reserve(Files.Num());
    for (const FString& P : Files)
    {
        FString Content;
        if (!FFileHelper::LoadFileToString(Content, *P))
        {
            UE_LOG(LogFunctionRedirectMapper, Warning, TEXT("Failed to read %s"), *P);
            FileTexts.Add(FString());
            continue;
        }
        FileTexts.Add(Content);
    }

    // Collect candidates
    struct CandidateTmp { FString Class; FString Caller; FString Callee; int32 NumIfs; FString SourceFile; TArray<FString> CallerParams; TArray<FString> CalleeParams; };
    TArray<CandidateTmp> Candidates;

    for (int32 idx = 0; idx < Files.Num(); ++idx)
    {
        const FString& Path = Files[idx];
        const std::string TextStd = ToStd(FileTexts[idx]);

        auto defs = FindCppFunctionDefs(TextStd);
        for (auto& d : defs)
        {
            std::string className, funcName, body;
            size_t headerPos, bodyOpen;
            std::tie(className, funcName, headerPos, bodyOpen, body) = d;
            auto calls = FindMemberCalls(body);
            for (auto& c : calls)
            {
                std::string callee = c.first;
                size_t relIdx = c.second;
                size_t callIdx = bodyOpen + relIdx;
                int nifs = CountIfsBetween(TextStd, bodyOpen, callIdx);
                if (nifs <= MaxIfs)
                {
                    CandidateTmp cand;
                    cand.Class = ToFString(className);
                    cand.Caller = ToFString(funcName);
                    cand.Callee = ToFString(callee);
                    cand.NumIfs = nifs;
                    cand.SourceFile = Path;
                    cand.CalleeParams = ExtractParamListFromDecls(FileTexts, cand.Class, cand.Callee);
                    cand.CallerParams = ExtractParamListFromDecls(FileTexts, cand.Class, cand.Caller);
                    Candidates.Add(cand);
                }
            }
        }
    }

    // Deduplicate: keep candidate with lowest NumIfs
    TMap<FString, CandidateTmp> Unique;
    for (const CandidateTmp& c : Candidates)
    {
        FString Key = c.Class + TEXT("::") + c.Callee + TEXT("->") + c.Caller;
        if (!Unique.Contains(Key) || c.NumIfs < Unique[Key].NumIfs)
        {
            Unique.Add(Key, c);
        }
    }

    // Fill result bindings
    for (auto& Pair : Unique)
    {
        const CandidateTmp& m = Pair.Value;
        FFunctionDescriptor BlueprintDesc(m.Class, m.Callee, m.CalleeParams);
        FFunctionDescriptor NativeDesc(m.Class, m.Caller, m.CallerParams);
        FFunctionBinding Bind;
        Bind.BlueprintFunction = BlueprintDesc;
        Bind.NativeFunction = NativeDesc;
        Results.Add(Bind);
    }

    UE_LOG(LogFunctionRedirectMapper, Log, TEXT("FindBindings produced %d candidate bindings."), Results.Num());
    return Results;
}

TArray<FFunctionBinding> UEFunctionRedirectMapper::FindEngineBindings(int32 MaxIfs)
{
    FString EngineSourcePath = FPaths::EngineSourceDir();

    TArray<FString> Extensions;
    Extensions.Add(TEXT(".cpp"));
    Extensions.Add(TEXT(".h"));

    return FindBindings(EngineSourcePath, MaxIfs, Extensions);
}
