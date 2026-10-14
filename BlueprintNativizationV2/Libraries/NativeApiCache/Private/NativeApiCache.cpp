#include "NativeApiCache.h"
#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>
#include <atomic>
#include <future>
#include <thread>
#include <functional>
#include <cctype>
#include <regex>

static std::string CanonicalCallableType(std::string Type)
{
    Type = std::regex_replace(Type, std::regex("\\b(class|struct|enum)\\s+"), "");
    Type.erase(std::remove_if(Type.begin(), Type.end(), [](unsigned char Character)
    {
        return std::isspace(Character) != 0;
    }), Type.end());
    return Type;
}

static FNativeApiEvidence ApiEvidence(const FCppSourceDocument &Document, const FCppSyntaxNode &Node)
{
    return {Node.Range.Begin.File, Node.Range.Begin.Line, Node.ConditionalExpression,
            Document.GetSourceText(Node.Range), Node.Range.Begin.Offset, Node.Range.End.Offset};
}

static const FCppSyntaxNode* FindClassMetadata(const FCppSourceDocument& Document, const FCppSyntaxNode& Record)
{
    const FCppSyntaxNode* Previous = nullptr;
    for (const auto& Candidate : Document.Nodes)
    {
        if (Candidate.Parent == Record.Parent && Candidate.Range.End.Offset <= Record.Range.Begin.Offset &&
            Candidate.Range.Begin.File == Record.Range.Begin.File &&
            (!Previous || Candidate.Range.End.Offset > Previous->Range.End.Offset))
        {
            Previous = &Candidate;
        }
    }
    if (Previous && Previous->Kind == "MacroInvocation" && Previous->Name == "UCLASS")
    {
        return Previous;
    }
    return nullptr;
}

static void ApplyUnrealClassAccess(FCppSourceDocument& Document, std::set<unsigned>& UncertainAccessOffsets)
{
    for (auto& Record : Document.Nodes)
    {
        if (!Record.bDefinition || (Record.Kind != "ClassDecl" && Record.Kind != "StructDecl") ||
            !FindClassMetadata(Document, Record))
        {
            continue;
        }
        ECppAccess Access = ECppAccess::Public;
        if (Record.Kind == "ClassDecl")
        {
            Access = ECppAccess::Private;
        }
        bool bAccessVerified = true;
        for (int ChildIndex : Record.Children)
        {
            auto& Child = Document.Nodes[ChildIndex];
            if (Child.Kind == "AccessSpecifier")
            {
                Access = Child.Access;
                bAccessVerified = Child.ConditionalExpression.empty();
            }
            else if (Child.Kind == "MacroInvocation" && Child.Name == "GENERATED_UCLASS_BODY")
            {
                Access = ECppAccess::Public;
                bAccessVerified = Child.ConditionalExpression.empty();
            }
            else
            {
                Child.Access = Access;
                if (!bAccessVerified)
                {
                    UncertainAccessOffsets.insert(Child.Range.Begin.Offset);
                }
            }
        }
    }
}

static void ReadClassContract(const FCppSourceDocument& Document, const FCppSyntaxNode& Record, FNativeApiType& Type)
{
    Type.Declaration = ApiEvidence(Document, Record);
    for (const auto& Token : Record.Tokens)
    {
        if (Token.Spelling == "{")
        {
            Type.Declaration.EndOffset = Token.Range.Begin.Offset;
            Type.Declaration.Text = Document.SourceFiles.at(Record.Range.Begin.File).substr(
                Type.Declaration.BeginOffset, Type.Declaration.EndOffset - Type.Declaration.BeginOffset);
            break;
        }
    }
    const auto* Metadata = FindClassMetadata(Document, Record);
    if (!Metadata)
    {
        return;
    }
    Type.bReflectedClass = true;
    for (const auto& Token : Metadata->Tokens)
    {
        Type.bMinimalApi |= Token.Spelling == "MinimalAPI";
        Type.bCustomConstructor |= Token.Spelling == "customConstructor";
    }
    for (int ChildIndex : Record.Children)
    {
        const auto& Child = Document.Nodes[ChildIndex];
        if (Child.Kind == "MacroInvocation" &&
            (Child.Name == "GENERATED_BODY" || Child.Name == "GENERATED_UCLASS_BODY"))
        {
            Type.GeneratedBody = ApiEvidence(Document, Child);
            Type.GeneratedBodyAccess = Child.Access;
            Type.bLegacyGeneratedBody = Child.Name == "GENERATED_UCLASS_BODY";
            break;
        }
    }
}

static std::string ApiType(const FCppSourceDocument &Document, int Index)
{
    if (Index < 0)
    {
        return {};
    }
    return Document.Types[Index].Spelling;
}

