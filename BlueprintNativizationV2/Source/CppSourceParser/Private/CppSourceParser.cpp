/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#include "CppSourceParser.h"
#include "CppTextLexer.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <unordered_set>

static std::string TrimCppText(const std::string &Text)
{
    const size_t Start = Text.find_first_not_of(" \t\r\n");
    if (Start == std::string::npos)
    {
        return {};
    }
    return Text.substr(Start, Text.find_last_not_of(" \t\r\n") - Start + 1);
}

static bool IsMacroIdentifier(const std::string &Text)
{
    bool bHasLetter = false;
    for (unsigned char Character : Text)
    {
        if (std::islower(Character))
        {
            return false;
        }
        bHasLetter |= std::isupper(Character) != 0;
    }
    return bHasLetter;
}

class FCppTextDocumentBuilder;

class FCppDeclarationReader
{
  public:
    FCppTextDocumentBuilder &Builder;
    std::vector<FCppTextToken> Tokens;
    std::string File;
    FCppDeclarationReader(FCppTextDocumentBuilder &InBuilder, std::vector<FCppTextToken> InTokens, std::string InFile)
        : Builder(InBuilder), Tokens(std::move(InTokens)), File(std::move(InFile))
    {
    }
    std::string At(size_t Position) const;
    size_t Match(size_t Position, size_t End) const;
    std::string Join(size_t Begin, size_t End) const;
    FCppSyntaxNode MakeNode(size_t Begin, size_t End, const std::string &Kind, ECppNodeCategory Category,
                            const std::string &Scope) const;
    bool IsMacro(size_t Position, const std::string &Scope) const;
    void ReadScope(size_t Begin, size_t End, int Parent, const std::string &Scope, bool bRecord = false,
                   ECppAccess Access = ECppAccess::Unspecified);
    size_t ReadRecord(size_t Start, size_t End, int Parent, const std::string &Scope, size_t TemplateStart);
    size_t ReadDeclaration(size_t Start, size_t End, int Parent, const std::string &Scope, bool bRecord,
                           ECppAccess Access);
    int ReadVariable(size_t Begin, size_t End, int Parent, const std::string &Scope, const std::string &Kind,
                     ECppAccess Access, const std::string& SharedType = {});
    void ReadBody(size_t Begin, size_t End, int Parent, const std::string &Scope);
    void ReadExpressions(size_t Begin, size_t End, int Parent, const std::string &Scope);
};

class FCppTextDocumentBuilder
{
  public:
    const FCppParseRequest &Request;
    FCppSourceDocument Document;
    std::unordered_map<std::string, std::string> Definitions;
    std::unordered_set<std::string> FunctionMacros;
    std::unordered_set<std::string> UncertainDefinitions;
    std::unordered_set<std::string> ReadFiles;
    std::unordered_map<std::string, std::string> UnsavedFiles;
    std::vector<std::string> IncludeDirectories;
    std::unordered_map<std::string, int> TypeIndices;

    explicit FCppTextDocumentBuilder(const FCppParseRequest &InRequest) : Request(InRequest)
    {
        Document.SourceFile = NormalizeCppPath(Request.SourceFile, Request.WorkingDirectory);
        Document.ParserVersion = "CppSourceParser.Text.2";
        Document.CompilerArguments = Request.Arguments;
        Document.Scopes.emplace("global", FCppScope{"global", {}, {}, {}});
        FCppSyntaxNode Root;
        Root.Kind = "TranslationUnit";
        Root.Category = ECppNodeCategory::TranslationUnit;
        Root.OwnedScope = "global";
        Document.Nodes.push_back(Root);
        for (const auto &File : Request.UnsavedFiles)
        {
            UnsavedFiles.emplace(NormalizeCppPath(File.File, Request.WorkingDirectory), File.Contents);
        }
        for (size_t Index = 0; Index < Request.Arguments.size(); ++Index)
        {
            const std::string Argument = Request.Arguments[Index];
            for (const std::string Prefix : {"/external:I", "-isystem", "-imsvc", "-iquote", "-I", "/I"})
            {
                if (Argument == Prefix && Index + 1 < Request.Arguments.size())
                {
                    IncludeDirectories.push_back(
                        NormalizeCppPath(Request.Arguments[++Index], Request.WorkingDirectory));
                    break;
                }
                if (Argument.size() > Prefix.size() && Argument.compare(0, Prefix.size(), Prefix) == 0)
                {
                    IncludeDirectories.push_back(
                        NormalizeCppPath(Argument.substr(Prefix.size()), Request.WorkingDirectory));
                    break;
                }
            }
            if (Argument.rfind("-D", 0) == 0 || Argument.rfind("/D", 0) == 0)
            {
                std::string Value = Argument.substr(2);
                if (Value.empty() && Index + 1 < Request.Arguments.size())
                {
                    Value = Request.Arguments[++Index];
                }
                const size_t Equal = Value.find('=');
                if (Equal == std::string::npos)
                {
                    Definitions[Value] = "1";
                }
                else
                {
                    Definitions[Value.substr(0, Equal)] = Value.substr(Equal + 1);
                }
            }
        }
    }

    int AddNode(FCppSyntaxNode Node, int Parent)
    {
        const int Index = static_cast<int>(Document.Nodes.size());
        Node.Parent = Parent;
        if (!Node.Symbol.empty())
        {
            auto &Symbol = Document.Symbols.at(Node.Symbol);
            Symbol.Declarations.push_back(Index);
            if (Node.bDefinition)
            {
                Symbol.Definitions.push_back(Index);
            }
            Document.Scopes.at(Node.SemanticScope).Members.push_back(Index);
        }
        Document.Nodes.push_back(std::move(Node));
        if (Parent >= 0)
        {
            Document.Nodes[Parent].Children.push_back(Index);
        }
        return Index;
    }

    std::string QualifiedName(const std::string &Scope, const std::string &Name) const
    {
        const auto Owner = Document.Symbols.find(Scope);
        if (Owner != Document.Symbols.end() && !Owner->second.QualifiedName.empty())
        {
            return Owner->second.QualifiedName + "::" + Name;
        }
        return Name;
    }

    std::string Declare(const std::string &Scope, const std::string &Name, const std::string &Kind,
                        const std::string &Signature = {})
    {
        const std::string Identifier = "text:" + Scope + ":" + Name + Signature;
        if (!Document.Symbols.count(Identifier))
        {
            Document.Symbols.emplace(Identifier,
                                     FCppSymbol{Identifier, Name, QualifiedName(Scope, Name), Kind, Scope, {}, {}});
        }
        return Identifier;
    }

    std::string ResolveScope(const std::string &Name, const std::string &Current) const
    {
        std::string Scope = Current;
        while (!Scope.empty())
        {
            const auto Found = Document.FindSymbols(QualifiedName(Scope, Name));
            for (const auto &Candidate : Found)
            {
                if (Document.Scopes.count(Candidate))
                {
                    return Candidate;
                }
            }
            const auto Parent = Document.Scopes.find(Scope);
            if (Parent == Document.Scopes.end())
            {
                break;
            }
            Scope = Parent->second.Parent;
        }
        return {};
    }

