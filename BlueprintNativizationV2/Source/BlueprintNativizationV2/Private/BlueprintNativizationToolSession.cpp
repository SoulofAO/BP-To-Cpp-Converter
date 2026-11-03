/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#include "BlueprintNativizationToolSession.h"
#include "EngineAssetPolicy.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "NativeModuleCache.h"
#include "NativeApiCatalog.h"

#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSettings.h"
#include "BlueprintNativizationSubsystem.h"
#include "Engine/Blueprint.h"
#include "Engine/Engine.h"
#include "Engine/UserDefinedEnum.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"

#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool UBlueprintNativizationToolSession::CheckNativeApi(bool bIncludeProjectClients)
{
    TArray<UObject*> InputTargets;
    for (UObject* Target : Targets)
    {
        if (bContinueOnError)
        {
            if (!Target)
            {
                continue;
            }
            if (UBlueprint* Blueprint = Cast<UBlueprint>(Target))
            {
                if (!Blueprint->GeneratedClass || Blueprint->Status == BS_Error)
                {
                    continue;
                }
            }
        }
        InputTargets.Add(Target);
    }
    NativeApiReview = FNativeApiPreflight::Analyze(InputTargets, bGenerateOneFunction, GetModuleName(), bIncludeProjectClients);
    bNativeApiChecked = true;
    NativeApiInputSignature = GetInputSignature();
    return NativeApiReview.bReady;
}

bool UBlueprintNativizationToolSession::ReviewNativeApi(FString& Report)
{
    if (IsBusy())
    {
        return Fail(TEXT("Conversion is busy."));
    }
    const bool bReady = CheckNativeApi(true);
    Report = NativeApiReview.Summary;
    Status = Report;
    bLastOperationSucceeded = bReady;
    return bReady;
}

bool UBlueprintNativizationToolSession::ApplyNativeApiFixes(const FString& ReviewIdentifier)
{
    if (IsBusy() || ReviewIdentifier.IsEmpty() || ReviewIdentifier != NativeApiReview.Identifier ||
        NativeApiInputSignature != GetInputSignature())
    {
        return Fail(TEXT("Source repair review is unavailable or expired. Analyze native API again."));
    }
    FString Report;
    if (!FNativeApiPreflight::Apply(NativeApiReview, Report))
    {
        return Fail(Report);
    }
    NativeApiReview.SourcePatches.Reset();
    NativeApiReview.Identifier.Reset();
    NativeApiReview.bReady = false;
    NativeApiReview.Summary = Report;
    Status = Report;
    bLastOperationSucceeded = true;
    return true;
}

FString UBlueprintNativizationToolSession::GetNativeApiReport() const
{
    if (!bNativeApiChecked || NativeApiInputSignature != GetInputSignature())
    {
        return TEXT("Analyze native API to check project C++ classes usable by Blueprint, inheritance and required Super calls. Selection is checked again before generation and export.");
    }
    return NativeApiReview.Summary;
}

FString UBlueprintNativizationToolSession::GetNativeApiReviewIdentifier() const
{
    return NativeApiReview.Identifier;
}

FString UBlueprintNativizationToolSession::GetNativeApiSummary() const
{
    if (CanRepairNativeApi())
    {
        return FString::Format(TEXT("Native API requires changes before C++ conversion.\n{0} project source files have proposed repairs. Open the review to inspect reasons and exact edits."),
            {NativeApiReview.SourcePatches.Num()});
    }
    const FString Report = GetNativeApiReport();
    const int32 FirstLine = Report.Find(TEXT("\n"));
    if (FirstLine != INDEX_NONE)
    {
        return Report.Left(FirstLine);
    }
    return Report;
}

TArray<FString> UBlueprintNativizationToolSession::GetNativeApiRepairFiles() const
{
    TArray<FString> Files;
    if (NativeApiInputSignature == GetInputSignature())
    {
        for (const auto& Patch : NativeApiReview.SourcePatches)
        {
            Files.Add(Patch.Filename);
        }
    }
    Files.Sort();
    return Files;
}

bool UBlueprintNativizationToolSession::CanRepairNativeApi() const
{
    return bNativeApiChecked && NativeApiInputSignature == GetInputSignature() &&
        !NativeApiReview.SourcePatches.IsEmpty() && !IsBusy();
}

bool UBlueprintNativizationToolSession::IsNativeApiReady() const
{
    return bNativeApiChecked && NativeApiInputSignature == GetInputSignature() && NativeApiReview.bReady;
}

bool UBlueprintNativizationToolSession::IsNativeApiChecked() const
{
    return bNativeApiChecked && NativeApiInputSignature == GetInputSignature();
}

FString UBlueprintNativizationToolSession::GetNativeApiCheckStatus() const
{
    if (!IsNativeApiChecked())
    {
        return TEXT("Not checked: analyze native API");
    }
    if (NativeApiReview.bReady)
    {
        return TEXT("Ready: native API check passed");
    }
    if (NativeApiReview.Identifier.IsEmpty())
    {
        return TEXT("Rebuild, restart, rebuild Project API cache and analyze again");
    }
    if (!NativeApiReview.SourcePatches.IsEmpty())
    {
        return TEXT("Changes required: review and fix project API");
    }
    return TEXT("Not ready: review the reported API issues");
}