static void AnalyzePublicForwarding(FNativeApiCallable& Callable, const std::vector<std::string>& Tokens)
{
    if (!Callable.bStatic || Callable.bConstructor || Callable.bDeleted || Callable.bVirtual)
    {
        return;
    }
    size_t Position = 0;
    if (Callable.ReturnType != "void")
    {
        if (Tokens.empty() || Tokens[Position++] != "return")
        {
            return;
        }
    }
    if (Position >= Tokens.size())
    {
        return;
    }
    const size_t ExpressionBegin = Position;
    auto IsIdentifier = [](const std::string& Token)
    {
        return !Token.empty() && (std::isalpha(static_cast<unsigned char>(Token[0])) || Token[0] == '_') &&
            std::all_of(Token.begin(), Token.end(), [](unsigned char Character)
            {
                return std::isalnum(Character) || Character == '_';
            });
    };
    size_t ExpressionPosition = ExpressionBegin;
    std::function<bool(unsigned)> ReadExpression;
    std::function<bool(unsigned)> ReadArguments = [&](unsigned Depth)
    {
        ++ExpressionPosition;
        if (ExpressionPosition < Tokens.size() && Tokens[ExpressionPosition] == ")")
        {
            ++ExpressionPosition;
            return true;
        }
        for (;;)
        {
            if (!ReadExpression(Depth + 1) || ExpressionPosition >= Tokens.size())
            {
                return false;
            }
            if (Tokens[ExpressionPosition] == ")")
            {
                ++ExpressionPosition;
                return true;
            }
            if (Tokens[ExpressionPosition++] != ",")
            {
                return false;
            }
        }
    };
    ReadExpression = [&](unsigned Depth)
    {
        if (Depth > 32 || ExpressionPosition >= Tokens.size() || !IsIdentifier(Tokens[ExpressionPosition++]))
        {
            return false;
        }
        if (ExpressionPosition < Tokens.size() && Tokens[ExpressionPosition] == "(" && !ReadArguments(Depth))
        {
            return false;
        }
        while (ExpressionPosition < Tokens.size() &&
            (Tokens[ExpressionPosition] == "." || Tokens[ExpressionPosition] == "->"))
        {
            ++ExpressionPosition;
            if (ExpressionPosition >= Tokens.size() || !IsIdentifier(Tokens[ExpressionPosition++]))
            {
                return false;
            }
            if (ExpressionPosition < Tokens.size() && Tokens[ExpressionPosition] == "(" && !ReadArguments(Depth))
            {
                return false;
            }
        }
        return true;
    };
    if (ReadExpression(0) && ExpressionPosition + 1 == Tokens.size() && Tokens[ExpressionPosition] == ";")
    {
        Callable.ForwardingExpression.assign(Tokens.begin() + ExpressionBegin, Tokens.end() - 1);
    }
    auto FindParameter = [&](const std::string& Name)
    {
        for (size_t ParameterIndex = 0; ParameterIndex < Callable.Parameters.size(); ++ParameterIndex)
        {
            if (Callable.Parameters[ParameterIndex].Name == Name)
            {
                return static_cast<int>(ParameterIndex);
            }
        }
        return -1;
    };
    FNativeApiForwarding Candidate;
    Candidate.RootParameter = FindParameter(Tokens[Position++]);
    if (Candidate.RootParameter < 0)
    {
        return;
    }
    while (Position + 2 < Tokens.size() && Tokens[Position] == "->")
    {
        ++Position;
        const std::string Name = Tokens[Position++];
        if (Tokens[Position] == "(")
        {
            Candidate.Method = Name;
            ++Position;
            while (Position < Tokens.size() && Tokens[Position] != ")")
            {
                const int ParameterIndex = FindParameter(Tokens[Position++]);
                if (ParameterIndex < 0)
                {
                    return;
                }
                Candidate.Arguments.push_back(ParameterIndex);
                if (Position < Tokens.size() && Tokens[Position] != ")")
                {
                    if (Tokens[Position++] != "," || Position >= Tokens.size() || Tokens[Position] == ")")
                    {
                        return;
                    }
                }
            }
            if (Position + 2 != Tokens.size() || Tokens[Position] != ")" || Tokens[Position + 1] != ";")
            {
                return;
            }
            Callable.PublicForwarding = std::move(Candidate);
            return;
        }
        Candidate.Members.push_back(Name);
    }
}