    int ReadType(const std::string &Spelling, const std::string &Scope)
    {
        const std::string Text = TrimCppText(Spelling);
        if (Text.empty())
        {
            return -1;
        }
        const std::string Key = Scope + ":" + Text;
        const auto Existing = TypeIndices.find(Key);
        if (Existing != TypeIndices.end())
        {
            return Existing->second;
        }
        const int Index = static_cast<int>(Document.Types.size());
        TypeIndices.emplace(Key, Index);
        Document.Types.emplace_back();
        FCppType Result;
        Result.Spelling = Text;
        Result.Kind = "TextType";
        Result.bConst = Text.rfind("const ", 0) == 0 || Text.find(" const") != std::string::npos;
        Result.bVolatile = Text.find("volatile") != std::string::npos;
        if (Text.back() == '*' || Text.back() == '&')
        {
            Result.Kind = "Pointer";
            size_t Suffix = 1;
            if (Text.back() == '&')
            {
                Result.Kind = "LValueReference";
                if (Text.size() > 1 && Text[Text.size() - 2] == '&')
                {
                    Result.Kind = "RValueReference";
                    Suffix = 2;
                }
            }
            Result.PointeeType = ReadType(Text.substr(0, Text.size() - Suffix), Scope);
        }
        const size_t Array = Text.find('[');
        if (Array != std::string::npos && Text.back() == ']')
        {
            Result.Kind = "Array";
            Result.ElementType = ReadType(Text.substr(0, Array), Scope);
            try
            {
                size_t Consumed = 0;
                const std::string Bound = Text.substr(Array + 1, Text.size() - Array - 2);
                Result.ArraySize = std::stoll(Bound, &Consumed);
                if (Consumed != Bound.size())
                {
                    Result.ArraySize = -1;
                }
            }
            catch (const std::exception &)
            {
                Result.ArraySize = -1;
            }
        }
        Result.DeclarationSymbol = ResolveScope(Text, Scope);
        Result.bHasUnexposedTemplateArguments = Text.find('<') != std::string::npos;
        Document.Types[Index] = std::move(Result);
        return Index;
    }

    std::optional<bool> EvaluateCondition(const std::string &Expression, unsigned Depth = 0) const;
    std::vector<FCppTextToken> Preprocess(const std::vector<FCppTextToken> &Tokens, const std::string &File);
    void ReadFile(const std::string &File);
};

std::optional<bool> FCppTextDocumentBuilder::EvaluateCondition(const std::string &Expression, unsigned Depth) const
{
    if (Depth > 24)
    {
        return {};
    }
    const std::string Text = TrimCppText(Expression);
    if (UncertainDefinitions.count(Text))
    {
        return {};
    }
    if (Text == "0")
    {
        return false;
    }
    if (Text == "1")
    {
        return true;
    }
    if (Text.size() > 1 && Text.front() == '!')
    {
        const auto Value = EvaluateCondition(Text.substr(1), Depth + 1);
        if (Value.has_value())
        {
            return !Value.value();
        }
        return {};
    }
    const auto Value = Definitions.find(Text);
    if (Value != Definitions.end())
    {
        return EvaluateCondition(Value->second, Depth + 1);
    }
    return {};
}

std::vector<FCppTextToken> FCppTextDocumentBuilder::Preprocess(const std::vector<FCppTextToken> &Tokens,
                                                               const std::string &File)
{
    struct FBranch
    {
        bool bParentActive;
        bool bActive;
        bool bKnown;
        bool bTaken;
        std::string Expression;
        int InitialBraceDepth;
        std::optional<int> PrimaryBraceDepth;
        int InitialParenthesisDepth = 0;
        int InitialBracketDepth = 0;
        int PrimaryParenthesisDepth = 0;
        int PrimaryBracketDepth = 0;
    };
    std::vector<FBranch> Branches;
    std::vector<FCppTextToken> Result;
    int BraceDepth = 0;
    int ParenthesisDepth = 0;
    int BracketDepth = 0;
    for (const auto &Token : Tokens)
    {
        bool bActive = true;
        if (!Branches.empty())
        {
            bActive = Branches.back().bActive;
        }
        if (Token.Kind != "Directive")
        {
            if (bActive)
            {
                auto Copy = Token;
                Copy.BraceDepth = BraceDepth;
                Copy.ParenthesisDepth = ParenthesisDepth;
                Copy.BracketDepth = BracketDepth;
                if (Token.Kind != "Literal")
                {
                    ParenthesisDepth += (Token.Text == "(") - (Token.Text == ")");
                    BracketDepth += (Token.Text == "[") - (Token.Text == "]");
                }
                if (Token.Kind != "Literal" && Token.Text == "{")
                {
                    ++BraceDepth;
                }
                else if (Token.Kind != "Literal" && Token.Text == "}")
                {
                    --BraceDepth;
                }
                for (const auto &Branch : Branches)
                {
                    if (!Branch.bKnown)
                    {
                        Copy.Condition += "[" + Branch.Expression + "]";
                    }
                }
                Result.push_back(std::move(Copy));
            }
            continue;
        }
        const std::string Text = TrimCppText(Token.Text.substr(1));
        size_t Split = 0;
        while (Split < Text.size() &&
               (std::isalnum(static_cast<unsigned char>(Text[Split])) || Text[Split] == '_'))
        {
            ++Split;
        }
        const std::string Operation = Text.substr(0, Split);
        std::string Value;
        if (Split != std::string::npos)
        {
            Value = TrimCppText(Text.substr(Split));
        }
        if (Operation == "if" || Operation == "ifdef" || Operation == "ifndef")
        {
            std::optional<bool> Enabled;
            if (Operation == "ifdef")
            {
                if (!UncertainDefinitions.count(Value) && Definitions.count(Value))
                {
                    Enabled = true;
                }
            }
            else if (Operation == "ifndef")
            {
                if (!UncertainDefinitions.count(Value))
                {
                    Enabled = Definitions.count(Value) == 0;
                }
            }
            else
            {
                Enabled = EvaluateCondition(Value);
            }
            bool bEnabled = true;
            if (Enabled.has_value())
            {
                bEnabled = Enabled.value();
            }
            else
            {
                Document.bHasUnresolvedPreprocessing = true;
            }
            Branches.push_back(
                {bActive, bActive && bEnabled, Enabled.has_value(), Enabled.has_value() && bEnabled, Value,
                 BraceDepth, {}, ParenthesisDepth, BracketDepth});
            continue;
        }
        if (Operation == "else" || Operation == "elif")
        {
            if (Branches.empty())
            {
                Document.Diagnostics.push_back(
                    {ECppDiagnosticSeverity::Error, "Conditional branch without #if.", Token.Range.Begin});
                continue;
            }
            auto &Branch = Branches.back();
            if (!Branch.bKnown)
            {
                if (!Branch.PrimaryBraceDepth.has_value())
                {
                    Branch.PrimaryBraceDepth = BraceDepth;
                    Branch.PrimaryParenthesisDepth = ParenthesisDepth;
                    Branch.PrimaryBracketDepth = BracketDepth;
                }
                BraceDepth = Branch.InitialBraceDepth;
                ParenthesisDepth = Branch.InitialParenthesisDepth;
                BracketDepth = Branch.InitialBracketDepth;
            }
            if (Operation == "else")
            {
                Branch.bActive = Branch.bParentActive && (!Branch.bKnown || !Branch.bTaken);
                Branch.Expression = "alternative:" + Branch.Expression;
            }
            else
            {
                const auto Enabled = EvaluateCondition(Value);
                Branch.bActive = Branch.bParentActive && (!Branch.bKnown || !Branch.bTaken) &&
                                 (!Enabled.has_value() || Enabled.value());
                Branch.bKnown = Branch.bKnown && Enabled.has_value();
                Branch.bTaken |= Enabled.has_value() && Enabled.value();
                Branch.Expression = Value;
            }
            continue;
        }
        if (Operation == "endif")
        {
            if (Branches.empty())
            {
                Document.Diagnostics.push_back({ECppDiagnosticSeverity::Error, "Unmatched #endif.", Token.Range.Begin});
            }
            else
            {
                if (Branches.back().PrimaryBraceDepth.has_value())
                {
                    BraceDepth = Branches.back().PrimaryBraceDepth.value();
                    ParenthesisDepth = Branches.back().PrimaryParenthesisDepth;
                    BracketDepth = Branches.back().PrimaryBracketDepth;
                }
                Branches.pop_back();
            }
            continue;
        }
        if (!bActive)
        {
            continue;
        }
        FCppSyntaxNode Node;
        Node.Kind = "PreprocessorDirective";
        Node.Category = ECppNodeCategory::Preprocessing;
        Node.Range = Token.Range;
        Node.SpellingLocation = Token.Range.Begin;
        Node.SemanticScope = "global";
        Node.LexicalScope = "global";
        Node.Name = Operation;
        const bool bUncertainBranch =
            std::any_of(Branches.begin(), Branches.end(), [](const FBranch &Branch) { return !Branch.bKnown; });
        for (const auto &Branch : Branches)
        {
            if (!Branch.bKnown)
            {
                Node.ConditionalExpression += "[" + Branch.Expression + "]";
            }
        }
        if (Operation == "define")
        {
            const size_t Separator = Value.find_first_of(" (\t");
            Node.Kind = "MacroDefinition";
            Node.Name = Value.substr(0, Separator);
            if (bUncertainBranch)
            {
                UncertainDefinitions.insert(Node.Name);
            }
            else
            {
                UncertainDefinitions.erase(Node.Name);
            }
            if (Separator == std::string::npos)
            {
                Definitions[Node.Name] = {};
            }
            else
            {
                Definitions[Node.Name] = TrimCppText(Value.substr(Separator));
                if (Value[Separator] == '(')
                {
                    FunctionMacros.insert(Node.Name);
                }
            }
        }
        else if (Operation == "undef")
        {
            if (bUncertainBranch)
            {
                UncertainDefinitions.insert(Value);
            }
            else
            {
                UncertainDefinitions.erase(Value);
            }
            Definitions.erase(Value);
            FunctionMacros.erase(Value);
        }
        else if (Operation == "include")
        {
            Node.Kind = "InclusionDirective";
            const size_t Close = Value.find_last_of("\">");
            if (!Value.empty() && (Value.front() == '"' || Value.front() == '<') && Close != std::string::npos &&
                Close > 0)
            {
                Node.Name = Value.substr(1, Close - 1);
                std::vector<std::string> Directories = IncludeDirectories;
                if (Value.front() == '"')
                {
                    Directories.insert(Directories.begin(), File.substr(0, File.find_last_of('/')));
                }
                const bool bGenerated = Node.Name.find(".generated.h") != std::string::npos ||
                                        Node.Name.find(".gen.cpp") != std::string::npos;
                if (!bGenerated)
                {
                    for (const auto &Directory : Directories)
                    {
                        const std::string Candidate = NormalizeCppPath(Node.Name, Directory);
                        if (UnsavedFiles.count(Candidate) ||
                            std::filesystem::is_regular_file(MakeCppSourcePath(Candidate)))
                        {
                            Node.IncludedFile = Candidate;
                            break;
                        }
                    }
                }
                if (Node.IncludedFile.empty())
                {
                    Document.bHasUnresolvedPreprocessing = true;
                    auto Severity = ECppDiagnosticSeverity::Note;
                    if (Request.bRequireIncludes && !bGenerated)
                    {
                        Severity = ECppDiagnosticSeverity::Error;
                    }
                    Document.Diagnostics.push_back({Severity, "Include not expanded: " + Node.Name, Token.Range.Begin});
                }
            }
            else
            {
                Node.bRequiresMacroExpansion = true;
                Document.bHasUnresolvedPreprocessing = true;
            }
        }
        const std::string Included = Node.IncludedFile;
        AddNode(std::move(Node), 0);
        if (!Included.empty() && Request.bFollowIncludes && !bUncertainBranch)
        {
            ReadFile(Included);
        }
    }
    if (!Branches.empty())
    {
        Document.Diagnostics.push_back(
            {ECppDiagnosticSeverity::Error, "Unterminated preprocessor conditional.", {File, 0, 0, 0}});
    }
    return Result;
}

