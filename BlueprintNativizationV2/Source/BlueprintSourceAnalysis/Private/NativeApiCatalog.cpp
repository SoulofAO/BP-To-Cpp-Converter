#include "NativeApiCatalog.h"
#include "BlueprintNativeApiCacheLibrary.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NativeApiCache.h"
#include "NativeModuleCache.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SourceCodeNavigation.h"
#include "Internationalization/Regex.h"
#include "UObject/UnrealType.h"
#include "UObject/Interface.h"

static TMap<FString, TSharedPtr<FJsonObject>> GenerationTypes;
static TArray<TSharedPtr<FJsonObject>> GenerationCatalogs;
static TArray<FString> GenerationErrors;
static TArray<TSharedPtr<FJsonObject>> ModuleIndexes;
static TSet<FString> LoadedModuleFiles;
static TMap<FString, TSet<FString>> ValueTypeModules;

void FNativeApiCatalog::AddError(const FString &Error)
{
    GenerationErrors.AddUnique(Error);
}

const TArray<FString> &FNativeApiCatalog::GetErrors()
{
    return GenerationErrors;
}

void FNativeApiCatalog::DiscardErrorsAfter(int32 ErrorCount)
{
    GenerationErrors.SetNum(ErrorCount);
}

void FNativeApiCatalog::BeginGeneration(bool bAllowSourceFallback)
{
    GenerationTypes.Reset();
    GenerationCatalogs.Reset();
    GenerationErrors.Reset();
    ModuleIndexes.Reset();
    LoadedModuleFiles.Reset();
    ValueTypeModules.Reset();
    bool bHasModuleIndex = false;
    for (const FString& Name : {TEXT("ProjectModules.json"), TEXT("EngineModules.json")})
    {
        const FString File = FNativeModuleCache::GetConfigurationDirectory() / Name;
        if (FPaths::FileExists(File))
        {
            FString Report;
            TSharedPtr<FJsonObject> Index;
            if (FNativeModuleCache::ReadIndex(File, Index, Report))
            {
                ModuleIndexes.Add(Index);
                bHasModuleIndex = true;
            }
            else if (!bAllowSourceFallback)
            {
                AddError(Report);
            }
        }
    }
    if (bHasModuleIndex || bAllowSourceFallback)
    {
        return;
    }
    for (const FString &Name : {TEXT("Project.json"), TEXT("Editor.json")})
    {
        const FString File = FNativeModuleCache::GetCacheRootDirectory() / Name;
        if (!FPaths::FileExists(File))
        {
            continue;
        }
        FString Report;
        TSharedPtr<FJsonObject> Catalog;
        if (!UBlueprintNativeApiCacheLibrary::ReadValidatedSourceCache(File, Catalog, Report))
        {
            AddError(Report);
            continue;
        }
        GenerationCatalogs.Add(Catalog);
    }
}

static TSharedPtr<FJsonObject> FindCatalogType(const TSharedPtr<FJsonObject> &Catalog, const FString &Name)
{
    FString ResolvedName = Name;
    for (const auto &Value : Catalog->GetArrayField(TEXT("ReflectionMatches")))
    {
        const auto Match = Value->AsObject();
        if (Match->GetStringField(TEXT("NativeName")) == Name && Match->GetBoolField(TEXT("Resolved")))
        {
            ResolvedName = Match->GetStringField(TEXT("Declaration"));
        }
    }
    for (const auto &Value : Catalog->GetArrayField(TEXT("Types")))
    {
        const auto Type = Value->AsObject();
        bool bDefinition = false;
        if (Type->GetStringField(TEXT("Name")) == ResolvedName &&
            Type->TryGetBoolField(TEXT("Definition"), bDefinition) && bDefinition)
        {
            return Type;
        }
    }
    return nullptr;
}

static bool LoadIndexedModule(const TSharedPtr<FJsonObject>& Index, const FString& ModuleName)
{
    const TSharedPtr<FJsonObject>* Entry = nullptr;
    FString File;
    if (!Index->GetObjectField(TEXT("Modules"))->TryGetObjectField(ModuleName, Entry) ||
        !(*Entry)->TryGetStringField(TEXT("File"), File) || LoadedModuleFiles.Contains(File))
    {
        return true;
    }
    LoadedModuleFiles.Add(File);
    FString Report;
    TArray<FString> CurrentSources;
    const FString Directory = (*Entry)->GetStringField(TEXT("Directory"));
    IFileManager::Get().FindFilesRecursive(CurrentSources, *Directory, TEXT("*.h"), true, false);
    IFileManager::Get().FindFilesRecursive(CurrentSources, *Directory, TEXT("*.cpp"), true, false, false);
    IFileManager::Get().FindFilesRecursive(CurrentSources, *Directory, TEXT("*.inl"), true, false, false);
    CurrentSources.RemoveAll([](FString& Source)
    {
        FPaths::NormalizeFilename(Source);
        return Source.Contains(TEXT("/Intermediate/")) || Source.EndsWith(TEXT(".generated.h")) || Source.EndsWith(TEXT(".gen.cpp"));
    });
    if (CurrentSources.Num() != (*Entry)->GetIntegerField(TEXT("SourceCount")))
    {
        Report = FString::Format(TEXT("Module source inventory changed; rebuild cache: {0}"), {ModuleName});
        FNativeModuleCache::MarkCacheInvalid(File, Report);
        FNativeApiCatalog::AddError(Report);
        return false;
    }
    TSharedPtr<FJsonObject> Catalog;
    if (!UBlueprintNativeApiCacheLibrary::ReadValidatedSourceCache(File, Catalog, Report))
    {
        FNativeApiCatalog::AddError(Report);
        return false;
    }
    GenerationCatalogs.Add(Catalog);
    return true;
}