static void AnalyzeBody(FNativeApiCallable &Callable, const FCppSyntaxNode &Node)
{
    std::vector<std::string> Tokens;
    for (const auto &Token : Node.Tokens)
    {
        if (Token.Range.Begin.Offset > Node.BodyRange.Begin.Offset &&
            Token.Range.End.Offset < Node.BodyRange.End.Offset && Token.Kind != "Comment")
        {
            Tokens.push_back(Token.Spelling);
        }
    }
    for (size_t Position = 0; Position + 1 < Tokens.size();)
    {
        if (Tokens[Position] == "this" && Tokens[Position + 1] == "->")
        {
            Tokens.erase(Tokens.begin() + Position, Tokens.begin() + Position + 2);
        }
        else
        {
            ++Position;
        }
    }
    if (Callable.Parameters.empty() && Callable.bConst && Tokens.size() == 3 && Tokens[0] == "return" &&
        Tokens[2] == ";")
    {
        Callable.ReadMember = Tokens[1];
    }
    if (Callable.Parameters.size() == 1 && Tokens.size() == 4 && Tokens[1] == "=" &&
        Tokens[2] == Callable.Parameters[0].Name && Tokens[3] == ";")
    {
        Callable.WriteMember = Tokens[0];
    }
    AnalyzePublicForwarding(Callable, Tokens);
    std::set<std::string> ThisAliases = {"this"};
    std::set<std::string> AssignedNames;
    for (size_t Position = 0; Position + 1 < Tokens.size(); ++Position)
    {
        if (Tokens[Position + 1] != "=")
        {
            continue;
        }
        if (!AssignedNames.insert(Tokens[Position]).second)
        {
            ThisAliases.erase(Tokens[Position]);
            continue;
        }
        const bool bDirectThis = Position + 3 < Tokens.size() && Tokens[Position + 2] == "this" &&
            Tokens[Position + 3] == ";";
        const bool bCastThis = Position + 7 < Tokens.size() && Tokens[Position + 2] == "(" &&
            !Tokens[Position + 3].empty() && std::isalpha(static_cast<unsigned char>(Tokens[Position + 3][0])) &&
            Tokens[Position + 4] == "*" && Tokens[Position + 5] == ")" && Tokens[Position + 6] == "this" &&
            Tokens[Position + 7] == ";";
        if (bDirectThis || bCastThis)
        {
            ThisAliases.insert(Tokens[Position]);
        }
    }
    const std::set<std::string> NonCalls = {"if", "while", "for", "switch", "sizeof", "decltype", "noexcept", "return", "catch", "&&", "||", "=", "!", "*", "&"};
    std::set<std::string> RepeatedCalls;
    size_t Start = 0;
    for (size_t Position = 0; Position + 1 < Tokens.size(); ++Position)
    {
        if (Tokens[Position + 1] != "(" || NonCalls.count(Tokens[Position]) != 0 || Tokens[Position].empty() ||
            (!std::isalpha(static_cast<unsigned char>(Tokens[Position][0])) && Tokens[Position][0] != '_'))
        {
            continue;
        }
        std::vector<std::string> Arguments;
        std::string Argument;
        unsigned Depth = 1;
        for (size_t Current = Position + 2; Current < Tokens.size(); ++Current)
        {
            if (Tokens[Current] == "(")
            {
                ++Depth;
            }
            if (Tokens[Current] == ")")
            {
                --Depth;
                if (Depth == 0)
                {
                    if (!Argument.empty())
                    {
                        Arguments.push_back(Argument);
                    }
                    break;
                }
            }
            if (Tokens[Current] == "," && Depth == 1)
            {
                Arguments.push_back(Argument);
                Argument.clear();
            }
            else
            {
                Argument += Tokens[Current];
            }
        }
        if (Depth == 0)
        {
            std::string Receiver;
            if (Position > 1 && (Tokens[Position - 1] == "." || Tokens[Position - 1] == "->" || Tokens[Position - 1] == "::"))
            {
                Receiver = Tokens[Position - 2];
            }
            Callable.CallSites.push_back({Tokens[Position], Receiver, Arguments, Position});
            if (Receiver.empty() || ThisAliases.count(Receiver) != 0)
            {
                if (Callable.DirectCalls.count(Tokens[Position]) != 0)
                {
                    RepeatedCalls.insert(Tokens[Position]);
                }
                Callable.DirectCalls.emplace(Tokens[Position], Arguments);
            }
        }
    }
    for (const auto& Name : RepeatedCalls)
    {
        Callable.DirectCalls.erase(Name);
    }
    if (!Tokens.empty() && Tokens.front() == "return")
    {
        Start = 1;
    }
    if (Tokens.size() >= Start + 4 && Tokens[Start + 1] == "(" && Tokens[Tokens.size() - 2] == ")" &&
        Tokens.back() == ";")
    {
        std::vector<std::string> Expected;
        for (const auto &Parameter : Callable.Parameters)
        {
            if (!Expected.empty())
            {
                Expected.push_back(",");
            }
            Expected.push_back(Parameter.Name);
        }
        if (std::vector<std::string>(Tokens.begin() + Start + 2, Tokens.end() - 2) == Expected)
        {
            Callable.ForwardedCall = Tokens[Start];
        }
    }
}