void FCppTextDocumentBuilder::ReadFile(const std::string &File)
{
    if (!ReadFiles.insert(File).second)
    {
        return;
    }
    std::string Contents;
    const auto Unsaved = UnsavedFiles.find(File);
    if (Unsaved != UnsavedFiles.end())
    {
        Contents = Unsaved->second;
    }
    else
    {
        std::ifstream Stream(MakeCppSourcePath(File), std::ios::binary);
        if (!Stream)
        {
            Document.Diagnostics.push_back({ECppDiagnosticSeverity::Error, "Cannot read source file: " + File, {}});
            return;
        }
        Contents.assign(std::istreambuf_iterator<char>(Stream), std::istreambuf_iterator<char>());
    }
    Document.SourceFiles.emplace(File, Contents);
    Document.IncludedFiles.push_back(File);
    auto Tokens = TokenizeCppText(File, Contents, Document.Diagnostics);
    FCppDeclarationReader Reader(*this, Preprocess(Tokens, File), File);
    Reader.ReadScope(0, Reader.Tokens.size(), 0, "global");
}

std::string FCppDeclarationReader::At(size_t Position) const
{
    if (Position < Tokens.size())
    {
        return Tokens[Position].Text;
    }
    return {};
}

size_t FCppDeclarationReader::Match(size_t Position, size_t End) const
{
    const std::string Open = At(Position);
    std::string Close;
    if (Open == "(")
    {
        Close = ")";
    }
    else if (Open == "[")
    {
        Close = "]";
    }
    else if (Open == "{")
    {
        Close = "}";
    }
    else if (Open == "<")
    {
        Close = ">";
    }
    else
    {
        return Position;
    }
    int Depth = 1;
    for (size_t Current = Position + 1; Current < End; ++Current)
    {
        if (Tokens[Current].Kind == "Literal")
        {
            continue;
        }
        if (Open == "{")
        {
            if (At(Current) == Close && Tokens[Current].BraceDepth == Tokens[Position].BraceDepth + 1)
            {
                return Current;
            }
            continue;
        }
        if (Open == "(" || Open == "[")
        {
            int OpeningDepth = Tokens[Position].ParenthesisDepth;
            int ClosingDepth = Tokens[Current].ParenthesisDepth;
            if (Open == "[")
            {
                OpeningDepth = Tokens[Position].BracketDepth;
                ClosingDepth = Tokens[Current].BracketDepth;
            }
            if (At(Current) == Close && ClosingDepth == OpeningDepth + 1)
            {
                return Current;
            }
            continue;
        }
        if (At(Current) == Open)
        {
            ++Depth;
        }
        if (At(Current) == Close)
        {
            --Depth;
            if (Depth == 0)
            {
                return Current;
            }
        }
    }
    return End;
}

std::string FCppDeclarationReader::Join(size_t Begin, size_t End) const
{
    std::string Result;
    End = std::min(End, Tokens.size());
    for (size_t Position = Begin; Position < End; ++Position)
    {
        if (!Result.empty() && Tokens[Position].Kind != "Punctuation" && Tokens[Position - 1].Kind != "Punctuation")
        {
            Result += ' ';
        }
        Result += At(Position);
    }
    return Result;
}

FCppSyntaxNode FCppDeclarationReader::MakeNode(size_t Begin, size_t End, const std::string &Kind,
                                               ECppNodeCategory Category, const std::string &Scope) const
{
    FCppSyntaxNode Result;
    Result.Kind = Kind;
    Result.Category = Category;
    Result.SemanticScope = Scope;
    Result.LexicalScope = Scope;
    if (Begin < End && End <= Tokens.size())
    {
        Result.Range = {Tokens[Begin].Range.Begin, Tokens[End - 1].Range.End};
        Result.SpellingLocation = Result.Range.Begin;
        Result.ConditionalExpression = Tokens[Begin].Condition;
        if (Builder.Request.bCaptureTokens)
        {
            for (size_t Position = Begin; Position < End; ++Position)
            {
                Result.Tokens.push_back({Tokens[Position].Kind, At(Position), Tokens[Position].Range});
            }
        }
    }
    return Result;
}