TSharedPtr<FJsonObject> FNativeApiCatalog::FindType(UStruct *Structure)
{
    if (!Structure || !Structure->IsNative())
    {
        return nullptr;
    }
    const FString Name = FString(Structure->GetPrefixCPP()) + Structure->GetName();
    FString DefinitionName = Name;
    if (const auto Found = GenerationTypes.Find(Structure->GetPathName()))
    {
        return *Found;
    }
    for (const auto& Index : ModuleIndexes)
    {
        FString ModuleName = FPackageName::GetShortName(Structure->GetOutermost());
        const TSharedPtr<FJsonObject>* Aliases = nullptr;
        if (Index->TryGetObjectField(TEXT("TypeAliases"), Aliases))
        {
            FString Declaration;
            if ((*Aliases)->TryGetStringField(Name, Declaration) && !Declaration.IsEmpty())
            {
                DefinitionName = Declaration;
            }
        }
        const TSharedPtr<FJsonObject>* Owners = nullptr;
        if (Index->TryGetObjectField(TEXT("TypeOwners"), Owners))
        {
            FString DefinitionOwner;
            if ((*Owners)->TryGetStringField(DefinitionName, DefinitionOwner) && !DefinitionOwner.IsEmpty())
            {
                ModuleName = DefinitionOwner;
            }
        }
        if (!LoadIndexedModule(Index, ModuleName))
        {
            return nullptr;
        }
    }
    for (const auto &Catalog : GenerationCatalogs)
    {
        if (auto Type = FindCatalogType(Catalog, DefinitionName))
        {
            GenerationTypes.Add(Structure->GetPathName(), Type);
            return Type;
        }
    }
    TArray<FString> Files;
    FString Header;
    FString Source;
    const FString MathDirectory = FPaths::EngineSourceDir() / TEXT("Runtime/Core/Public/Math");
    const FString MathHeader = MathDirectory / (Structure->GetName() + TEXT(".h"));
    if (FPaths::FileExists(MathHeader))
    {
        Files.Add(MathHeader);
        Files.Add(MathDirectory / TEXT("MathFwd.h"));
    }
    else if (FSourceCodeNavigation::FindClassHeaderPath(Structure, Header))
    {
        if (Header.EndsWith(TEXT("NoExportTypes.h")))
        {
            TArray<FString> Headers;
            IFileManager::Get().FindFilesRecursive(Headers, *MathDirectory, TEXT("*.h"), true, false);
            const FRegexPattern DefinitionPattern(TEXT("\\b(?:struct|class)\\s+(?:\\w+_API\\s+)?") + Name + TEXT("\\s*(?:final\\s*)?(?:\\{|:)"));
            for (const FString& Candidate : Headers)
            {
                FString Contents;
                FFileHelper::LoadFileToString(Contents, *Candidate);
                FRegexMatcher Definition(DefinitionPattern, Contents);
                if (Definition.FindNext())
                {
                    Files.Add(Candidate);
                }
            }
            Files.Add(MathDirectory / TEXT("MathFwd.h"));
        }
        else
        {
            Files.Add(Header);
        }
    }
    if (FSourceCodeNavigation::FindClassSourcePath(Structure, Source))
    {
        Files.AddUnique(Source);
    }
    const TArray<FString> DeclarationFiles = Files;
    for (const FString& DeclarationFile : DeclarationFiles)
    {
        const FString InlineFile = FPaths::ChangeExtension(DeclarationFile, TEXT("inl"));
        if (FPaths::FileExists(InlineFile))
        {
            Files.AddUnique(InlineFile);
        }
    }
    FNativeApiCacheRequest Request;
    for (const FString& Argument : UBlueprintNativeApiCacheLibrary::GetSourceArguments())
    {
        Request.Arguments.push_back(TCHAR_TO_UTF8(*Argument));
    }
    Request.ReflectedTypes.push_back({TCHAR_TO_UTF8(*Structure->GetPathName()), TCHAR_TO_UTF8(*Name), {}});
    for (const FString &File : Files)
    {
        FNativeSourceInput Input;
        FString SourceFile = FPaths::ConvertRelativePathToFull(File);
        FPaths::NormalizeFilename(SourceFile);
        Input.File = TCHAR_TO_UTF8(*SourceFile);
        FString Directory = FPaths::GetPath(SourceFile);
        while (!Directory.IsEmpty())
        {
            TArray<FString> Rules;
            IFileManager::Get().FindFiles(Rules, *(Directory / TEXT("*.Build.cs")), true, false);
            if (Rules.Num() == 1)
            {
                FString Module = Rules[0];
                Module.RemoveFromEnd(TEXT(".Build.cs"));
                Input.Module = TCHAR_TO_UTF8(*Module);
                for (const FString& Prefix : {TEXT("Public/"), TEXT("Classes/")})
                {
                    const FString PublicRoot = Directory / Prefix;
                    if (SourceFile.StartsWith(PublicRoot))
                    {
                        Input.Include = TCHAR_TO_UTF8(*SourceFile.RightChop(PublicRoot.Len()));
                    }
                }
                if (Input.Include.empty() && !FPaths::DirectoryExists(Directory / TEXT("Public")))
                {
                    const FString RelativeFile = SourceFile.RightChop(Directory.Len() + 1);
                    if (!RelativeFile.StartsWith(TEXT("Private/")) && !RelativeFile.StartsWith(TEXT("Internal/")))
                    {
                        Input.Include = TCHAR_TO_UTF8(*RelativeFile);
                    }
                }
                break;
            }
            const FString Parent = FPaths::GetPath(Directory);
            if (Parent == Directory)
            {
                break;
            }
            Directory = Parent;
        }
        Request.Sources.push_back(Input);
    }
    TSharedPtr<FJsonObject> Type;
    try
    {
        const auto Cache = FNativeApiCacheBuilder::Build(Request);
        if (Cache.bSucceeded)
        {
            TSharedPtr<FJsonObject> Catalog;
            const FString Json = UTF8_TO_TCHAR(Cache.ToJson().c_str());
            if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Catalog))
            {
                Type = FindCatalogType(Catalog, Name);
                GenerationCatalogs.Add(Catalog);
            }
        }
    }
    catch (const std::exception &Error)
    {
        AddError(FString::Format(TEXT("Cannot analyze {0}: {1}"), {Name, UTF8_TO_TCHAR(Error.what())}));
    }
    GenerationTypes.Add(Structure->GetPathName(), Type);
    return Type;
}

#include "UObject/UObjectIterator.h"

static bool IsUnconditionalDeclaration(const TSharedPtr<FJsonObject>& Declaration)
{
    const TSharedPtr<FJsonObject>* Definition = nullptr;
    if (Declaration->TryGetObjectField(TEXT("Declaration"), Definition))
    {
        return (*Definition)->GetStringField(TEXT("Condition")).IsEmpty();
    }
    for (const auto& Value : Declaration->GetArrayField(TEXT("Evidence")))
    {
        if (!Value->AsObject()->GetStringField(TEXT("Condition")).IsEmpty())
        {
            return false;
        }
    }
    return true;
}

static FString NormalizeForwardingType(FString Type)
{
    Type.ReplaceInline(TEXT("class "), TEXT(""));
    Type.ReplaceInline(TEXT("struct "), TEXT(""));
    Type.ReplaceInline(TEXT(" "), TEXT(""));
    return Type;
}

bool FNativeApiCatalog::CanCallFunctionDirectly(UFunction* Function, UClass* CallingClass)
{
    const auto Owner = FindType(Function->GetOwnerClass());
    if (!Owner || Owner->GetStringField(TEXT("Include")).IsEmpty())
    {
        return false;
    }
    int32 ParameterCount = 0;
    for (TFieldIterator<FProperty> Parameter(Function); Parameter; ++Parameter)
    {
        if (Parameter->HasAnyPropertyFlags(CPF_Parm) && !Parameter->HasAnyPropertyFlags(CPF_ReturnParm))
        {
            ++ParameterCount;
        }
    }
    int32 MatchingDeclarations = 0;
    bool bAvailable = false;
    for (const auto& Value : Owner->GetArrayField(TEXT("Callables")))
    {
        const auto Callable = Value->AsObject();
        if (Callable->GetStringField(TEXT("Name")) != Function->GetName() ||
            Callable->GetBoolField(TEXT("Static")) != Function->HasAnyFunctionFlags(FUNC_Static) ||
            Callable->GetArrayField(TEXT("Parameters")).Num() != ParameterCount)
        {
            continue;
        }
        bool bHasHeaderDeclaration = false;
        for (const auto& Evidence : Callable->GetArrayField(TEXT("Evidence")))
        {
            bHasHeaderDeclaration |= Evidence->AsObject()->GetStringField(TEXT("File")).EndsWith(TEXT(".h"));
        }
        if (!bHasHeaderDeclaration)
        {
            continue;
        }
        ++MatchingDeclarations;
        const bool bAccessible = Callable->GetIntegerField(TEXT("Access")) == 1 ||
            (Callable->GetIntegerField(TEXT("Access")) == 2 && CallingClass && CallingClass->IsChildOf(Function->GetOwnerClass()));
        bAvailable = bAccessible && Callable->GetBoolField(TEXT("AccessVerified")) &&
            !Callable->GetBoolField(TEXT("Deleted")) && IsUnconditionalDeclaration(Callable) &&
            (Owner->GetBoolField(TEXT("Exported")) || Callable->GetBoolField(TEXT("Exported")) || Callable->GetBoolField(TEXT("InlineDefinition")));
    }
    return MatchingDeclarations == 1 && bAvailable;
}