FString UBlueprintNativizationToolSession::GetPreparationReport(TSet<FString>* ReadySections) const
{
    if (ReadySections)
    {
        ReadySections->Reset();
    }
    FString Reason;
    FString ModuleStatus = TEXT("Not prepared");
    if (IsModuleReady(Reason))
    {
        ModuleStatus = TEXT("Prepared. Close the editor, build the project's Editor target and reopen the project before building API caches.");
        if (IsPreparedModuleLoaded())
        {
            ModuleStatus = TEXT("Prepared; module loaded in this editor session.");
        }
        if (ReadySections)
        {
            ReadySections->Add(TEXT("Module"));
        }
    }
    FString Report = FString::Format(TEXT("Module: {0}\n{1}\n\n"), { GetModuleName(), ModuleStatus });
    for (const FString& Scope : { FString(TEXT("Project")), FString(TEXT("Engine")) })
    {
        const FString IndexFile = FNativeModuleCache::GetIndexFile(Scope == TEXT("Engine"));
        Report += Scope + TEXT(" API cache\n");
        const FString InvalidReason = FNativeModuleCache::GetCacheInvalidReason(Scope == TEXT("Engine"));
        if (!InvalidReason.IsEmpty())
        {
            Report += TEXT("Invalid: rebuild required. ") + InvalidReason + TEXT("\n\n");
            continue;
        }
        if (!FPaths::FileExists(IndexFile))
        {
            Report += TEXT("Not built: module index is missing.\n\n");
            continue;
        }
        TSharedPtr<FJsonObject> Index;
        if (!FNativeModuleCache::ReadIndex(IndexFile, Index, Reason, false))
        {
            Report += TEXT("Needs rebuild: ") + Reason + TEXT("\n\n");
            continue;
        }
        if (Scope == TEXT("Project"))
        {
            bool bCurrentProject = false;
            const FString ProjectFilename = FPaths::GetCleanFilename(FPaths::GetProjectFilePath());
            for (const auto& Input : Index->GetObjectField(TEXT("TargetInputs"))->Values)
            {
                if (FPaths::GetCleanFilename(FString(Input.Key)) == ProjectFilename)
                {
                    bCurrentProject = true;
                    break;
                }
            }
            if (!bCurrentProject)
            {
                Report += TEXT("Not available: cache belongs to another project.\n\n");
                continue;
            }
        }
        int32 AvailableModules = 0;
        int32 MissingModules = 0;
        int32 SourceLessModules = 0;
        for (const auto& Entry : Index->GetObjectField(TEXT("Modules"))->Values)
        {
            const TSharedPtr<FJsonObject>* Module = nullptr;
            FString ModuleCacheStatus;
            FString Filename;
            if (!Entry.Value->TryGetObject(Module))
            {
                ++MissingModules;
                continue;
            }
            (*Module)->TryGetStringField(TEXT("Status"), ModuleCacheStatus);
            if (ModuleCacheStatus == TEXT("NoOriginalSources"))
            {
                ++SourceLessModules;
            }
            else if (ModuleCacheStatus == TEXT("Ready") && (*Module)->TryGetStringField(TEXT("File"), Filename) && FPaths::FileExists(Filename))
            {
                ++AvailableModules;
            }
            else
            {
                ++MissingModules;
            }
        }
        if (ReadySections && MissingModules == 0 && AvailableModules + SourceLessModules > 0)
        {
            ReadySections->Add(Scope + TEXT(" API cache"));
        }
        Report += FString::Format(TEXT("{0} module files available | {1} missing/invalid entries | {2} without original sources\nIndex updated: {3} UTC\n{4}\n\n"),
            { AvailableModules, MissingModules, SourceLessModules, IFileManager::Get().GetTimeStamp(*IndexFile).ToString(), FPaths::ConvertRelativePathToFull(IndexFile) });
    }
    Report += TEXT("Status shows cache presence for this project and engine configuration. Target inputs, source freshness and cache contents are verified when consumed.");
    return Report;
}

void UBlueprintNativizationToolSession::LoadSetup()
{
    const TCHAR* Section = TEXT("BlueprintNativizationSetup");
    GConfig->GetString(Section, TEXT("ModuleName"), ModuleNameOverride, GEditorPerProjectIni);
    GConfig->GetString(Section, TEXT("OutputFolder"), OutputFolder.Path, GEditorPerProjectIni);
    GConfig->GetString(Section, TEXT("MigrationCacheFolder"), MigrationCacheFolder.Path, GEditorPerProjectIni);
    FString LastExportedMigrationCacheFile;
    GConfig->GetString(Section, TEXT("LastExportedMigrationCacheFile"), LastExportedMigrationCacheFile, GEditorPerProjectIni);
    if (LastExportedMigrationCacheFile.IsEmpty())
    {
        FString CacheDirectory = MigrationCacheFolder.Path;
        if (CacheDirectory.IsEmpty())
        {
            if (OutputFolder.Path.IsEmpty())
            {
                CacheDirectory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), GetModuleName());
            }
            else
            {
                CacheDirectory = FPaths::Combine(OutputFolder.Path, TEXT("Nativized"), GetModuleName());
            }
        }
        LastExportedMigrationCacheFile = FPaths::Combine(CacheDirectory, TEXT("BlueprintNativizationCache.bpncache"));
    }
    MigrationCacheFile.FilePath.Reset();
    if (FPaths::FileExists(LastExportedMigrationCacheFile))
    {
        MigrationCacheFile.FilePath = FPaths::ConvertRelativePathToFull(LastExportedMigrationCacheFile);
    }
    GConfig->GetBool(Section, TEXT("MergeModule"), bMergeWithExistingModule, GEditorPerProjectIni);
    GConfig->GetBool(Section, TEXT("SaveMigrationCache"), bSaveMigrationCache, GEditorPerProjectIni);
    TextGenerationMode = GetDefault<UBlueprintNativizationV2EditorSettings>()->TextGenerationMode;
    bRemoveUnrealPrefixesFromFileNames = GetDefault<UBlueprintNativizationV2EditorSettings>()->bRemoveUnrealPrefixesFromFileNames;
    GConfig->GetBool(Section, TEXT("RemoveUnrealPrefixesFromFileNames"), bRemoveUnrealPrefixesFromFileNames, GEditorPerProjectIni);
    bReplaceDefaultOnlyChildrenWithParents = GetDefault<UBlueprintNativizationV2EditorSettings>()->bReplaceDefaultOnlyChildrenWithParents;
    GConfig->GetBool(Section, TEXT("ReplaceDefaultOnlyChildrenWithParents"), bReplaceDefaultOnlyChildrenWithParents, GEditorPerProjectIni);
    bExcludeEngineAssetsFromConversion = GetDefault<UBlueprintNativizationV2EditorSettings>()->bExcludeEngineAssetsFromConversion;
    GConfig->GetBool(Section, TEXT("ExcludeEngineAssetsFromConversion"), bExcludeEngineAssetsFromConversion, GEditorPerProjectIni);
    bContinueOnError = GetDefault<UBlueprintNativizationV2EditorSettings>()->bContinueOnError;
    GConfig->GetBool(Section, TEXT("ContinueOnError"), bContinueOnError, GEditorPerProjectIni);
    bAllowExportAfterFailedGeneration = GetDefault<UBlueprintNativizationV2EditorSettings>()->bAllowExportAfterFailedGeneration;
    GConfig->GetBool(Section, TEXT("AllowExportAfterFailedGeneration"), bAllowExportAfterFailedGeneration, GEditorPerProjectIni);
    RepeatedConstructionThreshold = GetDefault<UBlueprintNativizationV2EditorSettings>()->RepeatedConstructionThreshold;
    GConfig->GetInt(Section, TEXT("RepeatedConstructionThreshold"), RepeatedConstructionThreshold, GEditorPerProjectIni);
    RepeatedConstructionThreshold = FMath::Max(2, RepeatedConstructionThreshold);
    int32 SavedTextGenerationMode = static_cast<int32>(TextGenerationMode);
    if (GConfig->GetInt(Section, TEXT("TextGenerationMode"), SavedTextGenerationMode, GEditorPerProjectIni) &&
        SavedTextGenerationMode >= static_cast<int32>(ETextGenerationMode::PlainText) &&
        SavedTextGenerationMode <= static_cast<int32>(ETextGenerationMode::PreserveTextData))
    {
        TextGenerationMode = static_cast<ETextGenerationMode>(SavedTextGenerationMode);
    }
}

