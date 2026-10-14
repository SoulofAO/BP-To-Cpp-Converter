#include "CppSourceIndex.h"
#include "CppTextLexer.h"
#include <algorithm>
#include <set>

std::string FCppSourceIndex::SubstituteTemplateParameters(const std::string &Text,
                                                          const std::vector<std::string> &Parameters,
                                                          const std::vector<std::string> &Arguments)
{
    std::vector<FCppDiagnostic> Diagnostics;
    const auto Tokens = TokenizeCppText({}, Text, Diagnostics);
    std::string Result;
    size_t Start = 0;
    for (const auto &Token : Tokens)
    {
        const auto Found = std::find(Parameters.begin(), Parameters.end(), Token.Text);
        if (Token.Kind != "Identifier" || Found == Parameters.end())
        {
            continue;
        }
        const size_t ParameterIndex = static_cast<size_t>(Found - Parameters.begin());
        if (ParameterIndex >= Arguments.size())
        {
            continue;
        }
        Result += Text.substr(Start, Token.Range.Begin.Offset - Start);
        Result += Arguments[ParameterIndex];
        Start = Token.Range.End.Offset;
    }
    return Result + Text.substr(Start);
}

static std::string TrimIndexText(const std::string &Text)
{
    const size_t Begin = Text.find_first_not_of(" \t\r\n");
    if (Begin == std::string::npos)
    {
        return {};
    }
    return Text.substr(Begin, Text.find_last_not_of(" \t\r\n") - Begin + 1);
}

FCppTypeResolution FCppSourceIndex::ResolveType(const std::string &Name, const std::string &Scope) const
{
    FCppTypeResolution Result;
    std::string Current = TrimIndexText(Name);
    std::string CurrentScope = Scope;
    std::set<std::string> Visited;
    for (unsigned Depth = 0; Depth < 64; ++Depth)
    {
        if (!Visited.insert(CurrentScope + ":" + Current).second)
        {
            Result.Error = "Cyclic type alias.";
            return Result;
        }
        const size_t Open = Current.find('<');
        if (Open != std::string::npos)
        {
            const size_t Close = Current.find_last_of('>');
            if (Close == std::string::npos)
            {
                Result.Error = "Incomplete template argument list.";
                return Result;
            }
            Result.TemplateArguments.clear();
            size_t Begin = Open + 1;
            unsigned Nesting = 0;
            for (size_t Position = Begin; Position <= Close; ++Position)
            {
                if (Position == Close || (Current[Position] == ',' && Nesting == 0))
                {
                    Result.TemplateArguments.push_back(TrimIndexText(Current.substr(Begin, Position - Begin)));
                    Begin = Position + 1;
                }
                else if (Current[Position] == '<')
                {
                    ++Nesting;
                }
                else if (Current[Position] == '>')
                {
                    --Nesting;
                }
            }
            Current = TrimIndexText(Current.substr(0, Open));
        }
        std::vector<std::string> SearchNames;
        std::string SearchScope = CurrentScope;
        while (!SearchScope.empty())
        {
            SearchNames.push_back(SearchScope + "::" + Current);
            const size_t Separator = SearchScope.rfind("::");
            if (Separator == std::string::npos)
            {
                break;
            }
            SearchScope.resize(Separator);
        }
        SearchNames.push_back(Current);
        std::set<std::string> Names;
        std::set<std::string> Aliases;
        bool bConditional = false;
        for (const std::string &Search : SearchNames)
        {
            for (const auto &Document : Documents)
            {
                for (const auto &Entry : Document.Symbols)
                {
                    const auto &Symbol = Entry.second;
                    if (Symbol.QualifiedName != Search)
                    {
                        continue;
                    }
                    if (Symbol.Kind != "TypeAliasDecl" && Symbol.Kind != "StructDecl" && Symbol.Kind != "ClassDecl" &&
                        Symbol.Kind != "ClassTemplate" && Symbol.Kind != "EnumDecl")
                    {
                        continue;
                    }
                    Names.insert(Symbol.QualifiedName);
                    for (int NodeIndex : Symbol.Declarations)
                    {
                        const auto &Node = Document.Nodes[NodeIndex];
                        bConditional |= !Node.ConditionalExpression.empty();
                        if (Node.UnderlyingType >= 0)
                        {
                            Aliases.insert(Document.Types[Node.UnderlyingType].Spelling);
                        }
                    }
                }
            }
            if (!Names.empty())
            {
                break;
            }
        }
        Result.Candidates.assign(Names.begin(), Names.end());
        if (Names.size() != 1 || Aliases.size() > 1 || bConditional)
        {
            Result.Error = "Type is missing, conditional or ambiguous.";
            return Result;
        }
        if (Aliases.empty())
        {
            Result.QualifiedName = *Names.begin();
            Result.bResolved = true;
            return Result;
        }
        Result.AliasChain.push_back(*Names.begin());
        const size_t Separator = Names.begin()->rfind("::");
        CurrentScope.clear();
        if (Separator != std::string::npos)
        {
            CurrentScope = Names.begin()->substr(0, Separator);
        }
        Current = *Aliases.begin();
    }
    Result.Error = "Type alias depth limit exceeded.";
    return Result;
}