TSharedPtr<FJsonObject> FNativeApiCatalog::FindCallable(UFunction* Function)
{
    if (!Function || !Function->HasAnyFunctionFlags(FUNC_Native))
    {
        return nullptr;
    }
    const auto Owner = FindType(Function->GetOwnerClass());
    if (!Owner)
    {
        return nullptr;
    }
    TArray<FProperty*> Parameters;
    for (TFieldIterator<FProperty> Parameter(Function); Parameter; ++Parameter)
    {
        if (Parameter->HasAnyPropertyFlags(CPF_Parm) && !Parameter->HasAnyPropertyFlags(CPF_ReturnParm))
        {
            Parameters.Add(*Parameter);
        }
    }
    TSharedPtr<FJsonObject> Selected;
    for (const auto& Value : Owner->GetArrayField(TEXT("Callables")))
    {
        const auto Callable = Value->AsObject();
        if (Callable->GetStringField(TEXT("Name")) != Function->GetName() ||
            Callable->GetArrayField(TEXT("Parameters")).Num() != Parameters.Num() || Callable->GetStringField(TEXT("Body")).IsEmpty())
        {
            continue;
        }
        if (Selected || !Callable->GetBoolField(TEXT("AccessVerified")) || !IsUnconditionalDeclaration(Callable) ||
            Callable->GetBoolField(TEXT("Deleted")))
        {
            return nullptr;
        }
        const auto& NativeParameters = Callable->GetArrayField(TEXT("Parameters"));
        for (int32 ParameterIndex = 0; ParameterIndex < Parameters.Num(); ++ParameterIndex)
        {
            const auto NativeParameter = NativeParameters[ParameterIndex]->AsObject();
            FString ExtendedType;
            FString ReflectedType = Parameters[ParameterIndex]->GetCPPType(&ExtendedType);
            ReflectedType += ExtendedType;
            FString NativeType = NormalizeForwardingType(NativeParameter->GetStringField(TEXT("Type")));
            NativeType.RemoveFromStart(TEXT("const"));
            NativeType.RemoveFromEnd(TEXT("&"));
            if (NativeParameter->GetStringField(TEXT("Name")) != Parameters[ParameterIndex]->GetName() ||
                NativeType != NormalizeForwardingType(ReflectedType))
            {
                return nullptr;
            }
        }
        Selected = Callable;
    }
    return Selected;
}

static TSharedPtr<FJsonObject> FindIndexedValueType(const FString& NativeName)
{
    FString DefinitionName = NativeName;
    for (const auto& Index : ModuleIndexes)
    {
        const TSharedPtr<FJsonObject>* Aliases = nullptr;
        if (Index->TryGetObjectField(TEXT("TypeAliases"), Aliases))
        {
            FString Declaration;
            if ((*Aliases)->TryGetStringField(NativeName, Declaration) && !Declaration.IsEmpty())
            {
                DefinitionName = Declaration;
            }
        }
        const TSharedPtr<FJsonObject>* Owners = nullptr;
        FString Module;
        if (Index->TryGetObjectField(TEXT("TypeOwners"), Owners) &&
            (*Owners)->TryGetStringField(DefinitionName, Module) && !Module.IsEmpty())
        {
            if (!LoadIndexedModule(Index, Module))
            {
                return nullptr;
            }
        }
    }
    for (const auto& Catalog : GenerationCatalogs)
    {
        if (auto Type = FindCatalogType(Catalog, DefinitionName))
        {
            return Type;
        }
    }
    return nullptr;
}

static TSharedPtr<FJsonObject> FindIndexedConstant(const FString& Name)
{
    for (const TSharedPtr<FJsonObject>& Index : ModuleIndexes)
    {
        const TSharedPtr<FJsonObject>* Owners = nullptr;
        FString Module;
        if (Index->TryGetObjectField(TEXT("ConstantOwners"), Owners) && (*Owners)->TryGetStringField(Name, Module))
        {
            if (Module.IsEmpty() || !LoadIndexedModule(Index, Module))
            {
                return nullptr;
            }
        }
    }
    TSharedPtr<FJsonObject> Selected;
    for (const TSharedPtr<FJsonObject>& Catalog : GenerationCatalogs)
    {
        const TArray<TSharedPtr<FJsonValue>>* Constants = nullptr;
        if (!Catalog->TryGetArrayField(TEXT("Constants"), Constants))
        {
            continue;
        }
        for (const TSharedPtr<FJsonValue>& Value : *Constants)
        {
            const TSharedPtr<FJsonObject> Constant = Value->AsObject();
            if (Constant->GetStringField(TEXT("Name")) != Name || Constant->GetStringField(TEXT("Include")).IsEmpty())
            {
                continue;
            }
            if (Selected && (Selected->GetStringField(TEXT("Module")) != Constant->GetStringField(TEXT("Module")) ||
                Selected->GetStringField(TEXT("Include")) != Constant->GetStringField(TEXT("Include")) ||
                Selected->GetStringField(TEXT("Expression")) != Constant->GetStringField(TEXT("Expression"))))
            {
                return nullptr;
            }
            Selected = Constant;
        }
    }
    return Selected;
}

TSharedPtr<FJsonObject> FNativeApiCatalog::FindPublicDeclaration(const FString& Name)
{
    if (const auto Type = FindIndexedValueType(Name))
    {
        if (!Type->GetStringField(TEXT("Include")).IsEmpty() && IsUnconditionalDeclaration(Type))
        {
            return Type;
        }
    }
    if (const auto Constant = FindIndexedConstant(Name))
    {
        return Constant;
    }
    TSharedPtr<FJsonObject> Enumeration;
    for (const auto& Catalog : GenerationCatalogs)
    {
        for (const auto& Value : Catalog->GetArrayField(TEXT("Types")))
        {
            const auto Type = Value->AsObject();
            if (!Type->GetStringField(TEXT("Name")).StartsWith(Name + TEXT("::")) ||
                Type->GetStringField(TEXT("Include")).IsEmpty())
            {
                continue;
            }
            for (const auto& Evidence : Type->GetArrayField(TEXT("Evidence")))
            {
                if (Evidence->AsObject()->GetStringField(TEXT("Text")).TrimStart().StartsWith(TEXT("enum ")))
                {
                    if (Enumeration && Enumeration->GetStringField(TEXT("Name")) != Type->GetStringField(TEXT("Name")))
                    {
                        return nullptr;
                    }
                    Enumeration = Type;
                }
            }
        }
    }
    if (Enumeration)
    {
        return Enumeration;
    }
    TSharedPtr<FJsonObject> Selected;
    for (const auto& Catalog : GenerationCatalogs)
    {
        for (const auto& Value : Catalog->GetArrayField(TEXT("Functions")))
        {
            const auto Function = Value->AsObject();
            if (Function->GetStringField(TEXT("Name")) == Name && !Function->GetStringField(TEXT("Include")).IsEmpty() &&
                (Function->GetBoolField(TEXT("Exported")) || Function->GetBoolField(TEXT("InlineDefinition"))) &&
                IsUnconditionalDeclaration(Function))
            {
                if (Selected && Selected->GetStringField(TEXT("Signature")) != Function->GetStringField(TEXT("Signature")))
                {
                    return nullptr;
                }
                Selected = Function;
            }
        }
    }
    return Selected;
}