void UBlueprintNativizationToolSession::SaveSetup() const
{
    const TCHAR* Section = TEXT("BlueprintNativizationSetup");
    GConfig->SetString(Section, TEXT("ModuleName"), *ModuleNameOverride, GEditorPerProjectIni);
    GConfig->SetString(Section, TEXT("OutputFolder"), *OutputFolder.Path, GEditorPerProjectIni);
    GConfig->SetString(Section, TEXT("MigrationCacheFolder"), *MigrationCacheFolder.Path, GEditorPerProjectIni);
    GConfig->SetBool(Section, TEXT("MergeModule"), bMergeWithExistingModule, GEditorPerProjectIni);
    GConfig->SetBool(Section, TEXT("SaveMigrationCache"), bSaveMigrationCache, GEditorPerProjectIni);
    GConfig->RemoveKey(Section, TEXT("KeepAssetReferences"), GEditorPerProjectIni);
    GConfig->SetInt(Section, TEXT("TextGenerationMode"), static_cast<int32>(TextGenerationMode), GEditorPerProjectIni);
    GConfig->SetBool(Section, TEXT("RemoveUnrealPrefixesFromFileNames"), bRemoveUnrealPrefixesFromFileNames, GEditorPerProjectIni);
    GConfig->SetBool(Section, TEXT("ReplaceDefaultOnlyChildrenWithParents"), bReplaceDefaultOnlyChildrenWithParents, GEditorPerProjectIni);
    GConfig->SetBool(Section, TEXT("ExcludeEngineAssetsFromConversion"), bExcludeEngineAssetsFromConversion, GEditorPerProjectIni);
    GConfig->SetBool(Section, TEXT("ContinueOnError"), bContinueOnError, GEditorPerProjectIni);
    GConfig->SetBool(Section, TEXT("AllowExportAfterFailedGeneration"), bAllowExportAfterFailedGeneration, GEditorPerProjectIni);
    GConfig->SetInt(Section, TEXT("RepeatedConstructionThreshold"), RepeatedConstructionThreshold, GEditorPerProjectIni);
    GConfig->Flush(false, GEditorPerProjectIni);
}

bool UBlueprintNativizationToolSession::IsPreparedModuleLoaded() const
{
    return FModuleManager::Get().IsModuleLoaded(FName(*GetModuleName()));
}

bool UBlueprintNativizationToolSession::IsModuleReady(FString& Reason) const
{
    Reason = TEXT("Prepare the module in Setup before exporting to project Source.");
    const FString ModuleName = GetModuleName();
    for (const FString& Extension : { FString(TEXT(".h")), FString(TEXT(".cpp")), FString(TEXT(".Build.cs")) })
    {
        if (IFileManager::Get().FileSize(*FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), ModuleName, ModuleName + Extension)) <= 0)
        {
            return false;
        }
    }
    FString ProjectText;
    TSharedPtr<FJsonObject> Project;
    if (!FFileHelper::LoadFileToString(ProjectText, *FPaths::GetProjectFilePath()) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ProjectText), Project))
    {
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
    if (!bRegistered || TargetFiles.IsEmpty())
    {
        return false;
    }
    for (const FString& Target : TargetFiles)
    {
        FString Content;
        if (!FFileHelper::LoadFileToString(Content, *Target) || !Content.Contains(TEXT("\"") + ModuleName + TEXT("\"")))
        {
            return false;
        }
    }
    Reason.Reset();
    return true;
}

bool UBlueprintNativizationToolSession::CanExport(FString& Reason) const
{
    if (bNativeApiChecked && !NativeApiReview.bReady)
    {
        Reason = TEXT("Resolve native API requirements in Setup before exporting.");
        return false;
    }
    if (!HasResult() || IsPreviewStale())
    {
        Reason = TEXT("Generate a current preview before exporting.");
        return false;
    }
    if (bFunctionResult && (OutputFolder.Path.IsEmpty() || (bSaveMigrationCache && (!HasGenerationErrors() || Result.bPartialSuccess))))
    {
        Reason = TEXT("Function fragments require an export folder and cannot create a migration cache.");
        return false;
    }
    if (OutputFolder.Path.IsEmpty())
    {
        return IsModuleReady(Reason);
    }
    Reason.Reset();
    return true;
}

FString UBlueprintNativizationToolSession::GetModuleName() const
{
    if (!ModuleNameOverride.IsEmpty())
    {
        return ModuleNameOverride;
    }
    return GetDefault<UBlueprintNativizationV2EditorSettings>()->ModuleName;
}

bool UBlueprintNativizationToolSession::ValidateModuleName()
{
    const FString ModuleName = GetModuleName();
    if (ModuleName.IsEmpty() || !FChar::IsAlpha(ModuleName[0]))
    {
        return Fail(TEXT("The module name must begin with an ASCII letter."));
    }
    for (TCHAR Character : ModuleName)
    {
        if (Character > 127 || (!FChar::IsAlnum(Character) && Character != TEXT('_')))
        {
            return Fail(TEXT("The module name must contain only ASCII letters, numbers and underscores."));
        }
    }
    return true;
}

bool UBlueprintNativizationToolSession::IsBusy() const
{
    return bOperationInProgress || GEngine->GetEngineSubsystem<UNativizationV2Subsystem>()->bIsNativizing;
}

bool UBlueprintNativizationToolSession::Fail(const FString& Message)
{
    Status = Message;
    bLastOperationSucceeded = false;
    return false;
}