bool FCppDeclarationReader::IsMacro(size_t Position, const std::string &Scope) const
{
    if (At(Position + 1) != "(")
    {
        return false;
    }
    const auto Owner = Builder.Document.Symbols.find(Scope);
    if (Owner != Builder.Document.Symbols.end() && Owner->second.Name == At(Position))
    {
        return false;
    }
    return Builder.FunctionMacros.count(At(Position)) || IsMacroIdentifier(At(Position));
}

void FCppDeclarationReader::ReadScope(size_t Begin, size_t End, int Parent, const std::string &Scope, bool bRecord,
                                      ECppAccess Access)
{
    size_t Position = Begin;
    while (Position < End)
    {
        if (Tokens[Position].BraceDepth < Tokens[Begin].BraceDepth && !Tokens[Position].Condition.empty())
        {
            if (At(Position) == "{")
            {
                ++Position;
                continue;
            }
            if (Parent >= 0 && (At(Position) == "namespace" || At(Position) == "class" || At(Position) == "struct"))
            {
                const int OuterParent = Builder.Document.Nodes[Parent].Parent;
                const auto CurrentScope = Builder.Document.Scopes.find(Scope);
                if (OuterParent >= 0 && CurrentScope != Builder.Document.Scopes.end() && End < Tokens.size() && At(End) == "}")
                {
                    ReadScope(Position, End + 1, OuterParent, CurrentScope->second.Parent);
                    return;
                }
            }
        }
        if (At(Position) == ";")
        {
            ++Position;
            continue;
        }
        if ((At(Position) == "public" || At(Position) == "private" || At(Position) == "protected") &&
            At(Position + 1) == ":")
        {
            if (At(Position) == "public")
            {
                Access = ECppAccess::Public;
            }
            else if (At(Position) == "private")
            {
                Access = ECppAccess::Private;
            }
            else
            {
                Access = ECppAccess::Protected;
            }
            auto Specifier = MakeNode(Position, Position + 2, "AccessSpecifier", ECppNodeCategory::Other, Scope);
            Specifier.Access = Access;
            Builder.AddNode(std::move(Specifier), Parent);
            Position += 2;
            continue;
        }
        if (IsMacro(Position, Scope))
        {
            const size_t Close = Match(Position + 1, End);
            if (Close == End)
            {
                Builder.Document.Diagnostics.push_back(
                    {ECppDiagnosticSeverity::Error, "Unterminated macro invocation.", Tokens[Position].Range.Begin});
                return;
            }
            auto Macro = MakeNode(Position, Close + 1, "MacroInvocation", ECppNodeCategory::Preprocessing, Scope);
            Macro.Name = At(Position);
            Macro.Access = Access;
            Macro.bRequiresMacroExpansion = true;
            Builder.Document.bHasUnresolvedPreprocessing = true;
            Builder.AddNode(std::move(Macro), Parent);
            Position = Close + 1;
            continue;
        }
        if (At(Position) == "[" && At(Position + 1) == "[")
        {
            const size_t Close = Match(Position, End);
            Builder.AddNode(
                MakeNode(Position, std::min(Close + 1, End), "Attribute", ECppNodeCategory::Attribute, Scope), Parent);
            Position = std::min(Close + 1, End);
            continue;
        }
        if (At(Position) == "namespace" || (At(Position) == "inline" && At(Position + 1) == "namespace"))
        {
            size_t NameStart = Position + 1;
            if (At(Position) == "inline")
            {
                ++NameStart;
            }
            size_t Open = NameStart;
            while (Open < End && At(Open) != "{" && At(Open) != ";" && At(Open) != "=")
            {
                ++Open;
            }
            if (At(Open) == "{")
            {
                const size_t Close = Match(Open, End);
                if (Close == End)
                {
                    Builder.Document.Diagnostics.push_back(
                        {ECppDiagnosticSeverity::Error, "Unterminated namespace.", Tokens[Position].Range.Begin});
                    return;
                }
                auto Namespace = MakeNode(Position, Close + 1, "Namespace", ECppNodeCategory::Declaration, Scope);
                Namespace.Name = Join(NameStart, Open);
                if (Namespace.Name.empty())
                {
                    Namespace.Name = "(anonymous@" + File + ")";
                }
                Namespace.Symbol = Builder.Declare(Scope, Namespace.Name, Namespace.Kind);
                Namespace.OwnedScope = Namespace.Symbol;
                Namespace.bDefinition = true;
                const std::string NewScope = Namespace.Symbol;
                Builder.Document.Scopes.emplace(NewScope, FCppScope{NewScope, Scope, NewScope, {}});
                const int Index = Builder.AddNode(std::move(Namespace), Parent);
                ReadScope(Open + 1, Close, Index, NewScope);
                Position = Close + 1;
                continue;
            }
        }
        size_t Actual = Position;
        if (At(Actual) == "template" && At(Actual + 1) == "<")
        {
            Actual = Match(Actual + 1, End) + 1;
        }
        if (At(Actual) == "class" || At(Actual) == "struct" || At(Actual) == "union" || At(Actual) == "enum")
        {
            const size_t RecordEnd = ReadRecord(Actual, End, Parent, Scope, Position);
            if (RecordEnd != Actual)
            {
                Position = RecordEnd;
                continue;
            }
        }
        Position = ReadDeclaration(Position, End, Parent, Scope, bRecord, Access);
    }
}