bool FNativeApiCatalog::FindEventOverride(UClass* Class, UFunction* Event, UClass*& MethodOwner,
    TSharedPtr<FJsonObject>& Method, FString& Body, FString& Diagnostic, TSet<FString>& Includes, TSet<FString>& Modules)
{
    MethodOwner = nullptr;
    Method.Reset();
    Body.Reset();
    Diagnostic.Reset();
    for (UClass* Owner = Class; Owner; Owner = Owner->GetSuperClass())
    {
        if (!Owner->HasAnyClassFlags(CLASS_Native))
        {
            continue;
        }
        const TSharedPtr<FJsonObject> Type = FindType(Owner);
        if (!Type)
        {
            continue;
        }
        TArray<TSharedPtr<FJsonObject>> Candidates;
        for (const TSharedPtr<FJsonValue>& Value : Type->GetArrayField(TEXT("Callables")))
        {
            const TSharedPtr<FJsonObject> Callable = Value->AsObject();
            const TSharedPtr<FJsonObject>* Calls = nullptr;
            bool bCallsEvent = Callable->TryGetObjectField(TEXT("DirectCalls"), Calls) && (*Calls)->HasField(Event->GetName());
            const TArray<TSharedPtr<FJsonValue>>* CallSites = nullptr;
            if (!bCallsEvent && Callable->TryGetArrayField(TEXT("CallSites"), CallSites))
            {
                for (const TSharedPtr<FJsonValue>& CallSiteValue : *CallSites)
                {
                    const TSharedPtr<FJsonObject> CallSite = CallSiteValue->AsObject();
                    const FString Receiver = CallSite->GetStringField(TEXT("Receiver"));
                    if (CallSite->GetStringField(TEXT("Name")) == Event->GetName() &&
                        (Receiver.IsEmpty() || Receiver == TEXT("this")))
                    {
                        bCallsEvent = true;
                    }
                }
            }
            if (Callable->GetBoolField(TEXT("Virtual")) && !Callable->GetBoolField(TEXT("Static")) &&
                !Callable->GetBoolField(TEXT("Deleted")) &&
                Callable->GetBoolField(TEXT("AccessVerified")) &&
                (Callable->GetIntegerField(TEXT("Access")) == 1 || Callable->GetIntegerField(TEXT("Access")) == 2) &&
                bCallsEvent &&
                IsUnconditionalDeclaration(Callable))
            {
                Candidates.Add(Callable);
            }
        }
        if (Candidates.Num() > 1)
        {
            Diagnostic = TEXT("Multiple accessible virtual callers of the event exist in the source cache.");
            return false;
        }
        if (Candidates.IsEmpty())
        {
            continue;
        }
        MethodOwner = Owner;
        Method = Candidates[0];
        for (const TSharedPtr<FJsonValue>& EvidenceValue : Method->GetArrayField(TEXT("Evidence")))
        {
            const TSharedPtr<FJsonObject> Evidence = EvidenceValue->AsObject();
            if (Evidence->GetStringField(TEXT("File")).EndsWith(TEXT(".h")) &&
                Evidence->GetStringField(TEXT("Text")).Contains(TEXT(" final")))
            {
                Diagnostic = TEXT("The cached virtual caller is final and cannot be overridden.");
                Method.Reset();
                return false;
            }
        }
        Includes.Add(Type->GetStringField(TEXT("Include")));
        Modules.Add(Type->GetStringField(TEXT("Module")));
        Body = Method->GetStringField(TEXT("Body"));
        for (UClass* Derived = Class; Derived && Derived != Owner; Derived = Derived->GetSuperClass())
        {
            if (!Derived->HasAnyClassFlags(CLASS_Native))
            {
                continue;
            }
            const TSharedPtr<FJsonObject> DerivedType = FindType(Derived);
            if (!DerivedType)
            {
                Body.Reset();
                Diagnostic = TEXT("An intermediate native parent has no validated source cache.");
                break;
            }
            for (const TSharedPtr<FJsonValue>& DerivedValue : DerivedType->GetArrayField(TEXT("Callables")))
            {
                const TSharedPtr<FJsonObject> DerivedMethod = DerivedValue->AsObject();
                if (DerivedMethod->GetStringField(TEXT("Signature")) == Method->GetStringField(TEXT("Signature")))
                {
                    Body.Reset();
                    Diagnostic = TEXT("An intermediate native parent overrides the event caller; its implementation must run through Super.");
                }
            }
        }
        if (Body.IsEmpty())
        {
            if (Diagnostic.IsEmpty())
            {
                Diagnostic = TEXT("The source cache has no reusable definition of the virtual caller.");
            }
            return true;
        }
        FCppParseRequest Request;
        Request.SourceFile = "NativeEventOverride.cpp";
        Request.bFollowIncludes = false;
        Request.UnsavedFiles.push_back({Request.SourceFile, std::string("void NativeEventOverride() ") + TCHAR_TO_UTF8(*Body)});
        const FCppSourceDocument Document = FCppSourceParser::Parse(Request);
        const std::vector<FCppToken>* Tokens = nullptr;
        for (const FCppSyntaxNode& Node : Document.Nodes)
        {
            if (!Node.Tokens.empty())
            {
                Tokens = &Node.Tokens;
                break;
            }
        }
        if (!Tokens)
        {
            Body.Reset();
            Diagnostic = TEXT("The cached body could not be tokenized for access validation.");
            return true;
        }
        for (UClass* AccessOwner = Owner; AccessOwner; AccessOwner = AccessOwner->GetSuperClass())
        {
            const TSharedPtr<FJsonObject> AccessType = FindType(AccessOwner);
            if (!AccessType)
            {
                continue;
            }
            for (const FString& Collection : {TEXT("Members"), TEXT("Callables")})
            {
                for (const TSharedPtr<FJsonValue>& MemberValue : AccessType->GetArrayField(Collection))
                {
                    const TSharedPtr<FJsonObject> Member = MemberValue->AsObject();
                    if (Member->GetIntegerField(TEXT("Access")) != 3)
                    {
                        continue;
                    }
                    const std::string Name(TCHAR_TO_UTF8(*Member->GetStringField(TEXT("Name"))));
                    for (const FCppToken& Token : *Tokens)
                    {
                        if (Token.Kind != "Comment" && Token.Spelling == Name)
                        {
                            Body.Reset();
                            Diagnostic = FString(TEXT("The cached parent body uses a private member: ")) + UTF8_TO_TCHAR(Name.c_str());
                            return true;
                        }
                    }
                }
            }
        }
        for (size_t TokenIndex = 0; TokenIndex < Tokens->size(); ++TokenIndex)
        {
            const FCppToken& Token = (*Tokens)[TokenIndex];
            if (Token.Kind == "Comment")
            {
                continue;
            }
            const FString Name = UTF8_TO_TCHAR(Token.Spelling.c_str());
            if (Name == TEXT("CLASS_CompiledFromBlueprint") || Name == TEXT("CLASS_Native"))
            {
                Body.Reset();
                Diagnostic = TEXT("The cached parent body depends on Blueprint/native class flags that change during conversion.");
                return true;
            }
            if (Name == TEXT("#") || Name.StartsWith(TEXT("TRACE_")) || Name.StartsWith(TEXT("DECLARE_")))
            {
                Body.Reset();
                Diagnostic = TEXT("The cached parent body contains preprocessing or implementation-only instrumentation.");
                return true;
            }
            if (!Name.IsEmpty() && (FChar::IsUpper(Name[0]) ||
                (TokenIndex + 1 < Tokens->size() && (*Tokens)[TokenIndex + 1].Spelling == "::")))
            {
                const TSharedPtr<FJsonObject> Dependency = FindIndexedValueType(Name);
                  if (!Dependency)
                  {
                      if (TokenIndex + 1 < Tokens->size() && (*Tokens)[TokenIndex + 1].Spelling == "::" &&
                          Name != TEXT("Super") && Name != Type->GetStringField(TEXT("Name")))
                      {
                          FString QualifiedName = Name;
                          size_t QualifiedEnd = TokenIndex;
                          while (QualifiedEnd + 2 < Tokens->size() && (*Tokens)[QualifiedEnd + 1].Spelling == "::" &&
                              (*Tokens)[QualifiedEnd + 2].Kind == "Identifier")
                          {
                              QualifiedEnd += 2;
                              QualifiedName += TEXT("::") + FString(UTF8_TO_TCHAR((*Tokens)[QualifiedEnd].Spelling.c_str()));
                          }
                          TSharedPtr<FJsonObject> Constant = FindPublicDeclaration(QualifiedName);
                          if (!Constant && QualifiedEnd >= TokenIndex + 2)
                          {
                              const FString ParentName = QualifiedName.Left(QualifiedName.Find(TEXT("::"), ESearchCase::CaseSensitive, ESearchDir::FromEnd));
                              Constant = FindPublicDeclaration(ParentName);
                          }
                          if (Constant)
                          {
                              Includes.Add(Constant->GetStringField(TEXT("Include")));
                              Modules.Add(Constant->GetStringField(TEXT("Module")));
                              TokenIndex = QualifiedEnd;
                              continue;
                          }
                        Body.Reset();
                        Diagnostic = TEXT("The cached parent body uses a qualified declaration without validated public source requirements: ") + Name;
                        return true;
                    }
                    continue;
                }
                const FString Include = Dependency->GetStringField(TEXT("Include"));
                if (Include.IsEmpty())
                {
                    Body.Reset();
                    Diagnostic = TEXT("The cached parent body uses a type without an accessible header: ") + Name;
                    return true;
                }
                Includes.Add(Include);
                Modules.Add(Dependency->GetStringField(TEXT("Module")));
            }
        }
        return true;
    }
    Diagnostic = TEXT("The validated source cache contains no accessible virtual caller of this event.");
    return false;
}