void UBlueprintNativizationToolSession::RemoveExcludedTargets()
{
    TArray<FString> ExclusionReasons;
    Targets.RemoveAll([this, &ExclusionReasons](const TObjectPtr<UObject>& Target)
    {
        const FString Reason = FEngineAssetPolicy::GetExclusionReason(Target.Get(), bExcludeEngineAssetsFromConversion);
        if (Reason.IsEmpty())
        {
            return false;
        }
        ExclusionReasons.AddUnique(Reason);
        return true;
    });
    if (!ExclusionReasons.IsEmpty())
    {
        Status = FString::Join(ExclusionReasons, TEXT("\n"));
    }
}
void UBlueprintNativizationToolSession::AuditDependencies()
{
    TGuardValue<bool> EngineAssetsGuard(GetMutableDefault<UBlueprintNativizationV2EditorSettings>()->bExcludeEngineAssetsFromConversion, bExcludeEngineAssetsFromConversion);
    TSet<UObject*> RequiredObjects;
    TArray<FName> PendingPackages;
    for (UObject* Target : Targets)
    {
        if (Target)
        {
            if (!RequiredObjects.Contains(Target))
            {
                RequiredObjects.Append(UBlueprintNativizationLibrary::GetAllDependencies(Target, {}, {}));
            }
            PendingPackages.AddUnique(Target->GetOutermost()->GetFName());
        }
    }
    TArray<FString> Warnings;
    const auto* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();
    for (UObject* Dependency : RequiredObjects)
    {
        if (UClass* DependencyClass = Cast<UClass>(Dependency))
        {
            Dependency = DependencyClass->ClassGeneratedBy;
        }
        if (!Dependency)
        {
            continue;
        }
        const FString ExclusionReason = FEngineAssetPolicy::GetExclusionReason(Dependency, bExcludeEngineAssetsFromConversion);
        if (!ExclusionReason.IsEmpty())
        {
            Warnings.AddUnique(TEXT("Dependency remains external: ") + ExclusionReason);
            continue;
        }
        bool bSupported = Dependency->IsA<UBlueprint>() || Dependency->IsA<UUserDefinedStruct>() || Dependency->IsA<UUserDefinedEnum>();
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Dependency))
        {
            for (const auto& ExcludedClass : Settings->IgnoreClassToRefGenerate)
            {
                UClass* Class = ExcludedClass.LoadSynchronous();
                if (Class && Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(Class))
                {
                    bSupported = false;
                }
            }
        }
        if (!bSupported)
        {
            if (Dependency->IsAsset())
            {
                Warnings.AddUnique(TEXT("Unsupported dependency (not converted): ") + Dependency->GetPathName());
            }
        }
        else if (!Targets.Contains(Dependency))
        {
            Warnings.AddUnique(TEXT("Dependency absent from generation list: ") + Dependency->GetPathName());
        }
    }
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    TSet<FName> VisitedPackages;
    for (int32 PackageIndex = 0; PackageIndex < PendingPackages.Num(); ++PackageIndex)
    {
        const FName PackageName = PendingPackages[PackageIndex];
        if (VisitedPackages.Contains(PackageName) || PackageName.ToString().StartsWith(TEXT("/Script/")))
        {
            continue;
        }
        VisitedPackages.Add(PackageName);
        TArray<FName> PackageDependencies;
        Registry.GetDependencies(PackageName, PackageDependencies);
        for (FName DependencyPackage : PackageDependencies)
        {
            if (bExcludeEngineAssetsFromConversion && FEngineAssetPolicy::IsEnginePackage(DependencyPackage.ToString()))
            {
                continue;
            }
            PendingPackages.AddUnique(DependencyPackage);
            if (DependencyPackage.ToString().StartsWith(TEXT("/Script/")))
            {
                continue;
            }
            TArray<FAssetData> Assets;
            Registry.GetAssetsByPackageName(DependencyPackage, Assets);
            if (Assets.IsEmpty())
            {
                Warnings.AddUnique(TEXT("Referenced package has no registered assets: ") + DependencyPackage.ToString());
            }
            for (const FAssetData& Asset : Assets)
            {
                const FString ExclusionReason = FEngineAssetPolicy::GetExclusionReason(Asset, bExcludeEngineAssetsFromConversion);
                if (!ExclusionReason.IsEmpty())
                {
                    Warnings.AddUnique(TEXT("Dependency remains external: ") + ExclusionReason);
                    continue;
                }
                UClass* AssetClass = Asset.GetClass();
                if (AssetClass && !AssetClass->IsChildOf(UBlueprint::StaticClass()) &&
                    !AssetClass->IsChildOf(UUserDefinedStruct::StaticClass()) && !AssetClass->IsChildOf(UUserDefinedEnum::StaticClass()))
                {
                    Warnings.AddUnique(TEXT("Asset dependency remains external (unsupported for conversion): ") + Asset.GetObjectPathString());
                }
                else if (!AssetClass)
                {
                    Warnings.AddUnique(TEXT("Cannot resolve dependency asset class: ") + Asset.GetObjectPathString());
                }
                else
                {
                    const FString AssetPath = Asset.GetObjectPathString();
                    const bool bSelected = Targets.ContainsByPredicate([&AssetPath](const TObjectPtr<UObject>& Target)
                    {
                        return Target && Target->GetPathName() == AssetPath;
                    });
                    if (!bSelected)
                    {
                        Warnings.AddUnique(TEXT("Dependency absent from generation list: ") + AssetPath);
                    }
                }
            }
        }
    }
    Warnings.Sort();
    DependencyWarnings.Reset();
    if (!Warnings.IsEmpty())
    {
        DependencyWarnings = FString::Format(TEXT("Warning: {0} dependency issues. Generation uses the selected list only.\n"), { Warnings.Num() }) + FString::Join(Warnings, TEXT("\n"));
    }
}

bool UBlueprintNativizationToolSession::GetDependencies()
{
    if (IsBusy())
    {
        return Fail(TEXT("Another conversion is running."));
    }
    RemoveExcludedTargets();
    TGuardValue<bool> EngineAssetsGuard(GetMutableDefault<UBlueprintNativizationV2EditorSettings>()->bExcludeEngineAssetsFromConversion, bExcludeEngineAssetsFromConversion);
    if (Targets.IsEmpty())
    {
        return Fail(TEXT("Select supported input assets before collecting dependencies."));
    }
    TGuardValue<bool> OperationGuard(bOperationInProgress, true);
    const auto* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();
    TArray<TSubclassOf<UObject>> ExcludedClasses;
    for (const auto& ExcludedClass : Settings->IgnoreClassToRefGenerate)
    {
        ExcludedClasses.Add(ExcludedClass.LoadSynchronous());
    }
    TArray<UObject*> ExcludedAssets;
    for (const auto& ExcludedAsset : Settings->IgnoreAssetsToRefGenerate)
    {
        ExcludedAssets.Add(ExcludedAsset.LoadSynchronous());
    }
    TSet<UObject*> Dependencies;
    int32 CompletedTargets = 0;
    for (UObject* Target : Targets)
    {
        if (OperationProgress && !OperationProgress(CompletedTargets, Targets.Num(), TEXT("Collecting dependencies")))
        {
            return Fail(TEXT("Dependency collection cancelled."));
        }
        if (!Dependencies.Contains(Target))
        {
            Dependencies.Append(UBlueprintNativizationLibrary::GetAllDependencies(Target, ExcludedClasses, ExcludedAssets));
        }
        ++CompletedTargets;
    }
    TArray<UObject*> OrderedDependencies;
    for (UObject* Dependency : Dependencies)
    {
        if (UClass* Class = Cast<UClass>(Dependency))
        {
            Dependency = Class->ClassGeneratedBy;
        }
        if (const UBlueprint* Blueprint = Cast<UBlueprint>(Dependency))
        {
            if (Blueprint->BlueprintType == BPTYPE_MacroLibrary)
            {
                continue;
            }
        }
        if (Dependency && (Dependency->IsA<UBlueprint>() || Dependency->IsA<UUserDefinedStruct>() || Dependency->IsA<UUserDefinedEnum>()))
        {
            OrderedDependencies.AddUnique(Dependency);
        }
    }
    OrderedDependencies.Sort([](const UObject& First, const UObject& Second) { return First.GetPathName() < Second.GetPathName(); });
    const int32 PreviousCount = Targets.Num();
    for (UObject* Dependency : OrderedDependencies)
    {
        Targets.AddUnique(Dependency);
    }
    Status = FString::Format(TEXT("Added {0} dependencies. Review the generation list before generating."), { Targets.Num() - PreviousCount });
    AuditDependencies();
    if (!DependencyWarnings.IsEmpty())
    {
        Status += TEXT("\n") + DependencyWarnings;
    }
    bLastOperationSucceeded = true;
    return true;
}