FNativeApiCache FNativeApiCacheBuilder::Build(const FNativeApiCacheRequest &Request)
{
    FNativeApiCache Result;
    Result.Configuration = Request.Configuration;
    Result.Arguments = Request.Arguments;
    FCppSourceIndex Index;
    std::set<std::string> ReadFiles;
    std::vector<FNativeSourceInput> Sources;
    for (const auto &Source : Request.Sources)
    {
        if (ReadFiles.insert(Source.File).second)
        {
            Sources.push_back(Source);
        }
    }
    std::vector<FCppSourceDocument> Documents(Sources.size());
    std::atomic<size_t> NextDocument{0};
    std::vector<std::future<void>> Workers;
    const size_t WorkerCount = std::min(Sources.size(), static_cast<size_t>(std::max(1u, std::min(4u, std::thread::hardware_concurrency()))));
    for (size_t WorkerIndex = 0; WorkerIndex < WorkerCount; ++WorkerIndex)
    {
        Workers.push_back(std::async(std::launch::async, [&]()
        {
            for (;;)
            {
                const size_t DocumentIndex = NextDocument.fetch_add(1);
                if (DocumentIndex >= Sources.size())
                {
                    return;
                }
                FCppParseRequest ParseRequest;
                ParseRequest.SourceFile = Sources[DocumentIndex].File;
                ParseRequest.Arguments = Request.Arguments;
                ParseRequest.bFollowIncludes = false;
                Documents[DocumentIndex] = FCppSourceParser::Parse(ParseRequest);
            }
        }));
    }
    for (auto& Worker : Workers)
    {
        Worker.get();
    }
    std::map<std::string, std::set<unsigned>> UncertainAccess;
    for (size_t DocumentIndex = 0; DocumentIndex < Documents.size(); ++DocumentIndex)
    {
        const auto& Source = Sources[DocumentIndex];
        auto& Document = Documents[DocumentIndex];
        ApplyUnrealClassAccess(Document, UncertainAccess[Document.SourceFile]);
        FNativeApiSourceStatus Status;
        Status.Source = Source;
        Status.bParsed = Document.bSucceeded;
        Status.Diagnostics = Document.Diagnostics;
        const auto Contents = Document.SourceFiles.find(Document.SourceFile);
        if (Contents != Document.SourceFiles.end())
        {
            std::uint64_t Hash = 14695981039346656037ULL;
            for (unsigned char Character : Contents->second)
            {
                Hash = (Hash ^ Character) * 1099511628211ULL;
            }
            std::ostringstream Fingerprint;
            Fingerprint << std::hex << Hash;
            Status.Fingerprint = Fingerprint.str();
        }
        std::set<std::string> NamespaceConstants;
        for (const auto &Node : Document.Nodes)
        {
            Status.UnresolvedDeclarations += Node.Kind == "UnresolvedDeclaration";
            Status.MacroInvocations += Node.Kind == "MacroInvocation";
            const auto Owner = Document.Symbols.find(Node.SemanticScope);
            if (Node.Kind == "VarDecl" && Owner != Document.Symbols.end() && Owner->second.Kind == "Namespace" &&
                Owner->second.QualifiedName.find("(anonymous@") == std::string::npos && !Source.Include.empty() &&
                Node.ConditionalExpression.empty() && !Node.DefaultExpression.empty() && Node.Type >= 0)
            {
                std::string ConstantType = ApiType(Document, Node.Type);
                const bool bConst = ConstantType.find("const ") != std::string::npos ||
                    ConstantType.find("constexpr ") != std::string::npos;
                for (const std::string& Qualifier : {"static ", "constexpr ", "const "})
                {
                    const size_t Position = ConstantType.find(Qualifier);
                    if (Position != std::string::npos)
                    {
                        ConstantType.erase(Position, Qualifier.size());
                    }
                }
                const std::set<std::string> IntegralTypes = {"bool", "char", "short", "int", "long", "long long",
                    "unsigned", "unsigned char", "unsigned short", "unsigned int", "unsigned long", "unsigned long long",
                    "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64"};
                if (bConst && IntegralTypes.count(ConstantType))
                {
                    bool bInitializer = false;
                    bool bConstantExpression = true;
                    const std::set<std::string> Operators = {"+", "-", "*", "/", "%", "<<", ">>", "&", "|", "^",
                        "~", "!", "&&", "||", "<", ">", "<=", ">=", "==", "!=", "?", ":", "(", ")"};
                    for (size_t TokenIndex = 0; TokenIndex < Node.Tokens.size(); ++TokenIndex)
                    {
                        const FCppToken& Token = Node.Tokens[TokenIndex];
                        if (!bInitializer)
                        {
                            bInitializer = Token.Spelling == "=";
                            continue;
                        }
                        if (Token.Kind == "Comment" || Operators.count(Token.Spelling) ||
                            Token.Spelling == "true" || Token.Spelling == "false" ||
                            (Token.Kind == "Literal" && !Token.Spelling.empty() &&
                                (std::isdigit(static_cast<unsigned char>(Token.Spelling[0])) || Token.Spelling[0] == '\'')))
                        {
                            continue;
                        }
                        std::string QualifiedName = Owner->second.QualifiedName + "::" + Token.Spelling;
                        if (TokenIndex + 1 < Node.Tokens.size() && Node.Tokens[TokenIndex + 1].Spelling == "::")
                        {
                            QualifiedName = Token.Spelling;
                            while (TokenIndex + 2 < Node.Tokens.size() && Node.Tokens[TokenIndex + 1].Spelling == "::" &&
                                Node.Tokens[TokenIndex + 2].Kind == "Identifier")
                            {
                                TokenIndex += 2;
                                QualifiedName += "::" + Node.Tokens[TokenIndex].Spelling;
                            }
                        }
                        if (Token.Kind != "Identifier" || !NamespaceConstants.count(QualifiedName))
                        {
                            bConstantExpression = false;
                            break;
                        }
                    }
                    if (bInitializer && bConstantExpression)
                    {
                        const std::string QualifiedName = Document.Symbols.at(Node.Symbol).QualifiedName;
                        Result.Constants.push_back({QualifiedName, Source.Include, Source.Module,
                            Node.DefaultExpression, ApiEvidence(Document, Node)});
                        NamespaceConstants.insert(QualifiedName);
                    }
                }
            }
            if (Node.Kind != "ClassDecl" && Node.Kind != "StructDecl" && Node.Kind != "ClassTemplate" && Node.Kind != "EnumDecl")
            {
                continue;
            }
            auto &Type = Result.Types[Document.Symbols.at(Node.Symbol).QualifiedName];
            Type.Name = Document.Symbols.at(Node.Symbol).QualifiedName;
            if (Node.bDefinition)
            {
                Type.bDefinition = true;
                if (Node.Kind != "EnumDecl")
                {
                    ReadClassContract(Document, Node, Type);
                }
                Type.Module = Source.Module;
                for (const auto& Token : Node.Tokens)
                {
                    if (Token.Spelling == "{" || Token.Spelling == ":")
                    {
                        break;
                    }
                    if (Token.Spelling.size() > 4 && Token.Spelling.substr(Token.Spelling.size() - 4) == "_API")
                    {
                        Type.bExported = true;
                    }
                }
                Type.Include = Source.Include;
                Type.BaseTypes = Node.BaseTypes;
                Type.TemplateParameters = Node.TemplateParameters;
            }
            Type.Evidence.push_back(ApiEvidence(Document, Node));
            if (Node.Kind != "EnumDecl")
            {
                Type.Evidence.back().Text = Type.Name;
            }
        }
        Result.Sources.push_back(std::move(Status));
        Index.Documents.push_back(std::move(Document));
    }
    std::map<std::pair<std::string, std::string>, const FCppSyntaxNode*> Definitions;
    for (const auto &Document : Index.Documents)
    {
        for (const auto &Node : Document.Nodes)
        {
            const auto Owner = Document.Symbols.find(Node.SemanticScope);
            const bool bFreeFunction = Node.Kind == "FunctionDecl";
            if (Owner == Document.Symbols.end() && !bFreeFunction)
            {
                continue;
            }
            auto Found = Result.Types.end();
            if (Owner != Document.Symbols.end())
            {
                Found = Result.Types.find(Owner->second.QualifiedName);
            }
            if (Found == Result.Types.end() && !bFreeFunction)
            {
                continue;
            }
            FNativeApiType FreeScope;
            FNativeApiType* OwningType = &FreeScope;
            if (!bFreeFunction)
            {
                OwningType = &Found->second;
            }
            auto& Callables = OwningType->Callables;
            if (bFreeFunction)
            {
                Callables.swap(Result.Functions);
            }
            if (Node.Kind == "FieldDecl")
            {
                OwningType->Members.push_back(
                    {Node.Name, ApiType(Document, Node.Type), Node.Access, ApiEvidence(Document, Node)});
                continue;
            }
            if (Node.Kind != "Constructor" && Node.Kind != "CXXMethod" && Node.Kind != "Destructor" && !bFreeFunction)
            {
                continue;
            }
            std::string CallableName = Node.Name;
            if (bFreeFunction)
            {
                CallableName = Document.Symbols.at(Node.Symbol).QualifiedName;
            }
            std::string Signature = CallableName + "(";
            std::vector<FNativeApiParameter> Parameters;
            for (int ParameterIndex : Node.Parameters)
            {
                const auto &Parameter = Document.Nodes[ParameterIndex];
                const std::string ParameterType = ApiType(Document, Parameter.Type);
                Signature += CanonicalCallableType(ParameterType) + ";";
                Parameters.push_back({Parameter.Name, ParameterType, Parameter.DefaultExpression});
            }
            Signature += ")";
            if (Node.bConst)
            {
                Signature += "const";
            }
            auto Existing =
                std::find_if(Callables.begin(), Callables.end(),
                             [&](const FNativeApiCallable &Callable) { return Callable.Signature == Signature; });
            if (Existing == Callables.end())
            {
                Callables.emplace_back();
                Existing = Callables.end() - 1;
                Existing->Name = Node.Name;
                Existing->Signature = Signature;
                Existing->Parameters = Parameters;
                Existing->ReturnType = ApiType(Document, Node.ResultType);
                Existing->bConstructor = Node.Kind == "Constructor";
                Existing->bDestructor = Node.Kind == "Destructor";
                Existing->bConst = Node.bConst;
                Existing->bStatic = Node.bStatic;
                Existing->bDeleted = Node.bDeleted;
            }
            if (bFreeFunction)
            {
                Existing->Access = ECppAccess::Public;
                Existing->Name = Document.Symbols.at(Node.Symbol).QualifiedName;
            }
            for (const auto& Source : Sources)
            {
                if (Source.File == Document.SourceFile && !Source.Include.empty())
                {
                    Existing->Include = Source.Include;
                    Existing->Module = Source.Module;
                    break;
                }
            }
            Existing->bStatic |= Node.bStatic;
            Existing->bDeleted |= Node.bDeleted;
            for (const auto& Token : Node.Tokens)
            {
                if (Token.Spelling == "{")
                {
                    break;
                }
                if (Token.Spelling.size() > 4 && Token.Spelling.substr(Token.Spelling.size() - 4) == "_API")
                {
                    Existing->bExported = true;
                }
            }
            const bool bExplicitInline = std::any_of(Node.Tokens.begin(), Node.Tokens.end(), [](const FCppToken& Token)
            {
                return Token.Spelling == "inline";
            });
            if (Node.bDefinition && Document.SourceFile.size() > 2 && Document.SourceFile.substr(Document.SourceFile.size() - 2) == ".h" &&
                ((!bFreeFunction && Node.LexicalScope == Node.SemanticScope) || bExplicitInline ||
                    std::any_of(Node.Tokens.begin(), Node.Tokens.end(), [](const FCppToken& Token)
                    {
                        return Token.Spelling.find("FORCEINLINE") != std::string::npos;
                    })))
            {
                Existing->bInlineDefinition = true;
            }
            Existing->Evidence.push_back(ApiEvidence(Document, Node));
            Existing->bVirtual |= Node.bVirtual || Node.bOverride;
            if (Node.Access != ECppAccess::Unspecified)
            {
                Existing->Access = Node.Access;
                Existing->bAccessVerified = UncertainAccess[Document.SourceFile].count(Node.Range.Begin.Offset) == 0;
            }
            for (size_t ParameterIndex = 0; ParameterIndex < Parameters.size(); ++ParameterIndex)
            {
                if (!Parameters[ParameterIndex].DefaultExpression.empty())
                {
                    Existing->Parameters[ParameterIndex].DefaultExpression =
                        Parameters[ParameterIndex].DefaultExpression;
                }
            }
            if (Node.bDefinition)
            {
                for (size_t ParameterIndex = 0; ParameterIndex < Parameters.size(); ++ParameterIndex)
                {
                    Parameters[ParameterIndex].DefaultExpression =
                        Existing->Parameters[ParameterIndex].DefaultExpression;
                }
                Existing->Parameters = Parameters;
                Existing->Initializers =
                    std::map<std::string, std::string>(Node.Initializers.begin(), Node.Initializers.end());
                Existing->Body = Document.GetSourceText(Node.BodyRange);
                Definitions[{OwningType->Name, Signature}] = &Node;
            }
            if (bFreeFunction)
            {
                Callables.swap(Result.Functions);
            }
        }
    }
    for (auto &Entry : Result.Types)
    {
        auto &Type = Entry.second;
        for (auto &Callable : Type.Callables)
        {
            const auto Definition = Definitions.find({Type.Name, Callable.Signature});
            if (Definition != Definitions.end())
            {
                AnalyzeBody(Callable, *Definition->second);
            }
            const auto HasMember = [&](const std::string &Name)
            {
                return std::any_of(Type.Members.begin(), Type.Members.end(),
                                   [&](const FNativeApiMember &Member) { return Member.Name == Name; });
            };
            if (!HasMember(Callable.ReadMember))
            {
                Callable.ReadMember.clear();
            }
            if (!HasMember(Callable.WriteMember))
            {
                Callable.WriteMember.clear();
            }
        }
    }
    for (auto& Callable : Result.Functions)
    {
        const auto Definition = Definitions.find({"", Callable.Signature});
        if (Definition != Definitions.end())
        {
            AnalyzeBody(Callable, *Definition->second);
        }
    }
    for (const auto &Reflected : Request.ReflectedTypes)
    {
        FNativeApiReflectionMatch Match;
        Match.Path = Reflected.Path;
        Match.NativeName = Reflected.NativeName;
        Match.Resolution = Index.ResolveType(Reflected.NativeName);
        const auto Type = Result.Types.find(Match.Resolution.QualifiedName);
        for (const auto &Property : Reflected.Properties)
        {
            bool bFound = false;
            if (Match.Resolution.bResolved && Type != Result.Types.end())
            {
                for (const auto &Member : Type->second.Members)
                {
                    if (Member.Name != Property.first)
                    {
                        continue;
                    }
                    bFound = true;
                    const std::string ResolvedType = FCppSourceIndex::SubstituteTemplateParameters(
                        Member.Type, Type->second.TemplateParameters, Match.Resolution.TemplateArguments);
                    Match.ResolvedMemberTypes[Property.first] = ResolvedType;
                    if (ResolvedType != Property.second)
                    {
                        Match.TypeMismatches.push_back(Property.first);
                    }
                }
            }
            if (bFound)
            {
                Match.MatchedProperties.push_back(Property.first);
            }
            else
            {
                Match.MissingProperties.push_back(Property.first);
            }
        }
        Result.ReflectionMatches.push_back(std::move(Match));
    }
    Result.bSucceeded =
        !Result.Sources.empty() && std::all_of(Result.Sources.begin(), Result.Sources.end(),
                                               [](const FNativeApiSourceStatus &Source) { return Source.bParsed; });
    return Result;
}