TSet<FString> FNativeApiCatalog::GetValueTypeModules(UStruct* Structure)
{
    if (!Structure || !Structure->IsNative())
    {
        return {};
    }
    if (const auto* Cached = ValueTypeModules.Find(Structure->GetPathName()))
    {
        return *Cached;
    }
    TSet<FString> Modules;
    TSet<FString> Visited;
    TFunction<void(const TSharedPtr<FJsonObject>&)> VisitType;
    auto VisitExpression = [&](const FString& Expression)
    {
        if (Expression.Contains(TEXT("*")) || Expression.Contains(TEXT("&")))
        {
            return;
        }
        for (const FString& Indirection : {TEXT("TObjectPtr<"), TEXT("TWeakObjectPtr<"), TEXT("TSoftObjectPtr<"),
            TEXT("TSoftClassPtr<"), TEXT("TSubclassOf<"), TEXT("TSharedPtr<"), TEXT("TSharedRef<"), TEXT("TWeakPtr<")})
        {
            if (Expression.Contains(Indirection))
            {
                return;
            }
        }
        FRegexMatcher Tokens(FRegexPattern(TEXT("[A-Za-z_][A-Za-z_0-9]*(?:::[A-Za-z_][A-Za-z_0-9]*)*")), Expression);
        while (Tokens.FindNext())
        {
            const FString Name = Tokens.GetCaptureGroup(0);
            if (!Visited.Contains(Name))
            {
                Visited.Add(Name);
                VisitType(FindIndexedValueType(Name));
            }
        }
    };
    VisitType = [&](const TSharedPtr<FJsonObject>& Type)
    {
        if (!Type)
        {
            return;
        }
        const FString Module = Type->GetStringField(TEXT("Module"));
        if (!Module.IsEmpty())
        {
            Modules.Add(Module);
        }
        bool bReflectedClass = false;
        if (Type->TryGetBoolField(TEXT("ReflectedClass"), bReflectedClass) && bReflectedClass)
        {
            return;
        }
        for (const auto& Base : Type->GetArrayField(TEXT("BaseTypes")))
        {
            VisitExpression(Base->AsString());
        }
        for (const auto& Member : Type->GetArrayField(TEXT("Members")))
        {
            VisitExpression(Member->AsObject()->GetStringField(TEXT("Type")));
        }
    };
    VisitType(FindType(Structure));
    ValueTypeModules.Add(Structure->GetPathName(), Modules);
    return Modules;
}
static TSharedPtr<FJsonObject> FindPublicForwardingType(const FString& NativeName,
    TSet<FString>& Includes, TSet<FString>& Modules)
{
    UStruct* Selected = nullptr;
    for (TObjectIterator<UStruct> Structure; Structure; ++Structure)
    {
        if (Structure->IsNative() && !Structure->IsA<UFunction>() &&
            FString(Structure->GetPrefixCPP()) + Structure->GetName() == NativeName)
        {
            if (Selected)
            {
                return nullptr;
            }
            Selected = *Structure;
        }
    }
    auto Type = FNativeApiCatalog::FindType(Selected);
    if (!Type)
    {
        for (const auto& Catalog : GenerationCatalogs)
        {
            Type = FindCatalogType(Catalog, NativeName);
            if (Type)
            {
                break;
            }
        }
    }
    if (!Type || Type->GetStringField(TEXT("Include")).IsEmpty() || !IsUnconditionalDeclaration(Type))
    {
        return nullptr;
    }
    Includes.Add(Type->GetStringField(TEXT("Include")));
    Modules.Add(Type->GetStringField(TEXT("Module")));
    return Type;
}

struct FForwardingValue
{
    FString Code;
    FString Type;
    bool bIsLvalue = false;
};

static bool AreForwardingTypesCompatible(FString Source, FString Destination)
{
    Source = NormalizeForwardingType(Source);
    Destination = NormalizeForwardingType(Destination);
    Source.RemoveFromEnd(TEXT("&"));
    Destination.RemoveFromEnd(TEXT("&"));
    if (Source == Destination)
    {
        return true;
    }
    if (Destination.RemoveFromStart(TEXT("const")))
    {
        Source.RemoveFromStart(TEXT("const"));
        return Source == Destination;
    }
    return false;
}