bool UBlueprintNativizationToolSession::Generate()
{

    if (IsBusy())
    {
        return Fail(TEXT("Another conversion is running."));
    }
    TGuardValue<bool> OperationGuard(bOperationInProgress, true);
    if (!ValidateModuleName())
    {
        return false;
    }
    TGuardValue<FString> ModuleNameGuard(GetMutableDefault<UBlueprintNativizationV2EditorSettings>()->ModuleName, GetModuleName());
    TGuardValue<ETextGenerationMode> TextGenerationModeGuard(GetMutableDefault<UBlueprintNativizationV2EditorSettings>()->TextGenerationMode, TextGenerationMode);
    TGuardValue<bool> FileNamePrefixGuard(GetMutableDefault<UBlueprintNativizationV2EditorSettings>()->bRemoveUnrealPrefixesFromFileNames, bRemoveUnrealPrefixesFromFileNames);
    TGuardValue<bool> EngineAssetsGuard(GetMutableDefault<UBlueprintNativizationV2EditorSettings>()->bExcludeEngineAssetsFromConversion, bExcludeEngineAssetsFromConversion);
    TGuardValue<bool> DefaultOnlyChildrenGuard(GetMutableDefault<UBlueprintNativizationV2EditorSettings>()->bReplaceDefaultOnlyChildrenWithParents, bReplaceDefaultOnlyChildrenWithParents);
    TGuardValue<int32> RepeatedConstructionThresholdGuard(GetMutableDefault<UBlueprintNativizationV2EditorSettings>()->RepeatedConstructionThreshold, RepeatedConstructionThreshold);
    Result = FNativizationModuleResult();
    GenerationErrors.Reset();
    GeneratedFields.Reset();
    GeneratedMemberNames.Reset();
    GeneratedDefaultOnlyClassReplacements.Reset();
    GeneratedModuleName.Reset();
    if (Targets.IsEmpty())
    {
        return Fail(TEXT("Select at least one Blueprint, structure or enumeration."));
    }

    AuditDependencies();
    if (!bContinueOnError || bGenerateOneFunction)
    {
        const FString SourceError = UBlueprintNativizationLibrary::ValidateBlueprintSources(TArray<UObject*>(Targets));
        if (!SourceError.IsEmpty())
        {
            return Fail(SourceError);
        }
    }
    TArray<UObject*> InputTargets;
    const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();
    for (UObject* Target : Targets)
    {
        if (!Target && !bContinueOnError)
        {
            return Fail(TEXT("Remove empty entries from the target list."));
        }
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Target))
        {
            if (!bContinueOnError && (!Blueprint->GeneratedClass || Blueprint->Status == BS_Error))
            {
                return Fail(TEXT("Compile the selected Blueprint successfully before conversion: ") + Blueprint->GetPathName());
            }
            for (const TSoftClassPtr<UObject>& ExcludedClass : Settings->IgnoreClassToRefGenerate)
            {
                UClass* LoadedClass = ExcludedClass.LoadSynchronous();
                if (!bContinueOnError && LoadedClass && Blueprint->GeneratedClass->IsChildOf(LoadedClass))
                {
                    return Fail(FString::Format(TEXT("The selected Blueprint type is excluded: {0}."), { Target->GetPathName() }));
                }
            }
        }
        else if (!bContinueOnError && !Target->IsA<UUserDefinedStruct>() && !Target->IsA<UUserDefinedEnum>())
        {
            return Fail(TEXT("Only Blueprints, user-defined structures and enumerations can be converted."));
        }
        InputTargets.AddUnique(Target);
    }

    UNativizationV2Subsystem* Subsystem = GEngine->GetEngineSubsystem<UNativizationV2Subsystem>();
    TGuardValue<TFunction<bool(int32, int32, const FString&)>> ProgressGuard(Subsystem->OperationProgress, OperationProgress);
    if (!CheckNativeApi())
    {
        return Fail(NativeApiReview.Summary);
    }
    bFunctionResult = bGenerateOneFunction;
    if (bFunctionResult)
    {
        UBlueprint* Blueprint = Cast<UBlueprint>(InputTargets[0]);
        if (InputTargets.Num() != 1 || !Blueprint || !GetFunctionNames().Contains(FunctionName))
        {
            return Fail(TEXT("Select one Blueprint and an existing function."));
        }
        Result.NativizationCodes.Add(Subsystem->RunNativizationForOneFunctionBlueprint(Blueprint, FunctionName));
        Result.Sucsess = !Result.NativizationCodes[0].OutputCppString.IsEmpty();
    }
    else
    {
        Result = Subsystem->RunNativizationForAllObjects(InputTargets, false, TEXT(""), true,
            false, false, false, TEXT(""), bContinueOnError);
    }

    TMap<FString, FString> DefaultOnlyClassReplacements = Subsystem->CaptureMigrationClassReplacements();
    GenerationErrors = FString::Join(FNativeApiCatalog::GetErrors(), TEXT("\n"));
    if (!GenerationErrors.IsEmpty())
    {
        Result.bPartialSuccess = false;
        Result.Sucsess = false;
    }
    if (!Result.GenerationErrors.IsEmpty())
    {
        GenerationErrors += TEXT("\n") + FString::Join(Result.GenerationErrors, TEXT("\n"));
    }
    if (!Result.Sucsess || (Result.NativizationCodes.IsEmpty() && !bReplaceDefaultOnlyChildrenWithParents))
    {
        if (GenerationErrors.IsEmpty())
        {
            GenerationErrors = TEXT("Generation did not produce a complete result. See the Output Log.");
        }
    }
    for (const FNativizationCode& Code : Result.NativizationCodes)
    {
        if (Code.OutputHeaderString.IsEmpty() && Code.OutputCppString.IsEmpty())
        {
            GenerationErrors += TEXT("\nAn input object produced no source code: ") + GetPathNameSafe(Code.Object);
            Result.bPartialSuccess = false;
            Result.Sucsess = false;
        }
    }
    if (!GenerationErrors.IsEmpty() && !Result.bPartialSuccess)
    {
        Result.Sucsess = false;
        if (!bAllowExportAfterFailedGeneration || Result.NativizationCodes.IsEmpty())
        {
            Result = FNativizationModuleResult();
            return Fail(GenerationErrors);
        }
    }

    GeneratedFields = UBlueprintNativizationLibrary::GetAllRegisterUniqueFieldsInNameSubsistem();
    GeneratedMemberNames = Subsystem->CaptureMigrationMemberNames();
    GeneratedDefaultOnlyClassReplacements = MoveTemp(DefaultOnlyClassReplacements);
    GeneratedModuleName = Settings->ModuleName;
    GeneratedInputSignature = GetInputSignature();
    SavedDocuments.Reset();
    for (int32 DocumentIndex = 0; DocumentIndex < GetDocumentNames().Num(); ++DocumentIndex)
    {
        SavedDocuments.Add(GetDocumentText(DocumentIndex));
    }
    bSourceSaved = false;
    if (Result.bPartialSuccess)
    {
        Status = FString::Format(TEXT("Partial preview ready: {0} objects generated; {1} objects skipped. Only the completed subset can be exported and migrated.\n{2}"),
            {Result.NativizationCodes.Num(), Result.SkippedObjects.Num(), GenerationErrors});
        UE_LOG(LogTemp, Warning, TEXT("%s"), *Status);
        bLastOperationSucceeded = false;
        return false;
    }
    if (HasGenerationErrors())
    {
        return Fail(TEXT("Generation failed. Partial preview retained for diagnostic export. Migration cache is disabled.\n") + GenerationErrors);
    }
    Status = TEXT("Preview ready.");
    if (!DependencyWarnings.IsEmpty())
    {
        Status += TEXT("\n") + DependencyWarnings;
    }
    bLastOperationSucceeded = true;
    return true;
}