size_t FCppDeclarationReader::ReadRecord(size_t Start, size_t End, int Parent, const std::string &Scope,
                                         size_t TemplateStart)
{
    const bool bEnum = At(Start) == "enum";
    size_t Name = Start + 1;
    bool bScoped = false;
    if (bEnum && (At(Name) == "class" || At(Name) == "struct"))
    {
        bScoped = true;
        ++Name;
    }
    while (Name + 1 < End && (At(Name).find("_API") != std::string::npos || IsMacro(Name, Scope) ||
                              At(Name) == "alignas" || At(Name) == "__declspec" || At(Name) == "__attribute__"))
    {
        if (At(Name + 1) == "(")
        {
            Name = Match(Name + 1, End) + 1;
        }
        else
        {
            ++Name;
        }
    }
    std::string SemanticScope = Scope;
    const size_t QualifiedStart = Name;
    while (Name + 2 < End && At(Name + 1) == "::" && Tokens[Name + 2].Kind == "Identifier")
    {
        Name += 2;
    }
    if (QualifiedStart < Name)
    {
        const std::string Qualifier = Join(QualifiedStart, Name - 1);
        const std::string Resolved = Builder.ResolveScope(Qualifier, Scope);
        if (!Resolved.empty())
        {
            SemanticScope = Resolved;
        }
        else
        {
            SemanticScope = Builder.Declare(Scope, Qualifier, "UnresolvedScope");
            Builder.Document.Scopes.emplace(SemanticScope, FCppScope{SemanticScope, Scope, SemanticScope, {}});
        }
    }
    size_t Open = Name + 1;
    if (At(Name) != "{" && (At(Open) == "*" || At(Open) == "&" || At(Open) == "&&" || At(Open) == "(" || At(Open) == "=" ||
        (Open < End && Tokens[Open].Kind == "Identifier" && At(Open) != "final" && !IsMacro(Open, Scope))))
    {
        return Start;
    }
    if (At(Name) == "{")
    {
        Open = Name;
    }
    while (Open < End && At(Open) != "{" && At(Open) != ";")
    {
        ++Open;
    }
    if (Open == End)
    {
        Builder.AddNode(MakeNode(TemplateStart, End, "UnresolvedDeclaration", ECppNodeCategory::Other, Scope), Parent);
        return End;
    }
    size_t Close = Open;
    if (At(Open) == "{")
    {
        Close = Match(Open, End);
    }
    if (Close == End)
    {
        Builder.Document.Diagnostics.push_back(
            {ECppDiagnosticSeverity::Error, "Unterminated record.", Tokens[Start].Range.Begin});
        return End;
    }
    std::string Kind = "StructDecl";
    if (At(Start) == "class")
    {
        Kind = "ClassDecl";
    }
    if (At(Start) == "union")
    {
        Kind = "UnionDecl";
    }
    if (bEnum)
    {
        Kind = "EnumDecl";
    }
    if (TemplateStart < Start)
    {
        Kind = "ClassTemplate";
    }
    auto Record = MakeNode(TemplateStart, Close + 1, Kind, ECppNodeCategory::Declaration, Scope);
    Record.Name = At(Name);
    if (Name == Open)
    {
        Record.Name = "(anonymous@" + File + ":" + std::to_string(Tokens[Name].Range.Begin.Offset) + ")";
    }
    std::string Specialization;
    if (At(Name + 1) == "<")
    {
        Specialization = Join(Name + 1, Match(Name + 1, Open) + 1);
    }
    Record.SemanticScope = SemanticScope;
    Record.Symbol = Builder.Declare(SemanticScope, Record.Name, Kind, Specialization);
    Record.OwnedScope = Record.Symbol;
    Record.bDefinition = At(Open) == "{";
    Record.bScopedEnum = bScoped;
    if (TemplateStart < Start)
    {
        for (size_t Current = TemplateStart + 2; Current + 1 < Start; ++Current)
        {
            if (At(Current) == "typename" || At(Current) == "class")
            {
                Record.TemplateParameters.push_back(At(Current + 1));
            }
        }
    }
    for (size_t Current = Name + 1; Current < Open; ++Current)
    {
        if (At(Current) == ":")
        {
            size_t BaseStart = Current + 1;
            for (size_t BaseEnd = BaseStart; BaseEnd <= Open; ++BaseEnd)
            {
                if (At(BaseEnd) == "<")
                {
                    BaseEnd = Match(BaseEnd, Open);
                }
                if (BaseEnd == Open || At(BaseEnd) == ",")
                {
                    while (At(BaseStart) == "public" || At(BaseStart) == "protected" || At(BaseStart) == "private" ||
                           At(BaseStart) == "virtual")
                    {
                        ++BaseStart;
                    }
                    Record.BaseTypes.push_back(Join(BaseStart, BaseEnd));
                    BaseStart = BaseEnd + 1;
                }
            }
            break;
        }
    }
    if (!Specialization.empty())
    {
        Record.SpecializedTemplate = Builder.Declare(SemanticScope, Record.Name, "ClassTemplate");
    }
    const std::string NewScope = Record.Symbol;
    Builder.Document.Scopes.emplace(NewScope, FCppScope{NewScope, SemanticScope, NewScope, {}});
    const int Index = Builder.AddNode(std::move(Record), Parent);
    if (bEnum && At(Open) == "{")
    {
        size_t Position = Open + 1;
        std::uint64_t NextValue = 0;
        bool bKnown = true;
        while (Position < Close)
        {
            size_t Finish = Position;
            while (Finish < Close && At(Finish) != ",")
            {
                if (At(Finish) == "(")
                {
                    Finish = Match(Finish, Close);
                }
                ++Finish;
            }
            auto Value = MakeNode(Position, std::min(Finish, Close), "EnumConstantDecl", ECppNodeCategory::Declaration,
                                  NewScope);
            Value.Name = At(Position);
            Value.Symbol = Builder.Declare(NewScope, Value.Name, Value.Kind);
            if (At(Position + 1) == "=")
            {
                std::string Number = Join(Position + 2, std::min(Finish, Close));
                Number.erase(std::remove(Number.begin(), Number.end(), '\''), Number.end());
                try
                {
                    size_t Consumed = 0;
                    NextValue = std::stoull(Number, &Consumed, 0);
                    bKnown = Number.substr(Consumed).find_first_not_of("uUlL") == std::string::npos;
                }
                catch (const std::exception &)
                {
                    bKnown = false;
                }
            }
            Value.bEnumValueKnown = bKnown;
            Value.UnsignedEnumValue = NextValue;
            Value.EnumValue = static_cast<std::int64_t>(NextValue);
            Builder.AddNode(std::move(Value), Index);
            ++NextValue;
            Position = Finish + 1;
        }
    }
    else if (At(Open) == "{")
    {
        ECppAccess MemberAccess = ECppAccess::Public;
        if (At(Start) == "class")
        {
            MemberAccess = ECppAccess::Private;
        }
        if (Name == Open && At(Close + 1) == ";")
        {
            const size_t FirstMember = Builder.Document.Nodes.size();
            ReadScope(Open + 1, Close, Index, Scope, true, MemberAccess);
            for (size_t MemberIndex = FirstMember; MemberIndex < Builder.Document.Nodes.size(); ++MemberIndex)
            {
                auto &Member = Builder.Document.Nodes[MemberIndex];
                if (Member.LexicalScope == Scope)
                {
                    Member.LexicalScope = NewScope;
                }
            }
        }
        else
        {
            ReadScope(Open + 1, Close, Index, NewScope, true, MemberAccess);
        }
    }
    return Close + 1;
}

int FCppDeclarationReader::ReadVariable(size_t Begin, size_t End, int Parent, const std::string &Scope,
                                        const std::string &Kind, ECppAccess Access, const std::string& SharedType)
{
    if (Begin >= End)
    {
        return -1;
    }
    std::vector<std::pair<size_t, size_t>> Declarators;
    size_t DeclaratorBegin = Begin;
    for (size_t Position = Begin; Position < End; ++Position)
    {
        if (At(Position) == "(" || At(Position) == "[" || At(Position) == "{" || At(Position) == "<")
        {
            const size_t Close = Match(Position, End);
            if (Close < End)
            {
                Position = Close;
            }
        }
        else if (At(Position) == ",")
        {
            Declarators.emplace_back(DeclaratorBegin, Position);
            DeclaratorBegin = Position + 1;
        }
    }
    Declarators.emplace_back(DeclaratorBegin, End);
    End = Declarators.front().second;
    size_t Boundary = Begin;
    while (Boundary < End && At(Boundary) != "=" && At(Boundary) != "[" && At(Boundary) != ":" && At(Boundary) != "{")
    {
        if (At(Boundary) == "<")
        {
            Boundary = Match(Boundary, End);
        }
        ++Boundary;
    }
    Boundary = std::min(Boundary, End);
    size_t Name = Boundary;
    while (Name > Begin && Tokens[Name - 1].Kind != "Identifier")
    {
        --Name;
    }
    if (Name <= Begin || (Name == Begin + 1 && SharedType.empty()))
    {
        return Builder.AddNode(MakeNode(Begin, End, "UnresolvedDeclaration", ECppNodeCategory::Other, Scope), Parent);
    }
    --Name;
    auto Variable = MakeNode(Begin, End, Kind, ECppNodeCategory::Declaration, Scope);
    Variable.Name = At(Name);
    Variable.Symbol = Builder.Declare(Scope, Variable.Name, Kind);
    Variable.Access = Access;
    std::string Type = Join(Begin, Name);
    if (!SharedType.empty())
    {
        if (Type.empty())
        {
            Type = SharedType;
        }
        else
        {
            Type = SharedType + " " + Type;
        }
    }
    if (At(Boundary) == "[")
    {
        Type += Join(Boundary, std::min(Match(Boundary, End) + 1, End));
    }
    Variable.Type = Builder.ReadType(Type, Scope);
    if (At(Boundary) == "=")
    {
        Variable.DefaultExpression = Join(Boundary + 1, End);
    }
    if (At(Boundary) == ":")
    {
        try
        {
            Variable.BitWidth = std::stoi(At(Boundary + 1));
        }
        catch (const std::exception &)
        {
            Variable.BitWidth = -1;
        }
    }
    const int Index = Builder.AddNode(std::move(Variable), Parent);
    if (Boundary < End)
    {
        ReadExpressions(Boundary + 1, End, Index, Scope);
    }
    if (Declarators.size() > 1)
    {
        size_t TypeEnd = Begin;
        while (TypeEnd < Name && At(TypeEnd) != "*" && At(TypeEnd) != "&" && At(TypeEnd) != "&&")
        {
            if (At(TypeEnd) == "<")
            {
                TypeEnd = Match(TypeEnd, Name);
            }
            ++TypeEnd;
        }
        const std::string CommonType = Join(Begin, TypeEnd);
        for (size_t DeclaratorIndex = 1; DeclaratorIndex < Declarators.size(); ++DeclaratorIndex)
        {
            ReadVariable(Declarators[DeclaratorIndex].first, Declarators[DeclaratorIndex].second,
                Parent, Scope, Kind, Access, CommonType);
        }
    }
    return Index;
}

