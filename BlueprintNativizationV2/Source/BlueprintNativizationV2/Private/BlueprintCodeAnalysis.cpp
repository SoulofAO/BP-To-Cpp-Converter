/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#include "BlueprintNativizationToolSession.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintConstructionCompatibility.h"
#include "BlueprintComponentRepair.h"
#include "BlueprintAssetReferenceCleanup.h"
#include "EngineAssetPolicy.h"
#include "TranslatorObjects/ComponentEventTranslatorObject.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/UserDefinedEnum.h"
#include "StructUtils/UserDefinedStruct.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_CallParentFunction.h"
#include "K2Node_CallFunction.h"
#include "EdGraphSchema_K2.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Components/Image.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/SceneComponent.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "ObjectTools.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "UObject/LinkerLoad.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectHash.h"
#include "Editor.h"
#include "EngineUtils.h"

static TArray<AActor*> FindSelectedActorsWithUnmatchedComponents(const TArray<TObjectPtr<UBlueprint>>& Blueprints)
{
    TArray<AActor*> Actors;
    UWorld* World = nullptr;
    if (GEditor)
    {
        World = GEditor->GetEditorWorldContext().World();
    }
    if (!World || World->WorldType != EWorldType::Editor)
    {
        return Actors;
    }
    for (TActorIterator<AActor> Actor(World); Actor; ++Actor)
    {
        for (UBlueprint* Blueprint : Blueprints)
        {
            if (Blueprint->GeneratedClass && Actor->IsA(Blueprint->GeneratedClass) &&
                !FBlueprintComponentRepair::FindUnmatchedConstructionComponents(*Actor).IsEmpty())
            {
                Actors.Add(*Actor);
                break;
            }
        }
    }
    return Actors;
}

TArray<FString> UBlueprintNativizationToolSession::InspectSourceActorComponents(AActor* Actor)
{
    TArray<FString> Components;
    for (UActorComponent* Component : FBlueprintComponentRepair::FindUnmatchedConstructionComponents(Actor))
    {
        Components.Add(Component->GetPathName());
    }
    return Components;
}

void UBlueprintNativizationToolSession::InvalidateBlueprintCodeAnalysis(UBlueprint*)
{
    BlueprintCodeAnalysisSignature.Reset();
}

bool UBlueprintNativizationToolSession::IsBlueprintCodeAnalysisCurrent() const
{
    return !BlueprintCodeAnalysisSignature.IsEmpty() && BlueprintCodeAnalysisSignature == GetInputSignature();
}

bool UBlueprintNativizationToolSession::IsBlueprintCodeAnalysisReady() const
{
    return IsBlueprintCodeAnalysisCurrent() && bBlueprintCodeAnalysisReady;
}

FString UBlueprintNativizationToolSession::GetBlueprintCodeAnalysisReport() const
{
    if (BlueprintCodeAnalysisReport.IsEmpty())
    {
        return TEXT("Analyze selected assets and their dependencies. With an empty selection, analyze project Content. Checks source diagnostics, missing packages and conversion in memory; no fixes or source files are saved.");
    }
    if (!IsBlueprintCodeAnalysisCurrent())
    {
        return TEXT("Analysis is out of date. Run Analyze Blueprint Code again.\n\n") + BlueprintCodeAnalysisReport;
    }
    return BlueprintCodeAnalysisReport;
}