bool UBlueprintNativizationToolSession::Save()
{
    if (IsBusy())
    {
        return Fail(TEXT("Another conversion is running."));
    }
    if (!CheckNativeApi())
    {
        return Fail(NativeApiReview.Summary);
    }
    FString ExportReason;
    if (!CanExport(ExportReason))
    {
        return Fail(ExportReason);
    }
    if (IsBusy())
    {
        return Fail(TEXT("Another conversion is running."));
    }
    if (!HasResult())
    {
        return Fail(TEXT("Generate source before saving."));
    }
    if (IsPreviewStale())
    {
        return Fail(TEXT("The conversion inputs changed. Generate a new preview before exporting."));
    }
    if (GeneratedModuleName != GetModuleName())
    {
        return Fail(TEXT("The module name changed. Generate source again before saving."));
    }
    if (bFunctionResult && OutputFolder.Path.IsEmpty())
    {
        return Fail(TEXT("Select an export folder for a function fragment. It is not a complete class."));
    }
    if (bFunctionResult && bSaveMigrationCache && (!HasGenerationErrors() || Result.bPartialSuccess))
    {
        return Fail(TEXT("A function fragment cannot be applied as a class migration."));
    }
    TGuardValue<bool> OperationGuard(bOperationInProgress, true);
    UNativizationV2Subsystem* Subsystem = GEngine->GetEngineSubsystem<UNativizationV2Subsystem>();
    FString OutputDirectory;
    TGuardValue<TFunction<bool(int32, int32, const FString&)>> ProgressGuard(Subsystem->OperationProgress, OperationProgress);
    TGuardValue<FString> ModuleNameGuard(GetMutableDefault<UBlueprintNativizationV2EditorSettings>()->ModuleName, GeneratedModuleName);
    FNativizationModuleResult SavedResult = Result;
    if (bFunctionResult)
    {
        SavedResult.ModuleCode.ObjectName = GeneratedModuleName;
    }
    if (!Subsystem->SaveNativizationResult(SavedResult, OutputFolder.Path, OutputDirectory, bMergeWithExistingModule))
    {
        return Fail(TEXT("Source could not be completely saved. See the Output Log."));
    }
    if (!bFunctionResult && OutputFolder.Path.IsEmpty())
    {
        Subsystem->AddModuleToUProject(GeneratedModuleName);
    }
    if (bSaveMigrationCache && (!HasGenerationErrors() || Result.bPartialSuccess))
    {
        FString CacheDirectory = MigrationCacheFolder.Path;
        if (CacheDirectory.IsEmpty())
        {
            CacheDirectory = OutputDirectory;
        }
        if (!Subsystem->WriteJSONCache(CacheDirectory, GeneratedFields, GeneratedMemberNames, GeneratedDefaultOnlyClassReplacements))
        {
            return Fail(TEXT("Source was saved, but the migration cache could not be written."));
        }
        MigrationCacheFile.FilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(CacheDirectory, TEXT("BlueprintNativizationCache.bpncache")));
        GConfig->SetString(TEXT("BlueprintNativizationSetup"), TEXT("LastExportedMigrationCacheFile"), *MigrationCacheFile.FilePath, GEditorPerProjectIni);
        GConfig->Flush(false, GEditorPerProjectIni);
    }
    Status = FString::Format(TEXT("Exported to {0}."), { OutputDirectory });
    if (IsBlueprintCodeAnalysisCurrent())
    {
        Status += TEXT("\n\nBlueprint code analysis:\n") + GetBlueprintCodeAnalysisReport();
    }
    if (Result.bPartialSuccess)
    {
        Status += TEXT("\nExported the completed subset. Skipped objects are excluded from source and migration mappings.\n") + GenerationErrors;
    }
    else if (HasGenerationErrors())
    {
        Status += TEXT("\nDiagnostic export only: generation failed, code may not compile, no migration cache was written.\n") + GenerationErrors;
    }
    SavedDocuments.Reset();
    for (int32 DocumentIndex = 0; DocumentIndex < GetDocumentNames().Num(); ++DocumentIndex)
    {
        SavedDocuments.Add(GetDocumentText(DocumentIndex));
    }
    bSourceSaved = true;
    bLastOperationSucceeded = true;
    return true;
}

bool UBlueprintNativizationToolSession::ApplyCache()
{
    if (IsBusy())
    {
        return Fail(TEXT("Another conversion is running."));
    }
    TGuardValue<bool> OperationGuard(bOperationInProgress, true);
    FString Error;
    UNativizationV2Subsystem* Subsystem = GEngine->GetEngineSubsystem<UNativizationV2Subsystem>();
    TGuardValue<TFunction<bool(int32, int32, const FString&)>> ProgressGuard(Subsystem->OperationProgress, OperationProgress);
    if (!Subsystem->TryReapplyJSONCache(MigrationCacheFile.FilePath, Error, false, nullptr, bContinueOnError))
    {
        UE_LOG(LogTemp, Error, TEXT("Migration result: %s"), *Error);
        return Fail(Error);
    }
    Status = TEXT("Migration completed. Review the converted assets and the Output Log.");
    bLastOperationSucceeded = true;
    return true;
}