static bool ResolveForwardingExpression(const TSharedPtr<FJsonObject>& Wrapper, FString& Expression,
    TSet<FString>& Includes, TSet<FString>& Modules)
{
    const TArray<TSharedPtr<FJsonValue>>* Tokens = nullptr;
    if (!Wrapper->TryGetArrayField(TEXT("ForwardingExpression"), Tokens) || Tokens->IsEmpty())
    {
        return false;
    }
    const auto& Parameters = Wrapper->GetArrayField(TEXT("Parameters"));
    int32 Position = 0;
    TFunction<bool(FForwardingValue&, int32)> ReadExpression;
    auto ReadArguments = [&](TArray<FForwardingValue>& Arguments, int32 Depth)
    {
        ++Position;
        if (Position < Tokens->Num() && (*Tokens)[Position]->AsString() == TEXT(")"))
        {
            ++Position;
            return true;
        }
        for (;;)
        {
            FForwardingValue Argument;
            if (!ReadExpression(Argument, Depth + 1) || Position >= Tokens->Num())
            {
                return false;
            }
            Arguments.Add(Argument);
            const FString Separator = (*Tokens)[Position++]->AsString();
            if (Separator == TEXT(")"))
            {
                return true;
            }
            if (Separator != TEXT(","))
            {
                return false;
            }
        }
    };
    auto MatchCallable = [&](const TArray<TSharedPtr<FJsonValue>>& Candidates, const FString& Name,
        const TArray<FForwardingValue>& Arguments, bool bConstReceiver, bool bFreeFunction,
        const TSharedPtr<FJsonObject>& Owner) -> TSharedPtr<FJsonObject>
    {
        TSharedPtr<FJsonObject> Selected;
        for (const auto& Candidate : Candidates)
        {
            const auto Callable = Candidate->AsObject();
            if (Callable->GetStringField(TEXT("Name")) != Name || Callable->GetBoolField(TEXT("Deleted")) ||
                Callable->GetIntegerField(TEXT("Access")) != 1 || !Callable->GetBoolField(TEXT("AccessVerified")) ||
                !IsUnconditionalDeclaration(Callable) ||
                (!bFreeFunction && (Callable->GetBoolField(TEXT("Static")) || (bConstReceiver && !Callable->GetBoolField(TEXT("Const"))))) ||
                (!Callable->GetBoolField(TEXT("Exported")) && !Callable->GetBoolField(TEXT("InlineDefinition")) &&
                    (!Owner || !Owner->GetBoolField(TEXT("Exported")))))
            {
                continue;
            }
            const auto& NativeParameters = Callable->GetArrayField(TEXT("Parameters"));
            if (NativeParameters.Num() != Arguments.Num())
            {
                continue;
            }
            bool bCompatible = true;
            for (int32 ArgumentIndex = 0; ArgumentIndex < Arguments.Num(); ++ArgumentIndex)
            {
                const FString ParameterType = NormalizeForwardingType(NativeParameters[ArgumentIndex]->AsObject()->GetStringField(TEXT("Type")));
                if (ParameterType.EndsWith(TEXT("&")) && !ParameterType.StartsWith(TEXT("const")) && !Arguments[ArgumentIndex].bIsLvalue)
                {
                    bCompatible = false;
                }
                bCompatible &= AreForwardingTypesCompatible(Arguments[ArgumentIndex].Type,
                    NativeParameters[ArgumentIndex]->AsObject()->GetStringField(TEXT("Type")));
            }
            bool bHeaderDeclaration = false;
            for (const auto& Evidence : Callable->GetArrayField(TEXT("Evidence")))
            {
                bHeaderDeclaration |= Evidence->AsObject()->GetStringField(TEXT("File")).EndsWith(TEXT(".h"));
            }
            if (!bCompatible || !bHeaderDeclaration || (bFreeFunction && Callable->GetStringField(TEXT("Include")).IsEmpty()))
            {
                continue;
            }
            if (Selected)
            {
                return nullptr;
            }
            Selected = Callable;
        }
        return Selected;
    };
    ReadExpression = [&](FForwardingValue& Value, int32 Depth)
    {
        if (Depth > 32 || Position >= Tokens->Num())
        {
            return false;
        }
        const FString Name = (*Tokens)[Position++]->AsString();
        Value.Code = Name;
        if (Position < Tokens->Num() && (*Tokens)[Position]->AsString() == TEXT("("))
        {
            TArray<FForwardingValue> Arguments;
            if (!ReadArguments(Arguments, Depth))
            {
                return false;
            }
            TArray<TSharedPtr<FJsonValue>> Functions;
            for (const auto& Catalog : GenerationCatalogs)
            {
                const TArray<TSharedPtr<FJsonValue>>* NativeFunctions = nullptr;
                if (Catalog->TryGetArrayField(TEXT("Functions"), NativeFunctions))
                {
                    Functions.Append(*NativeFunctions);
                }
            }
            const auto Callable = MatchCallable(Functions, Name, Arguments, false, true, nullptr);
            if (!Callable)
            {
                return false;
            }
            Includes.Add(Callable->GetStringField(TEXT("Include")));
            Modules.Add(Callable->GetStringField(TEXT("Module")));
            TArray<FString> Codes;
            for (const auto& Argument : Arguments)
            {
                Codes.Add(Argument.Code);
            }
            Value.Code += TEXT("(") + FString::Join(Codes, TEXT(", ")) + TEXT(")");
            Value.Type = Callable->GetStringField(TEXT("ReturnType"));
            Value.bIsLvalue = Value.Type.EndsWith(TEXT("&")) && !Value.Type.EndsWith(TEXT("&&"));
        }
        else
        {
            for (const auto& Parameter : Parameters)
            {
                if (Parameter->AsObject()->GetStringField(TEXT("Name")) == Name)
                {
                    Value.Type = Parameter->AsObject()->GetStringField(TEXT("Type"));
                    Value.bIsLvalue = true;
                    break;
                }
            }
            if (Value.Type.IsEmpty())
            {
                return false;
            }
        }
        while (Position < Tokens->Num() &&
            ((*Tokens)[Position]->AsString() == TEXT(".") || (*Tokens)[Position]->AsString() == TEXT("->")))
        {
            const FString Operator = (*Tokens)[Position++]->AsString();
            if (Position >= Tokens->Num())
            {
                return false;
            }
            FString ReceiverType = NormalizeForwardingType(Value.Type);
            ReceiverType.RemoveFromEnd(TEXT("&"));
            if (Operator == TEXT("->"))
            {
                if (ReceiverType.StartsWith(TEXT("TObjectPtr<")) && ReceiverType.EndsWith(TEXT(">")))
                {
                    ReceiverType = ReceiverType.Mid(11, ReceiverType.Len() - 12) + TEXT("*");
                }
                if (!ReceiverType.RemoveFromEnd(TEXT("*")))
                {
                    return false;
                }
            }
            else if (ReceiverType.EndsWith(TEXT("*")))
            {
                return false;
            }
            const bool bConstReceiver = ReceiverType.RemoveFromStart(TEXT("const"));
            const auto Owner = FindPublicForwardingType(ReceiverType, Includes, Modules);
            if (!Owner)
            {
                return false;
            }
            const FString MemberName = (*Tokens)[Position++]->AsString();
            Value.Code += Operator + MemberName;
            if (Position < Tokens->Num() && (*Tokens)[Position]->AsString() == TEXT("("))
            {
                TArray<FForwardingValue> Arguments;
                if (!ReadArguments(Arguments, Depth))
                {
                    return false;
                }
                const auto Callable = MatchCallable(Owner->GetArrayField(TEXT("Callables")), MemberName,
                    Arguments, bConstReceiver, false, Owner);
                if (!Callable)
                {
                    return false;
                }
                TArray<FString> Codes;
                for (const auto& Argument : Arguments)
                {
                    Codes.Add(Argument.Code);
                }
                Value.Code += TEXT("(") + FString::Join(Codes, TEXT(", ")) + TEXT(")");
                Value.Type = Callable->GetStringField(TEXT("ReturnType"));
                Value.bIsLvalue = Value.Type.EndsWith(TEXT("&")) && !Value.Type.EndsWith(TEXT("&&"));
            }
            else
            {
                TSharedPtr<FJsonObject> Selected;
                for (const auto& Member : Owner->GetArrayField(TEXT("Members")))
                {
                    const auto Candidate = Member->AsObject();
                    if (Candidate->GetStringField(TEXT("Name")) == MemberName)
                    {
                        if (Selected || Candidate->GetIntegerField(TEXT("Access")) != 1 ||
                            !Candidate->GetObjectField(TEXT("Evidence"))->GetStringField(TEXT("Condition")).IsEmpty())
                        {
                            return false;
                        }
                        Selected = Candidate;
                    }
                }
                if (!Selected)
                {
                    return false;
                }
                Value.Type = Selected->GetStringField(TEXT("Type"));
                Value.bIsLvalue = true;
                if (bConstReceiver)
                {
                    Value.Type = TEXT("const ") + Value.Type;
                }
            }
        }
        return true;
    };
    FForwardingValue Value;
    if (!ReadExpression(Value, 0) || Position != Tokens->Num() ||
        !AreForwardingTypesCompatible(Value.Type, Wrapper->GetStringField(TEXT("ReturnType"))))
    {
        return false;
    }
    Expression = Value.Code;
    return true;
}