bool UBlueprintNativizationToolSession::AnalyzeBlueprintCode()
{
    if (IsBusy())
    {
        return Fail(TEXT("Another conversion is running."));
    }
    TGuardValue<bool> OperationGuard(bOperationInProgress, true);
    for (UBlueprint* Blueprint : AnalyzedBlueprints)
    {
        Blueprint->OnChanged().RemoveAll(this);
        Blueprint->OnCompiled().RemoveAll(this);
    }
    AnalyzedBlueprints.Reset();
    MissingDependencyOwners.Reset();
    BlueprintCodeAnalysisSignature.Reset();
    bBlueprintCodeAnalysisReady = false;
    const FString InputSignature = GetInputSignature();
    bool bCancelled = false;
    TStrongObjectPtr<UBlueprintNativizationToolSession> Analysis(NewObject<UBlueprintNativizationToolSession>());
    Analysis->ModuleNameOverride = GetModuleName();
    Analysis->Targets = Targets;
    Analysis->bGenerateOneFunction = bGenerateOneFunction;
    Analysis->FunctionName = FunctionName;
    Analysis->bIncludeDependencies = true;
    Analysis->TextGenerationMode = TextGenerationMode;
    Analysis->bRemoveUnrealPrefixesFromFileNames = bRemoveUnrealPrefixesFromFileNames;
    Analysis->bReplaceDefaultOnlyChildrenWithParents = bReplaceDefaultOnlyChildrenWithParents;
    Analysis->RepeatedConstructionThreshold = RepeatedConstructionThreshold;
    Analysis->bExcludeEngineAssetsFromConversion = bExcludeEngineAssetsFromConversion;
    Analysis->bAllowExportAfterFailedGeneration = true;
    Analysis->OperationProgress = [this, &bCancelled](int32 Completed, int32 Total, const FString& Stage)
    {
        if (OperationProgress && !OperationProgress(Completed, Total, Stage))
        {
            bCancelled = true;
            return false;
        }
        return true;
    };
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    TArray<FString> Errors;
    TArray<FString> Warnings;
    TSet<FName> Packages;
    const bool bWholeContent = Targets.IsEmpty();
    for (UObject* Target : Targets)
    {
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Target))
        {
            AnalyzedBlueprints.AddUnique(Blueprint);
            if (!Blueprint->ParentClass)
            {
                Packages.Add(Blueprint->GetOutermost()->GetFName());
                continue;
            }
        }
        const FString Exclusion = FEngineAssetPolicy::GetExclusionReason(Target, bExcludeEngineAssetsFromConversion);
        if (!Exclusion.IsEmpty())
        {
            Errors.AddUnique(Exclusion);
        }
        for (UObject* Dependency : UBlueprintNativizationLibrary::GetAllDependencies(Target, {}, {}))
        {
            if (UBlueprint* Blueprint = Cast<UBlueprint>(Dependency))
            {
                AnalyzedBlueprints.AddUnique(Blueprint);
            }
            if (Dependency)
            {
                Packages.Add(Dependency->GetOutermost()->GetFName());
            }
        }
    }
    if (bWholeContent)
    {
        FARFilter Filter;
        Filter.PackagePaths.Add(TEXT("/Game"));
        Filter.bRecursivePaths = true;
        TArray<FAssetData> Assets;
        Registry.GetAssets(Filter, Assets);
        const auto* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();
        int32 CompletedAssets = 0;
        for (const FAssetData& Asset : Assets)
        {
            if (!Analysis->OperationProgress(CompletedAssets++, Assets.Num(), TEXT("Inspecting project Content")))
            {
                BlueprintCodeAnalysisReport = TEXT("Analysis cancelled. Results are incomplete.");
                return Fail(BlueprintCodeAnalysisReport);
            }
            Packages.Add(Asset.PackageName);
            UClass* AssetClass = Asset.GetClass();
            if (!AssetClass || (!AssetClass->IsChildOf(UBlueprint::StaticClass()) &&
                !AssetClass->IsChildOf(UUserDefinedStruct::StaticClass()) && !AssetClass->IsChildOf(UUserDefinedEnum::StaticClass())))
            {
                continue;
            }
            const FString Exclusion = FEngineAssetPolicy::GetExclusionReason(Asset, bExcludeEngineAssetsFromConversion);
            if (!Exclusion.IsEmpty())
            {
                Warnings.AddUnique(Exclusion);
                continue;
            }
            UObject* Object = Asset.GetAsset();
            if (!Object)
            {
                Errors.AddUnique(TEXT("Cannot load source asset: ") + Asset.GetObjectPathString());
                continue;
            }
            bool bExcluded = false;
            if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
            {
                AnalyzedBlueprints.AddUnique(Blueprint);
                bExcluded = Blueprint->BlueprintType == BPTYPE_MacroLibrary;
                for (const auto& ExcludedClass : Settings->IgnoreClassToRefGenerate)
                {
                    UClass* LoadedClass = ExcludedClass.LoadSynchronous();
                    if (LoadedClass && Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(LoadedClass))
                    {
                        bExcluded = true;
                    }
                }
            }
            if (!bExcluded)
            {
                Analysis->Targets.AddUnique(Object);
            }
        }
    }
    Analysis->Targets.RemoveAll([](UObject* Target)
    {
        UBlueprint* Blueprint = Cast<UBlueprint>(Target);
        return Blueprint && !Blueprint->ParentClass;
    });
    bool bDependenciesComplete = !Analysis->Targets.IsEmpty();
    if (bDependenciesComplete)
    {
        bDependenciesComplete = Analysis->GetDependencies();
    }
    if (!bDependenciesComplete)
    {
        if (bCancelled)
        {
            BlueprintCodeAnalysisReport = TEXT("Analysis cancelled. Results are incomplete.");
            return Fail(BlueprintCodeAnalysisReport);
        }
        Errors.AddUnique(TEXT("Conversion analysis could not collect its inputs: ") + Analysis->Status);
    }
    for (UObject* Object : Analysis->Targets)
    {
        if (!Object)
        {
            Errors.AddUnique(TEXT("The selection contains an empty asset entry."));
            continue;
        }
        Packages.Add(Object->GetOutermost()->GetFName());
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
        {
            AnalyzedBlueprints.AddUnique(Blueprint);
        }
    }
    int32 CompletedBlueprints = 0;
    for (UBlueprint* Blueprint : AnalyzedBlueprints)
    {
        if (!Analysis->OperationProgress(CompletedBlueprints++, AnalyzedBlueprints.Num(), TEXT("Checking source Blueprint code")))
        {
            BlueprintCodeAnalysisReport = TEXT("Analysis cancelled. Results are incomplete.");
            return Fail(BlueprintCodeAnalysisReport);
        }
        if (!Blueprint->GeneratedClass || Blueprint->Status == BS_Error)
        {
            Errors.AddUnique(TEXT("Source Blueprint has compilation errors or no generated class: ") + Blueprint->GetPathName());
        }
        if (!Blueprint->ParentClass)
        {
            Errors.AddUnique(TEXT("Blueprint has no parent class. TryFix can delete this invalid asset: ") + Blueprint->GetPathName());
        }
        for (const FConstructionAttachmentConflict& Conflict : FBlueprintConstructionCompatibility::FindConflicts(Blueprint))
        {
            Warnings.AddUnique(Conflict.Description);
        }
        for (const FString& Diagnostic : FBlueprintComponentRepair::Inspect(Blueprint).Diagnostics)
        {
            Warnings.AddUnique(Diagnostic);
        }
        TArray<UEdGraph*> Graphs;
        Blueprint->GetAllGraphs(Graphs);
        for (UEdGraph* Graph : Graphs)
        {
            if (Graph == FBlueprintEditorUtils::FindUserConstructionScript(Blueprint))
            {
                for (UK2Node* Entry : UBlueprintNativizationLibrary::GetEntryNodesPerGraph(Graph, false))
                {
                    bool bHasExecutableLogic = false;
                    for (UK2Node* ExecutableNode : UBlueprintNativizationLibrary::GetAllExecuteNodesInFunction(Entry))
                    {
                        if (ExecutableNode != Entry && ExecutableNode->IsNodeEnabled() &&
                            !ExecutableNode->IsA<UK2Node_CallParentFunction>() && !ExecutableNode->IsA<UK2Node_FunctionResult>())
                        {
                            bHasExecutableLogic = true;
                            break;
                        }
                    }
                    if (bHasExecutableLogic)
                    {
                        Warnings.AddUnique(Blueprint->GetPathName() + TEXT(" / ") + Graph->GetName() +
                            TEXT(": Construction Script is generated as OnConstruction and requires compatibility review. Blueprint UCS context is not preserved. Review component attachment and mobility, instance-data restoration, delegate ordering, and APIs that depend on construction-script context. Adjust the source graph where necessary."));
                    }
                }
            }
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (Node->bHasCompilerMessage)
                {
                    const FString Message = Blueprint->GetPathName() + TEXT(" / ") + Graph->GetName() + TEXT(": ") + Node->ErrorMsg;
                    if (Node->ErrorType == EMessageSeverity::Error)
                    {
                        Errors.AddUnique(Message);
                    }
                    else if (Node->ErrorType == EMessageSeverity::Warning)
                    {
                        Warnings.AddUnique(Message);
                    }
                }
                if (UK2Node_ComponentBoundEvent* Event = Cast<UK2Node_ComponentBoundEvent>(Node))
                {
                    FObjectPropertyBase* ComponentProperty = nullptr;
                    FString Error;
                    if (!UComponentEventTranslatorObject::ResolveComponentDelegate(Event, ComponentProperty, Error))
                    {
                        Errors.AddUnique(Error);
                    }
                }
            }
        }
    }
    int32 CompletedPackages = 0;
    for (FName Package : Packages)
    {
        if (!Analysis->OperationProgress(CompletedPackages++, Packages.Num(), TEXT("Checking referenced packages")))
        {
            BlueprintCodeAnalysisReport = TEXT("Analysis cancelled. Results are incomplete.");
            return Fail(BlueprintCodeAnalysisReport);
        }
        TArray<FName> Dependencies;
        Registry.GetDependencies(Package, Dependencies);
        TArray<FName> HardDependencies;
        Registry.GetDependencies(Package, HardDependencies, UE::AssetRegistry::EDependencyCategory::Package,
            UE::AssetRegistry::FDependencyQuery(UE::AssetRegistry::EDependencyQuery::Hard));
        for (FName Dependency : Dependencies)
        {
            const FString Path = Dependency.ToString();
            const UPackage* LoadedPackage = FindPackage(nullptr, *Path);
            if (Path.StartsWith(TEXT("/Script/")) || Path == TEXT("/Engine/Transient") ||
                (LoadedPackage && LoadedPackage->HasAnyPackageFlags(PKG_NewlyCreated)) || FPackageName::DoesPackageExist(Path))
            {
                continue;
            }
            const FString Message = FString::Format(TEXT("Missing referenced package {0}, referenced by {1}."), {Path, Package.ToString()});
            MissingDependencyOwners.Add(Package);
            if (HardDependencies.Contains(Dependency))
            {
                Errors.AddUnique(Message);
            }
            else
            {
                Warnings.AddUnique(Message);
            }
        }
    }
    for (AActor* Actor : FindSelectedActorsWithUnmatchedComponents(AnalyzedBlueprints))
    {
        for (UActorComponent* Component : FBlueprintComponentRepair::FindUnmatchedConstructionComponents(Actor))
        {
            Errors.AddUnique(FString::Format(TEXT("Unmatched SCS component {0}. TryFix backs up the map and reruns this actor's construction scripts using the current Blueprint. Obsolete components may be removed; they are not promoted to instance components."),
                {Component->GetPathName()}));
        }
    }
    bool bConversionSucceeded = false;
    if (bDependenciesComplete)
    {
        if (bGenerateOneFunction)
        {
            Analysis->Targets = Targets;
        }
        bConversionSucceeded = Analysis->Generate();
        if (!bConversionSucceeded)
        {
            TArray<FString> GenerationMessages;
            if (Analysis->GenerationErrors.IsEmpty())
            {
                Errors.AddUnique(TEXT("Conversion analysis stopped before producing code: ") + Analysis->Status);
            }
            else
            {
                Analysis->GenerationErrors.ParseIntoArrayLines(GenerationMessages);
                for (const FString& Message : GenerationMessages)
                {
                    Errors.AddUnique(Message);
                }
            }
        }
    }
    Errors.Sort();
    Warnings.Sort();
    BlueprintCodeAnalysisReport = FString::Format(TEXT("Analyzed {0} Blueprints and {1} conversion inputs. Errors: {2}. Warnings: {3}."),
        {AnalyzedBlueprints.Num(), Analysis->Targets.Num(), Errors.Num(), Warnings.Num()});
    BlueprintCodeAnalysisReport += TEXT("\n\nSource diagnostics and component events were inspected without compiling or saving the original assets. Actor component consistency was checked in the open editor world only; migration also checks affected assets before changing types. Conversion was attempted in memory using the current options. A blocked or cancelled conversion cannot check subsequent stages. UHT, C++ compilation and gameplay are not checked. Re-run after changing assets, plugin settings or API caches.\n");
    if (!Errors.IsEmpty())
    {
        BlueprintCodeAnalysisReport += TEXT("\nErrors:\n") + FString::Join(Errors, TEXT("\n\n"));
    }
    if (!Warnings.IsEmpty())
    {
        BlueprintCodeAnalysisReport += TEXT("\n\nWarnings:\n") + FString::Join(Warnings, TEXT("\n\n"));
    }
    for (UBlueprint* Blueprint : AnalyzedBlueprints)
    {
        Blueprint->OnChanged().AddUObject(this, &UBlueprintNativizationToolSession::InvalidateBlueprintCodeAnalysis);
        Blueprint->OnCompiled().AddUObject(this, &UBlueprintNativizationToolSession::InvalidateBlueprintCodeAnalysis);
    }
    if (!bCancelled)
    {
        BlueprintCodeAnalysisSignature = InputSignature;
    }
    bBlueprintCodeAnalysisReady = bConversionSucceeded && Errors.IsEmpty();
    Status = BlueprintCodeAnalysisReport;
    bLastOperationSucceeded = bBlueprintCodeAnalysisReady;
    return bLastOperationSucceeded;
}