bool UBlueprintNativizationToolSession::PrepareModule()
{
    if (IsBusy())
    {
        return Fail(TEXT("Another conversion is running."));
    }
    TGuardValue<bool> OperationGuard(bOperationInProgress, true);
    if (!ValidateModuleName())
    {
        return false;
    }
    TGuardValue<FString> ModuleNameGuard(GetMutableDefault<UBlueprintNativizationV2EditorSettings>()->ModuleName, GetModuleName());
    FString Error;
    if (!GEngine->GetEngineSubsystem<UNativizationV2Subsystem>()->TryPrepareNewModule(Error))
    {
        return Fail(Error);
    }
    Status = TEXT("Module prepared. Close the editor, build the project's Editor target and reopen the project before building API caches or continuing conversion.");
    bLastOperationSucceeded = true;
    return true;
}

void UBlueprintNativizationToolSession::ResetNames()
{
    if (!IsBusy())
    {
        GEngine->GetEngineSubsystem<UNativizationV2Subsystem>()->ResetNameSubsystem();
        Status = TEXT("Generated name registry reset. Existing preview and its migration mapping are preserved.");
    }
}

void UBlueprintNativizationToolSession::PrintNodes()
{
    if (IsBusy())
    {
        return;
    }
    if (Targets.Num() != 1 || !Cast<UBlueprint>(Targets[0]))
    {
        Fail(TEXT("Select one Blueprint to print its nodes."));
        return;
    }
    GEngine->GetEngineSubsystem<UNativizationV2Subsystem>()->PrintAllK2Nodes(Cast<UBlueprint>(Targets[0]), FunctionName);
    Status = TEXT("Blueprint nodes were written to the Output Log.");
}

TArray<FString> UBlueprintNativizationToolSession::GetFunctionNames() const
{
    TArray<FString> Names;
    if (Targets.Num() == 1)
    {
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Targets[0]))
        {
            for (FName Name : UBlueprintNativizationLibrary::GetAllBlueprintCallableNames(Blueprint, true, false))
            {
                Names.AddUnique(Name.ToString());
            }
        }
    }
    Names.Sort();
    return Names;
}

FString UBlueprintNativizationToolSession::GetInputSignature() const
{
    FString Signature = FString::Format(TEXT("{0}|{1}|{2}|{3}|{4}|{5}|{6}|{7}|{8}|{9}"),
        { bGenerateOneFunction, bIncludeDependencies, FunctionName,
          GetModuleName(), static_cast<int32>(TextGenerationMode), RepeatedConstructionThreshold, bRemoveUnrealPrefixesFromFileNames, bReplaceDefaultOnlyChildrenWithParents, bExcludeEngineAssetsFromConversion, bContinueOnError });
    for (UObject* Target : Targets)
    {
        Signature += TEXT("|") + GetPathNameSafe(Target);
    }
    return Signature;
}

bool UBlueprintNativizationToolSession::IsPreviewStale() const
{
    return HasResult() && GeneratedInputSignature != GetInputSignature();
}

bool UBlueprintNativizationToolSession::IsDocumentModified(int32 DocumentIndex) const
{
    return SavedDocuments.IsValidIndex(DocumentIndex) && SavedDocuments[DocumentIndex] != GetDocumentText(DocumentIndex);
}

bool UBlueprintNativizationToolSession::HasUnsavedChanges() const
{
    if (!HasResult())
    {
        return false;
    }
    if (!bSourceSaved)
    {
        return true;
    }
    for (int32 DocumentIndex = 0; DocumentIndex < SavedDocuments.Num(); ++DocumentIndex)
    {
        if (IsDocumentModified(DocumentIndex))
        {
            return true;
        }
    }
    return false;
}

TArray<FString> UBlueprintNativizationToolSession::GetGeneratedObjects() const
{
    TArray<FString> Objects;
    for (const FNativizationCode& Code : Result.NativizationCodes)
    {
        Objects.Add(GetPathNameSafe(Code.Object));
    }
    return Objects;
}

bool UBlueprintNativizationToolSession::ReviewMigration(FString& Summary)
{
    if (IsBusy())
    {
        return Fail(TEXT("Another conversion is running."));
    }
    TGuardValue<bool> OperationGuard(bOperationInProgress, true);
    FString Error;
    UNativizationV2Subsystem* Subsystem = GEngine->GetEngineSubsystem<UNativizationV2Subsystem>();
    TGuardValue<TFunction<bool(int32, int32, const FString&)>> ProgressGuard(Subsystem->OperationProgress, OperationProgress);
    if (!Subsystem->TryReapplyJSONCache(MigrationCacheFile.FilePath, Error, true, &Summary, bContinueOnError))
    {
        Summary = Error;
        return Fail(Error);
    }
    return true;
}