static std::string JsonString(const std::string &Value)
{
    std::ostringstream Output;
    Output << '"';
    for (unsigned char Character : Value)
    {
        if (Character == '"' || Character == '\\')
        {
            Output << '\\' << Character;
        }
        else if (Character < 32)
        {
            Output << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(Character);
        }
        else
        {
            Output << Character;
        }
    }
    Output << '"';
    return Output.str();
}

template <typename Element, typename Writer>
static std::string JsonArray(const std::vector<Element> &Values, Writer Write)
{
    std::string Result = "[";
    for (const auto &Value : Values)
    {
        if (Result.size() > 1)
        {
            Result += ",";
        }
        Result += Write(Value);
    }
    return Result + "]";
}

static std::string JsonEvidence(const FNativeApiEvidence &Evidence)
{
    return "{\"File\":" + JsonString(Evidence.File) + ",\"Line\":" + std::to_string(Evidence.Line) +
           ",\"Condition\":" + JsonString(Evidence.Condition) + ",\"Text\":" + JsonString(Evidence.Text) +
           ",\"BeginOffset\":" + std::to_string(Evidence.BeginOffset) +
           ",\"EndOffset\":" + std::to_string(Evidence.EndOffset) + "}";
}

static std::string JsonCallable(const FNativeApiCallable &Callable)
{
    std::ostringstream Output;
    Output << std::boolalpha << "{\"Name\":" << JsonString(Callable.Name)
           << ",\"Include\":" << JsonString(Callable.Include) << ",\"Module\":" << JsonString(Callable.Module)
           << ",\"ForwardingExpression\":" << JsonArray(Callable.ForwardingExpression, JsonString)
           << ",\"Signature\":" << JsonString(Callable.Signature)
           << ",\"ReturnType\":" << JsonString(Callable.ReturnType)
           << ",\"Access\":" << static_cast<int>(Callable.Access) << ",\"Constructor\":" << Callable.bConstructor
           << ",\"Destructor\":" << Callable.bDestructor << ",\"AccessVerified\":" << Callable.bAccessVerified
           << ",\"Const\":" << Callable.bConst << ",\"Static\":" << Callable.bStatic
           << ",\"Exported\":" << Callable.bExported << ",\"InlineDefinition\":" << Callable.bInlineDefinition
           << ",\"Deleted\":" << Callable.bDeleted << ",\"Virtual\":" << Callable.bVirtual
           << ",\"ReadMember\":" << JsonString(Callable.ReadMember)
           << ",\"WriteMember\":" << JsonString(Callable.WriteMember)
           << ",\"ForwardedCall\":" << JsonString(Callable.ForwardedCall) << ",\"Body\":" << JsonString(Callable.Body)
           << ",\"Evidence\":" << JsonArray(Callable.Evidence, JsonEvidence) << ",\"Parameters\":"
           << JsonArray(Callable.Parameters,
                        [](const FNativeApiParameter &Parameter)
                        {
                            return "{\"Name\":" + JsonString(Parameter.Name) +
                                   ",\"Type\":" + JsonString(Parameter.Type) +
                                   ",\"Default\":" + JsonString(Parameter.DefaultExpression) + "}";
                        })
           << ",\"Initializers\":{";
    bool bFirst = true;
    for (const auto &Initializer : Callable.Initializers)
    {
        if (!bFirst)
        {
            Output << ',';
        }
        bFirst = false;
        Output << JsonString(Initializer.first) << ':' << JsonString(Initializer.second);
    }
    Output << "},\"DirectCalls\":{";
    bFirst = true;
    for (const auto &Call : Callable.DirectCalls)
    {
        if (!bFirst)
        {
            Output << ',';
        }
        bFirst = false;
        Output << JsonString(Call.first) << ':' << JsonArray(Call.second, JsonString);
    }
    Output << "},\"CallSites\":" << JsonArray(Callable.CallSites, [](const FNativeApiCallable::FCallSite& Site)
    {
        return std::string("{\"Name\":") + JsonString(Site.Name) + ",\"Receiver\":" + JsonString(Site.Receiver) +
            ",\"Arguments\":" + JsonArray(Site.Arguments, JsonString) + ",\"TokenPosition\":" + std::to_string(Site.TokenPosition) + "}";
    });
    Output << ",\"PublicForwarding\":{\"RootParameter\":" << Callable.PublicForwarding.RootParameter
           << ",\"Members\":" << JsonArray(Callable.PublicForwarding.Members, JsonString)
           << ",\"Method\":" << JsonString(Callable.PublicForwarding.Method)
           << ",\"Arguments\":" << JsonArray(Callable.PublicForwarding.Arguments,
               [](int ParameterIndex) { return std::to_string(ParameterIndex); }) << "}}";
    return Output.str();
}