size_t FCppDeclarationReader::ReadDeclaration(size_t Start, size_t End, int Parent, const std::string &Scope,
                                              bool bRecord, ECppAccess Access)
{
    size_t Position = Start;
    size_t Parameters = End;
    size_t ParameterEnd = End;
    size_t Finish = End;
    size_t BodyOpen = End;
    bool bInitializer = false;
    bool bConstructorInitializers = false;
    while (Position < End)
    {
        if (At(Position) == ";")
        {
            Finish = Position;
            break;
        }
        if (At(Position) == "=" && Parameters == End && (Position == Start || At(Position - 1) != "operator"))
        {
            bInitializer = true;
        }
        if (At(Position) == ":" && Parameters < End)
        {
            bConstructorInitializers = true;
        }
        if (At(Position) == "(")
        {
            const size_t Close = Match(Position, End);
            if (Close == End)
            {
                Builder.Document.Diagnostics.push_back({ECppDiagnosticSeverity::Error,
                                                        "Unterminated parameter or expression list.",
                                                        Tokens[Position].Range.Begin});
                return End;
            }
            if (!bInitializer && Parameters == End && Position > Start && !IsMacro(Position - 1, Scope))
            {
                if (At(Position - 1) != "operator" || At(Close + 1) != "(")
                {
                    Parameters = Position;
                    ParameterEnd = Close;
                }
            }
            Position = Close + 1;
            continue;
        }
        if (At(Position) == "[")
        {
            Position = std::min(Match(Position, End) + 1, End);
            continue;
        }
        if (At(Position) == "{")
        {
            if (bConstructorInitializers && Position > Start &&
                (Tokens[Position - 1].Kind == "Identifier" || At(Position - 1) == ">"))
            {
                const size_t InitializerEnd = Match(Position, End);
                if (InitializerEnd == End)
                {
                    Builder.Document.Diagnostics.push_back({ECppDiagnosticSeverity::Error,
                                                            "Unterminated constructor initializer.",
                                                            Tokens[Position].Range.Begin});
                    return End;
                }
                Position = InitializerEnd + 1;
                continue;
            }
            BodyOpen = Position;
            Finish = Match(Position, End);
            if (Finish == End)
            {
                Builder.Document.Diagnostics.push_back(
                    {ECppDiagnosticSeverity::Error, "Unterminated declaration body.", Tokens[Position].Range.Begin});
                return End;
            }
            break;
        }
        if (At(Position) == "}")
        {
            break;
        }
        ++Position;
    }
    if (Finish == End)
    {
        if (Position == Start)
        {
            return Start + 1;
        }
        Builder.AddNode(MakeNode(Start, Position, "UnresolvedDeclaration", ECppNodeCategory::Other, Scope), Parent);
        return Position;
    }
    if (At(Start) == "using" && At(Start + 2) == "=")
    {
        auto Alias = MakeNode(Start, Finish, "TypeAliasDecl", ECppNodeCategory::Declaration, Scope);
        Alias.Name = At(Start + 1);
        Alias.Symbol = Builder.Declare(Scope, Alias.Name, Alias.Kind);
        Alias.UnderlyingType = Builder.ReadType(Join(Start + 3, Finish), Scope);
        Builder.AddNode(std::move(Alias), Parent);
        return Finish + 1;
    }
    if (Parameters < End && (At(Parameters + 1) == "*" || At(Parameters + 1) == "&") && Parameters + 2 < ParameterEnd)
    {
        const size_t PointerName = Parameters + 2;
        std::string Kind = "VarDecl";
        if (bRecord)
        {
            Kind = "FieldDecl";
        }
        auto Variable = MakeNode(Start, Finish, Kind, ECppNodeCategory::Declaration, Scope);
        Variable.Name = At(PointerName);
        Variable.Symbol = Builder.Declare(Scope, Variable.Name, Kind);
        Variable.Access = Access;
        Variable.Type = Builder.ReadType(Join(Start, PointerName) + Join(PointerName + 1, Finish), Scope);
        Builder.AddNode(std::move(Variable), Parent);
        return Finish + 1;
    }
    if (Parameters == End || At(Start) == "typedef")
    {
        std::string Kind = "VarDecl";
        if (bRecord)
        {
            Kind = "FieldDecl";
        }
        ReadVariable(Start, Finish, Parent, Scope, Kind, Access);
        return Finish + 1;
    }
    size_t Name = Parameters - 1;
    size_t OperatorStart = Parameters;
    for (size_t Current = Start; Current < Parameters; ++Current)
    {
        if (At(Current) == "operator")
        {
            OperatorStart = Current;
        }
    }
    if (OperatorStart < Parameters)
    {
        Name = OperatorStart;
    }
    if (bRecord && At(Name) == ">")
    {
        size_t TemplateStart = Name;
        unsigned TemplateDepth = 1;
        while (TemplateStart > Start && TemplateDepth > 0)
        {
            --TemplateStart;
            if (At(TemplateStart) == ">")
            {
                ++TemplateDepth;
            }
            else if (At(TemplateStart) == "<")
            {
                --TemplateDepth;
            }
        }
        const auto Record = Builder.Document.Symbols.find(Scope);
        if (TemplateDepth == 0 && TemplateStart > Start && Record != Builder.Document.Symbols.end() &&
            At(TemplateStart - 1) == Record->second.Name)
        {
            Name = TemplateStart - 1;
        }
    }
    if (Tokens[Name].Kind != "Identifier")
    {
        Builder.AddNode(MakeNode(Start, Finish + 1, "UnresolvedDeclaration", ECppNodeCategory::Other, Scope), Parent);
        return Finish + 1;
    }
    size_t QualifiedStart = Name;
    while (QualifiedStart >= Start + 2 && At(QualifiedStart - 1) == "::")
    {
        QualifiedStart -= 2;
        if (At(QualifiedStart) == ">")
        {
            unsigned Depth = 1;
            while (QualifiedStart > Start && Depth > 0)
            {
                --QualifiedStart;
                if (At(QualifiedStart) == ">")
                {
                    ++Depth;
                }
                else if (At(QualifiedStart) == "<")
                {
                    --Depth;
                }
            }
            if (QualifiedStart > Start)
            {
                --QualifiedStart;
            }
        }
    }
    std::string SemanticScope = Scope;
    if (QualifiedStart < Name)
    {
        std::string Qualifier;
        for (size_t Current = QualifiedStart; Current < Name - 1; ++Current)
        {
            if (At(Current) == "<")
            {
                Current = Match(Current, Name - 1);
                continue;
            }
            Qualifier += At(Current);
        }
        const std::string Resolved = Builder.ResolveScope(Qualifier, Scope);
        if (!Resolved.empty())
        {
            SemanticScope = Resolved;
        }
        else
        {
            SemanticScope = Builder.Declare(Scope, Qualifier, "UnresolvedScope");
            Builder.Document.Scopes.emplace(SemanticScope, FCppScope{SemanticScope, Scope, SemanticScope, {}});
        }
    }
    std::string Kind = "FunctionDecl";
    const auto Owner = Builder.Document.Symbols.find(SemanticScope);
    if (bRecord || (Owner != Builder.Document.Symbols.end() && Owner->second.Kind != "Namespace"))
    {
        Kind = "CXXMethod";
    }
    if (Owner != Builder.Document.Symbols.end() && Owner->second.Name == At(Name))
    {
        Kind = "Constructor";
    }
    auto Function = MakeNode(Start, Finish + 1, Kind, ECppNodeCategory::Declaration, SemanticScope);
    Function.LexicalScope = Scope;
    Function.Name = At(Name);
    if (OperatorStart < Parameters)
    {
        Function.Name = Join(OperatorStart, Parameters);
    }
    else if (Name > Start && At(Name - 1) == "~")
    {
        Function.Name = "~" + Function.Name;
        Function.Kind = "Destructor";
    }
    Function.Access = Access;
    Function.bDefinition = BodyOpen < End;
    if (BodyOpen < End)
    {
        Function.BodyRange = MakeNode(BodyOpen, Finish + 1, {}, ECppNodeCategory::Other, Scope).Range;
        for (size_t Current = ParameterEnd + 1; Current < BodyOpen; ++Current)
        {
            if (At(Current) == ":")
            {
                ++Current;
                while (Current + 1 < BodyOpen)
                {
                    const std::string Member = At(Current++);
                    if (At(Current) != "(" && At(Current) != "{")
                    {
                        break;
                    }
                    const size_t Close = Match(Current, BodyOpen);
                    Function.Initializers[Member] = Join(Current + 1, Close);
                    Current = Close + 1;
                    if (At(Current) != ",")
                    {
                        break;
                    }
                    ++Current;
                }
                break;
            }
        }
    }
    std::vector<std::pair<size_t, size_t>> ParameterRanges;
    std::vector<std::string> ParameterTypes;
    size_t ParameterStart = Parameters + 1;
    for (size_t Current = ParameterStart; Current <= ParameterEnd; ++Current)
    {
        if (At(Current) == "<" || At(Current) == "(" || At(Current) == "[")
        {
            Current = Match(Current, ParameterEnd);
            continue;
        }
        if (Current == ParameterEnd || At(Current) == ",")
        {
            if (ParameterStart < Current && Join(ParameterStart, Current) != "void")
            {
                while (ParameterStart + 1 < Current && At(ParameterStart) == "UPARAM" && At(ParameterStart + 1) == "(")
                {
                    ParameterStart = Match(ParameterStart + 1, Current) + 1;
                }
                ParameterRanges.emplace_back(ParameterStart, Current);
                size_t TypeEnd = ParameterStart;
                while (TypeEnd < Current && At(TypeEnd) != "=")
                {
                    ++TypeEnd;
                }
                if (TypeEnd > ParameterStart + 1 && Tokens[TypeEnd - 1].Kind == "Identifier" && At(TypeEnd - 2) != "::")
                {
                    --TypeEnd;
                }
                ParameterTypes.push_back(Join(ParameterStart, TypeEnd));
            }
            ParameterStart = Current + 1;
        }
    }
    std::string Signature = "(";
    for (const auto &Type : ParameterTypes)
    {
        Signature += Type + ";";
    }
    Signature += ")";
    for (size_t Current = Start; Current < Finish; ++Current)
    {
        if (Current == BodyOpen)
        {
            break;
        }
        if (At(Current) == "static")
        {
            Function.bStatic = true;
        }
        if (At(Current) == "virtual")
        {
            Function.bVirtual = true;
        }
        if (At(Current) == "override")
        {
            Function.bOverride = true;
        }
        if (At(Current) == "explicit")
        {
            Function.bExplicit = true;
        }
        if (Current > ParameterEnd && At(Current) == "const")
        {
            Function.bConst = true;
        }
        if (Current > ParameterEnd && At(Current) == "delete")
        {
            Function.bDeleted = true;
        }
        if (Current > ParameterEnd && At(Current) == "default")
        {
            Function.bDefaulted = true;
        }
        if (Current > ParameterEnd && At(Current) == "=" && At(Current + 1) == "0")
        {
            Function.bPureVirtual = true;
        }
    }
    if (Function.bConst)
    {
        Signature += "const";
    }
    Function.Symbol = Builder.Declare(SemanticScope, Function.Name, Kind, Signature);
    Function.OwnedScope = Function.Symbol;
    size_t ReturnStart = Start;
    if (At(ReturnStart) == "template" && At(ReturnStart + 1) == "<")
    {
        ReturnStart = Match(ReturnStart + 1, QualifiedStart) + 1;
    }
    while (ReturnStart < QualifiedStart &&
           (At(ReturnStart) == "inline" || At(ReturnStart) == "static" || At(ReturnStart) == "virtual" ||
            At(ReturnStart) == "constexpr" || At(ReturnStart) == "consteval" || At(ReturnStart) == "explicit" ||
            At(ReturnStart) == "friend" || At(ReturnStart) == "extern" ||
            (IsMacroIdentifier(At(ReturnStart)) && ReturnStart + 1 < QualifiedStart)))
    {
        ++ReturnStart;
    }
    if (Kind != "Constructor")
    {
        Function.ResultType = Builder.ReadType(Join(ReturnStart, QualifiedStart), SemanticScope);
        if (OperatorStart + 1 < Parameters && Tokens[OperatorStart + 1].Kind == "Identifier")
        {
            Function.ResultType = Builder.ReadType(Join(OperatorStart + 1, Parameters), SemanticScope);
        }
    }
    const std::string FunctionScope = Function.Symbol;
    Builder.Document.Scopes.emplace(FunctionScope, FCppScope{FunctionScope, SemanticScope, FunctionScope, {}});
    const int Index = Builder.AddNode(std::move(Function), Parent);
    for (size_t ParameterIndex = 0; ParameterIndex < ParameterRanges.size(); ++ParameterIndex)
    {
        const auto Range = ParameterRanges[ParameterIndex];
        auto Parameter = MakeNode(Range.first, Range.second, "ParmDecl", ECppNodeCategory::Declaration, FunctionScope);
        size_t NameEnd = Range.first;
        while (NameEnd < Range.second && At(NameEnd) != "=")
        {
            ++NameEnd;
        }
        Parameter.Name = "Parameter" + std::to_string(ParameterIndex + 1);
        if (NameEnd > Range.first + 1 && Tokens[NameEnd - 1].Kind == "Identifier")
        {
            Parameter.Name = At(NameEnd - 1);
        }
        Parameter.Symbol = Builder.Declare(FunctionScope, Parameter.Name, Parameter.Kind);
        if (NameEnd < Range.second)
        {
            Parameter.DefaultExpression = Join(NameEnd + 1, Range.second);
        }
        Parameter.Type = Builder.ReadType(ParameterTypes[ParameterIndex], FunctionScope);
        const int ParameterNode = Builder.AddNode(std::move(Parameter), Index);
        Builder.Document.Nodes[Index].Parameters.push_back(ParameterNode);
    }
    if (BodyOpen < End)
    {
        if (ParameterEnd + 1 < BodyOpen)
        {
            ReadExpressions(ParameterEnd + 1, BodyOpen, Index, FunctionScope);
        }
        ReadBody(BodyOpen, Finish + 1, Index, FunctionScope);
    }
    return Finish + 1;
}