static TSharedPtr<FJsonObject> FindForwardingReceiver(FString TypeName,
    bool& bConstReceiver, TSet<FString>& Includes, TSet<FString>& Modules, bool bAllowObjectWrapper)
{
    TypeName = NormalizeForwardingType(TypeName);
    if (bAllowObjectWrapper && TypeName.StartsWith(TEXT("TObjectPtr<")) && TypeName.EndsWith(TEXT(">")))
    {
        TypeName = TypeName.Mid(11, TypeName.Len() - 12) + TEXT("*");
    }
    if (!TypeName.RemoveFromEnd(TEXT("*")) || TypeName.Contains(TEXT("*")) || TypeName.Contains(TEXT("&")))
    {
        return nullptr;
    }
    bConstReceiver = TypeName.RemoveFromStart(TEXT("const"));
    return FindPublicForwardingType(TypeName, Includes, Modules);
}

bool FNativeApiCatalog::FindPublicForwarding(UFunction* Function, FString& Expression,
    TSet<FString>& Includes, TSet<FString>& Modules)
{
    Expression.Reset();
    Includes.Reset();
    Modules.Reset();
    if (!Function || !Function->HasAllFunctionFlags(FUNC_Native | FUNC_Static) ||
        Function->HasAnyFunctionFlags(FUNC_Event | FUNC_Net | FUNC_Delegate))
    {
        return false;
    }
    const auto Owner = FindType(Function->GetOwnerClass());
    if (!Owner || CanCallFunctionDirectly(Function))
    {
        return false;
    }
    TArray<FProperty*> ReflectedParameters;
    for (TFieldIterator<FProperty> Property(Function); Property; ++Property)
    {
        if (Property->HasAnyPropertyFlags(CPF_Parm) && !Property->HasAnyPropertyFlags(CPF_ReturnParm))
        {
            ReflectedParameters.Add(*Property);
        }
    }
    TSharedPtr<FJsonObject> Wrapper;
    for (const auto& Value : Owner->GetArrayField(TEXT("Callables")))
    {
        const auto Callable = Value->AsObject();
        if (Callable->GetStringField(TEXT("Name")) == Function->GetName() &&
            Callable->GetBoolField(TEXT("Static")) &&
            !Callable->GetBoolField(TEXT("Deleted")) && IsUnconditionalDeclaration(Callable) &&
            Callable->GetArrayField(TEXT("Parameters")).Num() == ReflectedParameters.Num())
        {
            if (Wrapper)
            {
                return false;
            }
            Wrapper = Callable;
        }
    }
    if (!Wrapper)
    {
        return false;
    }
    FindType(UObject::StaticClass());
    TArray<FString> ExpressionParameters;
    bool bExpressionParametersCompatible = true;
    const auto& WrapperParameters = Wrapper->GetArrayField(TEXT("Parameters"));
    for (int32 ParameterIndex = 0; ParameterIndex < WrapperParameters.Num(); ++ParameterIndex)
    {
        const auto Parameter = WrapperParameters[ParameterIndex]->AsObject();
        FProperty* Property = ReflectedParameters[ParameterIndex];
        FString ExtendedType;
        const FString ReflectedType = Property->GetCPPType(&ExtendedType) + ExtendedType;
        FString NativeType = NormalizeForwardingType(Parameter->GetStringField(TEXT("Type")));
        NativeType.RemoveFromStart(TEXT("const"));
        NativeType.RemoveFromEnd(TEXT("&"));
        bool bCompatible = AreForwardingTypesCompatible(NativeType, ReflectedType);
        if (const FInterfaceProperty* Interface = CastField<FInterfaceProperty>(Property))
        {
            bCompatible = NativeType == TEXT("FScriptInterface") && Interface->InterfaceClass == UInterface::StaticClass();
            if (bCompatible)
            {
                bCompatible = FindPublicForwardingType(NativeType, Includes, Modules).IsValid();
            }
        }
        else if (const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(Property))
        {
            bCompatible &= FindPublicForwardingType(FString(Object->PropertyClass->GetPrefixCPP()) + Object->PropertyClass->GetName(), Includes, Modules).IsValid();
        }
        else if (const FStructProperty* Structure = CastField<FStructProperty>(Property))
        {
            bCompatible &= FindPublicForwardingType(Structure->Struct->GetStructCPPName(), Includes, Modules).IsValid();
        }
        bExpressionParametersCompatible &= bCompatible && !NativeType.Contains(TEXT("&&"));
        ExpressionParameters.Add(Parameter->GetStringField(TEXT("Type")) + TEXT(" ") + Parameter->GetStringField(TEXT("Name")));
    }
    FString BodyExpression;
    FProperty* ExpressionReturn = Function->GetReturnProperty();
    FString ReflectedReturn = TEXT("void");
    if (ExpressionReturn)
    {
        FString ExtendedType;
        ReflectedReturn = ExpressionReturn->GetCPPType(&ExtendedType) + ExtendedType;
        if (const FStructProperty* Structure = CastField<FStructProperty>(ExpressionReturn))
        {
            bExpressionParametersCompatible &= FindPublicForwardingType(Structure->Struct->GetStructCPPName(), Includes, Modules).IsValid();
        }
        else if (const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(ExpressionReturn))
        {
            bExpressionParametersCompatible &= FindPublicForwardingType(FString(Object->PropertyClass->GetPrefixCPP()) + Object->PropertyClass->GetName(), Includes, Modules).IsValid();
        }
    }
    if (bExpressionParametersCompatible && AreForwardingTypesCompatible(Wrapper->GetStringField(TEXT("ReturnType")), ReflectedReturn) &&
        ResolveForwardingExpression(Wrapper, BodyExpression, Includes, Modules))
    {
        FString Statement = BodyExpression + TEXT(";\n");
        if (ExpressionReturn)
        {
            Statement = TEXT("return ") + Statement;
        }
        Expression = TEXT("[](") + FString::Join(ExpressionParameters, TEXT(", ")) + TEXT(") -> ") +
            Wrapper->GetStringField(TEXT("ReturnType")) + TEXT("\n{\n") + Statement + TEXT("}");
        Modules.Remove(TEXT(""));
        return true;
    }
    const TSharedPtr<FJsonObject>* Route = nullptr;
    if (!Wrapper->TryGetObjectField(TEXT("PublicForwarding"), Route))
    {
        return false;
    }
    const auto& Parameters = Wrapper->GetArrayField(TEXT("Parameters"));
    const int32 RootParameter = (*Route)->GetIntegerField(TEXT("RootParameter"));
    if (!Parameters.IsValidIndex(RootParameter) || (*Route)->GetStringField(TEXT("Method")).IsEmpty())
    {
        return false;
    }
    TArray<FString> ParameterDeclarations;
    for (int32 ParameterIndex = 0; ParameterIndex < Parameters.Num(); ++ParameterIndex)
    {
        const auto Parameter = Parameters[ParameterIndex]->AsObject();
        FProperty* Property = ReflectedParameters[ParameterIndex];
        FString ExtendedType;
        FString ReflectedType = Property->GetCPPType(&ExtendedType) + ExtendedType;
        const FString NativeType = Parameter->GetStringField(TEXT("Type"));
        FString ValueType = NormalizeForwardingType(NativeType);
        ValueType.RemoveFromStart(TEXT("const"));
        ValueType.RemoveFromEnd(TEXT("&"));
        if (ValueType != NormalizeForwardingType(ReflectedType) || NativeType.Contains(TEXT("&&")))
        {
            return false;
        }
        if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
        {
            if (!FindPublicForwardingType(FString(ObjectProperty->PropertyClass->GetPrefixCPP()) + ObjectProperty->PropertyClass->GetName(), Includes, Modules))
            {
                return false;
            }
        }
        else if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
        {
            if (!FindPublicForwardingType(StructProperty->Struct->GetStructCPPName(), Includes, Modules))
            {
                return false;
            }
        }
        else if (Property->IsA<FEnumProperty>() || Property->IsA<FByteProperty>() || Property->IsA<FArrayProperty>() ||
            Property->IsA<FSetProperty>() || Property->IsA<FMapProperty>() || Property->IsA<FInterfaceProperty>())
        {
            return false;
        }
        ParameterDeclarations.Add(NativeType + TEXT(" ") + Parameter->GetStringField(TEXT("Name")));
    }
    bool bConstReceiver = false;
    auto ReceiverType = FindForwardingReceiver(Parameters[RootParameter]->AsObject()->GetStringField(TEXT("Type")),
        bConstReceiver, Includes, Modules, false);
    FString Receiver = Parameters[RootParameter]->AsObject()->GetStringField(TEXT("Name"));
    for (const auto& MemberValue : (*Route)->GetArrayField(TEXT("Members")))
    {
        if (!ReceiverType)
        {
            return false;
        }
        TSharedPtr<FJsonObject> SelectedMember;
        for (const auto& Value : ReceiverType->GetArrayField(TEXT("Members")))
        {
            const auto Member = Value->AsObject();
            if (Member->GetStringField(TEXT("Name")) == MemberValue->AsString())
            {
                if (SelectedMember || Member->GetIntegerField(TEXT("Access")) != 1 ||
                    !Member->GetObjectField(TEXT("Evidence"))->GetStringField(TEXT("Condition")).IsEmpty())
                {
                    return false;
                }
                SelectedMember = Member;
            }
        }
        if (!SelectedMember)
        {
            return false;
        }
        Receiver += TEXT("->") + SelectedMember->GetStringField(TEXT("Name"));
        ReceiverType = FindForwardingReceiver(SelectedMember->GetStringField(TEXT("Type")),
            bConstReceiver, Includes, Modules, true);
    }
    if (!ReceiverType)
    {
        return false;
    }
    TArray<FString> Arguments;
    TArray<FString> ArgumentTypes;
    for (const auto& Value : (*Route)->GetArrayField(TEXT("Arguments")))
    {
        const int32 ParameterIndex = static_cast<int32>(Value->AsNumber());
        if (!Parameters.IsValidIndex(ParameterIndex))
        {
            return false;
        }
        Arguments.Add(Parameters[ParameterIndex]->AsObject()->GetStringField(TEXT("Name")));
        ArgumentTypes.Add(NormalizeForwardingType(Parameters[ParameterIndex]->AsObject()->GetStringField(TEXT("Type"))));
    }
    TSharedPtr<FJsonObject> SelectedMethod;
    for (const auto& Value : ReceiverType->GetArrayField(TEXT("Callables")))
    {
        const auto Method = Value->AsObject();
        if (Method->GetStringField(TEXT("Name")) != (*Route)->GetStringField(TEXT("Method")) ||
            Method->GetIntegerField(TEXT("Access")) != 1 || Method->GetBoolField(TEXT("Static")) ||
            Method->GetBoolField(TEXT("Deleted")) || !IsUnconditionalDeclaration(Method) ||
            (bConstReceiver && !Method->GetBoolField(TEXT("Const"))) ||
            (!ReceiverType->GetBoolField(TEXT("Exported")) && !Method->GetBoolField(TEXT("Exported")) &&
                !Method->GetBoolField(TEXT("InlineDefinition"))))
        {
            continue;
        }
        const auto& MethodParameters = Method->GetArrayField(TEXT("Parameters"));
        bool bCompatible = MethodParameters.Num() == ArgumentTypes.Num();
        for (int32 ParameterIndex = 0; bCompatible && ParameterIndex < MethodParameters.Num(); ++ParameterIndex)
        {
            bCompatible = NormalizeForwardingType(MethodParameters[ParameterIndex]->AsObject()->GetStringField(TEXT("Type"))) == ArgumentTypes[ParameterIndex];
        }
        FString MethodReturn = NormalizeForwardingType(Method->GetStringField(TEXT("ReturnType")));
        FString WrapperReturn = NormalizeForwardingType(Wrapper->GetStringField(TEXT("ReturnType")));
        if (!WrapperReturn.Contains(TEXT("&")))
        {
            MethodReturn.RemoveFromStart(TEXT("const"));
            MethodReturn.RemoveFromEnd(TEXT("&"));
        }
        if (bCompatible && MethodReturn == WrapperReturn)
        {
            if (SelectedMethod)
            {
                return false;
            }
            SelectedMethod = Method;
        }
    }
    if (!SelectedMethod)
    {
        return false;
    }
    FProperty* ReturnProperty = Function->GetReturnProperty();
    if (FStructProperty* StructProperty = CastField<FStructProperty>(ReturnProperty))
    {
        if (!FindPublicForwardingType(StructProperty->Struct->GetStructCPPName(), Includes, Modules))
        {
            return false;
        }
    }
    else if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(ReturnProperty))
    {
        if (!FindPublicForwardingType(FString(ObjectProperty->PropertyClass->GetPrefixCPP()) + ObjectProperty->PropertyClass->GetName(), Includes, Modules))
        {
            return false;
        }
    }
    else if (ReturnProperty && (ReturnProperty->IsA<FEnumProperty>() || ReturnProperty->IsA<FByteProperty>() ||
        ReturnProperty->IsA<FArrayProperty>() || ReturnProperty->IsA<FSetProperty>() || ReturnProperty->IsA<FMapProperty>() || ReturnProperty->IsA<FInterfaceProperty>()))
    {
        return false;
    }
    const FString ReturnType = Wrapper->GetStringField(TEXT("ReturnType"));
    FString Statement = Receiver + TEXT("->") + SelectedMethod->GetStringField(TEXT("Name")) + TEXT("(") + FString::Join(Arguments, TEXT(", ")) + TEXT(");\n");
    if (ReturnType != TEXT("void"))
    {
        Statement = TEXT("return ") + Statement;
    }
    Expression = TEXT("[](") + FString::Join(ParameterDeclarations, TEXT(", ")) + TEXT(") -> ") + ReturnType + TEXT("\n{\n") + Statement + TEXT("}");
    Modules.Remove(TEXT(""));
    return true;
}