bool UBlueprintNativizationToolSession::ReviewExport(FString& Summary)
{
    if (IsBusy())
    {
        Summary = TEXT("Another conversion is running.");
        return Fail(Summary);
    }
    if (!CheckNativeApi())
    {
        Summary = NativeApiReview.Summary;
        return Fail(NativeApiReview.Summary);
    }
    FString ExportReason;
    if (!CanExport(ExportReason))
    {
        return Fail(ExportReason);
    }
    if (!HasResult() || IsPreviewStale())
    {
        return Fail(TEXT("Generate a current preview before exporting."));
    }
    if (bFunctionResult && (OutputFolder.Path.IsEmpty() || (bSaveMigrationCache && (!HasGenerationErrors() || Result.bPartialSuccess))))
    {
        return Fail(TEXT("Function fragments require an export folder and cannot create a migration cache."));
    }
    FString Directory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), GeneratedModuleName);
    if (!OutputFolder.Path.IsEmpty())
    {
        Directory = FPaths::Combine(OutputFolder.Path, TEXT("Nativized"), GeneratedModuleName);
    }
    TArray<FString> Files;
    for (const FNativizationCode& Code : Result.NativizationCodes)
    {
        if (!Code.Object)
        {
            continue;
        }
        FString PackagePath = FPackageName::LongPackageNameToFilename(Code.Object->GetOutermost()->GetName());
        const int32 ContentIndex = PackagePath.Find(TEXT("Content/"), ESearchCase::IgnoreCase);
        if (ContentIndex != INDEX_NONE)
        {
            PackagePath = PackagePath.Mid(ContentIndex + 8);
        }
        int32 LastSlashIndex;
        if (PackagePath.FindLastChar('/', LastSlashIndex))
        {
            PackagePath = PackagePath.Left(LastSlashIndex);
        }
        if (!Code.OutputHeaderString.IsEmpty())
        {
            Files.Add(FPaths::Combine(Directory, PackagePath, Code.ObjectName + TEXT(".h")));
        }
        if (!Code.OutputCppString.IsEmpty())
        {
            Files.Add(FPaths::Combine(Directory, PackagePath, Code.ObjectName + TEXT(".cpp")));
        }
    }
    if (!Result.ModuleCode.OutputHeaderString.IsEmpty())
    {
        Files.Add(FPaths::Combine(Directory, GeneratedModuleName + TEXT(".h")));
    }
    if (!Result.ModuleCode.OutputCppString.IsEmpty())
    {
        Files.Add(FPaths::Combine(Directory, GeneratedModuleName + TEXT(".cpp")));
    }
    if (!Result.ModuleBuildCsCode.IsEmpty())
    {
        Files.Add(FPaths::Combine(Directory, GeneratedModuleName + TEXT(".Build.cs")));
    }
    if (bSaveMigrationCache && (!HasGenerationErrors() || Result.bPartialSuccess))
    {
        FString CacheDirectory = MigrationCacheFolder.Path;
        if (CacheDirectory.IsEmpty())
        {
            CacheDirectory = Directory;
        }
        Files.Add(FPaths::Combine(CacheDirectory, TEXT("BlueprintNativizationCache.bpncache")));
    }
    Summary = TEXT("Files to write:\n\n");
    int32 CreatedFiles = 0;
    int32 ReplacedFiles = 0;
    int32 MergedFiles = 0;
    for (const FString& Filename : Files)
    {
        FText PathError;
        if (!FPaths::ValidatePath(Filename, &PathError))
        {
            return Fail(PathError.ToString());
        }
        FString Operation = TEXT("Create");
        if (FPaths::FileExists(Filename))
        {
            if (IFileManager::Get().IsReadOnly(*Filename))
            {
                return Fail(FString::Format(TEXT("File is read-only: {0}"), { Filename }));
            }
            ++ReplacedFiles;
            Operation = TEXT("Overwrite");
            if (bMergeWithExistingModule && Filename.EndsWith(TEXT(".Build.cs")))
            {
                ++MergedFiles;
                Operation = TEXT("Merge dependencies and overwrite");
            }
        }
        else
        {
            ++CreatedFiles;
        }
        Summary += FString::Format(TEXT("{0}: {1}\n"), { Operation, FPaths::ConvertRelativePathToFull(Filename) });
    }
    int32 EditedDocuments = 0;
    for (int32 DocumentIndex = 0; DocumentIndex < GetDocumentNames().Num(); ++DocumentIndex)
    {
        if (IsDocumentModified(DocumentIndex))
        {
            ++EditedDocuments;
        }
    }
    FString CacheStatus = TEXT("Disabled");
    if (bSaveMigrationCache && (!HasGenerationErrors() || Result.bPartialSuccess))
    {
        CacheStatus = TEXT("Will be written");
    }
    Summary = FString::Format(TEXT("Destination: {0}\nFiles: {1} | New: {2} | Existing: {3} | Build.cs merges: {4}\nEdited documents: {5}\nMigration cache: {6}\n\n"),
        { FPaths::ConvertRelativePathToFull(Directory), Files.Num(), CreatedFiles, ReplacedFiles, MergedFiles, EditedDocuments, CacheStatus }) + Summary;
    Summary += TEXT("\nThis writes the edited preview. It does not compile code or migrate assets.");
    if (IsBlueprintCodeAnalysisCurrent())
    {
        Summary += TEXT("\n\nBlueprint code analysis:\n") + GetBlueprintCodeAnalysisReport();
    }
    if (Result.bPartialSuccess)
    {
        Summary += TEXT("\nPartial export: only the completed subset and its migration mappings will be written. Existing source files outside this export are not removed; use a clean output folder to build the subset in isolation.\n") + GenerationErrors;
    }
    else if (HasGenerationErrors())
    {
        Summary += TEXT("\nWARNING: Diagnostic export of failed generation. Source may be incomplete or invalid. No migration cache will be written.\n") + GenerationErrors;
    }
    return true;
}

bool UBlueprintNativizationToolSession::HasResult() const
{
    return Result.Sucsess || (bAllowExportAfterFailedGeneration && !Result.NativizationCodes.IsEmpty());
}

bool UBlueprintNativizationToolSession::HasPartialResult() const
{
    return Result.bPartialSuccess && Result.Sucsess;
}

bool UBlueprintNativizationToolSession::HasGenerationErrors() const
{
    return !GenerationErrors.IsEmpty();
}

TArray<FString> UBlueprintNativizationToolSession::GetDocumentNames() const
{
    TArray<FString> Names;
    if (!HasResult())
    {
        return Names;
    }
    for (const FNativizationCode& Code : Result.NativizationCodes)
    {
        Names.Add(Code.ObjectName + TEXT(".h"));
        Names.Add(Code.ObjectName + TEXT(".cpp"));
    }
    if (!bFunctionResult)
    {
        Names.Add(Result.ModuleCode.ObjectName + TEXT(".h"));
        Names.Add(Result.ModuleCode.ObjectName + TEXT(".cpp"));
        Names.Add(Result.ModuleCode.ObjectName + TEXT(".Build.cs"));
    }
    return Names;
}

FString UBlueprintNativizationToolSession::GetDocumentText(int32 DocumentIndex) const
{
    if (!GetDocumentNames().IsValidIndex(DocumentIndex))
    {
        return FString();
    }
    if (DocumentIndex < Result.NativizationCodes.Num() * 2)
    {
        const FNativizationCode& Code = Result.NativizationCodes[DocumentIndex / 2];
        if (DocumentIndex % 2 == 0)
        {
            return Code.OutputHeaderString;
        }
        return Code.OutputCppString;
    }
    switch (DocumentIndex - Result.NativizationCodes.Num() * 2)
    {
    case 0: return Result.ModuleCode.OutputHeaderString;
    case 1: return Result.ModuleCode.OutputCppString;
    default: return Result.ModuleBuildCsCode;
    }
}

void UBlueprintNativizationToolSession::SetDocumentText(int32 DocumentIndex, const FString& Text)
{
    if (!GetDocumentNames().IsValidIndex(DocumentIndex))
    {
        return;
    }
    if (DocumentIndex < Result.NativizationCodes.Num() * 2)
    {
        FNativizationCode& Code = Result.NativizationCodes[DocumentIndex / 2];
        if (DocumentIndex % 2 == 0)
        {
            Code.OutputHeaderString = Text;
        }
        else
        {
            Code.OutputCppString = Text;
        }
        return;
    }
    switch (DocumentIndex - Result.NativizationCodes.Num() * 2)
    {
    case 0: Result.ModuleCode.OutputHeaderString = Text; break;
    case 1: Result.ModuleCode.OutputCppString = Text; break;
    default: Result.ModuleBuildCsCode = Text; break;
    }
}

UBlueprint* UBlueprintNativizationToolSession::GetDocumentBlueprint(int32 DocumentIndex) const
{
    if (DocumentIndex < 0 || !Result.NativizationCodes.IsValidIndex(DocumentIndex / 2))
    {
        return nullptr;
    }
    UObject* SourceObject = Result.NativizationCodes[DocumentIndex / 2].Object;
    if (UClass* SourceClass = Cast<UClass>(SourceObject))
    {
        SourceObject = SourceClass->ClassGeneratedBy;
    }
    return Cast<UBlueprint>(SourceObject);
}