bool UBlueprintNativizationToolSession::TryFixBlueprintCode()
{
    if (IsBusy())
    {
        return Fail(TEXT("Another conversion is running."));
    }
    if (!IsBlueprintCodeAnalysisCurrent())
    {
        return Fail(TEXT("Run Analyze Blueprint Code before TryFix. The repair scope must match the current selection."));
    }
    TGuardValue<bool> OperationGuard(bOperationInProgress, true);
    const FString BackupDirectory = FPaths::ProjectSavedDir() / TEXT("BlueprintTryFix") / FGuid::NewGuid().ToString();
    TArray<FString> Messages;
    bool bRepairIncomplete = false;
    TArray<UBlueprint*> InvalidBlueprints;
    TMap<UBlueprint*, TArray<FConstructionAttachmentConflict>> ComponentsToFix;
    for (UBlueprint* Blueprint : AnalyzedBlueprints)
    {
        if (!Blueprint->ParentClass)
        {
            InvalidBlueprints.AddUnique(Blueprint);
            continue;
        }
        FBlueprintComponentRepair ComponentRepair = FBlueprintComponentRepair::Inspect(Blueprint);
        if (ComponentRepair.HasRepairs())
        {
            ComponentsToFix.FindOrAdd(Blueprint);
        }
        for (const FConstructionAttachmentConflict& Conflict : FBlueprintConstructionCompatibility::FindConflicts(Blueprint))
        {
            if (Conflict.Blueprint && Conflict.Blueprint->Status != BS_Error)
            {
                TArray<FConstructionAttachmentConflict>& Repairs = ComponentsToFix.FindOrAdd(Conflict.Blueprint);
                if (!Repairs.ContainsByPredicate([&Conflict](const FConstructionAttachmentConflict& Existing)
                {
                    return Existing.Component == Conflict.Component && Existing.MobilityInput == Conflict.MobilityInput;
                }))
                {
                    Repairs.Add(Conflict);
                }
            }
            else
            {
                bRepairIncomplete = true;
                Messages.AddUnique(TEXT("Manual repair required: ") + Conflict.Description);
            }
        }
    }
    TSet<FName> BackedUpPackages;
    const auto BackupAsset = [&BackupDirectory, &Messages, &bRepairIncomplete, &BackedUpPackages](UObject* Asset)
    {
        UPackage* Package = Asset->GetPackage();
        if (BackedUpPackages.Contains(Package->GetFName()))
        {
            return true;
        }
        FString Filename;
        if (!FPackageName::DoesPackageExist(Package->GetName(), &Filename))
        {
            bRepairIncomplete = true;
            Messages.Add(TEXT("Cannot repair an unsaved asset: ") + Asset->GetPathName());
            return false;
        }
        Filename = FPaths::ConvertRelativePathToFull(Filename);
        if (!Package->GetName().StartsWith(TEXT("/Game/")) ||
            !FPaths::IsUnderDirectory(Filename, FPaths::ConvertRelativePathToFull(FPaths::ProjectDir())) ||
            IFileManager::Get().IsReadOnly(*Filename))
        {
            bRepairIncomplete = true;
            Messages.Add(TEXT("Cannot repair an external or read-only asset: ") + Asset->GetPathName());
            return false;
        }
        const FString Backup = BackupDirectory / (Package->GetName().RightChop(1) + FPaths::GetExtension(Filename, true));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
        if (IFileManager::Get().Copy(*Backup, *Filename, false) != COPY_OK)
        {
            bRepairIncomplete = true;
            Messages.Add(TEXT("Asset backup failed: ") + Asset->GetPathName());
            return false;
        }
        BackedUpPackages.Add(Package->GetFName());
        return true;
    };
    int32 FixedComponents = 0;
    int32 FixedMobilityCalls = 0;
    int32 RepairedBlueprints = 0;
    int32 DeletedBlueprints = 0;
    int32 ResavedPackages = 0;
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    TSet<UPackage*> ReconstructedPackages;
    TSet<UPackage*> FailedReconstructionPackages;
    for (AActor* Actor : FindSelectedActorsWithUnmatchedComponents(AnalyzedBlueprints))
    {
        if (Actor->IsChildActor())
        {
            bRepairIncomplete = true;
            Messages.Add(TEXT("Manual reconstruction required for a child actor: ") + Actor->GetPathName());
            continue;
        }
        if (!BackupAsset(Actor))
        {
            continue;
        }
        if (Actor->GetLevel()->MapBuildData && !BackupAsset(Actor->GetLevel()->MapBuildData))
        {
            continue;
        }
        const FString ActorPath = Actor->GetPathName();
        UPackage* Package = Actor->GetPackage();
        for (UActorComponent* Component : FBlueprintComponentRepair::FindUnmatchedConstructionComponents(Actor))
        {
            Messages.Add(TEXT("Reconstructing source actor with unmatched SCS component: ") + Component->GetPathName());
        }
        Actor->Modify();
        Actor->RerunConstructionScripts();
        if (!IsValid(Actor) || !FBlueprintComponentRepair::FindUnmatchedConstructionComponents(Actor).IsEmpty())
        {
            bRepairIncomplete = true;
            FailedReconstructionPackages.Add(Package);
            Messages.Add(TEXT("Reconstruction did not resolve the source component mismatch; package was not saved: ") + ActorPath);
            continue;
        }
        Actor->MarkPackageDirty();
        ReconstructedPackages.Add(Package);
        Messages.Add(TEXT("Reconstructed source actor using the current Blueprint: ") + ActorPath);
    }
    for (UPackage* Package : ReconstructedPackages)
    {
        if (FailedReconstructionPackages.Contains(Package))
        {
            continue;
        }
        if (!UEditorLoadingAndSavingUtils::SavePackages({Package}, false))
        {
            bRepairIncomplete = true;
            Messages.Add(TEXT("Reconstructed map could not be saved; backup is retained: ") + Package->GetName());
            continue;
        }
        ++ResavedPackages;
        Messages.Add(TEXT("Saved source component repair: ") + Package->GetName());
    }
    for (const auto& Repair : ComponentsToFix)
    {
        UBlueprint* Blueprint = Repair.Key;
        if (!BackupAsset(Blueprint))
        {
            continue;
        }
        Blueprint->Modify();
        const FBlueprintComponentRepair ComponentRepair = FBlueprintComponentRepair::Inspect(Blueprint);
        if (ComponentRepair.HasRepairs())
        {
            Messages.Append(ComponentRepair.Diagnostics);
            ComponentRepair.TryRemoveBrokenNodes(Blueprint, Messages);
        }
        int32 ChangedTemplates = 0;
        int32 ChangedCalls = 0;
        for (const FConstructionAttachmentConflict& Conflict : Repair.Value)
        {
            if (Conflict.MobilityInput)
            {
                Conflict.MobilityInput->GetOwningNode()->Modify();
                Conflict.MobilityInput->BreakAllPinLinks();
                GetDefault<UEdGraphSchema_K2>()->TrySetDefaultValue(*Conflict.MobilityInput, TEXT("Movable"));
                ++ChangedCalls;
                Messages.Add(TEXT("Repaired construction mobility request: ") + Conflict.Description);
            }
            else
            {
                Conflict.Component->Modify();
                Conflict.Component->SetMobility(EComponentMobility::Movable);
                ++ChangedTemplates;
            }
        }
        if (ComponentRepair.HasRepairs() || ChangedCalls > 0)
        {
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        }
        else
        {
            FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
        }
        FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipSave);
        if (Blueprint->Status == BS_Error || !UEditorLoadingAndSavingUtils::SavePackages({Blueprint->GetOutermost()}, false))
        {
            bRepairIncomplete = true;
            Messages.Add(TEXT("Repair was not saved; restore from the backup if needed: ") + Blueprint->GetPathName());
            continue;
        }
        FixedComponents += ChangedTemplates;
        FixedMobilityCalls += ChangedCalls;
        ++RepairedBlueprints;
        Messages.Add(TEXT("Saved Blueprint repair attempt: ") + Blueprint->GetPathName());
        FString SavedFilename;
        if (FPackageName::DoesPackageExist(Blueprint->GetOutermost()->GetName(), &SavedFilename))
        {
            Registry.ScanModifiedAssetFiles({SavedFilename});
        }
        TArray<FName> RemainingDependencies;
        Registry.GetDependencies(Blueprint->GetOutermost()->GetFName(), RemainingDependencies,
            UE::AssetRegistry::EDependencyCategory::Package);
        for (FName Dependency : RemainingDependencies)
        {
            const FString PackagePath = Dependency.ToString();
            if (PackagePath.StartsWith(TEXT("/Game/")) && !FPackageName::DoesPackageExist(PackagePath) &&
                !FindPackage(nullptr, *PackagePath))
            {
                bRepairIncomplete = true;
                Messages.Add(FString::Format(TEXT("Manual repair still required: {0} references missing package {1}."),
                    {Blueprint->GetPathName(), PackagePath}));
            }
        }
    }
    TArray<FName> PackagesToResave = MissingDependencyOwners.Array();
    PackagesToResave.Sort(FNameLexicalLess());
    for (FName PackageName : PackagesToResave)
    {
        const FString PackagePath = PackageName.ToString();
        TArray<FName> HardDependencies;
        Registry.GetDependencies(PackageName, HardDependencies, UE::AssetRegistry::EDependencyCategory::Package,
            UE::AssetRegistry::FDependencyQuery(UE::AssetRegistry::EDependencyQuery::Hard));
        HardDependencies.RemoveAll([](FName Dependency)
        {
            return Dependency.ToString().StartsWith(TEXT("/Script/")) || FPackageName::DoesPackageExist(Dependency.ToString());
        });
        TArray<FName> PackageDependencies;
        Registry.GetDependencies(PackageName, PackageDependencies, UE::AssetRegistry::EDependencyCategory::Package);
        TSet<FName> MissingPackages;
        for (FName Dependency : PackageDependencies)
        {
            if (!Dependency.ToString().StartsWith(TEXT("/Script/")) && !FPackageName::DoesPackageExist(Dependency.ToString()))
            {
                MissingPackages.Add(Dependency);
            }
        }
        if (MissingPackages.IsEmpty())
        {
            continue;
        }
        FString Filename;
        if (!PackagePath.StartsWith(TEXT("/Game/")) || !FPackageName::DoesPackageExist(PackagePath, &Filename))
        {
            bRepairIncomplete = true;
            Messages.Add(TEXT("Manual repair required for unavailable or external package: ") + PackagePath);
            continue;
        }
        TStrongObjectPtr<UPackage> Package(LoadPackage(nullptr, *PackagePath, LOAD_None));
        if (!Package.IsValid())
        {
            bRepairIncomplete = true;
            Messages.Add(TEXT("Cannot load package for repair: ") + PackagePath);
            continue;
        }
        if (FailedReconstructionPackages.Contains(Package.Get()))
        {
            Messages.Add(TEXT("Package resave skipped after unresolved source reconstruction: ") + PackagePath);
            continue;
        }
        if (!BackupAsset(Package.Get()))
        {
            continue;
        }
        Package->FullyLoad();
        if (!Package->IsFullyLoaded() || !Package->GetExternalPackages().IsEmpty())
        {
            bRepairIncomplete = true;
            Messages.Add(TEXT("Manual repair required: package is not fully loaded or uses external packages: ") + PackagePath);
            continue;
        }
        bool bCanResave = true;
        TMap<UK2Node_CallFunction*, UFunction*> FunctionCallsToRepair;
        TArray<UImage*> ImagesToClear;
        const UBlueprintNativizationV2EditorSettings* RepairSettings = GetDefault<UBlueprintNativizationV2EditorSettings>();
        FLinkerLoad* Linker = Package->GetLinker();
        if (!Linker && !HardDependencies.IsEmpty())
        {
            bCanResave = false;
            Messages.Add(TEXT("Cannot verify original package exports; resave skipped: ") + PackagePath);
        }
        else if (Linker)
        {
            for (int32 ImportIndex = 0; ImportIndex < Linker->ImportMap.Num(); ++ImportIndex)
            {
                const FObjectImport& Import = Linker->ImportMap[ImportIndex];
                if (Import.ClassName == NAME_Package || Import.XObject)
                {
                    continue;
                }
                FPackageIndex OuterIndex = Import.OuterIndex;
                bool bMissingDependency = false;
                for (int32 RemainingImports = Linker->ImportMap.Num(); RemainingImports > 0 && OuterIndex.IsImport(); --RemainingImports)
                {
                    if (!Linker->ImportMap.IsValidIndex(OuterIndex.ToImport()))
                    {
                        break;
                    }
                    const FObjectImport& OuterImport = Linker->ImportMap[OuterIndex.ToImport()];
                    if (OuterImport.OuterIndex.IsNull() && OuterImport.ClassName == NAME_Package)
                    {
                        bMissingDependency = HardDependencies.Contains(OuterImport.ObjectName);
                        break;
                    }
                    OuterIndex = OuterImport.OuterIndex;
                }
                bool bOnlyUsedByGeneratedFunctions = false;
                if (bMissingDependency && Import.ObjectName.ToString() == TEXT("Default__") + Import.ClassName.ToString())
                {
                    bool bHasConsumer = false;
                    bool bHasOtherConsumer = false;
                    for (int32 ExportIndex = 0; ExportIndex < Linker->DependsMap.Num(); ++ExportIndex)
                    {
                        if (!Linker->DependsMap[ExportIndex].Contains(FPackageIndex::FromImport(ImportIndex)))
                        {
                            continue;
                        }
                        bHasConsumer = true;
                        if (Linker->ExportMap.IsValidIndex(ExportIndex))
                        {
                            UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Linker->ExportMap[ExportIndex].Object);
                            const FString MissingClassPath = Import.ClassPackage.ToString() + TEXT(".") + Import.ClassName.ToString();
                            const FString* ReplacementClassPath = RepairSettings->BlueprintRepairLibraryReplacements.Find(MissingClassPath);
                            if (CallNode && ReplacementClassPath)
                            {
                                UClass* ReplacementClass = LoadObject<UClass>(nullptr, **ReplacementClassPath);
                                UFunction* ReplacementFunction = nullptr;
                                if (ReplacementClass && ReplacementClass->HasAnyClassFlags(CLASS_Native) &&
                                    ReplacementClass->IsChildOf(UBlueprintFunctionLibrary::StaticClass()))
                                {
                                    ReplacementFunction = ReplacementClass->FindFunctionByName(CallNode->FunctionReference.GetMemberName());
                                }
                                UFunction* CurrentFunction = CallNode->GetTargetFunction();
                                if (ReplacementFunction && ReplacementFunction->HasAllFunctionFlags(FUNC_Static | FUNC_BlueprintCallable) &&
                                    (!CurrentFunction || CurrentFunction == ReplacementFunction))
                                {
                                    TStrongObjectPtr<UK2Node_CallFunction> ExpectedNode(NewObject<UK2Node_CallFunction>(CallNode->GetGraph(), NAME_None, RF_Transient));
                                    ExpectedNode->SetFromFunction(ReplacementFunction);
                                    ExpectedNode->AllocateDefaultPins();
                                    bool bCompatiblePins = CallNode->Pins.Num() == ExpectedNode->Pins.Num();
                                    for (UEdGraphPin* Pin : CallNode->Pins)
                                    {
                                        UEdGraphPin* ExpectedPin = ExpectedNode->FindPin(Pin->PinName, Pin->Direction);
                                        if (!ExpectedPin || Pin->bOrphanedPin)
                                        {
                                            bCompatiblePins = false;
                                            break;
                                        }
                                        if (Pin->PinName == UEdGraphSchema_K2::PN_Self)
                                        {
                                            if (!Pin->LinkedTo.IsEmpty())
                                            {
                                                bCompatiblePins = false;
                                            }
                                        }
                                        else if (Pin->PinType != ExpectedPin->PinType)
                                        {
                                            bCompatiblePins = false;
                                        }
                                        else if (Pin->Direction == EGPD_Input && Pin->LinkedTo.IsEmpty() &&
                                            Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
                                        {
                                            UClass* ArgumentClass = Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get());
                                            if (ArgumentClass && ReplacementClass->IsChildOf(ArgumentClass))
                                            {
                                                // The missing CDO could have been an argument rather than the static call target.
                                                bCompatiblePins = false;
                                            }
                                        }
                                    }
                                    if (bCompatiblePins && CallNode->FindPin(UEdGraphSchema_K2::PN_Self))
                                    {
                                        FunctionCallsToRepair.Add(CallNode, ReplacementFunction);
                                        continue;
                                    }
                                }
                                Messages.Add(TEXT("Cannot replace missing library call: native function or pin signature does not match: ") + CallNode->GetPathName());
                            }
                        }
                        if (!Linker->ExportMap.IsValidIndex(ExportIndex) || Linker->GetExportClassName(ExportIndex) != NAME_Function)
                        {
                            bHasOtherConsumer = true;
                            break;
                        }
                        const FObjectExport& Consumer = Linker->ExportMap[ExportIndex];
                        UBlueprintGeneratedClass* ConsumerClass = nullptr;
                        if (Consumer.OuterIndex.IsExport() && Linker->ExportMap.IsValidIndex(Consumer.OuterIndex.ToExport()))
                        {
                            ConsumerClass = Cast<UBlueprintGeneratedClass>(Linker->ExportMap[Consumer.OuterIndex.ToExport()].Object);
                        }
                        if (!ConsumerClass || !Cast<UBlueprint>(ConsumerClass->ClassGeneratedBy))
                        {
                            bHasOtherConsumer = true;
                            break;
                        }
                    }
                    bOnlyUsedByGeneratedFunctions = !bHasOtherConsumer &&
                        (bHasConsumer || Linker->DependsMap.Num() == Linker->ExportMap.Num());
                    for (const FObjectImport& OtherImport : Linker->ImportMap)
                    {
                        if (OtherImport.OuterIndex == FPackageIndex::FromImport(ImportIndex))
                        {
                            bOnlyUsedByGeneratedFunctions = false;
                        }
                    }
                }
                bool bOnlyUsedByMissingImages = false;
                if (bMissingDependency && RepairSettings->bClearMissingWidgetImages && Import.ClassName == TEXT("Texture2D") &&
                    Linker->DependsMap.Num() == Linker->ExportMap.Num())
                {
                    TArray<UImage*> ImportImages;
                    bool bHasOtherConsumer = false;
                    for (int32 ConsumerIndex = 0; ConsumerIndex < Linker->DependsMap.Num(); ++ConsumerIndex)
                    {
                        if (!Linker->DependsMap[ConsumerIndex].Contains(FPackageIndex::FromImport(ImportIndex)))
                        {
                            continue;
                        }
                        UImage* Image = Cast<UImage>(Linker->ExportMap[ConsumerIndex].Object);
                        if (!Image || Image->GetBrush().GetResourceObject())
                        {
                            bHasOtherConsumer = true;
                            break;
                        }
                        UBlueprint* ImageBlueprint = Image->GetTypedOuter<UBlueprint>();
                        if (!ImageBlueprint)
                        {
                            UBlueprintGeneratedClass* ImageClass = Image->GetTypedOuter<UBlueprintGeneratedClass>();
                            if (ImageClass)
                            {
                                ImageBlueprint = Cast<UBlueprint>(ImageClass->ClassGeneratedBy);
                            }
                        }
                        if (!ImageBlueprint)
                        {
                            bHasOtherConsumer = true;
                            break;
                        }
                        ImportImages.AddUnique(Image);
                    }
                    bOnlyUsedByMissingImages = !bHasOtherConsumer && !ImportImages.IsEmpty();
                    if (bOnlyUsedByMissingImages)
                    {
                        for (UImage* Image : ImportImages)
                        {
                            ImagesToClear.AddUnique(Image);
                        }
                    }
                }
                bool bLoadedDataConsumers = bMissingDependency &&
                    Import.ClassName != NAME_Class && Import.ClassName != NAME_Function &&
                    Import.ClassName != TEXT("BlueprintGeneratedClass") && Import.ClassName != TEXT("ScriptStruct") &&
                    Import.ClassName != TEXT("UserDefinedStruct") && Import.ClassName != TEXT("Enum") &&
                    Import.ClassName != TEXT("UserDefinedEnum") &&
                    !Import.ObjectName.ToString().StartsWith(TEXT("Default__")) &&
                    Linker->DependsMap.Num() == Linker->ExportMap.Num();
                if (bLoadedDataConsumers)
                {
                    for (int32 ConsumerIndex = 0; ConsumerIndex < Linker->DependsMap.Num(); ++ConsumerIndex)
                    {
                        if (Linker->DependsMap[ConsumerIndex].Contains(FPackageIndex::FromImport(ImportIndex)) &&
                            !IsValid(Linker->ExportMap[ConsumerIndex].Object))
                        {
                            bLoadedDataConsumers = false;
                            break;
                        }
                    }
                    if (bLoadedDataConsumers)
                    {
                        Messages.Add(FString::Format(TEXT("Missing data import {0} ({1}) has loaded consumers; resave will discard unresolved hard references already cleared by the loader."),
                            {Linker->GetImportPathName(ImportIndex), Import.ClassName.ToString()}));
                    }
                }
                if (bMissingDependency && !bOnlyUsedByGeneratedFunctions && !bOnlyUsedByMissingImages && !bLoadedDataConsumers && Import.ClassName != NAME_Class &&
                    Import.ClassName != TEXT("BlueprintGeneratedClass") && Import.ClassName != TEXT("MapBuildDataRegistry"))
                {
                    bCanResave = false;
                    TArray<FString> Consumers;
                    for (int32 ConsumerIndex = 0; ConsumerIndex < Linker->DependsMap.Num(); ++ConsumerIndex)
                    {
                        if (Linker->DependsMap[ConsumerIndex].Contains(FPackageIndex::FromImport(ImportIndex)))
                        {
                            Consumers.Add(Linker->GetExportPathName(ConsumerIndex));
                        }
                    }
                    Messages.Add(FString::Format(
                        TEXT("Manual repair required: {0} has an unresolved {1} import {2}. Consumers: {3}. Its data cannot be recovered by resaving; package was left unchanged."),
                        {PackagePath, Import.ClassName.ToString(), Import.ObjectName.ToString(), FString::Join(Consumers, TEXT(", "))}));
                }
            }
            for (int32 ExportIndex = 0; ExportIndex < Linker->ExportMap.Num(); ++ExportIndex)
            {
                const FObjectExport& Export = Linker->ExportMap[ExportIndex];
                if (IsValid(Export.Object) || (!Export.bExportLoadFailed && !Export.bWasFiltered))
                {
                    continue;
                }
                UObject* ExportOwner = nullptr;
                if (Export.OuterIndex.IsExport() && Linker->ExportMap.IsValidIndex(Export.OuterIndex.ToExport()))
                {
                    ExportOwner = Linker->ExportMap[Export.OuterIndex.ToExport()].Object;
                }
                UBlueprintGeneratedClass* GeneratedOwner = Cast<UBlueprintGeneratedClass>(ExportOwner);
                // UE migrates the legacy metadata UObject into UPackage::MetaData and invalidates its export.
                if (Export.ObjectName == NAME_PackageMetaData && Export.OuterIndex.IsNull() &&
                    Linker->GetExportClassName(ExportIndex) == TEXT("MetaData"))
                {
                    Package->GetMetaData();
                    continue;
                }
                if ((Linker->GetExportClassName(ExportIndex) == NAME_Function ||
                    Linker->GetExportClassName(ExportIndex) == TEXT("ComponentDelegateBinding")) && GeneratedOwner &&
                    Cast<UBlueprint>(GeneratedOwner->ClassGeneratedBy))
                {
                    continue;
                }
                AActor* ActorOwner = Cast<AActor>(ExportOwner);
                bool bExplicitlyRetiredComponent = false;
                if (ActorOwner && Export.ClassIndex.IsImport() && Linker->ImportMap.IsValidIndex(Export.ClassIndex.ToImport()))
                {
                    const FString MissingClassPath = Linker->GetImportPathName(Export.ClassIndex);
                    const FString* RetiredOwnerPath = RepairSettings->BlueprintRepairRetiredComponentOwners.Find(MissingClassPath);
                    if (RetiredOwnerPath && !Linker->ImportMap[Export.ClassIndex.ToImport()].XObject)
                    {
                        for (UClass* OwnerClass = ActorOwner->GetClass(); OwnerClass; OwnerClass = OwnerClass->GetSuperClass())
                        {
                            if (OwnerClass->GetPathName() == *RetiredOwnerPath)
                            {
                                bExplicitlyRetiredComponent = true;
                                break;
                            }
                        }
                    }
                }
                bool bHasSourceComponentTemplate = false;
                if (ActorOwner && Linker->DependsMap.IsValidIndex(ExportIndex))
                {
                    for (FPackageIndex Dependency : Linker->DependsMap[ExportIndex])
                    {
                        if (!Dependency.IsImport() || !Linker->ImportMap.IsValidIndex(Dependency.ToImport()))
                        {
                            continue;
                        }
                        const FObjectImport& TemplateImport = Linker->ImportMap[Dependency.ToImport()];
                        if (TemplateImport.ObjectName.ToString() != Export.ObjectName.ToString() + USimpleConstructionScript::ComponentTemplateNameSuffix ||
                            TemplateImport.ClassName != Linker->GetExportClassName(ExportIndex) ||
                            !TemplateImport.OuterIndex.IsImport() || !Linker->ImportMap.IsValidIndex(TemplateImport.OuterIndex.ToImport()))
                        {
                            continue;
                        }
                        UBlueprintGeneratedClass* TemplateClass = Cast<UBlueprintGeneratedClass>(
                            Linker->ImportMap[TemplateImport.OuterIndex.ToImport()].XObject);
                        if (TemplateClass && ActorOwner->IsA(TemplateClass))
                        {
                            bHasSourceComponentTemplate = true;
                            break;
                        }
                    }
                }
                if ((Export.bIsInheritedInstance || bHasSourceComponentTemplate || bExplicitlyRetiredComponent) && ActorOwner &&
                    !FindFProperty<FObjectPropertyBase>(ActorOwner->GetClass(), Export.ObjectName) &&
                    !ActorOwner->GetArchetype()->GetDefaultSubobjectByName(Export.ObjectName))
                {
                    bool bHasComponentTemplate = false;
                    for (UClass* ParentClass = ActorOwner->GetClass(); ParentClass; ParentClass = ParentClass->GetSuperClass())
                    {
                        UBlueprintGeneratedClass* BlueprintClass = Cast<UBlueprintGeneratedClass>(ParentClass);
                        if (BlueprintClass && BlueprintClass->SimpleConstructionScript &&
                            BlueprintClass->SimpleConstructionScript->FindSCSNode(Export.ObjectName))
                        {
                            bHasComponentTemplate = true;
                            break;
                        }
                    }
                    if (!bHasComponentTemplate)
                    {
                        Messages.Add(FString::Format(TEXT("Retired component export {0} on {1} has no current property or component template; omit during resave (explicit project rule: {2})."),
                            {Export.ObjectName.ToString(), ActorOwner->GetPathName(), bExplicitlyRetiredComponent}));
                        continue;
                    }
                }
                TArray<FString> ExportDependencies;
                if (Linker->DependsMap.IsValidIndex(ExportIndex))
                {
                    for (FPackageIndex Dependency : Linker->DependsMap[ExportIndex])
                    {
                        if (Dependency.IsImport())
                        {
                            ExportDependencies.Add(Linker->GetImportPathName(Dependency));
                        }
                    }
                }
                bCanResave = false;
                Messages.Add(FString::Format(
                    TEXT("Manual repair required: {0} could not load export {1} (class {2}, inherited {3}, outer {4}, dependencies {5}). Resaving could discard an object; its replacement or removal is ambiguous."),
                    {PackagePath, Export.ObjectName.ToString(), Linker->GetExportClassName(ExportIndex).ToString(),
                        Export.bIsInheritedInstance, GetPathNameSafe(ExportOwner), FString::Join(ExportDependencies, TEXT(", "))}));
            }
        }
        if (!bCanResave)
        {
            bRepairIncomplete = true;
            continue;
        }
        FBlueprintAssetReferenceCleanup ReferenceCleanup(MissingPackages, Messages);
        ReferenceCleanup.Apply(Package.Get());
        for (UImage* Image : ImagesToClear)
        {
            Image->Modify();
            FSlateBrush Brush = Image->GetBrush();
            Brush.SetResourceObject(nullptr);
            Brush.DrawAs = ESlateBrushDrawType::NoDrawType;
            Image->SetBrush(Brush);
            Messages.Add(TEXT("Cleared missing image resource and disabled empty brush drawing: ") + Image->GetPathName());
        }
        for (const auto& FunctionRepair : FunctionCallsToRepair)
        {
            UK2Node_CallFunction* CallNode = FunctionRepair.Key;
            UFunction* ReplacementFunction = FunctionRepair.Value;
            CallNode->Modify();
            CallNode->SetFromFunction(ReplacementFunction);
            UEdGraphPin* SelfPin = CallNode->FindPin(UEdGraphSchema_K2::PN_Self);
            SelfPin->Modify();
            SelfPin->PinType.PinSubCategoryObject = ReplacementFunction->GetOuterUClass();
            SelfPin->DefaultObject = ReplacementFunction->GetOuterUClass()->GetDefaultObject();
            SelfPin->DefaultValue.Reset();
            SelfPin->DefaultTextValue = FText::GetEmpty();
            FBlueprintEditorUtils::MarkBlueprintAsModified(CallNode->GetBlueprint());
            Messages.Add(FString::Format(TEXT("Rebound library call {0} to {1}; argument defaults and graph connections were preserved."),
                {CallNode->GetPathName(), ReplacementFunction->GetPathName()}));
        }
        TArray<TStrongObjectPtr<UBlueprint>> PackageBlueprints;
        ForEachObjectWithPackage(Package.Get(), [&PackageBlueprints](UObject* Object)
        {
            if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
            {
                PackageBlueprints.Emplace(Blueprint);
            }
            return true;
        }, EGetObjectsFlags::IncludeNestedObjects, RF_Transient, EInternalObjectFlags::Garbage);
        for (const TStrongObjectPtr<UBlueprint>& Blueprint : PackageBlueprints)
        {
            if (!Blueprint->ParentClass)
            {
                bCanResave = false;
                Messages.Add(TEXT("Cannot resave package containing a Blueprint with no parent: ") + Blueprint->GetPathName());
                continue;
            }
            FKismetEditorUtilities::CompileBlueprint(Blueprint.Get(), EBlueprintCompileOptions::SkipSave);
            if (Blueprint->Status == BS_Error || !Blueprint->GeneratedClass)
            {
                bCanResave = false;
                Messages.Add(TEXT("Cannot resave package containing a Blueprint that failed compilation: ") + Blueprint->GetPathName());
            }
        }
        if (!bCanResave)
        {
            bRepairIncomplete = true;
            continue;
        }
        FSavePackageArgs SaveArguments;
        SaveArguments.TopLevelFlags = RF_Standalone;
        UWorld* World = UWorld::FindWorldInPackage(Package.Get());
        if (!UPackage::SavePackage(Package.Get(), World, *Filename, SaveArguments))
        {
            bRepairIncomplete = true;
            Messages.Add(TEXT("Package resave failed; original backup is retained: ") + PackagePath);
            continue;
        }
        ++ResavedPackages;
        Registry.ScanModifiedAssetFiles({Filename});
        Messages.Add(TEXT("Resaved package to rebuild serialized dependencies without replacing classes or deleting graph nodes: ") + PackagePath);
    }
    if (!InvalidBlueprints.IsEmpty())
    {
        Registry.SearchAllAssets(true);
    }
    for (UBlueprint* Blueprint : InvalidBlueprints)
    {
        TArray<FName> Referencers;
        Registry.GetReferencers(Blueprint->GetOutermost()->GetFName(), Referencers, UE::AssetRegistry::EDependencyCategory::Package);
        Referencers.Remove(Blueprint->GetOutermost()->GetFName());
        if (!Referencers.IsEmpty())
        {
            bRepairIncomplete = true;
            Messages.Add(TEXT("Invalid Blueprint is referenced by other assets; deletion requires manual review: ") + Blueprint->GetPathName());
            continue;
        }
        if (!BackupAsset(Blueprint))
        {
            continue;
        }
        const FString AssetPath = Blueprint->GetPathName();
        Blueprint->OnChanged().RemoveAll(this);
        Blueprint->OnCompiled().RemoveAll(this);
        const bool bWasSelected = Targets.Contains(Blueprint);
        Targets.Remove(Blueprint);
        AnalyzedBlueprints.Remove(Blueprint);
        if (ObjectTools::ForceDeleteObjects({Blueprint}, false) == 1)
        {
            ++DeletedBlueprints;
            Messages.Add(TEXT("Deleted invalid Blueprint with no parent class: ") + AssetPath);
        }
        else
        {
            bRepairIncomplete = true;
            if (bWasSelected)
            {
                Targets.Add(Blueprint);
            }
            AnalyzedBlueprints.Add(Blueprint);
            Messages.Add(TEXT("Failed to delete invalid Blueprint: ") + AssetPath);
        }
    }
    int32 RemainingMissingDependencies = 0;
    for (FName PackageName : MissingDependencyOwners)
    {
        if (!FPackageName::DoesPackageExist(PackageName.ToString()))
        {
            continue;
        }
        TArray<FName> Dependencies;
        Registry.GetDependencies(PackageName, Dependencies, UE::AssetRegistry::EDependencyCategory::Package);
        for (FName Dependency : Dependencies)
        {
            const FString DependencyPath = Dependency.ToString();
            const UPackage* LoadedDependency = FindPackage(nullptr, *DependencyPath);
            if (DependencyPath.StartsWith(TEXT("/Script/")) || DependencyPath == TEXT("/Engine/Transient") ||
                (LoadedDependency && LoadedDependency->HasAnyPackageFlags(PKG_NewlyCreated)) ||
                FPackageName::DoesPackageExist(DependencyPath))
            {
                continue;
            }
            ++RemainingMissingDependencies;
            bRepairIncomplete = true;
            Messages.AddUnique(FString::Format(
                TEXT("Manual repair still required: {0} references missing package {1}. No unambiguous replacement was established; restore the original dependency or review its use."),
                {PackageName.ToString(), DependencyPath}));
        }
    }
    BlueprintCodeAnalysisSignature.Reset();
    GeneratedInputSignature.Reset();
    bBlueprintCodeAnalysisReady = false;
    Status = FString::Format(TEXT("TryFix saved repair attempts for {0} Blueprints, changed {1} mobility templates, deleted {2} invalid Blueprints and resaved {3} dependency-owning packages. Remaining missing dependencies: {4}. Backups: {5}. Missing behavior and built lighting are not restored. Changed construction mobility calls: {7}. Run Analyze Blueprint Code again and verify the saved assets after reloading.\n\n{6}"),
        {RepairedBlueprints, FixedComponents, DeletedBlueprints, ResavedPackages, RemainingMissingDependencies,
            FPaths::ConvertRelativePathToFull(BackupDirectory), FString::Join(Messages, TEXT("\n")), FixedMobilityCalls});
    IFileManager::Get().MakeDirectory(*BackupDirectory, true);
    if (!FFileHelper::SaveStringToFile(Status, *(BackupDirectory / TEXT("Report.txt"))))
    {
        bRepairIncomplete = true;
        Status += TEXT("\nThe repair report could not be saved.");
    }
    BlueprintCodeAnalysisReport = Status;
    bLastOperationSucceeded = !bRepairIncomplete;
    return bLastOperationSucceeded;
}