std::string FNativeApiCache::ToJson() const
{
    std::ostringstream Output;
    Output << std::boolalpha << "{\"SchemaVersion\":1,\"AnalyzerVersion\":\"NativeApiCache.Text.18\",\"Configuration\":"
           << JsonString(Configuration) << ",\"Arguments\":" << JsonArray(Arguments, JsonString)
           << ",\"SemanticallyVerified\":false,\"Succeeded\":" << bSucceeded << ",\"Sources\":"
           << JsonArray(Sources,
                        [](const FNativeApiSourceStatus &Source)
                        {
                            std::ostringstream Entry;
                            Entry << std::boolalpha << "{\"File\":" << JsonString(Source.Source.File)
                                  << ",\"Module\":" << JsonString(Source.Source.Module)
                                  << ",\"Include\":" << JsonString(Source.Source.Include)
                                  << ",\"Fingerprint\":" << JsonString(Source.Fingerprint)
                                  << ",\"Parsed\":" << Source.bParsed
                                  << ",\"UnresolvedDeclarations\":" << Source.UnresolvedDeclarations
                                  << ",\"MacroInvocations\":" << Source.MacroInvocations << ",\"Diagnostics\":"
                                  << JsonArray(Source.Diagnostics,
                                               [](const FCppDiagnostic &Diagnostic)
                                               {
                                                   return "{\"Severity\":" +
                                                          std::to_string(static_cast<int>(Diagnostic.Severity)) +
                                                          ",\"Line\":" + std::to_string(Diagnostic.Location.Line) +
                                                          ",\"Message\":" + JsonString(Diagnostic.Message) + "}";
                                               })
                                  << "}";
                            return Entry.str();
                        })
           << ",\"Types\":[";
    bool bFirst = true;
    for (const auto &Entry : Types)
    {
        if (!bFirst)
        {
            Output << ',';
        }
        bFirst = false;
        const auto &Type = Entry.second;
        Output << "{\"Name\":" << JsonString(Type.Name) << ",\"Module\":" << JsonString(Type.Module)
               << ",\"Definition\":" << (Type.bDefinition ? "true" : "false")
               << ",\"Exported\":" << Type.bExported
               << ",\"ReflectedClass\":" << Type.bReflectedClass << ",\"MinimalApi\":" << Type.bMinimalApi
               << ",\"CustomConstructor\":" << Type.bCustomConstructor << ",\"LegacyGeneratedBody\":" << Type.bLegacyGeneratedBody
               << ",\"GeneratedBodyAccess\":" << static_cast<int>(Type.GeneratedBodyAccess)
               << ",\"Declaration\":" << JsonEvidence(Type.Declaration) << ",\"GeneratedBody\":" << JsonEvidence(Type.GeneratedBody)
               << ",\"Include\":" << JsonString(Type.Include)
               << ",\"BaseTypes\":" << JsonArray(Type.BaseTypes, JsonString)
               << ",\"TemplateParameters\":" << JsonArray(Type.TemplateParameters, JsonString)
               << ",\"Evidence\":" << JsonArray(Type.Evidence, JsonEvidence) << ",\"Members\":"
               << JsonArray(Type.Members,
                            [](const FNativeApiMember &Member)
                            {
                                return "{\"Name\":" + JsonString(Member.Name) + ",\"Type\":" + JsonString(Member.Type) +
                                       ",\"Access\":" + std::to_string(static_cast<int>(Member.Access)) +
                                       ",\"Evidence\":" + JsonEvidence(Member.Evidence) + "}";
                            })
               << ",\"Callables\":" << JsonArray(Type.Callables, JsonCallable) << "}";
    }
    Output << "],\"Functions\":" << JsonArray(Functions, JsonCallable)
           << ",\"Constants\":" << JsonArray(Constants,
                [](const FNativeApiConstant& Constant)
                {
                    return "{\"Name\":" + JsonString(Constant.Name) + ",\"Include\":" + JsonString(Constant.Include) +
                        ",\"Module\":" + JsonString(Constant.Module) + ",\"Expression\":" + JsonString(Constant.Expression) +
                        ",\"Evidence\":" + JsonEvidence(Constant.Evidence) + "}";
                }) << ",\"ReflectionMatches\":"
           << JsonArray(ReflectionMatches,
                        [](const FNativeApiReflectionMatch &Match)
                        {
                            std::ostringstream Entry;
                            Entry << std::boolalpha << "{\"Path\":" << JsonString(Match.Path)
                                  << ",\"NativeName\":" << JsonString(Match.NativeName)
                                  << ",\"Resolved\":" << Match.Resolution.bResolved
                                  << ",\"Declaration\":" << JsonString(Match.Resolution.QualifiedName)
                                  << ",\"TemplateArguments\":"
                                  << JsonArray(Match.Resolution.TemplateArguments, JsonString)
                                  << ",\"AliasChain\":" << JsonArray(Match.Resolution.AliasChain, JsonString)
                                  << ",\"Error\":" << JsonString(Match.Resolution.Error)
                                  << ",\"MatchedProperties\":" << JsonArray(Match.MatchedProperties, JsonString)
                                  << ",\"MissingProperties\":" << JsonArray(Match.MissingProperties, JsonString)
                                  << ",\"TypeMismatches\":" << JsonArray(Match.TypeMismatches, JsonString)
                                  << ",\"ResolvedMemberTypes\":{";
                            bool bFirstMemberType = true;
                            for (const auto &MemberType : Match.ResolvedMemberTypes)
                            {
                                if (!bFirstMemberType)
                                {
                                    Entry << ',';
                                }
                                bFirstMemberType = false;
                                Entry << JsonString(MemberType.first) << ':' << JsonString(MemberType.second);
                            }
                            Entry << "}}";
                            return Entry.str();
                        })
           << "}";
    return Output.str();
}
