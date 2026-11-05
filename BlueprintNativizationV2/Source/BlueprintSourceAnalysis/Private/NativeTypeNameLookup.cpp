/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#include "NativeModuleCache.h"
#include "BlueprintNativeApiCacheLibrary.h"
#include "CppSourceParser.h"
#include "HAL/FileManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool FNativeModuleCache::FindNativeTypeNameConflicts(const TSet<FString>& CandidateNames,
    const FString& ExcludedModule, TMap<FString, FString>& Conflicts, FString& Report)
{
    Conflicts.Reset();
    if (CandidateNames.IsEmpty())
    {
        return true;
    }
    auto IsRequestedName = [&CandidateNames](const FString& Name)
    {
        if (CandidateNames.Contains(Name))
        {
            return true;
        }
        int32 PrefixLength = Name.Len();
        while (PrefixLength > 0 && FChar::IsDigit(Name[PrefixLength - 1]))
        {
            --PrefixLength;
            if (CandidateNames.Contains(Name.Left(PrefixLength)))
            {
                return true;
            }
        }
        return false;
    };
    FString TargetFile;
    if (!ExportTarget(TargetFile, Report))
    {
        Report = TEXT("Cannot resolve native name lookup target: ") + Report;
        return false;
    }
    FString TargetText;
    TSharedPtr<FJsonObject> Target;
    const TSharedPtr<FJsonObject>* Modules = nullptr;
    if (!FFileHelper::LoadFileToString(TargetText, *TargetFile) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TargetText), Target) ||
        !Target->TryGetObjectField(TEXT("Modules"), Modules))
    {
        Report = TEXT("Cannot read native name lookup target modules.");
        return false;
    }
    FString ExcludedDirectory;
    const TSharedPtr<FJsonObject>* ExcludedEntry = nullptr;
    if ((*Modules)->TryGetObjectField(ExcludedModule, ExcludedEntry))
    {
        ExcludedDirectory = FPaths::ConvertRelativePathToFull((*ExcludedEntry)->GetStringField(TEXT("Directory")));
    }
    for (const bool bEngine : {false, true})
    {
        TSharedPtr<FJsonObject> Index;
        FString IndexReport;
        const TSharedPtr<FJsonObject>* Owners = nullptr;
        if (!ReadIndex(GetIndexFile(bEngine), Index, IndexReport, false) ||
            !Index->TryGetObjectField(TEXT("TypeOwners"), Owners))
        {
            continue;
        }
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Owners)->Values)
        {
            const FString Owner = Entry.Value->AsString();
            if (IsRequestedName(Entry.Key) && !Owner.IsEmpty() &&
                Owner != ExcludedModule && (*Modules)->HasField(Owner))
            {
                Conflicts.Add(Entry.Key, FString::Format(TEXT("native declaration indexed in module {0}"), {Owner}));
            }
        }
    }
    TMap<FString, FString> SourceModules;
    TMap<FString, FString> SourceRoots;
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Modules)->Values)
    {
        if (Entry.Key == ExcludedModule)
        {
            continue;
        }
        const TSharedPtr<FJsonObject> Module = Entry.Value->AsObject();
        const bool bExternalModule = Module->GetStringField(TEXT("Type")) == TEXT("External");
        FString ModuleDirectory;
        if (!bExternalModule && Module->TryGetStringField(TEXT("Directory"), ModuleDirectory))
        {
            SourceRoots.Add(FPaths::ConvertRelativePathToFull(ModuleDirectory), Entry.Key);
        }
        if (!bExternalModule)
        {
            continue;
        }
        for (const FString& IncludeKey : {TEXT("PublicSystemIncludePaths"), TEXT("PublicIncludePaths"),
                                         TEXT("PrivateIncludePaths"), TEXT("InternalIncludePaths")})
        {
            const TArray<TSharedPtr<FJsonValue>>* Includes = nullptr;
            if (!Module->TryGetArrayField(IncludeKey, Includes))
            {
                continue;
            }
            for (const TSharedPtr<FJsonValue>& Include : *Includes)
            {
                SourceRoots.Add(FPaths::ConvertRelativePathToFull(Include->AsString()), Entry.Key);
            }
        }
    }
    for (const TPair<FString, FString>& Root : SourceRoots)
    {
        if (!IFileManager::Get().DirectoryExists(*Root.Key))
        {
            continue;
        }
        TArray<FString> Sources;
        for (const FString& Pattern : {TEXT("*.h"), TEXT("*.hpp"), TEXT("*.hxx"), TEXT("*.inl")})
        {
            IFileManager::Get().FindFilesRecursive(Sources, *Root.Key, *Pattern, true, false, false);
        }
        for (FString& Source : Sources)
        {
            Source = FPaths::ConvertRelativePathToFull(Source);
            FPaths::NormalizeFilename(Source);
            if (Source.Contains(TEXT("/Intermediate/")) || Source.EndsWith(TEXT(".generated.h")) ||
                Source.EndsWith(TEXT(".gen.cpp")) ||
                (!ExcludedDirectory.IsEmpty() && FPaths::IsUnderDirectory(Source, ExcludedDirectory)))
            {
                continue;
            }
            SourceModules.FindOrAdd(Source, Root.Value);
        }
    }
    const FString CacheFile = FPaths::Combine(GetConfigurationDirectory(), TEXT("NativeTypeNames"), ExcludedModule + TEXT(".json"));
    FString CacheText;
    TSharedPtr<FJsonObject> PreviousCache;
    const TSharedPtr<FJsonObject>* PreviousSources = nullptr;
    TSet<FString> PreviousCandidates;
    double PreviousVersion = 0;
    FString PreviousIdentity;
    if (FFileHelper::LoadFileToString(CacheText, *CacheFile) &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(CacheText), PreviousCache) &&
        PreviousCache->TryGetNumberField(TEXT("Version"), PreviousVersion) && PreviousVersion == 1 &&
        PreviousCache->TryGetStringField(TEXT("Identity"), PreviousIdentity) && PreviousIdentity == GetIdentity() &&
        PreviousCache->TryGetObjectField(TEXT("Sources"), PreviousSources))
    {
        const TArray<TSharedPtr<FJsonValue>>* CheckedNames = nullptr;
        if (PreviousCache->TryGetArrayField(TEXT("Candidates"), CheckedNames))
        {
            for (const TSharedPtr<FJsonValue>& Name : *CheckedNames)
            {
                PreviousCandidates.Add(Name->AsString());
            }
        }
    }
    bool bCandidatesAlreadyChecked = true;
    TSet<TCHAR> CandidateFirstCharacters;
    for (const FString& Candidate : CandidateNames)
    {
        if (!Candidate.IsEmpty())
        {
            CandidateFirstCharacters.Add(Candidate[0]);
        }
        if (!PreviousCandidates.Contains(Candidate))
        {
            bCandidatesAlreadyChecked = false;
        }
    }
    auto ContainsCandidate = [&IsRequestedName, &CandidateFirstCharacters](const FString& Text)
    {
        int32 Position = 0;
        while (Position < Text.Len())
        {
            if (!FChar::IsAlpha(Text[Position]) && Text[Position] != TEXT('_'))
            {
                ++Position;
                continue;
            }
            const int32 Begin = Position++;
            while (Position < Text.Len() && (FChar::IsAlnum(Text[Position]) || Text[Position] == TEXT('_')))
            {
                ++Position;
            }
            if (CandidateFirstCharacters.Contains(Text[Begin]) && IsRequestedName(Text.Mid(Begin, Position - Begin)))
            {
                return true;
            }
        }
        return false;
    };
    TArray<FString> SourceFiles;
    SourceModules.GetKeys(SourceFiles);
    SourceFiles.Sort();
    UKismetSystemLibrary::PrintString(nullptr, FString::Format(
        TEXT("Checking {0} generated type names against {1} native source files."),
        {CandidateNames.Num(), SourceFiles.Num()}), false, true);
    TSharedPtr<FJsonObject> PublishedSources = MakeShared<FJsonObject>();
    int32 ParsedSources = 0;
    int32 ReusedSources = 0;
    for (const FString& SourceFile : SourceFiles)
    {
        FString SourceText;
        if (!FFileHelper::LoadFileToString(SourceText, *SourceFile))
        {
            Report = TEXT("Cannot read source for native name lookup: ") + SourceFile;
            return false;
        }
        const FTCHARToUTF8 SourceBytes(*SourceText);
        const FString Fingerprint = FMD5::HashBytes(
            reinterpret_cast<const uint8*>(SourceBytes.Get()), SourceBytes.Length());
        TSharedPtr<FJsonObject> SourceEntry;
        const TSharedPtr<FJsonObject>* PreviousEntry = nullptr;
        FString PreviousFingerprint;
        bool bPreviouslyParsed = false;
        if (PreviousSources && (*PreviousSources)->TryGetObjectField(SourceFile, PreviousEntry) &&
            (*PreviousEntry)->TryGetStringField(TEXT("Fingerprint"), PreviousFingerprint) && PreviousFingerprint == Fingerprint &&
            (*PreviousEntry)->TryGetBoolField(TEXT("Parsed"), bPreviouslyParsed) &&
            (bPreviouslyParsed || bCandidatesAlreadyChecked))
        {
            SourceEntry = *PreviousEntry;
            ++ReusedSources;
        }
        else
        {
            SourceEntry = MakeShared<FJsonObject>();
            SourceEntry->SetStringField(TEXT("Fingerprint"), Fingerprint);
            TArray<TSharedPtr<FJsonValue>> Symbols;
            const bool bParseSource = ContainsCandidate(SourceText);
            SourceEntry->SetBoolField(TEXT("Parsed"), bParseSource);
            if (bParseSource)
            {
                FCppParseRequest Request;
                Request.SourceFile = TCHAR_TO_UTF8(*SourceFile);
                Request.bCaptureTokens = false;
                Request.bFollowIncludes = false;
                Request.UnsavedFiles.push_back({Request.SourceFile, std::string(SourceBytes.Get(), SourceBytes.Length())});
                for (const FString& Argument : UBlueprintNativeApiCacheLibrary::GetSourceArguments())
                {
                    Request.Arguments.push_back(TCHAR_TO_UTF8(*Argument));
                }
                const FCppSourceDocument Document = FCppSourceParser::Parse(Request);
                if (!Document.bSucceeded)
                {
                    Report = TEXT("Native name lookup could not parse source declarations: ") + SourceFile;
                    return false;
                }
                TSet<FString> AddedNames;
                for (const FCppSyntaxNode& Node : Document.Nodes)
                {
                    bool bGlobalDeclaration = Node.SemanticScope == "global" &&
                        (Node.Kind == "EnumDecl" || Node.Kind == "ClassDecl" || Node.Kind == "StructDecl" ||
                            Node.Kind == "ClassTemplate" || Node.Kind == "TypeAliasDecl" || Node.Kind == "VarDecl" ||
                            Node.Kind == "FunctionDecl" || Node.Kind == "MacroDefinition");
                    if (Node.Kind == "EnumConstantDecl" && Node.Parent >= 0)
                    {
                        const FCppSyntaxNode& Enumeration = Document.Nodes[Node.Parent];
                        bGlobalDeclaration = Enumeration.Kind == "EnumDecl" &&
                            Enumeration.SemanticScope == "global" && !Enumeration.bScopedEnum;
                    }
                    const FString Name = UTF8_TO_TCHAR(Node.Name.c_str());
                    if (!bGlobalDeclaration || Name.IsEmpty() || AddedNames.Contains(Name))
                    {
                        continue;
                    }
                    AddedNames.Add(Name);
                    TSharedPtr<FJsonObject> Symbol = MakeShared<FJsonObject>();
                    Symbol->SetStringField(TEXT("Name"), Name);
                    Symbol->SetStringField(TEXT("Kind"), UTF8_TO_TCHAR(Node.Kind.c_str()));
                    Symbol->SetStringField(TEXT("Condition"), UTF8_TO_TCHAR(Node.ConditionalExpression.c_str()));
                    Symbol->SetNumberField(TEXT("Line"), Node.Range.Begin.Line);
                    Symbols.Add(MakeShared<FJsonValueObject>(Symbol));
                }
                ++ParsedSources;
            }
            SourceEntry->SetArrayField(TEXT("Symbols"), Symbols);
        }
        PublishedSources->SetObjectField(SourceFile, SourceEntry);
        for (const TSharedPtr<FJsonValue>& Value : SourceEntry->GetArrayField(TEXT("Symbols")))
        {
            const TSharedPtr<FJsonObject> Symbol = Value->AsObject();
            const FString Name = Symbol->GetStringField(TEXT("Name"));
            if (IsRequestedName(Name))
            {
                FString Evidence = FString::Format(TEXT("{0}:{1} ({2}, module {3})"),
                    {SourceFile, Symbol->GetIntegerField(TEXT("Line")), Symbol->GetStringField(TEXT("Kind")), SourceModules.FindChecked(SourceFile)});
                FString Condition;
                if (Symbol->TryGetStringField(TEXT("Condition"), Condition) && !Condition.IsEmpty())
                {
                    Evidence += TEXT("; condition: ") + Condition;
                }
                Conflicts.Add(Name, Evidence);
            }
        }
    }
    TSharedPtr<FJsonObject> PublishedCache = MakeShared<FJsonObject>();
    PublishedCache->SetNumberField(TEXT("Version"), 1);
    PublishedCache->SetStringField(TEXT("Identity"), GetIdentity());
    PublishedCache->SetObjectField(TEXT("Sources"), PublishedSources);
    TArray<FString> CheckedCandidates = CandidateNames.Array();
    CheckedCandidates.Sort();
    TArray<TSharedPtr<FJsonValue>> CheckedNames;
    for (const FString& Name : CheckedCandidates)
    {
        CheckedNames.Add(MakeShared<FJsonValueString>(Name));
    }
    PublishedCache->SetArrayField(TEXT("Candidates"), CheckedNames);
    FString PublishedText;
    FJsonSerializer::Serialize(PublishedCache.ToSharedRef(), TJsonWriterFactory<>::Create(&PublishedText));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(CacheFile), true);
    const FString StagingFile = CacheFile + TEXT(".") + FGuid::NewGuid().ToString() + TEXT(".tmp");
    if (!FFileHelper::SaveStringToFile(PublishedText, *StagingFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ||
        !IFileManager::Get().Move(*CacheFile, *StagingFile, true, true))
    {
        Report = TEXT("Cannot publish native name lookup cache: ") + CacheFile;
        return false;
    }
    Report = FString::Format(TEXT("Native name lookup: {0} sources checked; {1} parsed; {2} reused; {3} conflicting names."),
        {SourceFiles.Num(), ParsedSources, ReusedSources, Conflicts.Num()});
    UKismetSystemLibrary::PrintString(nullptr, Report, false, true);
    return true;
}