void FCppDeclarationReader::ReadExpressions(size_t Begin, size_t End, int Parent, const std::string &Scope)
{
    for (size_t Position = Begin; Position < End; ++Position)
    {
        if (IsMacro(Position, Scope))
        {
            const size_t Close = Match(Position + 1, End);
            if (Close < End)
            {
                auto Macro = MakeNode(Position, Close + 1, "MacroInvocation", ECppNodeCategory::Preprocessing, Scope);
                Macro.Name = At(Position);
                Macro.bRequiresMacroExpansion = true;
                Builder.Document.bHasUnresolvedPreprocessing = true;
                Builder.AddNode(std::move(Macro), Parent);
                Position = Close;
                continue;
            }
        }
        if (Tokens[Position].Kind == "Literal")
        {
            std::string Kind = "NumericLiteral";
            if (At(Position).find('"') != std::string::npos)
            {
                Kind = "StringLiteral";
            }
            Builder.AddNode(MakeNode(Position, Position + 1, Kind, ECppNodeCategory::Expression, Scope), Parent);
        }
        if (At(Position) == "=")
        {
            auto Assignment = MakeNode(Begin, End, "BinaryOperator", ECppNodeCategory::Expression, Scope);
            Assignment.Operator = "=";
            Builder.AddNode(std::move(Assignment), Parent);
        }
        if (Tokens[Position].Kind == "Identifier")
        {
            std::string Kind = "DeclRefExpr";
            if (At(Position + 1) == "(")
            {
                Kind = "CallExpr";
            }
            auto Expression = MakeNode(Position, Position + 1, Kind, ECppNodeCategory::Expression, Scope);
            Expression.Name = At(Position);
            std::string CurrentScope = Scope;
            while (!CurrentScope.empty())
            {
                const auto FoundScope = Builder.Document.Scopes.find(CurrentScope);
                if (FoundScope == Builder.Document.Scopes.end())
                {
                    break;
                }
                for (int Member : FoundScope->second.Members)
                {
                    const auto &Candidate = Builder.Document.Nodes[Member];
                    if (Candidate.Name == Expression.Name && Candidate.Kind == "ParmDecl")
                    {
                        Expression.ReferencedSymbol = Candidate.Symbol;
                    }
                }
                CurrentScope = FoundScope->second.Parent;
            }
            Builder.AddNode(std::move(Expression), Parent);
        }
    }
}

