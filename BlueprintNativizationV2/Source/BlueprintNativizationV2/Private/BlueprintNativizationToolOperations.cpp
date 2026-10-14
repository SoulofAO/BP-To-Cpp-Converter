#include "EngineAssetPolicy.h"
#include "BlueprintNativizationSubsystem.h"

#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSettings.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool UNativizationV2Subsystem::TryPrepareNewModule(FString& Error)
{
    const FString ModuleName = UBlueprintNativizationSettingsLibrary::GetModuleName();
    if (ModuleName.IsEmpty() || !FChar::IsAlpha(ModuleName[0]))
    {
        Error = TEXT("The module name must begin with a letter.");
        return false;
    }
    for (TCHAR Character : ModuleName)
    {
        if (!FChar::IsAlnum(Character) && Character != TEXT('_'))
        {
            Error = TEXT("The module name must contain only letters, numbers and underscores.");
            return false;
        }
    }
    const FString Directory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), ModuleName);
    FNativizationCode Module = GenerateUnrealModule(ModuleName, {});
    TMap<FString, FString> ModuleFiles;
    ModuleFiles.Add(ModuleName + TEXT(".h"), Module.OutputHeaderString);
    ModuleFiles.Add(ModuleName + TEXT(".cpp"), Module.OutputCppString);
    ModuleFiles.Add(ModuleName + TEXT(".Build.cs"), GenerateUnrealBuildCsModule(ModuleName, {}));
    for (const TPair<FString, FString>& ModuleFile : ModuleFiles)
    {
        const FString Filename = FPaths::Combine(Directory, ModuleFile.Key);
        if (!FPaths::FileExists(Filename) && !CreateFile(Filename, ModuleFile.Value))
        {
            Error = FString::Format(TEXT("Could not write {0}."), { Filename });
            return false;
        }
    }
    AddModuleToUProject(ModuleName);
    AddModuleToTargets(ModuleName);
    FString ProjectText;
    TSharedPtr<FJsonObject> Project;
    if (!FFileHelper::LoadFileToString(ProjectText, *FPaths::GetProjectFilePath()) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ProjectText), Project))
    {
        Error = TEXT("Could not verify the project descriptor after module registration.");
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>* Modules = nullptr;
    bool bRegistered = false;
    if (Project->TryGetArrayField(TEXT("Modules"), Modules))
    {
        for (const TSharedPtr<FJsonValue>& Entry : *Modules)
        {
            if (Entry->AsObject()->GetStringField(TEXT("Name")) == ModuleName)
            {
                bRegistered = true;
            }
        }
    }
    TArray<FString> TargetFiles;
    IFileManager::Get().FindFilesRecursive(TargetFiles, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Source")), TEXT("*.Target.cs"), true, false);
    for (const FString& TargetFile : TargetFiles)
    {
        FString TargetText;
        if (!FFileHelper::LoadFileToString(TargetText, *TargetFile) || !TargetText.Contains(TEXT("\"") + ModuleName + TEXT("\"")))
        {
            Error = FString::Format(TEXT("Module registration is incomplete in {0}."), { TargetFile });
            return false;
        }
    }
    if (!bRegistered || TargetFiles.IsEmpty())
    {
        Error = TEXT("Module registration is incomplete. Check the project descriptor and targets.");
        return false;
    }
    return true;
}

bool UNativizationV2Subsystem::TryReapplyJSONCache(FString CacheFilePath, FString& Error, bool bPreviewOnly, FString* Summary, bool bContinueOnError)
{
    if (bIsNativizing)
    {
        Error = TEXT("Another conversion is running.");
        return false;
    }
    if (CacheFilePath.IsEmpty())
    {
        CacheFilePath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"),
            UBlueprintNativizationSettingsLibrary::GetModuleName(), TEXT("BlueprintNativizationCache.bpncache"));
    }
    FString Content;
    TSharedPtr<FJsonObject> Cache;
    if (!FFileHelper::LoadFileToString(Content, *CacheFilePath) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Content), Cache) || !Cache.IsValid())
    {
        Error = TEXT("The migration cache could not be read as JSON.");
        return false;
    }
    FString Format;
    int32 Version = 0;
    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    if (!Cache->TryGetStringField(TEXT("_format"), Format) || Format != TEXT("BlueprintNativizationCache") ||
        !Cache->TryGetNumberField(TEXT("version"), Version) || Version != 3 ||
        !Cache->TryGetArrayField(TEXT("items"), Items) || Items->IsEmpty())
    {
        Error = TEXT("The migration cache is incompatible with serialized Blueprint defaults. Regenerate and rebuild the exported module before migration.");
        return false;
    }
    TMap<UField*, UField*> Mappings;
    TMap<UClass*, UClass*> DefaultOnlyClassReplacements;
    TMap<FString, FString> MemberNames;
    bool bKeepAssetReferencesInBlueprint = false;
    FString DefaultsContract;
    if (!Cache->TryGetStringField(TEXT("DefaultsContract"), DefaultsContract) ||
        DefaultsContract != TEXT("SerializedBlueprintOverrides") ||
        !Cache->TryGetBoolField(TEXT("KeepAssetReferences"), bKeepAssetReferencesInBlueprint) ||
        !bKeepAssetReferencesInBlueprint)
    {
        Error = TEXT("Only serialized Blueprint overrides are supported. Regenerate the migration cache with Blueprint asset references enabled.");
        return false;
    }
    FString ModuleName;
    if (Version == 3)
    {
        const TSharedPtr<FJsonObject>* Members = nullptr;
        if (!Cache->TryGetStringField(TEXT("ModuleName"), ModuleName) || ModuleName.IsEmpty() ||
            !Cache->TryGetObjectField(TEXT("Members"), Members))
        {
            Error = TEXT("The migration cache is missing its module or member name snapshot.");
            return false;
        }
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Member : (*Members)->Values)
        {
            FString Name;
            if (!Member.Value->TryGetString(Name) || Name.IsEmpty())
            {
                Error = TEXT("The migration cache contains an invalid member name.");
                return false;
            }
            MemberNames.Add(Member.Key, Name);
        }
    }
    FString MigrationSummary = TEXT("Direct type mappings (dependent Blueprints may also be updated):\n\n");
    int32 UnresolvedMappings = 0;
    TArray<TObjectPtr<UBlueprint>> Blueprints;
    int32 ResolvedItems = 0;
    for (const TSharedPtr<FJsonValue>& Item : *Items)
    {
        if (OperationProgress)
        {
            OperationProgress(ResolvedItems, Items->Num(), TEXT("Resolving migration types"));
        }
        ++ResolvedItems;
        const TSharedPtr<FJsonObject>* Entry = nullptr;
        FString OriginalName;
        FString GeneratedName;
        if (!Item->TryGetObject(Entry) || !(*Entry)->TryGetStringField(TEXT("OriginalField"), OriginalName) ||
            !(*Entry)->TryGetStringField(TEXT("NewFieldName"), GeneratedName))
        {
            Error = TEXT("A migration mapping is missing its original or generated symbol.");
            return false;
        }
        UField* Original = UBlueprintNativizationLibrary::FindFieldByStableIdentifier(OriginalName);
        FString GeneratedPath = GeneratedName;
        if (Version == 3)
        {
            if (!(*Entry)->TryGetStringField(TEXT("NewFieldPath"), GeneratedPath) ||
                !GeneratedPath.StartsWith(TEXT("/Script/") + ModuleName + TEXT(".")))
            {
                Error = TEXT("A migration mapping points outside its exported module.");
                return false;
            }
        }
        if (Version == 3 && Original && Original->IsA<UEnum>() &&
            GeneratedPath == TEXT("/Script/") + ModuleName + TEXT(".") + GeneratedName.RightChop(1))
        {
            GeneratedPath = TEXT("/Script/") + ModuleName + TEXT(".") + GeneratedName;
        }
        if (Version == 3)
        {
            if (UClass* OriginalClass = Cast<UClass>(Original))
            {
                const FString ModulePath = TEXT("/Script/") + ModuleName + TEXT(".");
                if (GeneratedPath == ModulePath + GeneratedName.RightChop(FCString::Strlen(OriginalClass->GetPrefixCPP())))
                {
                    FString ReflectionName = GeneratedName;
                    if (!OriginalClass->HasAnyClassFlags(CLASS_Interface))
                    {
                        ReflectionName.RightChopInline(1);
                    }
                    GeneratedPath = ModulePath + ReflectionName;
                }
            }
        }
        UField* Generated = FindFieldByNameViaCDO(FName(*GeneratedPath));
        if (!Original || !Generated)
        {
            ++UnresolvedMappings;
            MigrationSummary += FString::Format(TEXT("UNRESOLVED: {0} -> {1}\n"), { OriginalName, GeneratedName });
            continue;
        }
        if (Original->IsA<UClass>() != Generated->IsA<UClass>() ||
            Original->IsA<UScriptStruct>() != Generated->IsA<UScriptStruct>() ||
            Original->IsA<UEnum>() != Generated->IsA<UEnum>())
        {
            Error = TEXT("A migration mapping has incompatible original and generated type categories.");
            return false;
        }
        const FString ExclusionReason = FEngineAssetPolicy::GetExclusionReason(Original, true);
        if (!ExclusionReason.IsEmpty())
        {
            Error = ExclusionReason;
            return false;
        }
        Mappings.Add(Original, Generated);
        MigrationSummary += FString::Format(TEXT("{0} -> {1}\n"), { OriginalName, GeneratedName });
        if (UClass* OriginalClass = Cast<UClass>(Original))
        {
            if (UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(OriginalClass))
            {
                Blueprints.Add(Blueprint);
            }
        }
    }
    if (Cache->HasField(TEXT("DefaultOnlyClassReplacements")))
    {
        const TSharedPtr<FJsonObject>* Replacements = nullptr;
        if (!Cache->TryGetObjectField(TEXT("DefaultOnlyClassReplacements"), Replacements))
        {
            Error = TEXT("The migration cache contains an invalid default-only class replacement snapshot.");
            return false;
        }
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Replacement : (*Replacements)->Values)
        {
            FString DestinationPath;
            if (Replacement.Key.IsEmpty() || !Replacement.Value->TryGetString(DestinationPath) ||
                !DestinationPath.StartsWith(TEXT("/Script/")))
            {
                Error = TEXT("The migration cache contains an invalid default-only class replacement: ") + Replacement.Key;
                return false;
            }
            UClass* OriginalClass = Cast<UClass>(UBlueprintNativizationLibrary::FindFieldByStableIdentifier(Replacement.Key));
            UClass* DestinationClass = Cast<UClass>(FindFieldByNameViaCDO(FName(*DestinationPath)));
            if (!OriginalClass || !DestinationClass)
            {
                Error = FString::Format(TEXT("Unresolved default-only class replacement: {0} -> {1}. Rebuild and restart the editor before applying. No assets were migrated."),
                    {Replacement.Key, DestinationPath});
                return false;
            }
            if (OriginalClass->HasAnyClassFlags(CLASS_Native) || !UBlueprint::GetBlueprintFromClass(OriginalClass) ||
                !DestinationClass->HasAnyClassFlags(CLASS_Native) || Mappings.Contains(OriginalClass))
            {
                Error = FString::Format(TEXT("Invalid default-only class replacement: {0} -> {1}. The source must be an omitted Blueprint class and the destination must be native."),
                    {Replacement.Key, DestinationPath});
                return false;
            }
            DefaultOnlyClassReplacements.Add(OriginalClass, DestinationClass);
            MigrationSummary += FString::Format(TEXT("Default-only class replacement: {0} -> {1}\n"), {Replacement.Key, DestinationPath});
        }
    }
    TSharedPtr<FJsonObject> ConfigurationTemplates;
    if (Cache->HasField(TEXT("ConfigurationTemplates")))
    {
        const TSharedPtr<FJsonObject>* Configuration = nullptr;
        if (!Cache->TryGetObjectField(TEXT("ConfigurationTemplates"), Configuration))
        {
            Error = TEXT("Invalid configuration templates in migration cache.");
            return false;
        }
        ConfigurationTemplates = *Configuration;
    }
    MigrationSummary = FString::Format(TEXT("Mappings: {0} | Resolved: {1} | Unresolved: {2}\nDirect Blueprint classes: {3}\nDefault-only class replacements: {4}\n\n"), { Items->Num(), Items->Num() - UnresolvedMappings, UnresolvedMappings, Blueprints.Num(), DefaultOnlyClassReplacements.Num() }) + MigrationSummary;
    if (UnresolvedMappings > 0)
    {
        Error = MigrationSummary + TEXT("\nRebuild and restart the editor before applying. No assets were migrated.");
        return false;
    }
    if (Summary)
    {
        *Summary = MigrationSummary + TEXT("\nBlueprints will be cleared and reparented. Defaults, components, table rows and dependent references will be transferred. Original value type assets are retained. This is not a behavioral validation.");
    }
    if (bPreviewOnly)
    {
        const bool bCanApply = ApplyMigrationTypes(Mappings, MemberNames, Error, true, DefaultOnlyClassReplacements, bContinueOnError, bKeepAssetReferencesInBlueprint, ConfigurationTemplates);
        if (bCanApply && Summary && !Error.IsEmpty())
        {
            *Summary += TEXT("\n\n") + Error;
        }
        return bCanApply;
    }
    TGuardValue<bool> GenerationGuard(bIsNativizing, true);
    if (OperationProgress)
    {
        OperationProgress(0, 0, TEXT("Applying migration changes"));
    }
    return ApplyMigrationTypes(Mappings, MemberNames, Error, false, DefaultOnlyClassReplacements, bContinueOnError, bKeepAssetReferencesInBlueprint, ConfigurationTemplates);
}