void FCppDeclarationReader::ReadBody(size_t Begin, size_t End, int Parent, const std::string &Scope)
{
    auto Block = MakeNode(Begin, End, "CompoundStmt", ECppNodeCategory::Statement, Scope);
    Block.OwnedScope = "scope:" + File + ":" + std::to_string(Tokens[Begin].Range.Begin.Offset);
    const std::string BlockScope = Block.OwnedScope;
    Builder.Document.Scopes.emplace(BlockScope, FCppScope{BlockScope, Scope, {}, {}});
    const int Index = Builder.AddNode(std::move(Block), Parent);
    size_t Start = Begin + 1;
    for (size_t Position = Start; Position + 1 < End; ++Position)
    {
        if (At(Position) == "{")
        {
            const size_t Close = Match(Position, End);
            ReadExpressions(Start, Position, Index, BlockScope);
            int BodyParent = Index;
            for (size_t Candidate = Start; Candidate < Position; ++Candidate)
            {
                if (At(Candidate) == "[")
                {
                    auto Lambda = MakeNode(Candidate, std::min(Close + 1, End), "LambdaExpr",
                                           ECppNodeCategory::Expression, BlockScope);
                    Lambda.OwnedScope =
                        "scope:" + File + ":lambda:" + std::to_string(Tokens[Candidate].Range.Begin.Offset);
                    Builder.Document.Scopes.emplace(Lambda.OwnedScope,
                                                    FCppScope{Lambda.OwnedScope, BlockScope, {}, {}});
                    BodyParent = Builder.AddNode(std::move(Lambda), Index);
                    break;
                }
            }
            std::string BodyScope = BlockScope;
            if (BodyParent != Index)
            {
                BodyScope = Builder.Document.Nodes[BodyParent].OwnedScope;
            }
            ReadBody(Position, std::min(Close + 1, End), BodyParent, BodyScope);
            Position = Close;
            Start = Position + 1;
        }
        else if (At(Position) == ";")
        {
            if (Start + 1 < Position && Tokens[Start].Kind == "Identifier" && Tokens[Start + 1].Kind == "Identifier" &&
                At(Start) != "return" && At(Start) != "throw")
            {
                ReadVariable(Start, Position, Index, BlockScope, "VarDecl", ECppAccess::Unspecified);
            }
            else
            {
                ReadExpressions(Start, Position, Index, BlockScope);
            }
            Start = Position + 1;
        }
    }
}

FCppSourceDocument FCppSourceParser::Parse(const FCppParseRequest &Request)
{
    FCppTextDocumentBuilder Builder(Request);
    if (Request.SourceFile.empty())
    {
        Builder.Document.Diagnostics.push_back({ECppDiagnosticSeverity::Error, "A source file is required.", {}});
    }
    else
    {
        Builder.ReadFile(Builder.Document.SourceFile);
    }
    Builder.Document.bSucceeded = true;
    for (const auto &Diagnostic : Builder.Document.Diagnostics)
    {
        if (Diagnostic.Severity == ECppDiagnosticSeverity::Error ||
            Diagnostic.Severity == ECppDiagnosticSeverity::Fatal)
        {
            Builder.Document.bSucceeded = false;
        }
    }
    return std::move(Builder.Document);
}

FCppSourceDocument FCppSourceParser::ParseCommand(const FCppCompileCommand &Command, bool bCaptureTokens)
{
    FCppParseRequest Request;
    Request.SourceFile = Command.SourceFile;
    Request.WorkingDirectory = Command.WorkingDirectory;
    Request.Arguments = Command.Arguments;
    Request.bCaptureTokens = bCaptureTokens;
    return Parse(Request);
}

std::vector<std::string> FCppSourceDocument::FindSymbols(const std::string &Name) const
{
    std::vector<std::string> Result;
    for (const auto &Entry : Symbols)
    {
        if (Entry.second.Name == Name || Entry.second.QualifiedName == Name)
        {
            Result.push_back(Entry.first);
        }
    }
    std::sort(Result.begin(), Result.end());
    return Result;
}

std::string FCppSourceDocument::GetSourceText(const FCppSourceRange &Range) const
{
    const auto File = SourceFiles.find(Range.Begin.File);
    if (File == SourceFiles.end() || Range.Begin.File != Range.End.File || Range.End.Offset < Range.Begin.Offset ||
        Range.End.Offset > File->second.size())
    {
        return {};
    }
    return File->second.substr(Range.Begin.Offset, Range.End.Offset - Range.Begin.Offset);
}

void FCppSourceDocument::Visit(int Root, const std::function<bool(int, const FCppSyntaxNode &)> &Visitor) const
{
    if (Root < 0 || static_cast<size_t>(Root) >= Nodes.size())
    {
        return;
    }
    std::vector<int> Pending{Root};
    while (!Pending.empty())
    {
        const int Current = Pending.back();
        Pending.pop_back();
        if (Visitor(Current, Nodes[Current]))
        {
            const auto &Children = Nodes[Current].Children;
            Pending.insert(Pending.end(), Children.rbegin(), Children.rend());
        }
    }
}
