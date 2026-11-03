/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "GameFramework/Pawn.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/MessageDialog.h"
#include "BlueprintNativizationSubsystem.h"
#include "StructInitialization.h"
#include "EngineAssetPolicy.h"
#include "NativeApiPreflight.h"
#include "IHotReload.h"
#include "NiagaraDefaultGeneration.h"
#include "NativeEventGeneration.h"
#include "NiagaraComponent.h"
#include "RetainedBlueprintGeneration.h"
#include "GeneratedSubsystemRuntimeGeneration.h"
#include "GameFramework/GameUserSettings.h"
#include "GeneratedComponentInputGeneration.h"
#include "NativeApiCatalog.h"
#include "Internationalization/Regex.h"
#include "BlueprintNativizationUniqueNameSubsystem.h"
#include "Misc/ScopeExit.h"
#include "NativeGenerationContext.h"
#include "UObject/StructOnScope.h"
#include "GenerationGraphContext.h"
#include "BlueprintNativizationV2.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/UnrealType.h"
#include "Components/PrimitiveComponent.h"
#include "UObject/ObjectMacros.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Knot.h"
#include "K2Node_SetFieldsInStruct.h"
#include "K2Node_Event.h"
#include "K2Node_ActorBoundEvent.h"
#include "K2Node_ComponentBoundEvent.h"
#include "GameFramework/PlayerController.h"
#include "TranslatorObjects/InputEventTranslatorObject.h"
#include "TranslatorObjects/LiteralTranslatorObject.h"
#include "K2Node_Tunnel.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Message.h"
#include "K2Node_CallParentFunction.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_VariableGet.h"
#include "K2Node_TemporaryVariable.h"

#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_Composite.h"
#include "K2Node_AssignmentStatement.h"
#include "SourceCodeNavigation.h"
#include "Kismet/KismetSystemLibrary.h"
#include "BlueprintNativizationLibrary.h"
#include "K2Node_Timeline.h"
#include "K2Node_Literal.h"
#include "Components/TimelineComponent.h"
#include "Engine/TimelineTemplate.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SimpleConstructionScript.h"
#include "BlueprintNativizationSettings.h"
#include "Engine/UserDefinedEnum.h"
#include "StructUtils/UserDefinedStruct.h"
#include "StructUtils/InstancedStruct.h"

#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UObjectIterator.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "ObjectTools.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Subsystems/Subsystem.h"
#include "BlueprintEditorLibrary.h"
#include "K2Node_EnhancedInputAction.h"
#include "NativeFunctionExpressions.h"
#include "InputAction.h"
#include "GeneratedCodeCleanup.h"
#include "FileHelpers.h"
#include "Components/ChildActorComponent.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

static TSet<FString> ReservedGeneratedHeaderNames;

static void RefreshReservedGeneratedHeaderNames()
{
    ReservedGeneratedHeaderNames.Reset();
    const FString ModuleName = UBlueprintNativizationSettingsLibrary::GetModuleName();
    TArray<FString> ManifestFiles;
    IFileManager::Get().FindFilesRecursive(ManifestFiles, *(FPaths::ProjectIntermediateDir() / TEXT("Build")), TEXT("*.uhtmanifest"), true, false);
    for (const FString& ManifestFile : ManifestFiles)
    {
        FString Content;
        TSharedPtr<FJsonObject> Manifest;
        const TArray<TSharedPtr<FJsonValue>>* Modules = nullptr;
        if (!FFileHelper::LoadFileToString(Content, *ManifestFile) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Content), Manifest) ||
            !Manifest->TryGetArrayField(TEXT("Modules"), Modules))
        {
            continue;
        }
        for (const TSharedPtr<FJsonValue>& ModuleValue : *Modules)
        {
            const TSharedPtr<FJsonObject> Module = ModuleValue->AsObject();
            if (Module->GetStringField(TEXT("Name")) == ModuleName)
            {
                continue;
            }
            for (const FString& Category : {TEXT("ClassesHeaders"), TEXT("PublicHeaders"), TEXT("InternalHeaders"), TEXT("PrivateHeaders")})
            {
                const TArray<TSharedPtr<FJsonValue>>* Headers = nullptr;
                if (Module->TryGetArrayField(Category, Headers))
                {
                    for (const TSharedPtr<FJsonValue>& Header : *Headers)
                    {
                        ReservedGeneratedHeaderNames.Add(FPaths::GetBaseFilename(Header->AsString()).ToLower());
                    }
                }
            }
        }
    }
    for (TObjectIterator<UField> Field; Field; ++Field)
    {
        if (Field->GetOutermost()->GetName() != TEXT("/Script/") + ModuleName)
        {
            const FString Header = Field->GetMetaData(TEXT("ModuleRelativePath"));
            if (!Header.IsEmpty())
            {
                ReservedGeneratedHeaderNames.Add(FPaths::GetBaseFilename(Header).ToLower());
            }
        }
    }
}

static int32 PrepareDefaultOnlyChildren(TArray<UObject*>& Objects)
{
    if (!GetDefault<UBlueprintNativizationV2EditorSettings>()->bReplaceDefaultOnlyChildrenWithParents)
    {
        return 0;
    }
    TSet<UClass*> SelectedClasses;
    for (UObject* Object : Objects)
    {
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
        {
            SelectedClasses.Add(Blueprint->GeneratedClass);
        }
        else if (UClass* Class = Cast<UClass>(Object))
        {
            SelectedClasses.Add(Class);
        }
    }
    return Objects.RemoveAll([&SelectedClasses](UObject* Object)
    {
        UBlueprint* Blueprint = Cast<UBlueprint>(Object);
        if (UClass* Class = Cast<UClass>(Object))
        {
            Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy);
        }
        if (!Blueprint || Blueprint->BlueprintType != BPTYPE_Normal || !Blueprint->GeneratedClass ||
            !FBlueprintEditorUtils::IsDataOnlyBlueprint(Blueprint))
        {
            return false;
        }
        UClass* Class = Blueprint->GeneratedClass;
        UClass* ParentClass = Class->GetSuperClass();
        for (UClass* SelectedClass : SelectedClasses)
        {
            if (SelectedClass != Class && SelectedClass->IsChildOf(Class))
            {
                return false;
            }
        }
        if (!ParentClass || (!ParentClass->HasAnyClassFlags(CLASS_Native) && !SelectedClasses.Contains(ParentClass)))
        {
            return false;
        }
        const EClassFlags RelevantFlags = CLASS_Abstract | CLASS_DefaultToInstanced | CLASS_EditInlineNew |
            CLASS_NotPlaceable | CLASS_Const | CLASS_Config | CLASS_PerObjectConfig | CLASS_DefaultConfig |
            CLASS_Transient | CLASS_Deprecated | CLASS_Hidden | CLASS_HideDropDown | CLASS_CollapseCategories |
            CLASS_GlobalUserConfig | CLASS_ProjectUserConfig | CLASS_PerPlatformConfig | CLASS_ConfigDoNotCheckDefaults;
        if ((Class->ClassFlags & RelevantFlags) != (ParentClass->ClassFlags & RelevantFlags) ||
            Class->ClassConfigName != ParentClass->ClassConfigName || Class->ClassWithin != ParentClass->ClassWithin ||
            !Blueprint->ComponentClassOverrides.IsEmpty() ||
            (Blueprint->SimpleConstructionScript && !Blueprint->SimpleConstructionScript->GetAllNodes().IsEmpty()) ||
            (Blueprint->InheritableComponentHandler && !Blueprint->InheritableComponentHandler->IsEmpty()))
        {
            return false;
        }
        UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(Class);
        if (GeneratedClass && (!GeneratedClass->DynamicBindingObjects.IsEmpty() || !GeneratedClass->Timelines.IsEmpty()))
        {
            return false;
        }
        return true;
    });
}
FNativizationModuleResult UNativizationV2Subsystem::RunNativizationForAllObjects(TArray<UObject*> InputTargets,
	bool bSaveToFile,
	FString InputOutputFolder,
	bool bTransformOnlyOneFileCode, bool HotReloadAndReplace, bool MergeWithExistModule,
	bool SaveCache, FString SaveCachePath, bool bContinueOnError
)
{
	FString ModuleName = UBlueprintNativizationSettingsLibrary::GetModuleName(); 

	FNativizationModuleResult NativizationModuleResult;

	const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();

	if (bIsNativizing)
	{
		UE_LOG(LogTemp, Warning, TEXT("RunNativization: Still Other Nativization In Process"));
		return NativizationModuleResult;
	}

    for (UObject* Object : InputTargets)
    {
        if (!Object)
        {
            const FString Error = TEXT("Source validation failed for None: the input object is null.");
            UE_LOG(LogTemp, Error, TEXT("%s"), *Error);
            NativizationModuleResult.GenerationErrors.Add(Error);
            if (!bContinueOnError)
            {
                return NativizationModuleResult;
            }
            NativizationModuleResult.SkippedObjects.AddUnique(nullptr);
        }
    }
    InputTargets.RemoveAll([](UObject* Object)
    {
        return !Object;
    });
    const FString SourceError = UBlueprintNativizationLibrary::ValidateBlueprintSources(InputTargets);
    if (!SourceError.IsEmpty() && !bContinueOnError)
    {
        UE_LOG(LogTemp, Warning, TEXT("%s"), *SourceError);
        return NativizationModuleResult;
    }

	TGuardValue<bool> GenerationGuard(bIsNativizing, true);
    RefreshReservedGeneratedHeaderNames();
    UBlueprintNativizationUniqueNameSubsystem* Names = GEngine->GetEngineSubsystem<UBlueprintNativizationUniqueNameSubsystem>();
    FNativeGenerationContext::BeginGeneration();
    Names->BeginNameLookup();
    ON_SCOPE_EXIT
    {
        Names->EndNameLookup();
    };

    if (!FNativeApiCatalog::GetErrors().IsEmpty())
    {
        NativizationModuleResult.GenerationErrors.Append(FNativeApiCatalog::GetErrors());
        return NativizationModuleResult;
    }

	TArray<FNativizationCode> NativizationCodesResult;
    int32 SkippedDefaultOnlyChildren = 0;

	LoadTranslatorObjects();

	if (!InputTargets.IsValidIndex(0))
	{
		UE_LOG(LogTemp, Warning, TEXT("RunNativization: Target is null"));
		return NativizationModuleResult;
	}

	TArray<UObject*> GenerationObjects = InputTargets;
	if (!bTransformOnlyOneFileCode)
	{
		TSet<UObject*> Dependencies;

		TArray<TSubclassOf<UObject>> IgnoreClassToRefGenerate;
		for (TSoftClassPtr<UObject> SoftClass : Settings->IgnoreClassToRefGenerate)
		{
			SoftClass.LoadSynchronous();
			IgnoreClassToRefGenerate.Add(SoftClass.Get());
		}

		TArray<UObject*> IgnoreAssetsToRefGenerate;
		for (TSoftObjectPtr<UObject> SoftRef : Settings->IgnoreAssetsToRefGenerate)
		{
			SoftRef.LoadSynchronous();
			IgnoreAssetsToRefGenerate.Add(SoftRef.Get());
		}

		for (UObject* InputTarget : InputTargets)
		{
			Dependencies.Append(UBlueprintNativizationLibrary::GetAllDependencies(InputTarget, IgnoreClassToRefGenerate, IgnoreAssetsToRefGenerate));
		}

		TSet<UObject*> FilteredDependencies;

		for (UObject* Object : Dependencies)
		{
			if (UClass* Class = Cast<UClass>(Object))
			{
				if (!(Class->ClassFlags & CLASS_Native))
				{
					FilteredDependencies.Add(Object);
				}
			}
			else
			{
				FilteredDependencies.Add(Object);
			}
		}

		GenerationObjects = FilteredDependencies.Array();
	}

    TSet<UObject*> FailedObjects;
    TMap<UObject*, TSet<UObject*>> ObjectDependencies;
    auto RecordObjectError = [&](UObject* Object, const FString& Stage, const FString& OriginalError)
    {
        const FString Error = FString::Format(TEXT("{0} failed for {1}: {2}"),
            {Stage, GetPathNameSafe(Object), OriginalError});
        NativizationModuleResult.GenerationErrors.Add(Error);
        UE_LOG(LogTemp, Error, TEXT("%s"), *Error);
    };
    if (bContinueOnError)
    {
        TArray<UClass*> ExcludedClasses;
        for (const TSoftClassPtr<UObject>& ExcludedClass : Settings->IgnoreClassToRefGenerate)
        {
            if (UClass* LoadedClass = ExcludedClass.LoadSynchronous())
            {
                ExcludedClasses.Add(LoadedClass);
            }
        }
        for (UObject*& Object : GenerationObjects)
        {
            if (UClass* Class = Cast<UClass>(Object))
            {
                if (UBlueprint* Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy))
                {
                    Object = Blueprint;
                }
            }
            if (!Object || (!Object->IsA<UBlueprint>() && !Object->IsA<UUserDefinedStruct>() && !Object->IsA<UUserDefinedEnum>()))
            {
                FailedObjects.Add(Object);
                RecordObjectError(Object, TEXT("Source validation"), TEXT("Only Blueprints, user-defined structures and enumerations can be converted."));
            }
            else if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
            {
                if (Blueprint->GeneratedClass)
                {
                    for (UClass* ExcludedClass : ExcludedClasses)
                    {
                        if (Blueprint->GeneratedClass->IsChildOf(ExcludedClass))
                        {
                            FailedObjects.Add(Object);
                            RecordObjectError(Object, TEXT("Source validation"),
                                TEXT("The selected Blueprint type is excluded: ") + ExcludedClass->GetPathName());
                            break;
                        }
                    }
                }
            }
        }
    }
    auto ExcludeFailedDependencies = [&]()
    {
        bool bChanged = true;
        while (bChanged)
        {
            bChanged = false;
            for (UObject* Object : GenerationObjects)
            {
                if (FailedObjects.Contains(Object))
                {
                    continue;
                }
                TSet<UObject*>* Dependencies = ObjectDependencies.Find(Object);
                if (!Dependencies)
                {
                    TSet<UObject*> CollectedDependencies;
                    UBlueprintNativizationLibrary::CollectDependencies(Object, CollectedDependencies);
                    ObjectDependencies.Add(Object, MoveTemp(CollectedDependencies));
                    Dependencies = ObjectDependencies.Find(Object);
                }
                for (UObject* Dependency : *Dependencies)
                {
                    if (FailedObjects.Contains(Dependency))
                    {
                        FailedObjects.Add(Object);
                        const FString Error = FString::Format(TEXT("Dependency exclusion for {0}: depends on failed object {1}."),
                            {Object->GetPathName(), Dependency->GetPathName()});
                        NativizationModuleResult.GenerationErrors.Add(Error);
                        UE_LOG(LogTemp, Warning, TEXT("%s"), *Error);
                        bChanged = true;
                        break;
                    }
                }
            }
        }
        GenerationObjects.RemoveAll([&](UObject* Object)
        {
            if (FailedObjects.Contains(Object))
            {
                NativizationModuleResult.SkippedObjects.AddUnique(Object);
                return true;
            }
            return false;
        });
    };
    if (bContinueOnError && !SourceError.IsEmpty())
    {
        for (UObject* Object : GenerationObjects)
        {
            const FString ObjectError = UBlueprintNativizationLibrary::ValidateBlueprintSources({Object});
            if (!ObjectError.IsEmpty())
            {
                FailedObjects.Add(Object);
                RecordObjectError(Object, TEXT("Source validation"), ObjectError);
            }
        }
        ExcludeFailedDependencies();
    }
    else if (bContinueOnError && !FailedObjects.IsEmpty())
    {
        ExcludeFailedDependencies();
    }
    TArray<UObject*> GeneratedObjects;
    bool bRetryGeneration = true;
    bool bFirstGeneration = true;
    while (bRetryGeneration && !GenerationObjects.IsEmpty())
    {
        bRetryGeneration = false;
        ResetNameSubsystem();
        if (!bFirstGeneration)
        {
            FNativeGenerationContext::BeginGeneration();
        }
        bFirstGeneration = false;
        GeneratedObjects = GenerationObjects;
        GeneratedObjects.RemoveAll([](UObject* Object)
        {
            UScriptStruct* Structure = Cast<UScriptStruct>(Object);
            if (FRetainedBlueprintGeneration::ShouldPreserveStructureAsset(Structure))
            {
                UE_LOG(LogTemp, Display, TEXT("Retaining the Blueprint structure asset with serialized external defaults: %s"), *Structure->GetPathName());
                return true;
            }
            return false;
        });
        SkippedDefaultOnlyChildren = PrepareDefaultOnlyChildren(GeneratedObjects);
        PreloadAllNamesForObjects(GeneratedObjects);
        const int32 SharedErrorCount = FNativeApiCatalog::GetErrors().Num();
        NativizationCodesResult.Reset();
        for (int32 ObjectIndex = 0; ObjectIndex < GeneratedObjects.Num(); ++ObjectIndex)
        {
            UObject* Object = GeneratedObjects[ObjectIndex];
            if (OperationProgress && !OperationProgress(ObjectIndex, GeneratedObjects.Num(), Object->GetPathName()))
            {
                return NativizationModuleResult;
            }
            const int32 ErrorCount = FNativeApiCatalog::GetErrors().Num();
            FNativizationCode Code = GenerateCodeForObject(Object, TEXT("None"));
            if (bContinueOnError && SharedErrorCount == 0 &&
                (FNativeApiCatalog::GetErrors().Num() > ErrorCount ||
                    (Code.OutputHeaderString.IsEmpty() && Code.OutputCppString.IsEmpty())))
            {
                for (int32 ErrorIndex = ErrorCount; ErrorIndex < FNativeApiCatalog::GetErrors().Num(); ++ErrorIndex)
                {
                    RecordObjectError(Object, TEXT("Generation"), FNativeApiCatalog::GetErrors()[ErrorIndex]);
                }
                if (FNativeApiCatalog::GetErrors().Num() == ErrorCount)
                {
                    RecordObjectError(Object, TEXT("Generation"), TEXT("The object produced no source code."));
                }
                FNativeApiCatalog::DiscardErrorsAfter(ErrorCount);
                FailedObjects.Add(Object);
                bRetryGeneration = true;
                continue;
            }
            NativizationCodesResult.Add(MoveTemp(Code));
        }
        if (bRetryGeneration)
        {
            ExcludeFailedDependencies();
        }
    }
    if (GenerationObjects.IsEmpty())
    {
        NativizationCodesResult.Reset();
        GeneratedObjects.Reset();
        SkippedDefaultOnlyChildren = 0;
        ResetNameSubsystem();
        FNativeGenerationContext::BeginGeneration();
    }
    NativizationModuleResult.ModuleCode = GenerateUnrealModule(ModuleName, GeneratedObjects);
    NativizationModuleResult.ModuleBuildCsCode = GenerateUnrealBuildCsModule(ModuleName, GeneratedObjects);

    if (SkippedDefaultOnlyChildren > 0)
    {
        UE_LOG(LogTemp, Display, TEXT("%s"), *FString::Format(TEXT("Retained {0} default-only Blueprint children as serialized configuration classes."), {SkippedDefaultOnlyChildren}));
    }
	if (Settings->bRemoveUnrealPrefixesFromFileNames)
    {
        TSet<FString> FileNames;
        FileNames.Add(ModuleName.ToLower());
        for (const FNativizationCode& Code : NativizationCodesResult)
        {
            const FString NormalizedFileName = Code.ObjectName.ToLower();
            if (FileNames.Contains(NormalizedFileName))
            {
                FNativeApiCatalog::AddError(FString::Format(
                    TEXT("Generated filename collision after removing Unreal prefixes: {0}. Rename the conflicting asset or disable filename prefix removal."),
                    {Code.ObjectName}));
            }
            FileNames.Add(NormalizedFileName);
        }
    }
    for (const FString& Error : FNativeApiCatalog::GetErrors())
    {
        NativizationModuleResult.GenerationErrors.AddUnique(Error);
    }
    NativizationModuleResult.Sucsess = (!NativizationCodesResult.IsEmpty() || SkippedDefaultOnlyChildren > 0) && FNativeApiCatalog::GetErrors().IsEmpty();
    NativizationModuleResult.bPartialSuccess = NativizationModuleResult.Sucsess && !NativizationModuleResult.SkippedObjects.IsEmpty();
	NativizationModuleResult.NativizationCodes = NativizationCodesResult;

	if (bSaveToFile && NativizationModuleResult.Sucsess)
	{
		SaveNativizationResult(NativizationModuleResult, InputOutputFolder, InputOutputFolder, MergeWithExistModule);
		if (SaveCache)
		{
			CreateJSONCache(SaveCachePath);
		}

		if (HotReloadAndReplace)
		{
			TArray<UObject*> AssetsToClose;
			for (FNativizationCode NativizationCode : NativizationModuleResult.NativizationCodes)
			{
				if (NativizationCode.Object)
				{
					AssetsToClose.Add(NativizationCode.Object);
				}
			}

			UBlueprintNativizationLibrary::CloseTabsWithSpecificAssets(AssetsToClose);

			for (UObject* Asset : AssetsToClose)
			{
				if (Asset)
				{
					Asset->MarkAsGarbage();
				}
			}
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

			auto& HotReloadModule = IHotReloadModule::Get();
			HotReloadModule.RecompileModule(
				*ModuleName,
				*GLog,
				ERecompileModuleFlags::ReloadAfterRecompile
			);

			ReplaceBlueprintTypesAfterHotReload(UBlueprintNativizationLibrary::GetAllRegisterUniqueFieldsInNameSubsistem());
		}

	}

	bIsNativizing = false;
	return NativizationModuleResult;
}


static FString GetGeneratedFileName(UObject* Object)
{
    UField* Field = Cast<UField>(Object);
    if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
    {
        Field = Blueprint->GeneratedClass;
    }
    FString FileName = UBlueprintNativizationLibrary::GetUniqueFieldName(Field);
    FString Prefix;
    if (const UClass* Class = Cast<UClass>(Field))
    {
        if (!Class->HasAnyClassFlags(CLASS_Interface))
        {
            Prefix = Class->GetPrefixCPP();
        }
    }
    else if (Field->IsA<UUserDefinedStruct>())
    {
        Prefix = TEXT("F");
    }
    else if (Field->IsA<UUserDefinedEnum>())
    {
        Prefix = TEXT("E");
    }
    if (GetDefault<UBlueprintNativizationV2EditorSettings>()->bRemoveUnrealPrefixesFromFileNames &&
        !Prefix.IsEmpty() && FileName.Len() > Prefix.Len())
    {
        FileName.RemoveFromStart(Prefix, ESearchCase::CaseSensitive);
    }
    while (ReservedGeneratedHeaderNames.Contains(FileName.ToLower()))
    {
        const FString ModuleName = UBlueprintNativizationSettingsLibrary::GetModuleName();
        if (ModuleName.IsEmpty())
        {
            FNativeApiCatalog::AddError(TEXT("A module name is required to resolve a generated header collision: ") + FileName);
            break;
        }
        FileName = ModuleName + FileName;
    }
    return FileName;
}

FNativizationCode UNativizationV2Subsystem::RunNativizationForOneFunctionBlueprint(UBlueprint* InputTarget, FString FunctionName)
{
	FNativizationCode OutContent;


	if (bIsNativizing)
	{
		UE_LOG(LogTemp, Warning, TEXT("RunNativization: Still Other Nativization In Process"));
		return  OutContent;
	}

    const FString SourceError = UBlueprintNativizationLibrary::ValidateBlueprintSources({ InputTarget });
    if (!SourceError.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("%s"), *SourceError);
        return OutContent;
    }

	TGuardValue<bool> GenerationGuard(bIsNativizing, true);
    RefreshReservedGeneratedHeaderNames();
    UBlueprintNativizationUniqueNameSubsystem* Names = GEngine->GetEngineSubsystem<UBlueprintNativizationUniqueNameSubsystem>();
    FNativeGenerationContext::BeginGeneration();
    Names->BeginNameLookup();
    ON_SCOPE_EXIT
    {
        Names->EndNameLookup();
    };

    if (!FNativeApiCatalog::GetErrors().IsEmpty())
    {
        return OutContent;
    }

	PreloadAllNamesForObjects(TArray<UObject*>({ InputTarget }));

	LoadTranslatorObjects();

	OutContent.Object = InputTarget;
	OutContent.ObjectName = GetGeneratedFileName(InputTarget);
	GenerateBlueprintCode(InputTarget, FunctionName, OutContent);
	FNativeGenerationContext::CompleteHeader(OutContent.OutputHeaderString);

	bIsNativizing = false;
    if (!FNativeApiCatalog::GetErrors().IsEmpty())
    {
        return FNativizationCode();
    }
	return OutContent;
}

void UNativizationV2Subsystem::PrepareNewModuleToWork()
{
	const FString ModuleName = UBlueprintNativizationSettingsLibrary::GetModuleName();

	FNativizationModuleResult NativizationModuleResult;
	NativizationModuleResult.ModuleCode = GenerateUnrealModule(ModuleName, {});
	NativizationModuleResult.ModuleBuildCsCode = GenerateUnrealBuildCsModule(ModuleName, {});

	FString OutputDirectory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), ModuleName);

	const FString ModuleFileHeaderPath = FPaths::Combine(OutputDirectory, ModuleName + TEXT(".h"));
	const FString ModuleFileCppPath = FPaths::Combine(OutputDirectory, ModuleName + TEXT(".cpp"));
	const FString BuildCsFilePath = FPaths::Combine(OutputDirectory, ModuleName + TEXT(".Build.cs"));

	bool bModuleExistsAndEqual = false;

	if (FPaths::DirectoryExists(OutputDirectory) &&
		FPaths::FileExists(ModuleFileHeaderPath) &&
		FPaths::FileExists(ModuleFileCppPath) &&
		FPaths::FileExists(BuildCsFilePath))
	{
		FString ExistingHeaderCode;
		FString ExistingCppCode;
		FString ExistingBuildCsCode;

		const FString& NewHeaderCode = NativizationModuleResult.ModuleCode.OutputHeaderString;
		const FString& NewCppCode = NativizationModuleResult.ModuleCode.OutputCppString;
		const FString& NewBuildCsCode = NativizationModuleResult.ModuleBuildCsCode;

		FFileHelper::LoadFileToString(ExistingHeaderCode, *ModuleFileHeaderPath);
		FFileHelper::LoadFileToString(ExistingCppCode, *ModuleFileCppPath);
		FFileHelper::LoadFileToString(ExistingBuildCsCode, *BuildCsFilePath);

		bModuleExistsAndEqual =
			ExistingHeaderCode == NewHeaderCode &&
			ExistingCppCode == NewCppCode &&
			ExistingBuildCsCode == NewBuildCsCode;

		if (bModuleExistsAndEqual)
		{
			UE_LOG(LogTemp, Log, TEXT("Модуль %s уже существует и идентичен — пропуск генерации."), *ModuleName);
			return;
		}
	}

	SaveNativizationResult(NativizationModuleResult, "", OutputDirectory, false);

	AddModuleToUProject(ModuleName);

	AddModuleToTargets(ModuleName);

	EAppReturnType::Type Result = FMessageDialog::Open(
		EAppMsgType::YesNo,
		FText::FromString(TEXT("Restart Project Please?"))
	);

	if (Result == EAppReturnType::Yes)
	{
		FUnrealEdMisc::Get().RestartEditor(false);
	}
}

void UNativizationV2Subsystem::AddModuleToUProject(const FString& ModuleName)
{
	FString ProjectPath =  FPaths::ProjectDir() + FApp::GetProjectName() + ".uproject";
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *ProjectPath))
	{
		UE_LOG(LogTemp, Error, TEXT("Не удалось прочитать .uproject файл."));
		return;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Ошибка при парсинге .uproject файла."));
		return;
	}

	// Получаем или создаём массив "Modules"
	TArray<TSharedPtr<FJsonValue>> ModulesArray;

	if (Root->HasTypedField<EJson::Array>(TEXT("Modules")))
	{
		ModulesArray = Root->GetArrayField(TEXT("Modules"));
	}
	else
	{
		Root->SetArrayField(TEXT("Modules"), ModulesArray); // пока пустой
	}

	// Проверяем, есть ли модуль
	bool bAlreadyExists = false;
    bool bDescriptorChanged = false;
	for (const auto& ModuleValue : ModulesArray)
	{
		TSharedPtr<FJsonObject> ModuleObj = ModuleValue->AsObject();
		if (!ModuleObj.IsValid())
			continue;

		FString Name;
		if (ModuleObj->TryGetStringField(TEXT("Name"), Name) && Name == ModuleName)
		{
			bAlreadyExists = true;
            FString LoadingPhase;
            if (!ModuleObj->TryGetStringField(TEXT("LoadingPhase"), LoadingPhase) ||
                LoadingPhase == TEXT("PostEngineInit") || LoadingPhase == TEXT("PostDefault"))
            {
                ModuleObj->SetStringField(TEXT("LoadingPhase"), TEXT("Default"));
                bDescriptorChanged = true;
            }
			break;
		}
	}

	if (!bAlreadyExists)
	{
		TSharedPtr<FJsonObject> NewModule = MakeShared<FJsonObject>();
		NewModule->SetStringField(TEXT("Name"), ModuleName);
		NewModule->SetStringField(TEXT("Type"), TEXT("Runtime"));
		NewModule->SetStringField(TEXT("LoadingPhase"), TEXT("Default"));

		ModulesArray.Add(MakeShared<FJsonValueObject>(NewModule));
		Root->SetArrayField(TEXT("Modules"), ModulesArray); // обновить

        bDescriptorChanged = true;
    }
    if (bDescriptorChanged)
    {
		FString UpdatedJson;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&UpdatedJson);
		if (FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
		{
			if (FFileHelper::SaveStringToFile(UpdatedJson, *ProjectPath))
			{
				UE_LOG(LogTemp, Log, TEXT("Добавлен модуль %s в .uproject"), *ModuleName);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Не удалось сохранить обновлённый .uproject"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Ошибка сериализации .uproject JSON"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Модуль %s уже присутствует в .uproject — пропуск добавления."), *ModuleName);
	}
}

int32 UNativizationV2Subsystem::FindConstructorClosingBraceIndex(const FString& Code)
{
	int32 CodeLen = Code.Len();
	int32 SignatureLineStart = INDEX_NONE;

	{
		TArray<FString> Lines;
		Code.ParseIntoArrayLines(Lines);

		for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
		{
			const FString& Line = Lines[LineIndex];
			if (Line.Contains(TEXT(": base(")) && Line.Contains(TEXT("(")) && Line.Contains(TEXT(")")))
			{
				SignatureLineStart = LineIndex;
				break;
			}
		}

		if (SignatureLineStart == INDEX_NONE)
		{
			return INDEX_NONE; 
		}
	}

	int32 Offset = 0;
	TArray<FString> AllLines;
	Code.ParseIntoArrayLines(AllLines);

	for (int32 i = 0; i <= SignatureLineStart; ++i)
	{
		Offset += AllLines[i].Len() + 1; // +1 на \n
	}

	int32 BraceStart = Code.Find(TEXT("{"), ESearchCase::IgnoreCase, ESearchDir::FromStart, Offset);
	if (BraceStart == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	int32 Depth = 0;
	for (int32 i = BraceStart; i < CodeLen; ++i)
	{
		TCHAR C = Code[i];
		if (C == '{')
		{
			++Depth;
		}
		else if (C == '}')
		{
			--Depth;
			if (Depth == 0)
			{
				return i;
			}
		}
	}

	return INDEX_NONE; // не нашли закрытие
}

void UNativizationV2Subsystem::AddModuleToTargets(const FString& ModuleName)
{
	const FString SourceDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"));

	TArray<FString> TargetFiles;
	IFileManager::Get().FindFilesRecursive(TargetFiles, *SourceDir, TEXT("*.Target.cs"), true, false);

	for (const FString& TargetPath : TargetFiles)
	{
		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *TargetPath))
			continue;

		if (Content.Contains(FString::Printf(TEXT("\"%s\""), *ModuleName)))
			continue;

		if (Content.Contains(ModuleName))
			return;

		int32 CloseBraceIndex = FindConstructorClosingBraceIndex(Content);
		if (CloseBraceIndex == INDEX_NONE)
		{
			UE_LOG(LogTemp, Warning, TEXT("Не удалось найти место вставки в %s"), *TargetPath);
			continue;
		}

		const FString ModuleLine = FString::Printf(TEXT("\tExtraModuleNames.AddRange( new string[] { \"%s\" } );\n"), *ModuleName);

		Content.InsertAt(CloseBraceIndex, ModuleLine);

		// Сохраняем
		if (FFileHelper::SaveStringToFile(Content, *TargetPath))
		{
			UE_LOG(LogTemp, Log, TEXT("Добавлен модуль %s в %s"), *ModuleName, *TargetPath);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Не удалось сохранить изменения в %s"), *TargetPath);
		}
	}
}

void UNativizationV2Subsystem::ResetNameSubsystem()
{
	UBlueprintNativizationLibrary::ResetNameSubsistem();
}

FNativizationCode UNativizationV2Subsystem::GenerateCodeForObject(UObject* InputTarget, FString FunctionName)
{
    FRetainedBlueprintGeneration::EndObject();
    const FString ExclusionReason = FEngineAssetPolicy::GetExclusionReason(InputTarget,
        GetDefault<UBlueprintNativizationV2EditorSettings>()->bExcludeEngineAssetsFromConversion);
    if (!ExclusionReason.IsEmpty())
    {
        UKismetSystemLibrary::PrintString(nullptr, ExclusionReason, false, true);
        return {};
    }
    UKismetSystemLibrary::PrintString(nullptr,
        FString::Format(TEXT("Generating C++ for {0}"), { GetPathNameSafe(InputTarget) }), false, true);
	FNativeGenerationContext::BeginObject();
	FNativizationCode NativizationCode;
	NativizationCode.Object = InputTarget;
	NativizationCode.ObjectName = GetGeneratedFileName(InputTarget);

	if (UBlueprint* Blueprint = Cast<UBlueprint>(InputTarget))
	{
		if (Blueprint->GeneratedClass->IsChildOf<UInterface>())
		{ 
			TSubclassOf<UInterface> InterfaceClass = Blueprint->GeneratedClass.Get();
			GenerateInterfaceCode(Blueprint, NativizationCode);
		}
		else
		{
			GenerateBlueprintCode(Blueprint, "None", NativizationCode);
		}
	}

	else if (UEnum* Enum = Cast<UEnum>(InputTarget))
	{
		NativizationCode.OutputHeaderString = ConvertUEnumToCppEnumDeclaration(Enum);
	}
	else if (UStruct* Struct = Cast<UStruct>(InputTarget))
	{
		NativizationCode.OutputHeaderString = ConvertUStructToCppStructDeclaration(Struct);
	}

	FNativeGenerationContext::CompleteHeader(NativizationCode.OutputHeaderString);
    FNativeGenerationContext::CompleteImplementation(NativizationCode.OutputCppString);
	return NativizationCode;
}

void UNativizationV2Subsystem::PrintAllK2Nodes(UBlueprint* InputTarget, FString FunctionName)
{
	EntryNodes = GenerateAllGenerateFunctionStruct(InputTarget, FunctionName);

	for (FGenerateFunctionStruct EntryNode : EntryNodes)
	{
		TArray<UK2Node*> Nodes = UBlueprintNativizationLibrary::GetAllExecuteNodesInFunction(EntryNode.Node, EntryNodes, "None");
		for (UK2Node* Node : Nodes)
		{
			/*
			if (UK2Node_Timeline* Timeline = Cast<UK2Node_Timeline>(Node))
			{
				UTimelineTemplate* TimelineTemplate = InputTarget->FindTimelineTemplateByVariableName(Timeline->TimelineName);
				TSet<UCurveBase*> InOutCurves;
				TimelineTemplate->GetAllCurves(InOutCurves);

				TArray<FName> FoundNames;
				TArray<UActorComponent*> Array = UBlueprintNativizationLibrary::GetClassComponentTemplates(FoundNames, InputTarget->GeneratedClass.Get(), UTimelineComponent::StaticClass(), false);

				for (UActorComponent* TimelineComponent : Array)
				{
					if (TimelineComponent->GetName() == Timeline->GetName())
					{
						UE_LOG(LogTemp, Log, TEXT("Node: %s"), *Node->GetName());
					}
				}
			}*/
			if (!Node) continue;


			UE_LOG(LogTemp, Log, TEXT("Node: %s"), *Node->GetName());
			UE_LOG(LogTemp, Log, TEXT("Node Class: %s"), *Node->GetClass()->GetName());
			if (UK2Node_CallFunction* CallFunction = Cast<UK2Node_CallFunction>(Node))
			{
				UE_LOG(LogTemp, Log, TEXT("Node Class: %s"), *CallFunction->GetFunctionName().ToString());
			}
		}
		TArray<UK2Node*> InputNodes = UBlueprintNativizationLibrary::GetAllInputNodes(Nodes);

		for (UK2Node* Node : InputNodes)
		{
			if (!Node) continue;

			UE_LOG(LogTemp, Log, TEXT("Input Node: %s"), *Node->GetName());
			UE_LOG(LogTemp, Log, TEXT("Input Node Class: %s"), *Node->GetClass()->GetName());
			if (UK2Node_CallFunction* CallFunction = Cast<UK2Node_CallFunction>(Node))
			{
				UE_LOG(LogTemp, Log, TEXT("Input Node Class: %s"), *CallFunction->GetFunctionName().ToString());
			}
		}

	}

	TArray<UEdGraph*> Graphs;
	InputTarget->GetAllGraphs(Graphs);

	for (UEdGraph* Graph : Graphs) // ��� UbergraphPages
	{
		if (Graph)
		{
			UE_LOG(LogTemp, Log, TEXT("%s : %s"),
				*Graph->GetName(),
				Graph->GetOuter() ? *Graph->GetOuter()->GetName() : TEXT("None"));
		}
	}

	for (TFieldIterator<UFunction> PropIt(InputTarget->GeneratedClass, EFieldIteratorFlags::ExcludeSuper); PropIt; ++PropIt)
	{
		const TArray<uint8>& Bytecode = PropIt->Script;
		UE_LOG(LogTemp, Log, TEXT("Bytecode length: %d"), Bytecode.Num());
		UE_LOG(LogTemp, Log, TEXT("Function Name %s"), *PropIt->GetName());
	}
}

void UNativizationV2Subsystem::LoadTranslatorObjects()
{
	TranslatorObjects.Reset();

	const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();
	if (!Settings)
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to get UBlueprintNativizationV2EditorSettings."));
		return;
	}

	for (const TSubclassOf<UTranslatorBPToCppObject>& TranslatorClass : Settings->TranslatorBPToCPPObjects)
	{
		if (!TranslatorClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("Null entry in TranslatorBPToCPPObjects."));
			continue;
		}

		UTranslatorBPToCppObject* CDO = Cast<UTranslatorBPToCppObject>(TranslatorClass->GetDefaultObject());
		if (CDO)
		{
			TranslatorObjects.Add(CDO);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Could not get CDO for class %s"), *TranslatorClass->GetName());
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Loaded %d translator objects."), TranslatorObjects.Num());
}

bool UNativizationV2Subsystem::CreateFile(const FString& FilePath, const FString& Content)
{
	FString ExistingContent;
	if (FFileHelper::LoadFileToString(ExistingContent, *FilePath) && ExistingContent == Content)
	{
		return true;
	}
	EnsureDirectory(FPaths::GetPath(FilePath));
	return FFileHelper::SaveStringToFile(Content, *FilePath);
}

void UNativizationV2Subsystem::EnsureDirectory(const FString& Directory)
{
	if (!FPlatformFileManager::Get().GetPlatformFile().DirectoryExists(*Directory))
	{
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*Directory);
	}
}
bool UNativizationV2Subsystem::SaveNativizationResult(FNativizationModuleResult NativizationModuleResult, FString Directory, FString& OutputDirectory, bool MergeWithExistModule)
{
	bool bSaved = true;
	if (Directory.IsEmpty())
	{
		Directory = FPaths::ProjectDir();
		OutputDirectory = FPaths::Combine(Directory, TEXT("Source"), NativizationModuleResult.ModuleCode.ObjectName);
	}
	else
	{
		OutputDirectory = FPaths::Combine(Directory, TEXT("Nativized"), NativizationModuleResult.ModuleCode.ObjectName);
	}
	IFileManager::Get().MakeDirectory(*OutputDirectory, true);

	if (!NativizationModuleResult.ModuleBuildCsCode.IsEmpty())
	{
		const FString BuildCsPath = FPaths::Combine(OutputDirectory, NativizationModuleResult.ModuleCode.ObjectName + TEXT(".Build.cs"));

		if (MergeWithExistModule && FPaths::FileExists(BuildCsPath))
        {
            FString ExistingContent;
            if (!FFileHelper::LoadFileToString(ExistingContent, *BuildCsPath))
            {
                return false;
            }
            const FRegexPattern DependenciesPattern(TEXT("PrivateDependencyModuleNames(?:\\s*=|\\s*\\.AddRange\\s*\\()\\s*new\\s*string\\s*\\[\\]\\s*\\{([^}]*)\\}"));
            FRegexMatcher ExistingMatcher(DependenciesPattern, ExistingContent);
            FRegexMatcher GeneratedMatcher(DependenciesPattern, NativizationModuleResult.ModuleBuildCsCode);
            if (!ExistingMatcher.FindNext() || !GeneratedMatcher.FindNext())
            {
                return false;
            }
            TArray<FString> Dependencies;
            ExistingMatcher.GetCaptureGroup(1).ParseIntoArray(Dependencies, TEXT(","), true);
            TArray<FString> GeneratedDependencies;
            GeneratedMatcher.GetCaptureGroup(1).ParseIntoArray(GeneratedDependencies, TEXT(","), true);
            Dependencies.Append(GeneratedDependencies);
            TArray<FString> UniqueDependencies;
            for (FString Dependency : Dependencies)
            {
                Dependency.TrimStartAndEndInline();
                if (!Dependency.IsEmpty())
                {
                    UniqueDependencies.AddUnique(Dependency);
                }
            }
            FString BuildCode = NativizationModuleResult.ModuleBuildCsCode;
            BuildCode = BuildCode.Left(GeneratedMatcher.GetCaptureGroupBeginning(1)) + FString::Join(UniqueDependencies, TEXT(", ")) + BuildCode.Mid(GeneratedMatcher.GetCaptureGroupEnding(1));
            bSaved = CreateFile(BuildCsPath, BuildCode) && bSaved;
        }
        else
        {
            bSaved = CreateFile(BuildCsPath, NativizationModuleResult.ModuleBuildCsCode) && bSaved;
        }
    }

    if (!NativizationModuleResult.ModuleCode.OutputCppString.IsEmpty())
	{
		const FString ModuleCppPath = FPaths::Combine(OutputDirectory, NativizationModuleResult.ModuleCode.ObjectName + TEXT(".cpp"));
		bSaved = CreateFile(ModuleCppPath, NativizationModuleResult.ModuleCode.OutputCppString) && bSaved;
	}

	if (!NativizationModuleResult.ModuleCode.OutputHeaderString.IsEmpty())
	{
		const FString ModuleHeaderPath = FPaths::Combine(OutputDirectory, NativizationModuleResult.ModuleCode.ObjectName + TEXT(".h"));
		bSaved = CreateFile(ModuleHeaderPath, NativizationModuleResult.ModuleCode.OutputHeaderString) && bSaved;
	}

	int32 SavedObjects = 0;
	for (const FNativizationCode& NativizationCode : NativizationModuleResult.NativizationCodes)
	{
		if (OperationProgress)
		{
			OperationProgress(SavedObjects, NativizationModuleResult.NativizationCodes.Num(), TEXT("Writing source files"));
		}
		++SavedObjects;
		if (!NativizationCode.Object) continue;

		FString PackagePath = FPackageName::LongPackageNameToFilename(NativizationCode.Object->GetOutermost()->GetName());

		const FString ContentToken = TEXT("Content/");
		int32 ContentIndex = PackagePath.Find(ContentToken, ESearchCase::IgnoreCase, ESearchDir::FromStart);
		if (ContentIndex != INDEX_NONE)
		{
			PackagePath = PackagePath.Mid(ContentIndex + 8);
		}

		int32 LastSlashIndex;
		if (PackagePath.FindLastChar('/', LastSlashIndex))
		{
			PackagePath = PackagePath.Left(LastSlashIndex);
		}

		const FString PathDirectory = FPaths::Combine(OutputDirectory, PackagePath);

		if (!NativizationCode.OutputHeaderString.IsEmpty())
		{
			const FString HeaderPath = FPaths::Combine(PathDirectory, NativizationCode.ObjectName + TEXT(".h"));
			bSaved = CreateFile(HeaderPath, NativizationCode.OutputHeaderString) && bSaved;
		}

		if (!NativizationCode.OutputCppString.IsEmpty())
		{
			const FString CppPath = FPaths::Combine(PathDirectory, NativizationCode.ObjectName + TEXT(".cpp"));
			bSaved = CreateFile(CppPath, NativizationCode.OutputCppString) && bSaved;
		}
	}


	return bSaved;
}

FNativizationCode UNativizationV2Subsystem::GenerateUnrealModule(const FString& ModuleName, TArray<UObject*> Targets)
{
	FNativizationCode NativizationCode;
	// Header
	NativizationCode.OutputHeaderString = FString::Format(TEXT(R"(
	#pragma once

	#include "Modules/ModuleManager.h"

	class F{0}Module : public IModuleInterface
	{
	public:
		virtual void StartupModule() override;
		virtual void ShutdownModule() override;
	};
	)"), { ModuleName });

	NativizationCode.OutputCppString = FString::Format(TEXT(R"(
	#include "{0}.h"
	#include "Modules/ModuleManager.h"

	IMPLEMENT_MODULE(F{0}Module, {0})

	void F{0}Module::StartupModule()
	{
		F{0}SubsystemRuntime::Startup();
	}

	void F{0}Module::ShutdownModule()
	{
		F{0}ComponentInputRuntime::Shutdown();
		F{0}SubsystemRuntime::Shutdown();
	}
	)"), { ModuleName });

	NativizationCode.ObjectName = ModuleName;
	const bool bContainsGameUserSettings = Targets.ContainsByPredicate([](UObject* Target)
	{
		const UBlueprint* Blueprint = Cast<UBlueprint>(Target);
		return Blueprint && Blueprint->GeneratedClass &&
			Blueprint->GeneratedClass->IsChildOf(UGameUserSettings::StaticClass());
	});
	if (bContainsGameUserSettings)
	{
		const FString ModuleHeaderInclude = FString::Format(TEXT("#include \"{0}.h\""), {ModuleName});
		NativizationCode.OutputCppString.InsertAt(
			NativizationCode.OutputCppString.Find(ModuleHeaderInclude) + ModuleHeaderInclude.Len(),
			TEXT("\n#include \"GameFramework/GameUserSettings.h\"\n#include \"Misc/ConfigCacheIni.h\""));
		const FString SettingsBootstrap = TEXT(R"(
        FString GameUserSettingsClassPath;
        if (GConfig->GetString(TEXT("/Script/Engine.Engine"), TEXT("GameUserSettingsClassName"), GameUserSettingsClassPath, GEngineIni))
        {
            if (UClass* SettingsClass = LoadClass<UGameUserSettings>(nullptr, *GameUserSettingsClassPath))
            {
                SettingsClass->GetDefaultObject();
            }
        }
)");
		NativizationCode.OutputCppString.ReplaceInline(
			*FString::Format(TEXT("\t\tF{0}SubsystemRuntime::Startup();"), {ModuleName}),
			*(SettingsBootstrap + FString::Format(TEXT("\t\tF{0}SubsystemRuntime::Startup();"), {ModuleName})));
	}
    FString SerializationBootstrap;
    const FString RedirectsName = ModuleName + TEXT("SerializationRedirects");
    for (UObject* Target : Targets)
    {
        UField* SourceField = Cast<UField>(Target);
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Target))
        {
            SourceField = Blueprint->GeneratedClass;
        }
        if (!SourceField)
        {
            continue;
        }
        FString ReflectionName = UBlueprintNativizationLibrary::GetUniqueFieldName(SourceField);
        if (SourceField->IsA<UStruct>())
        {
            ReflectionName.RightChopInline(1);
        }
        const FString NativePath = TEXT("/Script/") + ModuleName + TEXT(".") + ReflectionName;
        if (UStruct* Structure = Cast<UStruct>(SourceField))
        {
            for (TFieldIterator<FProperty> Property(Structure, EFieldIteratorFlags::ExcludeSuper); Property; ++Property)
            {
                const FString OriginalName = Property->GetName();
                const FString GeneratedName = UBlueprintNativizationLibrary::GetUniquePropertyName(*Property);
                if (OriginalName == GeneratedName || Property->HasAnyPropertyFlags(CPF_Transient))
                {
                    continue;
                }
                SerializationBootstrap += FString::Format(
                    TEXT("        {0}.Emplace(ECoreRedirectFlags::Type_Property, TEXT(\"{1}.{2}\"), TEXT(\"{1}.{3}\"));\n"),
                    {RedirectsName, NativePath, OriginalName.ReplaceCharWithEscapedChar(), GeneratedName});
            }
        }
        if (UEnum* Enumeration = Cast<UEnum>(SourceField))
        {
            SerializationBootstrap += FString::Format(
                TEXT("        {\n            FCoreRedirect& EnumerationRedirect = {0}.Emplace_GetRef(ECoreRedirectFlags::Type_Enum, TEXT(\"{1}\"), TEXT(\"{1}\"));\n"),
                {RedirectsName, NativePath});
            for (int32 Index = 0; Index < Enumeration->NumEnums(); ++Index)
            {
                const FString GeneratedName = ReflectionName + TEXT("::") + UBlueprintNativizationLibrary::GetEnumValueName(Enumeration, Enumeration->GetValueByIndex(Index));
                TArray<FString> OriginalNames = {Enumeration->GetNameByIndex(Index).ToString(), Enumeration->GetNameStringByIndex(Index)};
                for (const FString& OriginalName : OriginalNames)
                {
                    SerializationBootstrap += FString::Format(
                        TEXT("            EnumerationRedirect.ValueChanges.Add(TEXT(\"{0}\"), TEXT(\"{1}\"));\n"),
                        {OriginalName.ReplaceCharWithEscapedChar(), GeneratedName});
                }
            }
            SerializationBootstrap += TEXT("        }\n");
        }
    }
    if (!SerializationBootstrap.IsEmpty())
    {
        const FString ModuleHeaderInclude = FString::Format(TEXT("#include \"{0}.h\""), {ModuleName});
        NativizationCode.OutputCppString.InsertAt(
            NativizationCode.OutputCppString.Find(ModuleHeaderInclude) + ModuleHeaderInclude.Len(),
            TEXT("\n#include \"UObject/CoreRedirects.h\"\n"));
        SerializationBootstrap = TEXT("        TArray<FCoreRedirect> ") + RedirectsName + TEXT(";\n") + SerializationBootstrap;
        SerializationBootstrap += FString::Format(
            TEXT("        FCoreRedirects::AddRedirectList({0}, TEXT(\"{1}Serialization\"));\n"), {RedirectsName, ModuleName});
        NativizationCode.OutputCppString.ReplaceInline(
            *FString::Format(TEXT("\t\tF{0}SubsystemRuntime::Startup();"), {ModuleName}),
            *(SerializationBootstrap + FString::Format(TEXT("\t\tF{0}SubsystemRuntime::Startup();"), {ModuleName})));
    }
	const FNativizationCode SubsystemRuntime = FGeneratedSubsystemRuntimeGeneration::Generate(ModuleName);
	NativizationCode.OutputHeaderString += SubsystemRuntime.OutputHeaderString;
	NativizationCode.OutputCppString += SubsystemRuntime.OutputCppString;
	const FNativizationCode ComponentInputRuntime = FGeneratedComponentInputGeneration::Generate(ModuleName);
	NativizationCode.OutputHeaderString += ComponentInputRuntime.OutputHeaderString;
	NativizationCode.OutputCppString += ComponentInputRuntime.OutputCppString;

	return NativizationCode;
};

FString UNativizationV2Subsystem::GenerateUnrealBuildCsModule(const FString& ModuleName, TArray<UObject*> Targets)
{
	TSet<FString> CSPrivateDependencyModuleNames = { TEXT("\"GameplayTags\"") };
	TSet<FString> CSIgnoreDependencyModuleNames = { "\"Core\"", "\"CoreUObject\"", "\"Engine\"", "\"InputCore\"" };
	TSet<FString> PublicDependencies = CSIgnoreDependencyModuleNames;
    TSet<FString> LegacyIncludeModules;
	auto AddNativeModule = [&](const UField* Field)
	{
        if (const UFunction* Function = Cast<UFunction>(Field))
        {
            FString Expression;
            TSet<FString> Includes;
            TSet<FString> Modules;
            if (FNativeApiCatalog::FindPublicForwarding(const_cast<UFunction*>(Function), Expression, Includes, Modules))
            {
                for (const FString& Dependency : Modules)
                {
                    PublicDependencies.Add(FString::Format(TEXT("\"{0}\""), {Dependency}));
                }
                return;
            }
            Field = Function->GetOwnerClass();
        }
        if (const UClass* Class = Cast<UClass>(Field))
        {
            Field = FRetainedBlueprintGeneration::GetStorageClass(const_cast<UClass*>(Class));
        }
        if (const UScriptStruct* Structure = Cast<UScriptStruct>(Field))
        {
            if (!UBlueprintNativizationSettingsLibrary::FindStructTranslator(const_cast<UScriptStruct*>(Structure)))
            {
                for (const FString& Dependency : FNativeApiCatalog::GetValueTypeModules(const_cast<UScriptStruct*>(Structure)))
                {
                    if (Dependency != ModuleName)
                    {
                        PublicDependencies.Add(FString::Format(TEXT("\"{0}\""), {Dependency}));
                    }
                }
            }
        }
		if (Field && Field->GetOutermost()->GetName().StartsWith(TEXT("/Script/")))
		{
			const FString NativeModule = FPackageName::GetShortName(Field->GetOutermost());
			if (NativeModule != ModuleName)
			{
				PublicDependencies.Add(FString::Format(TEXT("\"{0}\""), { NativeModule }));
                const FString NativeModuleDirectory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), NativeModule);
                if (FPaths::FileExists(FPaths::Combine(NativeModuleDirectory, NativeModule + TEXT(".Build.cs"))) &&
                    !FPaths::DirectoryExists(FPaths::Combine(NativeModuleDirectory, TEXT("Public"))))
                {
                    LegacyIncludeModules.Add(NativeModule);
                }
			}
		}
	};
	auto AddPinModules = [&](const FEdGraphPinType& PinType)
	{
		AddNativeModule(Cast<UField>(PinType.PinSubCategoryObject.Get()));
		AddNativeModule(Cast<UField>(PinType.PinValueType.TerminalSubCategoryObject.Get()));
	};
	auto AddPropertyModules = [&](UStruct* Structure)
	{
		for (TFieldIterator<FProperty> Property(Structure, EFieldIteratorFlags::ExcludeSuper); Property; ++Property)
		{
			FEdGraphPinType PinType;
			GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(*Property, PinType);
			AddPinModules(PinType);
		}
	};

	for (UObject* Target : Targets)
	{
		if (UStruct* Structure = Cast<UStruct>(Target))
		{
			AddPropertyModules(Structure);
		}
		TArray<UK2Node*> AnswerNodes;
		if (UBlueprint* Blueprint = Cast<UBlueprint>(Target))
		{
			AddNativeModule(Blueprint->ParentClass);
			AddPropertyModules(Blueprint->GeneratedClass);
			for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
			{
				AddNativeModule(Interface.Interface);
			}
			for (TFieldIterator<UFunction> Function(Blueprint->GeneratedClass, EFieldIteratorFlags::ExcludeSuper); Function; ++Function)
			{
				AddPropertyModules(*Function);
			}
			TArray<UEdGraph*> SourceGraphs;
			Blueprint->GetAllGraphs(SourceGraphs);
			for (UEdGraph* SourceGraph : SourceGraphs)
			{
				for (UEdGraphNode* SourceNode : SourceGraph->Nodes)
				{
					if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(SourceNode))
					{
						AddNativeModule(Call->GetTargetFunction());
					}
					for (UEdGraphPin* Pin : SourceNode->Pins)
					{
                        UK2Node_CallFunction* StaticCall = Cast<UK2Node_CallFunction>(SourceNode);
                        if (Pin->PinName == UEdGraphSchema_K2::PN_Self && Pin->LinkedTo.IsEmpty() && StaticCall &&
                            StaticCall->GetTargetFunction() && StaticCall->GetTargetFunction()->HasAnyFunctionFlags(FUNC_Static))
                        {
                            continue;
                        }
						AddPinModules(Pin->PinType);
						AddNativeModule(Cast<UField>(Pin->DefaultObject));
					}
				}
			}
			if (!Blueprint->GeneratedClass->IsChildOf<UInterface>())
			{
				EntryNodes = GenerateAllGenerateFunctionStruct(Blueprint, "");
				AnswerNodes.Append(GetAllContextNodes());
			}
		}

		for (UK2Node* ExecNode : AnswerNodes)
		{
			UTranslatorBPToCppObject* TranslatorObject = FindTranslatorForNode(ExecNode);
			if (TranslatorObject)
			{
				CSPrivateDependencyModuleNames.Append(TranslatorObject->GenerateCSPrivateDependencyModuleNames(ExecNode, this));
			}
		}

	}
	for (const FString& Dependency : FNativeGenerationContext::GetModuleDependencies())
	{
		PublicDependencies.Add(FString::Format(TEXT("\"{0}\""), {Dependency}));
	}
    if (PublicDependencies.Contains(TEXT("\"Niagara\"")) || CSPrivateDependencyModuleNames.Contains(TEXT("\"Niagara\"")))
    {
        CSPrivateDependencyModuleNames.Add(TEXT("\"NiagaraCore\""));
    }
	CSPrivateDependencyModuleNames = CSPrivateDependencyModuleNames.Difference(PublicDependencies);

    FString LegacyIncludePaths;
    for (const FString& LegacyModule : LegacyIncludeModules)
    {
        LegacyIncludePaths += FString::Format(TEXT("PublicIncludePaths.Add(GetModuleDirectory(\"{0}\"));\n"), { LegacyModule });
    }
	FString BuildCS = FString::Format(TEXT(R"(
	using UnrealBuildTool;

	public class {0} : ModuleRules
	{
		public {0}(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
			PublicIncludePaths.Add(ModuleDirectory);
            {3}
			PublicDependencyModuleNames.AddRange(new string[] {{2}});
			PrivateDependencyModuleNames.AddRange(new string[] {{1}});
		}
	}
	)"), { ModuleName, *FString::Join(CSPrivateDependencyModuleNames, TEXT(", ")), FString::Join(PublicDependencies, TEXT(", ")), LegacyIncludePaths });

	return BuildCS;
}

TMap<FString, FString> UNativizationV2Subsystem::CaptureMigrationClassReplacements() const
{
    TMap<FString, FString> Replacements;
    const TMap<UField*, FName> TranslatedObjects = UBlueprintNativizationLibrary::GetAllRegisterUniqueFieldsInNameSubsistem();
    const FString ModuleName = UBlueprintNativizationSettingsLibrary::GetModuleName();
    for (const TPair<UClass*, UClass*>& Replacement : FNativeGenerationContext::GetClassReplacements())
    {
        UBlueprint* Blueprint = Cast<UBlueprint>(Replacement.Key->ClassGeneratedBy);
        if (!Blueprint || Blueprint->GeneratedClass != Replacement.Key)
        {
            continue;
        }
        UClass* DestinationClass = FNativeGenerationContext::ResolveClass(Replacement.Value);
        FString DestinationPath;
        if (DestinationClass->HasAnyClassFlags(CLASS_Native))
        {
            DestinationPath = DestinationClass->GetPathName();
        }
        else if (const FName* TranslatedName = TranslatedObjects.Find(DestinationClass))
        {
            FString ReflectionName = TranslatedName->ToString();
            if (!DestinationClass->HasAnyClassFlags(CLASS_Interface))
            {
                ReflectionName.RightChopInline(1);
            }
            DestinationPath = TEXT("/Script/") + ModuleName + TEXT(".") + ReflectionName;
        }
        else
        {
            FNativeApiCatalog::AddError(TEXT("Default-only class replacement has no generated destination: ") + Blueprint->GetPathName());
            continue;
        }
        Replacements.Add(UBlueprintNativizationLibrary::GetStableFieldIdentifier(Replacement.Key), DestinationPath);
    }
    return Replacements;
}

void UNativizationV2Subsystem::CreateJSONCache(FString CacheDirectory)
{
    WriteJSONCache(CacheDirectory, UBlueprintNativizationLibrary::GetAllRegisterUniqueFieldsInNameSubsistem(), CaptureMigrationMemberNames(), CaptureMigrationClassReplacements());
}
bool UNativizationV2Subsystem::WriteJSONCache(FString CacheDirectory, const TMap<UField*, FName>& TranslatedObjects, const TMap<FString, FString>& MemberNames, const TMap<FString, FString>& DefaultOnlyClassReplacements)
{

	FString ModuleName = UBlueprintNativizationSettingsLibrary::GetModuleName();
	if (CacheDirectory.IsEmpty())
	{
		CacheDirectory = FPaths::Combine(FPaths::ProjectDir(), FString::Format(TEXT("Source/{0}"), { ModuleName }));
	}

	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());

	JsonObject->SetStringField(TEXT("_format"), TEXT("BlueprintNativizationCache"));
	JsonObject->SetNumberField(TEXT("version"), 3);
    JsonObject->SetStringField(TEXT("DefaultsContract"), TEXT("SerializedBlueprintOverrides"));
    JsonObject->SetStringField(TEXT("ModuleName"), ModuleName);
    JsonObject->SetBoolField(TEXT("KeepAssetReferences"), true);
    TSharedRef<FJsonObject> Members = MakeShared<FJsonObject>();
    for (const TPair<FString, FString>& Member : MemberNames)
    {
        Members->SetStringField(Member.Key, Member.Value);
    }
    JsonObject->SetObjectField(TEXT("Members"), Members);
    TSharedPtr<FJsonObject> ConfigurationTemplates;
    FString ConfigurationError;
    if (!CaptureConfigurationTemplates(TranslatedObjects, MemberNames, ConfigurationTemplates, ConfigurationError))
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot export configuration templates: %s"), *ConfigurationError);
        return false;
    }
    JsonObject->SetObjectField(TEXT("ConfigurationTemplates"), ConfigurationTemplates);

    TSharedRef<FJsonObject> ClassReplacements = MakeShared<FJsonObject>();
    for (const TPair<FString, FString>& Replacement : DefaultOnlyClassReplacements)
    {
        ClassReplacements->SetStringField(Replacement.Key, Replacement.Value);
    }
    JsonObject->SetObjectField(TEXT("DefaultOnlyClassReplacements"), ClassReplacements);

	TArray<TSharedPtr<FJsonValue>> TranslateObjectJsons;

	for (const TPair<UField*, FName>& Pair : TranslatedObjects)
	{
		TSharedPtr<FJsonObject> TranslateObjectJson = MakeShareable(new FJsonObject());
		TranslateObjectJson->SetStringField("OriginalField", UBlueprintNativizationLibrary::GetStableFieldIdentifier(Pair.Key));
		TranslateObjectJson->SetStringField("NewFieldName", Pair.Value.ToString());
        FString ReflectionName = Pair.Value.ToString();
        if (UClass* Class = Cast<UClass>(Pair.Key))
        {
            if (!Class->HasAnyClassFlags(CLASS_Interface))
            {
                ReflectionName.RightChopInline(1);
            }
        }
        else if (Pair.Key->IsA<UScriptStruct>())
        {
            ReflectionName.RightChopInline(1);
        }
        TranslateObjectJson->SetStringField(TEXT("NewFieldPath"), TEXT("/Script/") + ModuleName + TEXT(".") + ReflectionName);
		TranslateObjectJsons.Add(MakeShareable(new FJsonValueObject(TranslateObjectJson)));
	}

	JsonObject->SetArrayField("items", TranslateObjectJsons);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);

	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

	if (!FPlatformFileManager::Get().GetPlatformFile().DirectoryExists(*CacheDirectory))
	{
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*CacheDirectory);
	}

	// Изменено расширение файла на .bpncache
	const FString CacheFilePath = FPaths::Combine(CacheDirectory, TEXT("BlueprintNativizationCache.bpncache"));
	if (FFileHelper::SaveStringToFile(OutputString, *CacheFilePath))
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool UNativizationV2Subsystem::ReapplyJSONCache(FString CacheFilePath)
{
    FString Error;
    if (!TryReapplyJSONCache(CacheFilePath, Error))
    {
        UE_LOG(LogTemp, Error, TEXT("Migration failed: %s"), *Error);
        return false;
    }
    return true;
}

bool UNativizationV2Subsystem::HasNewClassesAfterHotReload()
{
	const FString TargetModuleName = UBlueprintNativizationSettingsLibrary::GetModuleName();

	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* CandidateClass = *ClassIt;
		FString ClassName = CandidateClass->GetName();
		UPackage* Package = CandidateClass->GetOutermost();
		if (Package->GetName().Contains(TargetModuleName))
		{
			if (!CandidateClass->HasAnyClassFlags(CLASS_NewerVersionExists))
			{
				if (Package && Package->GetName().Contains(TargetModuleName) &&
					Package->GetName().Contains(TEXT("HotReload")))
				{
					return true;
				}
			}
		}

	}
	return false;
}



UField* UNativizationV2Subsystem::FindFieldByNameViaCDO(const FName& TargetName)
{
    UField* MatchingField = nullptr;
    const FString RequestedName = TargetName.ToString();
    for (TObjectIterator<UField> FieldIterator; FieldIterator; ++FieldIterator)
    {
        UField* Field = *FieldIterator;
        if (!Field->IsNative())
        {
            continue;
        }
        FString Symbol;
        if (UClass* Class = Cast<UClass>(Field))
        {
            Symbol = Class->GetPrefixCPP() + Class->GetName();
        }
        else if (UScriptStruct* Structure = Cast<UScriptStruct>(Field))
        {
            Symbol = Structure->GetStructCPPName();
        }
        else if (UEnum* Enumeration = Cast<UEnum>(Field))
        {
            Symbol = Enumeration->CppType;
        }
        else
        {
            continue;
        }
        if (Field->GetPathName() != RequestedName && Symbol != RequestedName)
        {
            continue;
        }
        if (MatchingField)
        {
            return nullptr;
        }
        MatchingField = Field;
    }
    return MatchingField;
}

void UNativizationV2Subsystem::ReplaceBlueprintTypesAfterHotReload(const TMap<UField*, FName>& TranslatedObjects)
{
    TMap<UField*, UField*> Mappings;
    for (const TPair<UField*, FName>& Mapping : TranslatedObjects)
    {
        UField* Generated = FindFieldByNameViaCDO(Mapping.Value);
        if (!Generated)
        {
            UE_LOG(LogTemp, Error, TEXT("Unresolved migration type: %s"), *Mapping.Value.ToString());
            return;
        }
        Mappings.Add(Mapping.Key, Generated);
    }
    FString Error;
    if (!ApplyMigrationTypes(Mappings, CaptureMigrationMemberNames(), Error, false, {}, false))
    {
        UE_LOG(LogTemp, Error, TEXT("Migration failed: %s"), *Error);
    }
}

FString UNativizationV2Subsystem::ConvertUEnumToCppEnumDeclaration(UEnum* Enum)
{
    const FString EnumName = UBlueprintNativizationLibrary::GetUniqueFieldName(Enum);
    FString Result = TEXT("#pragma once\n\n#include \"CoreMinimal.h\"\n");
    Result += FString::Format(TEXT("#include \"{0}.generated.h\"\n\nUENUM(BlueprintType)\nenum class {1} : uint8\n{\n"),
        { GetGeneratedFileName(Enum), EnumName });
    for (int32 EntryIndex = 0; EntryIndex < Enum->NumEnums(); ++EntryIndex)
    {
        const int64 Value = Enum->GetValueByIndex(EntryIndex);
        FString Metadata = FString::Format(TEXT("DisplayName = \"{0}\""),
            { Enum->GetDisplayNameTextByIndex(EntryIndex).ToString().ReplaceCharWithEscapedChar() });
        if (Value == Enum->GetMaxEnumValue() || Enum->HasMetaData(TEXT("Hidden"), EntryIndex))
        {
            Metadata += TEXT(", Hidden");
        }
        Result += FString::Format(TEXT("{0} = {1} UMETA({2}),\n"),
            { UBlueprintNativizationLibrary::GetEnumValueName(Enum, Value), Value, Metadata });
    }
    Result += TEXT("};\n");
    return UBlueprintNativizationDataLibrary::FormatNamespaceCodeStyle(Result);
}

FString UNativizationV2Subsystem::ConvertUStructToCppStructDeclaration(UStruct* Struct)
{
    TGuardValue<bool> InlineDefaultsGuard(FNativeGenerationContext::bGeneratingInlineDefaults, true);
    const FString StructName = UBlueprintNativizationLibrary::GetUniqueFieldName(Struct);
    FString Result = TEXT("#pragma once\n\n#include \"CoreMinimal.h\"\n#include \"StructUtils/UserDefinedStruct.h\"\n#include \"UObject/UnrealType.h\"\n");
    TSet<FString> Includes;
    TSet<FString> ForwardDeclarations;
    for (TFieldIterator<FProperty> PropertyIterator(Struct); PropertyIterator; ++PropertyIterator)
    {
        FEdGraphPinType Type;
        UBlueprintNativizationLibrary::ConvertPropertyToPinType(*PropertyIterator, Type);
        for (UObject* TypeObject : { Type.PinSubCategoryObject.Get(), Type.PinValueType.TerminalSubCategoryObject.Get() })
        {
            if (UClass* PropertyClass = Cast<UClass>(TypeObject))
            {
                if (PropertyClass->HasAnyClassFlags(CLASS_Interface))
                {
                    ForwardDeclarations.Add(TEXT("class ") + UBlueprintNativizationLibrary::GetUniqueIInterfaceFieldName(PropertyClass) + TEXT(";"));
                    ForwardDeclarations.Add(TEXT("class ") + UBlueprintNativizationLibrary::GetUniqueUInterfaceFieldName(PropertyClass) + TEXT(";"));
                }
                else
                {
                    ForwardDeclarations.Add(TEXT("class ") + FRetainedBlueprintGeneration::GetClassName(PropertyClass) + TEXT(";"));
                }
            }
            else if (UField* Field = Cast<UField>(TypeObject))
            {
                FString Include;
                if (Field != Struct && GetIncludeFromField(Field, Include))
                {
                    Includes.Add(Include);
                }
            }
        }
    }
    for (const FString& Include : Includes)
    {
        Result += Include + TEXT("\n");
    }
    Result += FString::Format(TEXT("#include \"{0}.generated.h\"\n\n"), { GetGeneratedFileName(Struct) });
    Result += FString::Join(ForwardDeclarations.Array(), TEXT("\n")) + TEXT("\n");
    Result += TEXT("USTRUCT(BlueprintType)\nstruct ") + StructName + TEXT("\n{\nGENERATED_BODY()\n\n");
    FStructOnScope Defaults(Struct);
    const uint8* DefaultMemory = Defaults.GetStructMemory();
    if (UUserDefinedStruct* UserStructure = Cast<UUserDefinedStruct>(Struct))
    {
        DefaultMemory = UserStructure->GetDefaultInstance();
    }
    TArray<FString> ConstructorParameters;
    TArray<FString> Initializers;
    FString DefaultInitializers;
    for (TFieldIterator<FProperty> PropertyIterator(Struct); PropertyIterator; ++PropertyIterator)
    {
        FProperty* Property = *PropertyIterator;
        if (Property->HasAnyPropertyFlags(CPF_Transient))
        {
            continue;
        }
        const FString Type = UBlueprintNativizationLibrary::GetPropertyType(Property);
        const FString Name = UBlueprintNativizationLibrary::GetUniquePropertyName(Property);
        Result += FString::Format(TEXT("UPROPERTY({0})\n{1} {2}"), { GetPropertyFlagsString(Property), Type, Name });
        Result += TEXT("{};\n\n");
        const uint8* PropertyMemory = Property->ContainerPtrToValuePtr<uint8>(DefaultMemory);
        if (UBlueprintNativizationLibrary::RequiresPostCDOInitialization(Property, PropertyMemory, false))
        {
            FNativeApiCatalog::AddError(TEXT("A standalone generated structure cannot initialize Blueprint-backed defaults before CDO construction: ") + Property->GetPathName());
            continue;
        }
        if (UBlueprintNativizationLibrary::IsAvailibleOneLineDefaultConstructorForProperty(Property))
        {
            DefaultInitializers += Name + TEXT(" = ") + UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructorForProperty(Property, const_cast<uint8*>(PropertyMemory), TEXT("nullptr")) + TEXT(";\n");
        }
        else
        {
            FString Value;
            DefaultInitializers += UBlueprintNativizationLibrary::GenerateManyLinesDefaultConstructorForProperty(Property,
                const_cast<uint8*>(PropertyMemory), nullptr, {}, nullptr, true, TEXT(""), Value, TEXT("nullptr"));
            DefaultInitializers += Name + TEXT(" = ") + Value + TEXT(";\n");
        }
        ConstructorParameters.Add(Type + TEXT(" In") + Name);
        Initializers.Add(Name + TEXT("(In") + Name + TEXT(")"));
    }
    DefaultInitializers = FGeneratedCodeCleanup::SimplifyImmediateInvocations(DefaultInitializers);
    Result += StructName + TEXT("()\n{\n") + DefaultInitializers + TEXT("}\n");
    if (!ConstructorParameters.IsEmpty())
    {
        Result += StructName + TEXT("(") + FString::Join(ConstructorParameters, TEXT(", ")) + TEXT(")\n: ") +
            FString::Join(Initializers, TEXT(",\n")) + TEXT("\n{}\n");
    }
    Result += TEXT("bool operator==(const ") + StructName + TEXT("& Other) const\n{\nreturn StaticStruct()->CompareScriptStruct(this, &Other, PPF_None);\n}\n");
    Result += TEXT("};\n");
    Result += TEXT("inline uint32 GetTypeHash(const ") + StructName + TEXT("& Value)\n{\nreturn UUserDefinedStruct::GetUserDefinedStructTypeHash(&Value, ") + StructName + TEXT("::StaticStruct());\n}\n");
    return UBlueprintNativizationDataLibrary::FormatNamespaceCodeStyle(Result);
}


void UNativizationV2Subsystem::PreloadAllNamesForObjects(TArray<UObject*> Objects)
{
    FRetainedBlueprintGeneration::Begin(Objects);
    Objects.StableSort([](const UObject& FirstObject, const UObject& SecondObject)
    {
        auto GetClassDepth = [](const UObject& Object)
        {
            int32 Depth = 0;
            if (const UBlueprint* Blueprint = Cast<UBlueprint>(&Object))
            {
                for (UClass* Class = Blueprint->GeneratedClass; Class; Class = Class->GetSuperClass())
                {
                    ++Depth;
                }
            }
            return Depth;
        };
        return GetClassDepth(FirstObject) < GetClassDepth(SecondObject);
    });

	for (UObject* Object : Objects)
	{
		if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
		{
			UBlueprintNativizationLibrary::GetUniqueFieldName(Blueprint->GeneratedClass);
		}
	}

	for (UObject* Object : Objects)
	{
		if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
		{
			TArray<FGenerateFunctionStruct> LocalEntryNodes = GenerateAllGenerateFunctionStruct(Blueprint);
			for (FGenerateFunctionStruct EntryNode : LocalEntryNodes)
			{
				UBlueprintNativizationLibrary::GetUniqueEntryNodeName(EntryNode.Node, LocalEntryNodes, "");
			}

			for (TFieldIterator<FProperty> PropIt(Blueprint->GeneratedClass, EFieldIteratorFlags::ExcludeSuper); PropIt; ++PropIt)
			{
				UBlueprintNativizationLibrary::GetUniquePropertyName(*PropIt);
			}
		}
	}
}

static FString FormatHeaderImports(TSet<FString> Includes, const FString& FileName)
{
    const FString GeneratedInclude = FString::Format(TEXT("#include \"{0}.generated.h\""), { FileName });
    Includes.Remove(TEXT("#pragma once\n\n"));
    Includes.Remove(TEXT("#include \"CoreMinimal.h\""));
    Includes.Remove(GeneratedInclude);
    TArray<FString> OrderedIncludes = Includes.Array();
    OrderedIncludes.Sort();
    return TEXT("#pragma once\n\n#include \"CoreMinimal.h\"\n") +
        FString::Join(OrderedIncludes, TEXT("\n")) + TEXT("\n") + GeneratedInclude + TEXT("\n\n");
}

void UNativizationV2Subsystem::GenerateInterfaceCode(UBlueprint* BlueprintInterfaceClass, FNativizationCode& OutContent)
{
	TArray<UK2Node*> DefaultEntryNodes;
	TArray<UEdGraph*> EdGraphs;

	TSubclassOf<UInterface> InterfaceClass = BlueprintInterfaceClass->GeneratedClass.Get();

	EntryNodes = GenerateAllGenerateFunctionStruct(BlueprintInterfaceClass);

	TSet<FString> HeaderImports = GenerateImportsDeclarationsHeaderCode(InterfaceClass);

	FString HFileContent;
	HFileContent += FString::Format(
		TEXT("UINTERFACE(MinimalAPI)\nclass {0} : public UInterface\n{\n    GENERATED_BODY()\n};\n\n"),
		{ UBlueprintNativizationLibrary::GetUniqueUInterfaceFieldName(InterfaceClass) }
	);

	HFileContent += FString::Format(
		TEXT("class {0} {1}\n{\n    GENERATED_BODY()\n\n"),
		{ UBlueprintNativizationSettingsLibrary::GetModuleApiName() ,UBlueprintNativizationLibrary::GetUniqueIInterfaceFieldName(InterfaceClass) }
	);

	HFileContent += GenerateFunctionDeclarationsCode(InterfaceClass, true);

	HFileContent += TEXT("};");

	HFileContent = UBlueprintNativizationDataLibrary::FormatNamespaceCodeStyle(HFileContent);

	OutContent.OutputHeaderString = FormatHeaderImports(HeaderImports, GetGeneratedFileName(InterfaceClass)) +
        FString::Join(GenerateClassPreDeclarationsHeaderCode(InterfaceClass).Array(), TEXT("\n")) + TEXT("\n\n") + HFileContent;
	OutContent.OutputCppString = TEXT("// Interface does not require .cpp file by default.\n");

}



void UNativizationV2Subsystem::GenerateBlueprintCode(UBlueprint* Blueprint, FString FunctionName, FNativizationCode& OutContent)
{
    FRetainedBlueprintGeneration::BeginObject(Blueprint);
    ON_SCOPE_EXIT
    {
        FRetainedBlueprintGeneration::EndObject();
    };
    if (FRetainedBlueprintGeneration::IsRetained(Blueprint->GeneratedClass->GetSuperClass()))
    {
        FNativeApiCatalog::AddError(TEXT("The Blueprint parent must be included in generation: ") + Blueprint->GetPathName());
        return;
    }
    UClass* NativeParent = Blueprint->GeneratedClass->GetSuperClass();
    if (NativeParent->HasAnyClassFlags(CLASS_Native) && FRetainedBlueprintGeneration::GetStorageClass(NativeParent) != NativeParent)
    {
        FNativeApiCatalog::AddError(TEXT("Cannot derive a generated class from a private native type: ") + NativeParent->GetPathName());
        return;
    }
    for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
    {
        if (FRetainedBlueprintGeneration::IsRetained(Interface.Interface))
        {
            FNativeApiCatalog::AddError(TEXT("Include the implemented Blueprint interface in generation: ") + Interface.Interface->GetPathName());
            return;
        }
    }
    LatentActionIdentifiers.Reset();
	const FString ClassName = GetGeneratedFileName(Blueprint);
	FGenerationGraphContext GraphContext(Blueprint, this);

	EntryNodes = GenerateAllGenerateFunctionStruct(Blueprint, FunctionName);
    RegisterPersistentEntryParameters();

	TSet<FString> HeaderImports = GenerateImportsDeclarationsHeaderCode(Blueprint->GeneratedClass);
	TSet<FString> ClassPreDeclarations = GenerateClassPreDeclarationsHeaderCode(Blueprint->GeneratedClass);
    if (Blueprint->GeneratedClass->IsChildOf(UActorComponent::StaticClass()) &&
        GetAllContextNodes().ContainsByPredicate([](UK2Node* Node)
        {
            return UInputEventTranslatorObject::HasInputBinding(Node);
        }))
    {
        ClassPreDeclarations.Add(TEXT("class UInputComponent;"));
    }
	TSet<FString> CppImports = GenerateImportsDeclarationsCppCode(Blueprint);
    if (EntryNodes.ContainsByPredicate([](const FGenerateFunctionStruct& Entry)
    {
        return Entry.Node->IsA<UK2Node_ActorBoundEvent>();
    }))
    {
        CppImports.Add(TEXT("#include \"Engine/Level.h\""));
        CppImports.Add(TEXT("#include \"UObject/UnrealType.h\""));
    }


	CppImports = CppImports.Difference(HeaderImports);
    for (UClass* ParentClass = Blueprint->GeneratedClass->GetSuperClass(); ParentClass; ParentClass = ParentClass->GetSuperClass())
    {
        FString ParentInclude;
        if (GetIncludeFromField(ParentClass, ParentInclude))
        {
            CppImports.Remove(TEXT("#include \"") + ParentInclude + TEXT("\""));
        }
    }

	FString HeaderImportsString = FormatHeaderImports(HeaderImports, ClassName);
    const FString HeaderBody = GenerateHFileBlueprintCode(Blueprint, FunctionName);
    const FString NativeClassName = UBlueprintNativizationLibrary::GetUniqueFieldName(Blueprint->GeneratedClass);
    const int32 ClassDeclaration = HeaderBody.Find(TEXT("UCLASS("));
    if (ClassDeclaration != INDEX_NONE && !HeaderBody.Left(ClassDeclaration).Contains(NativeClassName))
    {
        ClassPreDeclarations.Remove(TEXT("class ") + NativeClassName + TEXT(";"));
    }
	FString HeaderClassPreDeclarationsString = FString::Join(ClassPreDeclarations.Array(), TEXT("\n")) + "\n";
	const FString OwnInclude = FString::Format(TEXT("#include \"{0}.h\""), { ClassName });
    CppImports.Remove(OwnInclude);
    TArray<FString> OrderedCppImports = CppImports.Array();
    OrderedCppImports.Sort();
    FString CppImportsString = OwnInclude + TEXT("\n") + FString::Join(OrderedCppImports, TEXT("\n")) + TEXT("\n\n");

	OutContent.OutputHeaderString = HeaderImportsString + HeaderClassPreDeclarationsString + HeaderBody;
	OutContent.OutputCppString = CppImportsString + GenerateCppFileBlueprintCode(Blueprint, FunctionName);
    FNativeEventGeneration::Complete(Blueprint, EntryNodes, OutContent.OutputHeaderString, OutContent.OutputCppString);
    FRetainedBlueprintGeneration::Complete(Blueprint, OutContent.OutputHeaderString, OutContent.OutputCppString);
    FGeneratedCodeCleanup::RemoveRedundantOverrides(OutContent.OutputHeaderString, OutContent.OutputCppString);
    OutContent.OutputHeaderString = FGeneratedCodeCleanup::RemoveUnusedForwardDeclarations(OutContent.OutputHeaderString);
    OutContent.OutputHeaderString = UBlueprintNativizationDataLibrary::FormatNamespaceCodeStyle(OutContent.OutputHeaderString);
    OutContent.OutputCppString = FGeneratedCodeCleanup::SimplifyImmediateInvocations(OutContent.OutputCppString);
    OutContent.OutputCppString = UBlueprintNativizationDataLibrary::FormatNamespaceCodeStyle(OutContent.OutputCppString);

}


FString UNativizationV2Subsystem::GenerateHFileBlueprintCode(UBlueprint* Blueprint, FString DirectFunctionName)
{
	if (!Blueprint)
	{
		UE_LOG(LogTemp, Warning, TEXT("GenerateHFileBlueprintCode: Invalid Blueprint."));
		return "";
	}

	const FString ClassName = UBlueprintNativizationLibrary::GetUniqueFieldName(Blueprint->GeneratedClass);
	const FString ParentClassName = UBlueprintNativizationLibrary::GetUniqueFieldName(Blueprint->GeneratedClass->GetSuperClass());

	const FString UpperClassName = ClassName.ToUpper();

	FString HFileContent;

	TArray<FString> InterfaceClassNames;
	for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
	{
		if (InterfaceDesc.Interface)
		{
			TSubclassOf<UInterface> InterfaceClass = InterfaceDesc.Interface;
			if (InterfaceClass)
			{
				const FString InterfaceName = UBlueprintNativizationLibrary::GetUniqueIInterfaceFieldName(InterfaceClass);
				InterfaceClassNames.Add(FString::Printf(TEXT("public %s"), *InterfaceName));
			}
		}
	}

	FString AllBaseClasses = ParentClassName;
	if (InterfaceClassNames.Num() > 0)
	{
		AllBaseClasses += ", " + FString::Join(InterfaceClassNames, TEXT(", "));
	}


	HFileContent += GenerateDelegateMacroDeclarationBlueprintCode(Blueprint);
	HFileContent += "\n";
	HFileContent += "\n";

    FString ClassSpecifiers;
    if (Blueprint->GeneratedClass->IsChildOf(USubsystem::StaticClass()))
    {
        ClassSpecifiers = TEXT("Abstract");
    }
	HFileContent += FString::Format(
		TEXT("UCLASS({3})\nclass {0} {1} : public {2}\n{\nGENERATED_BODY()\n\n"),
		{ UBlueprintNativizationSettingsLibrary::GetModuleApiName(), ClassName, AllBaseClasses, ClassSpecifiers });

	HFileContent += "public: \n";

	HFileContent += ClassName + TEXT("(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());\n");
    for (TFieldIterator<FProperty> Property(Blueprint->GeneratedClass, EFieldIteratorFlags::ExcludeSuper); Property; ++Property)
    {
        if (Property->HasAnyPropertyFlags(CPF_Net) &&
            UBlueprintNativizationLibrary::GetConversionPropertyFlagForProperty(*Property, Blueprint) != EConversionPropertyFlag::Ignore)
        {
            HFileContent += TEXT("virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;\n");
            break;
        }
    }
    if (EntryNodes.ContainsByPredicate([](const FGenerateFunctionStruct& Entry)
    {
        return Entry.Node->IsA<UK2Node_ActorBoundEvent>();
    }))
    {
        HFileContent += TEXT("virtual void PostInitializeComponents() override;\n");
    }
	HFileContent += "\n";

	if (Blueprint->GeneratedClass->IsChildOf(APawn::StaticClass()))
	{
		HFileContent += "virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;\n";
	}
    else if (GetAllContextNodes().ContainsByPredicate([](UK2Node* Node)
    {
        return UInputEventTranslatorObject::HasInputBinding(Node);
    }))
    {
        if (Blueprint->GeneratedClass->IsChildOf(APlayerController::StaticClass()))
        {
            HFileContent += TEXT("virtual void SetupInputComponent() override;\n");
        }
        else if (Blueprint->GeneratedClass->IsChildOf(AActor::StaticClass()))
        {
            HFileContent += TEXT("virtual void CreateInputComponent(TSubclassOf<UInputComponent> InputComponentToCreate) override;\n");
            HFileContent += TEXT("virtual void EnableInput(APlayerController* PlayerController) override;\n");
        }
        else if (Blueprint->GeneratedClass->IsChildOf(UActorComponent::StaticClass()))
        {
            HFileContent += TEXT("virtual void OnRegister() override;\n");
            HFileContent += TEXT("void BindGeneratedComponentInput(UInputComponent* PlayerInputComponent);\n");
        }
    }
	HFileContent += "\n";

	HFileContent += GenerateVariableDeclarationBlueprintCode(Blueprint);
	HFileContent += GenerateGlobalFunctionVariables(Blueprint);
	HFileContent += GenerateFunctionDeclarationsCode(Blueprint->GeneratedClass, false);
	HFileContent += "};";

	HFileContent = UBlueprintNativizationDataLibrary::FormatNamespaceCodeStyle(HFileContent);

	return HFileContent;
}

static TArray<FProperty*> GetDeclarationProperties(UClass* Class)
{
    TArray<FProperty*> Properties;
    for (TFieldIterator<FProperty> Property(Class, EFieldIteratorFlags::ExcludeSuper); Property; ++Property)
    {
        Properties.Add(*Property);
    }
    for (TFieldIterator<UFunction> Function(Class, EFieldIteratorFlags::ExcludeSuper); Function; ++Function)
    {
        for (TFieldIterator<FProperty> Parameter(*Function); Parameter && Parameter->HasAnyPropertyFlags(CPF_Parm); ++Parameter)
        {
            Properties.Add(*Parameter);
        }
    }
    for (int32 PropertyIndex = 0; PropertyIndex < Properties.Num(); ++PropertyIndex)
    {
        FProperty* Property = Properties[PropertyIndex];
        if (FArrayProperty* Array = CastField<FArrayProperty>(Property))
        {
            Properties.Add(Array->Inner);
        }
        else if (FSetProperty* Set = CastField<FSetProperty>(Property))
        {
            Properties.Add(Set->ElementProp);
        }
        else if (FMapProperty* Map = CastField<FMapProperty>(Property))
        {
            Properties.Add(Map->KeyProp);
            Properties.Add(Map->ValueProp);
        }
    }
    return Properties;
}

TSet<FString> UNativizationV2Subsystem::GenerateImportsDeclarationsHeaderCode(TSubclassOf<UObject> Class)
{
    TSet<FString> Includes;
    Includes.Add(TEXT("#pragma once\n\n"));
    Includes.Add(TEXT("#include \"CoreMinimal.h\""));
    auto AddValueType = [this, &Includes](UField* Field)
    {
        FString Include;
        if (Field && !Field->IsA<UClass>() && GetIncludeFromField(Field, Include))
        {
            Includes.Add(Include);
        }
    };
    for (FProperty* Property : GetDeclarationProperties(Class))
    {
        if (FStructProperty* Structure = CastField<FStructProperty>(Property))
        {
            AddValueType(Structure->Struct);
        }
        else if (FEnumProperty* Enumeration = CastField<FEnumProperty>(Property))
        {
            AddValueType(Enumeration->GetEnum());
        }
        else if (FByteProperty* Byte = CastField<FByteProperty>(Property))
        {
            AddValueType(Byte->Enum);
        }
    }
    FString Include;
    for (const FImplementedInterface& Interface : Class->Interfaces)
    {
        if (GetIncludeFromField(Interface.Class, Include))
        {
            Includes.Add(Include);
        }
    }
    if (GetIncludeFromField(Class->GetSuperClass(), Include))
    {
        Includes.Add(Include);
    }
    for (UK2Node* Node : GetAllContextNodes())
    {
        for (UEdGraphPin* Pin : Node->Pins)
        {
            AddValueType(Cast<UField>(Pin->PinType.PinSubCategoryObject.Get()));
            AddValueType(Cast<UField>(Pin->PinType.PinValueType.TerminalSubCategoryObject.Get()));
        }
        if (UTranslatorBPToCppObject* Translator = FindTranslatorForNode(Node))
        {
            Includes.Append(Translator->GenerateHeaderIncludeInstructions(Node, this));
        }
    }
    Includes.Add(FString::Format(TEXT("#include \"{0}.generated.h\""), { GetGeneratedFileName(Class) }));
    return Includes;
}

TSet<FString> UNativizationV2Subsystem::GenerateClassPreDeclarationsHeaderCode(TSubclassOf<UObject> Class)
{
    TSet<FString> PreDeclarations;
    auto AddClass = [&PreDeclarations](UClass* PropertyClass)
    {
        if (PropertyClass)
        {
            if (PropertyClass->HasAnyClassFlags(CLASS_Interface))
            {
                PreDeclarations.Add(TEXT("class ") + UBlueprintNativizationLibrary::GetUniqueIInterfaceFieldName(PropertyClass) + TEXT(";"));
                PreDeclarations.Add(TEXT("class ") + UBlueprintNativizationLibrary::GetUniqueUInterfaceFieldName(PropertyClass) + TEXT(";"));
            }
            else
            {
                PreDeclarations.Add(TEXT("class ") + FRetainedBlueprintGeneration::GetClassName(PropertyClass) + TEXT(";"));
            }
        }
    };
    for (FProperty* Property : GetDeclarationProperties(Class))
    {
        if (FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
        {
            AddClass(ClassProperty->MetaClass);
        }
        else if (FSoftClassProperty* SoftClassProperty = CastField<FSoftClassProperty>(Property))
        {
            AddClass(SoftClassProperty->MetaClass);
        }
        else if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
        {
            AddClass(ObjectProperty->PropertyClass);
        }
        else if (FInterfaceProperty* InterfaceProperty = CastField<FInterfaceProperty>(Property))
        {
            AddClass(InterfaceProperty->InterfaceClass);
        }
    }
    for (UK2Node* Node : GetAllContextNodes())
    {
        for (UEdGraphPin* Pin : Node->Pins)
        {
            UK2Node_CallFunction* StaticCall = Cast<UK2Node_CallFunction>(Node);
            if (Pin->PinName == UEdGraphSchema_K2::PN_Self && Pin->LinkedTo.IsEmpty() && StaticCall &&
                StaticCall->GetTargetFunction() && StaticCall->GetTargetFunction()->HasAnyFunctionFlags(FUNC_Static))
            {
                continue;
            }
            AddClass(Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get()));
            AddClass(Cast<UClass>(Pin->PinType.PinValueType.TerminalSubCategoryObject.Get()));
        }
    }
    return PreDeclarations;
}

TSet<FString> UNativizationV2Subsystem::GenerateImportsDeclarationsCppCode(UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		UE_LOG(LogTemp, Warning, TEXT("GenerateImportsDeclarationsHeaderCode: Invalid Blueprint."));
		return TSet<FString>();
	}

	const FString ClassName = GetGeneratedFileName(Blueprint);

	TSet<FString> Includes;

	Includes.Add(FString::Format(TEXT("#include \"{0}.h\""), { ClassName }));

	/**/
	UClass* GeneratedClass = Blueprint->GeneratedClass;
	if (GeneratedClass)
	{
		for (TFieldIterator<FProperty> PropertyIt(GeneratedClass, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			FProperty* Property = *PropertyIt;
			if (!Property) continue;

            UObject* DefaultObject = GeneratedClass->GetDefaultObject();
            if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
            {
                if (Property->ArrayDim == 1 && !ObjectProperty->GetObjectPropertyValue_InContainer(DefaultObject))
                {
                    continue;
                }
            }
            if (Property->ArrayDim == 1 && Property->GetOwnerClass() != GeneratedClass &&
                Property->Identical_InContainer(DefaultObject, GeneratedClass->GetSuperClass()->GetDefaultObject()))
            {
                continue;
            }
			if (Property->GetOwnerClass() != GeneratedClass)
			{
				FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property);
				if (!ObjectProperty || !ObjectProperty->PropertyClass->IsChildOf(UActorComponent::StaticClass()))
				{
					continue;
				}
			}

            if (FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
            {
                UClass* DefaultClass = Cast<UClass>(ClassProperty->GetObjectPropertyValue_InContainer(GeneratedClass->GetDefaultObject()));
                FString IncludePath;
                if (DefaultClass && GetIncludeFromField(DefaultClass, IncludePath))
                {
                    Includes.Add(IncludePath);
                }
            }
			if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property))
			{
				FString IncludePath;

				if (GetIncludeFromField(ObjProp->PropertyClass, IncludePath))
				{
					Includes.Add(IncludePath);
				}
			}
		}
	}

	// === 2. Collect types from functions ===
	if (GeneratedClass)
	{
		for (TFieldIterator<UFunction> FuncIt(GeneratedClass, EFieldIteratorFlags::ExcludeSuper); FuncIt; ++FuncIt)
		{
			UFunction* Function = *FuncIt;
			if (!Function) continue;

			// Collect parameter and return types
			for (TFieldIterator<FProperty> ParamIt(Function); ParamIt && (ParamIt->PropertyFlags & CPF_Parm); ++ParamIt)
			{
				FProperty* Param = *ParamIt;
				if (!Param) continue;

				if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Param))
				{
					FString IncludePath;

					if (GetIncludeFromField(ObjProp->PropertyClass, IncludePath))
					{
						Includes.Add(IncludePath);
					}
				}
			}

		}
	}
	// === 3. Collect types from all nodes in all graphs ===

	/*
	{
		TArray<UEdGraph*> AllGraphs;
		Blueprint->GetAllGraphs(AllGraphs);

		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph) continue;

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node) continue;

				// Add the include for the node's class
				if (UClass* NodeClass = Node->GetClass())
				{
					const FString Path = NodeClass->GetPathName().Replace(TEXT("."), TEXT("/")) + TEXT(".h");
					Includes.Add(FString::Format(TEXT("#include \"{0}\""), { Path }));
				}

				// Optional: add include for schema (if needed)
				if (const UEdGraphSchema* Schema = Graph->GetSchema())
				{
					if (UClass* SchemaClass = Schema->GetClass())
					{
						const FString Path = SchemaClass->GetPathName().Replace(TEXT("."), TEXT("/")) + TEXT(".h");
						Includes.Add(FString::Format(TEXT("#include \"{0}\""), { Path }));
					}
				}
			}
		}
	}*/

	TArray<UK2Node*> AnswerNodes = GetAllContextNodes();

	for (UK2Node* ExecNode : AnswerNodes)
	{
		UTranslatorBPToCppObject* TranslatorObject = FindTranslatorForNode(ExecNode);
		if (TranslatorObject)
		{
			Includes.Append(TranslatorObject->GenerateCppIncludeInstructions(ExecNode, this));
		}
	}

    for (FProperty* Property : GetDeclarationProperties(GeneratedClass))
    {
        FEdGraphPinType Type;
        UBlueprintNativizationLibrary::ConvertPropertyToPinType(Property, Type);
        for (UObject* TypeObject : { Type.PinSubCategoryObject.Get(), Type.PinValueType.TerminalSubCategoryObject.Get() })
        {
            UField* Field = Cast<UField>(TypeObject);
            FString Include;
            if (Field && Field != GeneratedClass && GetIncludeFromField(Field, Include))
            {
                Includes.Add(Include);
            }
        }
    }
    for (UK2Node* Node : GetAllContextNodes())
    {
        for (UEdGraphPin* Pin : Node->Pins)
        {
            FString Include;
            if (UField* DefaultField = Cast<UField>(Pin->DefaultObject))
            {
                if (DefaultField != GeneratedClass && GetIncludeFromField(DefaultField, Include))
                {
                    Includes.Add(Include);
                }
            }
        }
    }
	return Includes;
}

FString UNativizationV2Subsystem::GenerateConstructorCppCode(UBlueprint* Blueprint)
{
    TGuardValue<bool> ConstructorScope(FRetainedBlueprintGeneration::bGeneratingConstructor, true);
	if (!Blueprint || !Blueprint->GeneratedClass)
		return TEXT("// Invalid Blueprint");

	UClass* BpClass = Blueprint->GeneratedClass;
	UClass* SuperClass = BpClass->GetSuperClass();

	UObject* CDO = BpClass->GetDefaultObject();
	UObject* SuperCDO = SuperClass ? SuperClass->GetDefaultObject() : nullptr;

	FString ClassName = UBlueprintNativizationLibrary::GetUniqueFieldName(Blueprint->GeneratedClass);
	FString Content;

	auto ShouldSkipProperty = [this](FProperty* Property, const void* Container) -> bool
		{
			const bool bIsEditAnywhere = Property->HasAnyPropertyFlags(CPF_Edit);

			if (!bIsEditAnywhere)
			{
				return true;
			}

			if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_EditorOnly) ||
				Property->IsA<FMulticastDelegateProperty>() ||
				Property->IsA<FDelegateProperty>())
			{
				return true;
			}
			if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property))
			{
				UClass* PropertyClass = ObjProp->PropertyClass;
				if (PropertyClass && PropertyClass->IsChildOf(UActorComponent::StaticClass()))
				{
					return true;
				}
			}

			return false;
		};

    auto ShouldPreserveBlueprintConfiguration = [&](FProperty* Property, UObject* DefaultOwner)
    {
        if (Property->HasAnyPropertyFlags(CPF_InstancedReference | CPF_PersistentInstance))
        {
            return true;
        }
        FGetterAndSetterPropertyDescriptor Descriptor;
        UBlueprintNativizationSettingsLibrary::FindGetterAndSetterDescriptorDescriptorByProperty(Property, Descriptor);
        if (!Descriptor.SetterPropertyFunctionName.IsEmpty())
        {
            return !FRetainedBlueprintGeneration::CanInitializeDefaultSetterBeforeEngineStartup(
                Property, DefaultOwner, Descriptor.SetterPropertyFunctionName);
        }
        for (int32 ElementIndex = 0; ElementIndex < Property->ArrayDim; ++ElementIndex)
        {
            const void* Address = Property->ContainerPtrToValuePtr<void>(DefaultOwner, ElementIndex);
            if (UBlueprintNativizationLibrary::RequiresPostCDOInitialization(Property, Address, false) || !FRetainedBlueprintGeneration::CanInitializeValueBeforeEngineStartup(Property, Address,
                    !UBlueprintNativizationLibrary::IsAvailibleOneLineDefaultConstructorForProperty(Property)))
            {
                return true;
            }
        }
        return false;
    };

    FString UnsupportedDefaultProperty;
    TSet<const UObject*> VisitedDefaultObjects;
    TFunction<bool(FProperty*, const void*)> CanMigrateDefault;
    CanMigrateDefault = [&](FProperty* Property, const void* Address)
    {
        const bool bExplicitComponentTemplate = Property->GetOwnerClass() == UChildActorComponent::StaticClass() &&
            Property->GetFName() == TEXT("ChildActorTemplate");
        if (!bExplicitComponentTemplate && (Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_Deprecated | CPF_EditorOnly) ||
            Property->HasMetaData(TEXT("BlueprintCompilerGeneratedDefaults")) ||
            Property->IsA<FDelegateProperty>() || Property->IsA<FMulticastDelegateProperty>()))
        {
            if (UBlueprintNativizationLibrary::RequiresPostCDOInitialization(Property, Address, false))
            {
                UnsupportedDefaultProperty = Property->GetPathName();
                return false;
            }
            return true;
        }
        if (FStructProperty* StructureProperty = CastField<FStructProperty>(Property))
        {
            UScriptStruct* Structure = StructureProperty->Struct;
            const void* StructureMemory = Address;
            if (Structure == FInstancedStruct::StaticStruct())
            {
                const FInstancedStruct& Instance = *static_cast<const FInstancedStruct*>(Address);
                if (!Instance.IsValid())
                {
                    return true;
                }
                Structure = const_cast<UScriptStruct*>(Instance.GetScriptStruct());
                StructureMemory = Instance.GetMemory();
            }
            for (TFieldIterator<FProperty> PropertyIterator(Structure); PropertyIterator; ++PropertyIterator)
            {
                for (int32 ElementIndex = 0; ElementIndex < PropertyIterator->ArrayDim; ++ElementIndex)
                {
                    if (!CanMigrateDefault(*PropertyIterator, PropertyIterator->ContainerPtrToValuePtr<void>(StructureMemory, ElementIndex)))
                    {
                        return false;
                    }
                }
            }
        }
        else if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
        {
            FScriptArrayHelper Array(ArrayProperty, Address);
            for (int32 ElementIndex = 0; ElementIndex < Array.Num(); ++ElementIndex)
            {
                if (!CanMigrateDefault(ArrayProperty->Inner, Array.GetRawPtr(ElementIndex)))
                {
                    return false;
                }
            }
        }
        else if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
        {
            FScriptSetHelper Set(SetProperty, Address);
            for (int32 ElementIndex = 0; ElementIndex < Set.GetMaxIndex(); ++ElementIndex)
            {
                if (Set.IsValidIndex(ElementIndex) && !CanMigrateDefault(SetProperty->ElementProp, Set.GetElementPtr(ElementIndex)))
                {
                    return false;
                }
            }
        }
        else if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
        {
            FScriptMapHelper Map(MapProperty, Address);
            for (int32 ElementIndex = 0; ElementIndex < Map.GetMaxIndex(); ++ElementIndex)
            {
                if (Map.IsValidIndex(ElementIndex) &&
                    (!CanMigrateDefault(MapProperty->KeyProp, Map.GetKeyPtr(ElementIndex)) ||
                        !CanMigrateDefault(MapProperty->ValueProp, Map.GetValuePtr(ElementIndex))))
                {
                    return false;
                }
            }
        }
        else if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
        {
            if (!Property->IsA<FSoftObjectProperty>() &&
                (bExplicitComponentTemplate || Property->HasAnyPropertyFlags(CPF_InstancedReference | CPF_PersistentInstance)))
            {
                UObject* Object = ObjectProperty->GetObjectPropertyValue(Address);
                if (Object && !VisitedDefaultObjects.Contains(Object))
                {
                    VisitedDefaultObjects.Add(Object);
                    for (TFieldIterator<FProperty> PropertyIterator(Object->GetClass()); PropertyIterator; ++PropertyIterator)
                    {
                        for (int32 ElementIndex = 0; ElementIndex < PropertyIterator->ArrayDim; ++ElementIndex)
                        {
                            if (!CanMigrateDefault(*PropertyIterator, PropertyIterator->ContainerPtrToValuePtr<void>(Object, ElementIndex)))
                            {
                                return false;
                            }
                        }
                    }
                }
            }
        }
        return true;
    };
    auto InitializeCustomPrimitiveData = [&](FProperty* Property, UObject* Owner, const FString& Target)
    {
        if (Property->GetOwnerClass() != UPrimitiveComponent::StaticClass() || Property->GetFName() != TEXT("CustomPrimitiveData"))
        {
            return false;
        }
        FString Value;
        const FString Initialization = UBlueprintNativizationLibrary::GenerateManyLinesDefaultConstructorForProperty(
            Property, Property->ContainerPtrToValuePtr<void>(Owner), nullptr, EntryNodes,
            BpClass, true, FString(), Value, Target);
        FNativeGenerationContext::AddImplementationInclude(TEXT("#include \"Components/PrimitiveComponent.h\""));
        FNativeGenerationContext::AddImplementationInclude(TEXT("#include \"UObject/UnrealType.h\""));
        // The public setters cannot shrink or clear defaults; replace the reflected value before registration.
        Content += TEXT("{\nconst FCustomPrimitiveData CustomPrimitiveDataValue = [&]()\n{\n") + Initialization +
            TEXT("return ") + Value + TEXT(";\n}();\n") +
            TEXT("FStructProperty* CustomPrimitiveDataProperty = FindFProperty<FStructProperty>(UPrimitiveComponent::StaticClass(), TEXT(\"CustomPrimitiveData\"));\n") +
            TEXT("CustomPrimitiveDataProperty->CopyCompleteValue(CustomPrimitiveDataProperty->ContainerPtrToValuePtr<void>(") + Target +
            TEXT("), &CustomPrimitiveDataValue);\n") + Target + TEXT("->ResetCustomPrimitiveData();\n}\n");
        return true;
    };
    auto IsMigratedDefault = [&](FProperty* Property, UObject* Owner)
    {
        UnsupportedDefaultProperty.Reset();

        VisitedDefaultObjects.Reset();
        bool bMigrationRequired = ShouldPreserveBlueprintConfiguration(Property, Owner);
        for (int32 ElementIndex = 0; ElementIndex < Property->ArrayDim; ++ElementIndex)
        {
            const void* Address = Property->ContainerPtrToValuePtr<void>(Owner, ElementIndex);
            if (UBlueprintNativizationLibrary::RequiresPostCDOInitialization(Property, Address, false))
            {
                bMigrationRequired = true;
                if (!CanMigrateDefault(Property, Address))
                {
                    FNativeApiCatalog::AddError(TEXT("Blueprint-backed defaults cannot be preserved by migration because a nested property is not serializable: ") +
                        UnsupportedDefaultProperty + TEXT(". Default owner property: ") + Property->GetPathName());
                    break;
                }
            }
        }
        return bMigrationRequired;
    };

	for (TFieldIterator<FProperty> PropIt(BpClass); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;

		if (ShouldSkipProperty(Property, CDO))
		{
			continue;
		}

		FString CDOValueStr, SuperValueStr;

		Property->ExportText_InContainer(0, CDOValueStr, CDO, nullptr, CDO, PPF_None);

		if (SuperCDO)
		{
			if (UBlueprintNativizationLibrary::DoesFieldContainProperty(Property, SuperClass))
			{
				Property->ExportText_InContainer(0, SuperValueStr, SuperCDO, nullptr, SuperCDO, PPF_None);
			}
		}

		if (CDOValueStr.IsEmpty() && SuperValueStr.IsEmpty())
		{
			continue;
		}
		if (!CDOValueStr.Equals(SuperValueStr))
		{
            if (IsMigratedDefault(Property, CDO))
            {
                continue;
            }
            if (InitializeCustomPrimitiveData(Property, CDO, TEXT("this")))
            {
                continue;
            }
			if (!UBlueprintNativizationLibrary::IsAvailibleOneLineDefaultConstructorForProperty(Property))
			{
				void* SuperAdress = nullptr;
				void* CDOAdress = nullptr;
				if (UBlueprintNativizationLibrary::DoesFieldContainProperty(Property, SuperCDO->GetClass()))
				{
					SuperAdress = Property->ContainerPtrToValuePtr<void>(SuperCDO);
				}
				if (UBlueprintNativizationLibrary::DoesFieldContainProperty(Property, CDO->GetClass()))
				{
					CDOAdress = Property->ContainerPtrToValuePtr<void>(CDO);
				}
				FString InsideProperty;
				FGetterAndSetterPropertyDescriptor Descriptor;
				UBlueprintNativizationSettingsLibrary::FindGetterAndSetterDescriptorDescriptorByProperty(Property, Descriptor);
				if (!Descriptor.SetterPropertyFunctionName.IsEmpty())
				{
					Content += UBlueprintNativizationLibrary::GenerateManyLinesDefaultConstructorForProperty(Property, CDOAdress, nullptr, EntryNodes, BpClass, true, FString(), InsideProperty);
					Content += FString::Format(TEXT("{0}({1});\n"), {Descriptor.SetterPropertyFunctionName, InsideProperty});
				}
				else
				{
					Content += UBlueprintNativizationLibrary::GenerateManyLinesDefaultConstructorForProperty(Property, CDOAdress, SuperAdress, EntryNodes, BpClass, false, "", InsideProperty);
				}
			}
			else
			{
				FString FixedValue = UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructorForProperty(Property, Property->ContainerPtrToValuePtr<void>(CDO));
				FString CPPName = UBlueprintNativizationLibrary::GetUniquePropertyName(Property);

				FGetterAndSetterPropertyDescriptor OutGetterAndSetterDescriptorDesc;
				UBlueprintNativizationSettingsLibrary::FindGetterAndSetterDescriptorDescriptorByProperty(Property, OutGetterAndSetterDescriptorDesc);
				if (Property->GetOwnerClass() == UActorComponent::StaticClass() &&
					Property->GetFName() == TEXT("bReplicates") &&
					BpClass->IsChildOf(UActorComponent::StaticClass()))
				{
					OutGetterAndSetterDescriptorDesc.SetterPropertyFunctionName = TEXT("SetIsReplicatedByDefault");
				}
				if (Property->GetOwnerClass() == UNiagaraComponent::StaticClass() &&
					Property->GetFName() == TEXT("Asset") &&
					BpClass->IsChildOf(UNiagaraComponent::StaticClass()))
				{
                    Content += FString::Format(TEXT("SetAsset({0}, false);\n"), { FixedValue });
				}
				else if (Property->GetOwnerClass() &&
                    Property->GetOwnerClass()->GetPathName() == TEXT("/Script/GeometryCollectionEngine.GeometryCollectionComponent") &&
                    Property->GetFName() == TEXT("RestCollection"))
                {
                    {
                        Content += TEXT("RestCollection = nullptr;\nResetState();\n");
                    }
                }
				else if (!OutGetterAndSetterDescriptorDesc.SetterPropertyFunctionName.IsEmpty())
				{
					Content += *(FString::Format(TEXT("{0}({1});\n"), { OutGetterAndSetterDescriptorDesc.SetterPropertyFunctionName, FixedValue }));
				}
				else if (Property->GetOwnerStruct()->IsNative() && Property->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPrivate))
				{
					FNativeApiCatalog::AddError(FString::Format(TEXT("No accessible constructor setter for {0}.{1}."), {Property->GetOwnerStruct()->GetName(), Property->GetName()}));
				}
				else
				{
					Content += FString::Format(TEXT("\t{0} = {1};\n"), { *CPPName, FixedValue });
				}
			}
		}
	}

    TArray<FName> ComponentNames;
    const TArray<UActorComponent*> ActorComponents = UBlueprintNativizationLibrary::GetClassComponentTemplates(
        ComponentNames, Blueprint->GeneratedClass.Get(), UActorComponent::StaticClass(), true);
    TArray<FName> ParentComponentNames;
    TArray<UActorComponent*> ParentComponents;
    if (SuperCDO)
    {
        ParentComponents = UBlueprintNativizationLibrary::GetClassComponentTemplates(
            ParentComponentNames, SuperCDO->GetClass(), UActorComponent::StaticClass(), false);
    }
    FString RuntimeComponentCreation;
    FString RuntimeComponentAttachments;
    FString ConstructorComponentAttachments;
    const FString CreatedComponentsName = UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByClass(TEXT("CreatedBlueprintComponents"), BpClass);
    TSet<UActorComponent*> RuntimeComponents;
    bool bHasRuntimeRoot = false;
    for (UActorComponent* Component : ActorComponents)
    {
        if (Component && FRetainedBlueprintGeneration::IsRetained(Component->GetClass()))
        {
            RuntimeComponents.Add(Component);
            if (UBlueprintNativizationLibrary::IsRootComponentTemplate(Component))
            {
                bHasRuntimeRoot = true;
            }
        }
    }
    for (int32 ComponentIndex = 0; ComponentIndex < ActorComponents.Num(); ++ComponentIndex)
    {
        UActorComponent* ActorComponent = ActorComponents[ComponentIndex];
        if (!ActorComponent)
        {
            continue;
        }
        const int32 ComponentContentStart = Content.Len();
        int32 ComponentInitializationStart = ComponentContentStart;
        const int32 ComponentAttachmentStart = ConstructorComponentAttachments.Len();
        FString AttachmentSocketArgument;
        const FName AttachmentSocketName = UBlueprintNativizationLibrary::GetComponentAttachmentSocketName(ActorComponent);
        if (!AttachmentSocketName.IsNone())
        {
            AttachmentSocketArgument = TEXT(", FName(") + FRetainedBlueprintGeneration::Quote(AttachmentSocketName.ToString()) + TEXT(")");
        }
        FString ComponentDefaultPropertyName;
        FString ComponentVarName = UBlueprintNativizationLibrary::GetUniquePropertyComponentGetterName(ActorComponent, EntryNodes, &ComponentDefaultPropertyName);
        FString ComponentClassName = FRetainedBlueprintGeneration::GetClassName(ActorComponent->GetClass());
        UActorComponent* SuperComponent = nullptr;
        const int32 ParentIndex = ParentComponentNames.IndexOfByKey(ComponentNames[ComponentIndex]);
        if (ParentIndex != INDEX_NONE && ParentComponents[ParentIndex]->GetClass() == ActorComponent->GetClass())
        {
            SuperComponent = ParentComponents[ParentIndex];
        }
        if (RuntimeComponents.Contains(ActorComponent))
        {
            FObjectPropertyBase* ComponentProperty = FindFProperty<FObjectPropertyBase>(BpClass, ComponentNames[ComponentIndex]);
            if (!ComponentProperty)
            {
                FNativeApiCatalog::AddError(TEXT("Cannot preserve a runtime Blueprint component without its reflected actor property: ") + ActorComponent->GetPathName());
                continue;
            }
            {
                for (TFieldIterator<FProperty> PropertyIterator(ActorComponent->GetClass()); PropertyIterator; ++PropertyIterator)
                {
                    IsMigratedDefault(*PropertyIterator, ActorComponent);
                }
            }
            const FString TemplateMember = FRetainedBlueprintGeneration::RegisterRuntimeComponentTemplate(ActorComponent, ComponentProperty);
            FString RuntimeClassValue;
            {
                TGuardValue<bool> PropertyConstructorScope(FRetainedBlueprintGeneration::bGeneratingConstructor, false);
                RuntimeClassValue = FRetainedBlueprintGeneration::GetClassValue(ActorComponent->GetClass());
            }
            const FString ComponentTarget = TEXT("this->") + ComponentVarName;
            RuntimeComponentCreation += TEXT("{\nUObject* ComponentConfiguration = ") + TemplateMember + TEXT(".Get();\n") +
                TEXT("if (!IsValid(ComponentConfiguration))\n{\nComponentConfiguration = UObject::GetClass()->GetDefaultObject<") + ClassName + TEXT(">()->") + TemplateMember + TEXT(".Get();\n}\n") +
                TEXT("UClass* ComponentClass = ") + RuntimeClassValue + TEXT(";\n") +
                TEXT("if (IsValid(ComponentConfiguration))\n{\nComponentClass = ComponentConfiguration->GetClass();\n}\n") +
                TEXT("if (!IsValid(ComponentConfiguration) || !ComponentClass)\n{\nUE_LOG(LogTemp, Error, TEXT(\"Missing Blueprint component configuration for ") +
                ComponentVarName.ReplaceCharWithEscapedChar() + TEXT(" on %s.\"), *GetPathName());\n}\nelse\n{\n") +
                TEXT("if (!IsValid(") + ComponentTarget + TEXT(") || ") + ComponentTarget + TEXT("->GetOwner() != this || ") +
                ComponentTarget + TEXT("->GetClass() != ComponentClass)\n{\n") +
                TEXT("auto* ExistingComponent = FindObjectFast<UActorComponent>(this, FName(") + FRetainedBlueprintGeneration::Quote(ComponentVarName) + TEXT("));\n") +
                TEXT("if (IsValid(ExistingComponent) && ExistingComponent->GetClass() == ComponentClass)\n{\n") +
                ComponentTarget + TEXT(" = Cast<") + ComponentClassName + TEXT(">(ExistingComponent);\n}\nelse\n{\n") +
                TEXT("if (ExistingComponent)\n{\nExistingComponent->DestroyComponent();\nExistingComponent->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);\n}\n") +
                ComponentTarget + TEXT(" = NewObject<") + ComponentClassName + TEXT(">(this, ComponentClass, FName(") +
                FRetainedBlueprintGeneration::Quote(ComponentVarName) + TEXT("), RF_Transactional, ComponentConfiguration);\n") +
                CreatedComponentsName + TEXT(".Add(") + ComponentTarget + TEXT(");\n}\n}\n") +
                TEXT("if (IsValid(") + ComponentTarget + TEXT(") && ") + ComponentTarget + TEXT("->GetOwner() == this)\n{\n") +
                ComponentTarget + TEXT("->CreationMethod = EComponentCreationMethod::Native;\nAddOwnedComponent(") + ComponentTarget + TEXT(");\n}\n}\n}\n");
            if (USceneComponent* SceneComponent = Cast<USceneComponent>(ActorComponent))
            {
                const FString SceneTarget = TEXT("CastChecked<USceneComponent>(") + ComponentVarName + TEXT(")");
                if (USceneComponent* ParentComponent = UBlueprintNativizationLibrary::GetParentComponentTemplate(SceneComponent))
                {
                    const FString ParentName = UBlueprintNativizationLibrary::GetUniquePropertyComponentGetterName(ParentComponent, EntryNodes);
                    RuntimeComponentAttachments += TEXT("if (IsValid(") + ComponentVarName + TEXT(") && IsValid(") + ParentName + TEXT(") && !") + SceneTarget + TEXT("->IsRegistered() && !") + SceneTarget + TEXT("->GetAttachParent())\n{\n") +
                        SceneTarget + TEXT("->SetupAttachment(CastChecked<USceneComponent>(") + ParentName + TEXT(")") + AttachmentSocketArgument + TEXT(");\n}\n");
                }
                else if (UBlueprintNativizationLibrary::IsRootComponentTemplate(SceneComponent))
                {
                    RuntimeComponentAttachments += TEXT("if (IsValid(") + ComponentVarName + TEXT(") && (!RootComponent || ") + CreatedComponentsName + TEXT(".Contains(") + ComponentVarName + TEXT(")))\n{\nRootComponent = ") + SceneTarget + TEXT(";\n}\n");
                }
                else
                {
                    RuntimeComponentAttachments += TEXT("if (IsValid(") + ComponentVarName + TEXT(") && !") + SceneTarget + TEXT("->IsRegistered() && !") + SceneTarget + TEXT("->GetAttachParent())\n{\n") + SceneTarget + TEXT("->SetupAttachment(RootComponent") + AttachmentSocketArgument + TEXT(");\n}\n");
                }
            }
            FNativeGenerationContext::AddImplementationInclude(TEXT("#include \"Components/ActorComponent.h\""));
            FNativeGenerationContext::AddImplementationInclude(TEXT("#include \"Components/SceneComponent.h\""));
            FNativeGenerationContext::AddImplementationInclude(TEXT("#include \"UObject/UObjectGlobals.h\""));
            continue;
        }

        if (SuperComponent)
        {
            if (USceneComponent* SceneComponent = Cast<USceneComponent>(ActorComponent))
            {
                USceneComponent* ParentComponent = UBlueprintNativizationLibrary::GetParentComponentTemplate(SceneComponent);
                if (ParentComponent)
                {
                    const FString ParentName = UBlueprintNativizationLibrary::GetUniquePropertyComponentGetterName(ParentComponent, EntryNodes);
                    USceneComponent* SuperParentComponent = UBlueprintNativizationLibrary::GetParentComponentTemplate(SuperComponent);
                    const bool bHasInheritedAttachment = SuperParentComponent &&
                        UBlueprintNativizationLibrary::GetUniquePropertyComponentGetterName(SuperParentComponent, EntryNodes) == ParentName &&
                        UBlueprintNativizationLibrary::GetComponentAttachmentSocketName(SuperComponent) == AttachmentSocketName;
                    if (!bHasInheritedAttachment && FRetainedBlueprintGeneration::IsRetained(ParentComponent->GetClass()))
                    {
                        RuntimeComponentAttachments += TEXT("if (IsValid(") + ComponentVarName + TEXT(") && IsValid(") + ParentName + TEXT(") && !") + ComponentVarName + TEXT("->IsRegistered() && !") + ComponentVarName + TEXT("->GetAttachParent())\n{\n") +
                            ComponentVarName + TEXT("->SetupAttachment(CastChecked<USceneComponent>(") + ParentName + TEXT(")") + AttachmentSocketArgument + TEXT(");\n}\n");
                    }
                    else if (!bHasInheritedAttachment)
                    {
                        FString AttachmentInclude;
                        if (GetIncludeFromField(ActorComponent->GetClass(), AttachmentInclude))
                        {
                            FNativeGenerationContext::AddImplementationInclude(AttachmentInclude);
                        }
                        if (GetIncludeFromField(ParentComponent->GetClass(), AttachmentInclude))
                        {
                            FNativeGenerationContext::AddImplementationInclude(AttachmentInclude);
                        }
                        ConstructorComponentAttachments += FString::Format(TEXT("\t{0}->SetupAttachment({1}{2});\n"), { ComponentVarName, ParentName, AttachmentSocketArgument });
                    }
                }
            }
        }
		if (!SuperComponent)
		{
            FString ComponentCreationFunction = TEXT("CreateDefaultSubobject");
            if (ActorComponent->IsEditorOnly())
            {
                ComponentCreationFunction = TEXT("CreateEditorOnlyDefaultSubobject");
            }
            Content += FString::Format(TEXT("\t{0} = {2}<{1}>(TEXT(\"{0}\"));\n"), { ComponentVarName, ComponentClassName, ComponentCreationFunction });
            ComponentInitializationStart = Content.Len();

			if (USceneComponent* SceneComp = Cast<USceneComponent>(ActorComponent))
			{
				USceneComponent* ParentSceneComponent = UBlueprintNativizationLibrary::GetParentComponentTemplate(SceneComp);

				if (ParentSceneComponent)
				{
					FString ParentComponentInclude;
					if (GetIncludeFromField(ParentSceneComponent->GetClass(), ParentComponentInclude))
					{
						FNativeGenerationContext::AddImplementationInclude(ParentComponentInclude);
					}
					FString ParentComponentVarName = UBlueprintNativizationLibrary::GetUniquePropertyComponentGetterName(ParentSceneComponent, EntryNodes);
                    if (FRetainedBlueprintGeneration::IsRetained(ParentSceneComponent->GetClass()))
                    {
                        RuntimeComponentAttachments += TEXT("if (IsValid(") + ComponentVarName + TEXT(") && IsValid(") + ParentComponentVarName + TEXT(") && !") + ComponentVarName + TEXT("->IsRegistered() && !") + ComponentVarName + TEXT("->GetAttachParent())\n{\n") +
                            ComponentVarName + TEXT("->SetupAttachment(CastChecked<USceneComponent>(") + ParentComponentVarName + TEXT(")") + AttachmentSocketArgument + TEXT(");\n}\n");
                    }
                    else
                    {
                        ConstructorComponentAttachments += FString::Format(TEXT("\t{0}->SetupAttachment({1}{2});\n"), { ComponentVarName, ParentComponentVarName, AttachmentSocketArgument });
                    }
				}
				else
				{

					if (UBlueprintNativizationLibrary::IsRootComponentTemplate(SceneComp))
					{
						Content += FString::Format(TEXT("\tRootComponent = {0};\n"), { ComponentVarName });
					}
					else
					{
                        if (bHasRuntimeRoot)
                        {
                            RuntimeComponentAttachments += TEXT("if (IsValid(") + ComponentVarName + TEXT(") && RootComponent && !") + ComponentVarName + TEXT("->IsRegistered() && !") + ComponentVarName + TEXT("->GetAttachParent())\n{\n") +
                                ComponentVarName + TEXT("->SetupAttachment(RootComponent") + AttachmentSocketArgument + TEXT(");\n}\n");
                        }
                        else
                        {
                            ConstructorComponentAttachments += FString::Format(TEXT("\t{0}->SetupAttachment(RootComponent{1});\n"), { ComponentVarName, AttachmentSocketArgument });
                        }
					}
				}
			}
		}

		for (TFieldIterator<FProperty> PropIt(ActorComponent->GetClass()); PropIt; ++PropIt)
		{
			FProperty* Property = *PropIt;

			if (ShouldSkipProperty(Property, ActorComponent))
			{
				continue;
			}

			UObject* ClassComponentCDO = ActorComponent->GetClass()->GetDefaultObject();
            UObject* BaselineComponent = SuperComponent;
            if (!BaselineComponent)
            {
                BaselineComponent = ClassComponentCDO;
            }
            if (!Property->Identical_InContainer(ActorComponent, BaselineComponent))
			{
                UChildActorComponent* ChildActorComponent = Cast<UChildActorComponent>(ActorComponent);
                if (ChildActorComponent && Property->GetFName() == TEXT("ChildActorTemplate"))
                {
                    IsMigratedDefault(Property, ActorComponent);
                    if (!UnsupportedDefaultProperty.IsEmpty())
                    {
                        continue;
                    }
                    const FString TemplateMember = FRetainedBlueprintGeneration::RegisterComponentInstanceTemplate(
                        ActorComponent, Property, ChildActorComponent->GetChildActorTemplate());
                    FString DesiredClass;
                    {
                        TGuardValue<bool> PropertyConstructorScope(FRetainedBlueprintGeneration::bGeneratingConstructor, false);
                        DesiredClass = FRetainedBlueprintGeneration::GetClassValue(ChildActorComponent->GetChildActorClass());
                    }
                    if (ChildActorComponent->GetChildActorClass() && !ChildActorComponent->GetChildActorClass()->HasAnyClassFlags(CLASS_Native))
                    {
                        DesiredClass = TEXT("UObject::GetClass()->GetDefaultObject<") + ClassName + TEXT(">()->") + DesiredClass;
                    }
                    FNativeGenerationContext::AddImplementationInclude(TEXT("#include \"Components/ChildActorComponent.h\""));
                    FNativeGenerationContext::AddImplementationInclude(TEXT("#include \"UObject/UnrealType.h\""));
                    FRetainedBlueprintGeneration::AddInstanceInitialization(TEXT("if (IsValid(this->") + ComponentVarName + TEXT("))\n{\n") +
                        TEXT("AActor* ChildActorConfiguration = Cast<AActor>(UObject::GetClass()->GetDefaultObject<") + ClassName + TEXT(">()->") + TemplateMember + TEXT(".Get());\n") +
                        TEXT("UClass* DesiredChildActorClass = ") + DesiredClass + TEXT(";\n") +
                        TEXT("if (this->") + ComponentVarName + TEXT("->GetChildActorTemplate() != ChildActorConfiguration || this->") + ComponentVarName +
                        TEXT("->GetChildActorClass() != DesiredChildActorClass)\n{\n") +
                        TEXT("FObjectPropertyBase* ChildActorTemplateProperty = CastFieldChecked<FObjectPropertyBase>(FindFProperty<FProperty>(UChildActorComponent::StaticClass(), TEXT(\"ChildActorTemplate\")));\n") +
                        TEXT("ChildActorTemplateProperty->SetObjectPropertyValue_InContainer(this->") + ComponentVarName + TEXT(", ChildActorConfiguration);\n") +
                        TEXT("this->") + ComponentVarName + TEXT("->SetChildActorClass(DesiredChildActorClass);\n}\n}\n"));
                    continue;
                }
                if (IsMigratedDefault(Property, ActorComponent))
                {
                    continue;
                }
                if (InitializeCustomPrimitiveData(Property, ActorComponent, ComponentVarName))
                {
                    continue;
                }
                if (ChildActorComponent && Property->GetFName() == TEXT("ChildActorClass"))
                {
                    FNativeGenerationContext::AddImplementationInclude(TEXT("#include \"Components/ChildActorComponent.h\""));
                    Content += FString::Format(TEXT("{0}->SetChildActorClass({1});\n"),
                        { ComponentVarName, FRetainedBlueprintGeneration::GetClassValue(ChildActorComponent->GetChildActorClass()) });
                    continue;
                }
                if (ChildActorComponent && Property->GetFName() == TEXT("ChildActorTemplate"))
                {
                    FNativeGenerationContext::AddImplementationInclude(TEXT("#include \"Components/ChildActorComponent.h\""));
                    AActor* ChildActorTemplate = ChildActorComponent->GetChildActorTemplate();
                    if (ChildActorTemplate)
                    {
                        const FString TemplateValue = FRetainedBlueprintGeneration::ConstructInstancedObject(
                            CastFieldChecked<FObjectPropertyBase>(Property), ChildActorTemplate, ComponentVarName);
                        Content += TEXT("if (") + ComponentVarName + TEXT("->IsTemplate())\n{\n") +
                            FString::Format(TEXT("{0}->SetChildActorClass({1}, {2});\n"),
                                { ComponentVarName, FRetainedBlueprintGeneration::GetClassValue(ChildActorTemplate->GetClass()), TemplateValue }) + TEXT("}\n");
                    }
                    else
                    {
                        Content += FString::Format(TEXT("{0}->SetChildActorClass({0}->GetChildActorClass(), nullptr);\n"), { ComponentVarName });
                    }
                    continue;
                }
                if (FRetainedBlueprintGeneration::IsRetained(Property->GetOwnerClass()))
                {
                    if (!UBlueprintNativizationLibrary::IsAvailibleOneLineDefaultConstructorForProperty(Property))
                    {
                        FNativeApiCatalog::AddError(TEXT("Complex retained Blueprint component defaults require explicit initialization: ") + Property->GetPathName());
                        continue;
                    }
                    const FString Value = UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructorForProperty(Property, Property->ContainerPtrToValuePtr<void>(ActorComponent), ComponentVarName);
                    Content += FRetainedBlueprintGeneration::WriteProperty(Property, ComponentVarName, Value);
                    continue;
                }
                if (!UBlueprintNativizationLibrary::IsAvailibleOneLineDefaultConstructorForProperty(Property) &&
                    Property->GetOwnerStruct()->IsNative() &&
                    Property->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPrivate | CPF_NativeAccessSpecifierProtected))
                {
                    FString Value;
                    const FString Initializer = UBlueprintNativizationLibrary::GenerateManyLinesDefaultConstructorForProperty(
                        Property, Property->ContainerPtrToValuePtr<void>(ActorComponent), nullptr, EntryNodes,
                        BpClass, true, FString(), Value, ComponentVarName);
                    const FString Expression = TEXT("([&]()\n{\n") + Initializer + TEXT("return ") + Value + TEXT(";\n}())");
                    FGetterAndSetterPropertyDescriptor Descriptor;
                    UBlueprintNativizationSettingsLibrary::FindGetterAndSetterDescriptorDescriptorByProperty(Property, Descriptor);
                    if (!Descriptor.SetterPropertyFunctionName.IsEmpty())
                    {
                        Content += ComponentVarName + TEXT("->") + Descriptor.SetterPropertyFunctionName + TEXT("(") + Expression + TEXT(");\n");
                    }
                    else if (Property->HasAnyPropertyFlags(CPF_NativeAccessSpecifierProtected))
                    {
                        Content += FRetainedBlueprintGeneration::WriteProperty(Property, ComponentVarName, Expression);
                    }
                    else
                    {
                        FNativeApiCatalog::AddError(TEXT("No accessible constructor setter for ") + Property->GetPathName());
                    }
                    continue;
                }
				if (!UBlueprintNativizationLibrary::IsAvailibleOneLineDefaultConstructorForProperty(Property))
				{
					void* SuperAdress = Property->ContainerPtrToValuePtr<void>(BaselineComponent);
					void* CDOAdress = nullptr;
					if (SuperComponent)
					{
						if (UBlueprintNativizationLibrary::DoesFieldContainProperty(Property, SuperComponent->GetClass()))
						{
							SuperAdress = Property->ContainerPtrToValuePtr<void>(SuperComponent);
						}
					}
					if (ActorComponent)
					{
						if (UBlueprintNativizationLibrary::DoesFieldContainProperty(Property, ActorComponent->GetClass()))
						{
							CDOAdress = Property->ContainerPtrToValuePtr<void>(ActorComponent);
						}
					}

					FString InsideProperty;
					FGetterAndSetterPropertyDescriptor Descriptor;
					UBlueprintNativizationSettingsLibrary::FindGetterAndSetterDescriptorDescriptorByProperty(Property, Descriptor);
					if (!Descriptor.SetterPropertyFunctionName.IsEmpty())
					{
						Content += UBlueprintNativizationLibrary::GenerateManyLinesDefaultConstructorForProperty(Property, CDOAdress, nullptr, EntryNodes, BpClass, true, FString(), InsideProperty, ComponentVarName);
						Content += FString::Format(TEXT("{0}->{1}({2});\n"), {ComponentVarName, Descriptor.SetterPropertyFunctionName, InsideProperty});
					}
					else
					{
						Content += UBlueprintNativizationLibrary::GenerateManyLinesDefaultConstructorForProperty(Property, CDOAdress, SuperAdress, EntryNodes, BpClass, false, ComponentVarName + "->", InsideProperty, ComponentVarName);
					}

				}
				else
				{
					FString FixedValue = UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructorForProperty(Property, Property->ContainerPtrToValuePtr<void>(ActorComponent), ComponentVarName);
					FString PropName = UBlueprintNativizationLibrary::GetUniquePropertyName(Property);

					FGetterAndSetterPropertyDescriptor OutGetterAndSetterDescriptorDesc;
					UBlueprintNativizationSettingsLibrary::FindGetterAndSetterDescriptorDescriptorByProperty(Property, OutGetterAndSetterDescriptorDesc);
					if (Property->GetOwnerClass() == UNiagaraComponent::StaticClass() && Property->GetFName() == TEXT("Asset"))
					{
                        Content += FString::Format(TEXT("{0}->SetAsset({1}, false);\n"), { ComponentVarName, FixedValue });
					}
					else if (Property->GetOwnerClass() &&
                        Property->GetOwnerClass()->GetPathName() == TEXT("/Script/GeometryCollectionEngine.GeometryCollectionComponent") &&
                        Property->GetFName() == TEXT("RestCollection"))
                    {
                        {
                            Content += FString::Format(TEXT("{0}->RestCollection = nullptr;\n{0}->ResetState();\n"), { ComponentVarName });
                        }
                    }
					else if (!OutGetterAndSetterDescriptorDesc.SetterPropertyFunctionName.IsEmpty())
					{
						Content += *(FString::Format(TEXT("{0}->{1}({2});\n"), { ComponentVarName, OutGetterAndSetterDescriptorDesc.SetterPropertyFunctionName, FixedValue }));
					}
                    else if (Property->GetOwnerStruct()->IsNative() && Property->HasAnyPropertyFlags(CPF_NativeAccessSpecifierProtected))
                    {
                        Content += FRetainedBlueprintGeneration::WriteProperty(Property, ComponentVarName, FixedValue);
                    }
					else if (Property->GetOwnerStruct()->IsNative() && Property->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPrivate))
					{
						FNativeApiCatalog::AddError(FString::Format(TEXT("No accessible constructor setter for {0}.{1}."), {Property->GetOwnerStruct()->GetName(), Property->GetName()}));
					}
					else
					{
						Content += FString::Format(TEXT("\t{0}->{1} = {2};\n"), { ComponentVarName, PropName, FixedValue });
					}


				}
			}
		}
        if (Content.Len() != ComponentContentStart)
        {
            FString ComponentInclude;
            if (GetIncludeFromField(ActorComponent->GetClass(), ComponentInclude))
            {
                FNativeGenerationContext::AddImplementationInclude(ComponentInclude);
            }
        }
        if (ActorComponent->IsEditorOnly())
        {
            const FString ComponentContent = Content.Mid(ComponentInitializationStart);
            if (!ComponentContent.IsEmpty())
            {
                Content.LeftInline(ComponentInitializationStart);
                Content += TEXT("if (IsValid(") + ComponentVarName + TEXT("))\n{\n") + ComponentContent + TEXT("}\n");
            }
            const FString ComponentAttachments = ConstructorComponentAttachments.Mid(ComponentAttachmentStart);
            if (!ComponentAttachments.IsEmpty())
            {
                ConstructorComponentAttachments.LeftInline(ComponentAttachmentStart);
                ConstructorComponentAttachments += TEXT("if (IsValid(") + ComponentVarName + TEXT("))\n{\n") + ComponentAttachments + TEXT("}\n");
            }
        }
	}

    Content += ConstructorComponentAttachments;
    if (!RuntimeComponentCreation.IsEmpty() || !RuntimeComponentAttachments.IsEmpty())
    {
        FRetainedBlueprintGeneration::AddInstanceInitialization(TEXT("TSet<UActorComponent*> ") + CreatedComponentsName + TEXT(";\n") + RuntimeComponentCreation + RuntimeComponentAttachments);
    }

    Content += FNativeEventGeneration::GenerateConstructor(Blueprint, EntryNodes);

	TArray<UK2Node*> AnswerNodes = GetAllContextNodes();

	for (UK2Node* Node : AnswerNodes)
	{
		UTranslatorBPToCppObject* Translator = FindTranslatorForNode(Node);
		if (Translator)
		{
            bool bInitializeInstance = false;
            if (Node->IsA<UK2Node_Literal>())
            {
                continue;
            }
            if (UK2Node_ComponentBoundEvent* ComponentEvent = Cast<UK2Node_ComponentBoundEvent>(Node))
            {
                FObjectPropertyBase* ComponentProperty = FindFProperty<FObjectPropertyBase>(BpClass, ComponentEvent->GetComponentPropertyName());
                bInitializeInstance = ComponentProperty && FRetainedBlueprintGeneration::IsRetained(ComponentProperty->PropertyClass);
                const int32 BoundComponentIndex = ComponentNames.IndexOfByKey(ComponentEvent->GetComponentPropertyName());
                if (BoundComponentIndex != INDEX_NONE && RuntimeComponents.Contains(ActorComponents[BoundComponentIndex]))
                {
                    bInitializeInstance = true;
                }
            }
            TGuardValue<bool> PropertyConstructorScope(FRetainedBlueprintGeneration::bGeneratingConstructor, !bInitializeInstance);
            const FString Initialization = Translator->GenerateConstruction(Node, "", this);
            if (bInitializeInstance)
            {
                FRetainedBlueprintGeneration::AddInstanceInitialization(Initialization);
            }
            else
            {
                Content += Initialization;
                if (Node->IsA<UK2Node_ComponentBoundEvent>())
                {
                    TGuardValue<bool> InstanceBindingScope(FRetainedBlueprintGeneration::bGeneratingConstructor, false);
                    FRetainedBlueprintGeneration::AddInstanceInitialization(Translator->GenerateConstruction(Node, TEXT(""), this));
                }
            }
		}
	}

    FString ParentArguments;
    if (SuperClass && !SuperClass->HasAnyClassFlags(CLASS_Native) && !FRetainedBlueprintGeneration::IsRetained(SuperClass))
    {
        ParentArguments = TEXT("ObjectInitializer");
    }
    else if (FNativeApiPreflight::UsesObjectInitializer(FRetainedBlueprintGeneration::GetStorageClass(SuperClass)))
    {
        ParentArguments = TEXT("ObjectInitializer");
    }
    return FString::Format(TEXT("{0}::{0}(const FObjectInitializer& ObjectInitializer) : Super({2})\n{\n{1}}"), { ClassName, Content, ParentArguments });
};

bool UNativizationV2Subsystem::GetIncludeFromField(UField* OwnerField, FString& IncludePath)
{
    if (UBlueprintNativizationLibrary::IsExternalEnum(Cast<UEnum>(OwnerField)))
    {
        return false;
    }
    if (FRetainedBlueprintGeneration::IsExternalStructure(Cast<UScriptStruct>(OwnerField)))
    {
        IncludePath = TEXT("#include \"StructUtils/InstancedStruct.h\"");
        return true;
    }
    if (UClass* Class = Cast<UClass>(OwnerField))
    {
        OwnerField = FRetainedBlueprintGeneration::GetStorageClass(Class);
    }
	FString HeaderPath;

	if (OwnerField->IsA<UUserDefinedStruct>() ||
		OwnerField->IsA<UUserDefinedEnum>() ||
		OwnerField->IsA<UBlueprintGeneratedClass>())
	{
        UObject* Asset = OwnerField;
        if (UClass* GeneratedClass = Cast<UClass>(OwnerField))
        {
            Asset = GeneratedClass->ClassGeneratedBy;
        }
        FString PackagePath = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName());

		const FString ContentToken = TEXT("Content/");
		int32 ContentIndex = PackagePath.Find(ContentToken, ESearchCase::IgnoreCase, ESearchDir::FromStart);
		if (ContentIndex != INDEX_NONE)
		{
			PackagePath = PackagePath.Mid(ContentIndex + 8);
		}

        int32 LastSlash = INDEX_NONE;
        if (PackagePath.FindLastChar(TEXT('/'), LastSlash))
        {
            PackagePath.LeftInline(LastSlash);
        }
        const FString Header = FPaths::Combine(PackagePath, GetGeneratedFileName(Asset) + TEXT(".h"));
        IncludePath = FString::Format(TEXT("#include \"{0}\""), { Header });
		return true;
	}
	else
	{
		FString RelativeHeader = OwnerField->GetMetaData(TEXT("ModuleRelativePath"));
		if (!RelativeHeader.IsEmpty())
		{
			RelativeHeader.ReplaceInline(TEXT("\\"), TEXT("/"));
			RelativeHeader.RemoveFromStart(TEXT("Public/"));
			RelativeHeader.RemoveFromStart(TEXT("Classes/"));
            if (RelativeHeader.StartsWith(TEXT("Private/")))
            {
                return false;
            }
            FString ModuleDirectory;
            const FString ModuleName = FPackageName::GetShortName(OwnerField->GetOutermost());
            if (FSourceCodeNavigation::FindModulePath(ModuleName, ModuleDirectory))
            {
                if (!FPaths::FileExists(FPaths::Combine(ModuleDirectory, TEXT("Public"), RelativeHeader)) &&
                    !FPaths::FileExists(FPaths::Combine(ModuleDirectory, TEXT("Classes"), RelativeHeader)) &&
                    !FPaths::FileExists(FPaths::Combine(ModuleDirectory, RelativeHeader)))
                {
                    return false;
                }
            }
            if (!RelativeHeader.StartsWith(TEXT("Private/")))
			{
				IncludePath = FString::Format(TEXT("#include \"{0}\""), { RelativeHeader });
				return true;
			}
		}
		if (FSourceCodeNavigation::FindClassHeaderPath(OwnerField, HeaderPath))
		{
			FString ModuleName;
			if (UBlueprintNativizationLibrary::FindFieldModuleName(OwnerField, ModuleName))
			{
				TArray<FString> Modules;
				ModuleName.ParseIntoArray(Modules, TEXT("-"), true);

				FString OutModulePath;
				if (FSourceCodeNavigation::FindModulePath(Modules[1], OutModulePath))
				{
					FPaths::NormalizeDirectoryName(OutModulePath);
					FPaths::NormalizeFilename(HeaderPath);


					if (HeaderPath.StartsWith(OutModulePath))
					{
						FString RelativePath = HeaderPath.Mid(OutModulePath.Len());
						if (RelativePath.StartsWith(TEXT("/")) || RelativePath.StartsWith(TEXT("\\")))
						{
							RelativePath = RelativePath.Mid(1);
						}
						RelativePath.ReplaceInline(TEXT("\\"), TEXT("/"));

                        if (RelativePath.StartsWith(TEXT("Private/")))
                        {
                            return false;
                        }
                        TArray<FString> DefaultImportNamespaces = { TEXT("Public/"), TEXT("Classes/") };

						for (const FString& DefaultImportNamespace : DefaultImportNamespaces)
						{
							RelativePath = UBlueprintNativizationDataLibrary::CutBeforeAndIncluding(RelativePath, DefaultImportNamespace);
						}
						IncludePath = FString::Format(TEXT("#include \"{0}\""), { *RelativePath });
						return true;
					}
				}
			}
		}
	}


	return false;
}

TArray<FGenerationNodeInstance> UNativizationV2Subsystem::GetNodeInstances(UK2Node* EntryNode,
    const TArray<UK2Node*>& MacroStack, bool bIncludeInputs)
{
    TArray<FGenerationNodeInstance> Result;
    TArray<FGenerationNodeInstance> Pending = {{EntryNode, MacroStack}};
    TSet<FString> Visited;
    while (!Pending.IsEmpty())
    {
        FGenerationNodeInstance Current = Pending.Pop();
        if (!Current.Node)
        {
            continue;
        }
        TGuardValue<TArray<UK2Node*>> MacroScope(CurrentMacroStack, Current.MacroStack);
        const FString Key = GetEvaluationKey(Current.Node);
        if (Visited.Contains(Key))
        {
            continue;
        }
        Visited.Add(Key);
        Result.Add(Current);
        for (UEdGraphPin* Pin : Current.Node->Pins)
        {
            const bool bExecution = Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
            const bool bInput = bIncludeInputs && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec;
            if (!bExecution && !bInput)
            {
                continue;
            }
            TArray<UK2Node*> Path = Current.MacroStack;
            if (UEdGraphPin* Destination = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Pin, 0, false, Path, this))
            {
                UK2Node* Next = Cast<UK2Node>(Destination->GetOwningNode());
                if (Next && Destination->Direction != Pin->Direction)
                {
                    Pending.Add({Next, Path});
                }
            }
        }
    }
    return Result;
}

TArray<UK2Node*> UNativizationV2Subsystem::GetAllContextNodes()
{
    TArray<UK2Node*> Result;
    for (UK2Node* Timeline : TimelineInitializationNodes)
    {
        Result.AddUnique(Timeline);
    }
    for (const FGenerateFunctionStruct& Entry : EntryNodes)
    {
        for (const FGenerationNodeInstance& Instance : GetNodeInstances(Entry.Node, Entry.MacroStack, true))
        {
            Result.AddUnique(Instance.Node);
        }
    }
    return Result;
}


TSet<FString> UNativizationV2Subsystem::GetInputParametersForEntryNode(UK2Node* Node, bool bReflectedDeclaration)
{
	TSet<FString> Parameters;

	FGenerateFunctionStruct GenerateFunctionStruct;
	UBlueprintNativizationDataLibrary::FindGenerateFunctionStructByNode(EntryNodes, Node, GenerateFunctionStruct);
    if (GenerateFunctionStruct.bIsLatentContinuation)
    {
        return Parameters;
    }
    const TSharedPtr<FJsonObject> NativeImplementation = FNativeEventGeneration::FindImplementation(GenerateFunctionStruct.OriginalUFunction);
    const UFunction* RootFunction = UBlueprintNativizationLibrary::GetRootFunctionDeclaration(GenerateFunctionStruct.OriginalUFunction);
    int32 NativeParameterIndex = 0;
	if (GenerateFunctionStruct.OriginalUFunction)
	{
		for (TFieldIterator<FProperty> It(GenerateFunctionStruct.OriginalUFunction); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* Property = *It;
			if (Property && !Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
                if (NativeImplementation)
                {
                    const TSharedPtr<FJsonObject> NativeParameter = NativeImplementation->GetArrayField(TEXT("Parameters"))[NativeParameterIndex++]->AsObject();
                    Parameters.Add(NativeParameter->GetStringField(TEXT("Type")) + TEXT(" ") +
                        UBlueprintNativizationLibrary::GetUniquePropertyName(Property));
                    continue;
                }
                FString ParameterType = UBlueprintNativizationLibrary::GetPropertyType(Property);
                if (RootFunction && RootFunction->GetOwnerClass()->HasAnyClassFlags(CLASS_Native) &&
                    Property->IsA<FObjectProperty>() && Property->HasAnyPropertyFlags(CPF_ConstParm) &&
                    !ParameterType.StartsWith(TEXT("const ")))
                {
                    ParameterType = TEXT("const ") + ParameterType;
                }
                if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
                {
                    if (ByteProperty->Enum &&
                        (GenerateFunctionStruct.OriginalUFunction->GetOwnerClass()->HasAnyClassFlags(CLASS_Native) ||
                            !Property->HasAnyPropertyFlags(CPF_OutParm | CPF_ReferenceParm)))
                    {
                        ParameterType = UBlueprintNativizationLibrary::GetUniqueFieldName(ByteProperty->Enum);
                    }
                }
                if ((Property->IsA<FTextProperty>() || Property->IsA<FStrProperty>() ||
                    Property->IsA<FArrayProperty>() || Property->IsA<FSetProperty>() ||
                    Property->IsA<FMapProperty>() || Property->IsA<FSoftObjectProperty>()) &&
                    !Property->HasAnyPropertyFlags(CPF_OutParm | CPF_ReferenceParm))
                {
                    ParameterType = TEXT("const ") + ParameterType + TEXT("&");
                }
                if (Property->HasAnyPropertyFlags(CPF_OutParm | CPF_ReferenceParm) &&
                    !(Property->IsA<FObjectPropertyBase>() && Property->HasAnyPropertyFlags(CPF_ConstParm)))
                {
                    if (!ParameterType.EndsWith(TEXT("&")))
                    {
                        ParameterType += TEXT("&");
                    }
                    if (Property->HasAnyPropertyFlags(CPF_ConstParm) && !ParameterType.StartsWith(TEXT("const ")))
                    {
                        ParameterType = TEXT("const ") + ParameterType;
                    }
                }
                if (bReflectedDeclaration && ParameterType.EndsWith(TEXT("&")) && !ParameterType.StartsWith(TEXT("const ")) &&
                    !Property->HasAnyPropertyFlags(CPF_ConstParm) &&
                    (!Property->HasAnyPropertyFlags(CPF_OutParm) || Property->HasAnyPropertyFlags(CPF_ReferenceParm)))
                {
                    ParameterType = TEXT("UPARAM(ref) ") + ParameterType;
                }
				Parameters.Add(FString::Format(TEXT("{0} {1}"), { ParameterType, UBlueprintNativizationLibrary::GetUniquePropertyName(Property) }));
			}
		}
	}
	else
	{
		Parameters = GetAllUsedLocalGenerateFunctionParameters(Node, "None");
	}
	return Parameters;
}




FString UNativizationV2Subsystem::GenerateFunctionDeclarationsCode(TSubclassOf<UObject> GeneratedClass, bool VirtualDeclarationToZeroImplementation)
{
	if (!GeneratedClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("GenerateFunctionDeclarationsBlueprintCode: Invalid Blueprint."));
		return FString();
	}

	FString PublicContent;
	FString ProtectedContent;
	FString PrivateContent;

	for (const FGenerateFunctionStruct& Entry : EntryNodes)
	{
		if (Entry.IsTransientGenerateFunction)
		{
			continue;
		}
		TSet<FString> UsedParameters;
		if (Entry.CustomParametersString.Num() > 0)
		{
			for (FString ParamString : Entry.CustomParametersString)
			{
				UsedParameters.Add(ParamString);
			}

		}
		else
		{
			UsedParameters = GetInputParametersForEntryNode(Entry.Node);
		}

        FString ReflectedParametersString = FString::Join(UsedParameters.Array(), TEXT(", "));
        if (Entry.CustomParametersString.IsEmpty())
        {
            ReflectedParametersString = FString::Join(GetInputParametersForEntryNode(Entry.Node, true).Array(), TEXT(", "));
        }

		UFunction* ParentOriginalFunction = UBlueprintNativizationLibrary::GetRootFunctionDeclaration(Entry.OriginalUFunction);

		FProperty* ReturnProperty = nullptr;
		if (ParentOriginalFunction)
		{
			ReturnProperty = ParentOriginalFunction->GetReturnProperty();
		}
		FFunctionDescriptor OutNativeDesc;
		FName OriginalFunctionName = "";
		if (Entry.OriginalUFunction)
		{
			OriginalFunctionName = Entry.OriginalUFunction->GetFName();
		}

        FString Declaration;
        FString DefaultImplementation;
        if (VirtualDeclarationToZeroImplementation)
        {
            if (ReturnProperty)
            {
                DefaultImplementation = TEXT(" { return {}; }");
            }
            else
            {
                DefaultImplementation = TEXT(" {}");
            }
        }

        const FString NetworkEventBody = UBlueprintNativizationLibrary::GetNetworkEventBodyName(Entry.OriginalUFunction);
        if (!NetworkEventBody.IsEmpty())
        {
            const FString ParametersString = FString::Join(UsedParameters.Array(), TEXT(", "));
            if (ParentOriginalFunction == Entry.OriginalUFunction)
            {
                Declaration = FString::Format(
                    TEXT("UFUNCTION({0}, meta=(NativeEventBody=\"{1}\"))\nvoid {2}({3});\nvirtual void {2}_Implementation({4});\n"
                         "UFUNCTION(BlueprintNativeEvent, Category=\"Default\")\nvoid {1}({3});\nvirtual void {1}_Implementation({4}){5};\n"),
                    {Entry.FlagsString, NetworkEventBody, Entry.Name.ToString(), ReflectedParametersString, ParametersString, DefaultImplementation});
            }
            else
            {
                Declaration = FString::Format(TEXT("virtual void {0}_Implementation({1});\n"), {NetworkEventBody, ParametersString});
            }
        }
        else if (Entry.OriginalUFunction && Entry.OriginalUFunction->HasAnyFunctionFlags(FUNC_Static))
        {
            FString ReturnType = TEXT("void");
            if (ReturnProperty)
            {
                ReturnType = UBlueprintNativizationLibrary::GetPropertyType(ReturnProperty);
            }
            Declaration = FString::Format(TEXT("UFUNCTION({0})\nstatic {1} {2}({3});\n"),
                {Entry.FlagsString, ReturnType, Entry.Name.ToString(), ReflectedParametersString});
        }
        else if (UBlueprintNativizationSettingsLibrary::FindNativeByClassAndName(GeneratedClass, OriginalFunctionName.ToString(), OutNativeDesc))
		{
            Declaration = FString::Format(TEXT("virtual void {0} override;\n"), {OutNativeDesc.GetDeclarationSignature()});
		}
		else
		{
			const FString ParametersString = FString::Join(UsedParameters.Array(), TEXT(", "));


			if (!ParentOriginalFunction)
			{
				Declaration = FString::Format(
					TEXT(
						"virtual void {1}({2});\n"
					),
					{
						*Entry.FlagsString,
						*Entry.Name.ToString(),
						*ParametersString
					}
				);
                if (Entry.bIsLatentContinuation || Entry.bRequiresReflection)
                {
                    Declaration = TEXT("UFUNCTION()\n") + Declaration;
                }
			}
			else
			{
				if (ParentOriginalFunction == Entry.OriginalUFunction &&
                    ParentOriginalFunction->GetOwnerClass()->ClassGeneratedBy == GeneratedClass->ClassGeneratedBy)
				{
					if (Entry.OriginalUFunction->HasAnyFunctionFlags(FUNC_Private))
					{
						Declaration = FString::Format(
							TEXT(
								"UFUNCTION({0})\n"
								"{1} {2}({3}){4};\n"
							),
							{
								*Entry.FlagsString,
								ReturnProperty ? UBlueprintNativizationLibrary::GetPropertyType(ReturnProperty) : TEXT("void"),
								*Entry.Name.ToString(),
								*ReflectedParametersString,
								DefaultImplementation
							}
						);
					}
					else
					{
						Declaration = FString::Format(
							TEXT(
								"UFUNCTION({0})\n"
								"{1} {2}({5});\n"
								"virtual {1} {2}_Implementation({3}) {4};\n"
							),
							{
								*Entry.FlagsString,
								ReturnProperty ? UBlueprintNativizationLibrary::GetPropertyType(ReturnProperty) : TEXT("void"),
								*Entry.Name.ToString(),
								*ParametersString,
								DefaultImplementation,
								*ReflectedParametersString
							}
						);
					}
				}
				else
				{
					Declaration = FString::Format(
						TEXT(
							"virtual {0} {1}_Implementation({2});\n"
						),
						{
							ReturnProperty ? UBlueprintNativizationLibrary::GetPropertyType(ReturnProperty) : TEXT("void"),
							*Entry.Name.ToString(),
							*ParametersString
						}
					);
				}
			}
		}

        TArray<FString> AutomaticReferenceTerms;
        if (Entry.OriginalUFunction)
        {
            for (TFieldIterator<FProperty> Parameter(Entry.OriginalUFunction); Parameter && Parameter->HasAnyPropertyFlags(CPF_Parm); ++Parameter)
            {
                if (!Parameter->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm | CPF_ReferenceParm) &&
                    (Parameter->IsA<FArrayProperty>() || Parameter->IsA<FSetProperty>() ||
                        Parameter->IsA<FMapProperty>() || Parameter->IsA<FSoftObjectProperty>()))
                {
                    AutomaticReferenceTerms.Add(UBlueprintNativizationLibrary::GetUniquePropertyName(*Parameter));
                }
            }
        }
        if (!AutomaticReferenceTerms.IsEmpty())
        {
            const FString Metadata = TEXT("AutoCreateRefTerm=\"") + FString::Join(AutomaticReferenceTerms, TEXT(",")) + TEXT("\"");
            int32 SearchPosition = 0;
            int32 FunctionPosition = Declaration.Find(TEXT("UFUNCTION("), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchPosition);
            while (FunctionPosition != INDEX_NONE)
            {
                const int32 EndPosition = Declaration.Find(TEXT(")\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, FunctionPosition);
                const int32 MetadataPosition = Declaration.Find(TEXT("meta=("), ESearchCase::CaseSensitive, ESearchDir::FromStart, FunctionPosition);
                if (MetadataPosition != INDEX_NONE && MetadataPosition < EndPosition)
                {
                    Declaration.InsertAt(MetadataPosition + 6, Metadata + TEXT(", "));
                }
                else
                {
                    Declaration.InsertAt(EndPosition, TEXT(", meta=(") + Metadata + TEXT(")"));
                }
                SearchPosition = Declaration.Find(TEXT(")\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, FunctionPosition) + 2;
                FunctionPosition = Declaration.Find(TEXT("UFUNCTION("), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchPosition);
            }
        }

        if (Entry.OriginalUFunction && !Entry.OriginalUFunction->HasAnyFunctionFlags(FUNC_Static) &&
            OutNativeDesc.FunctionName.IsEmpty() &&
            FNativeEventGeneration::IsImplementationConst(Entry.OriginalUFunction, GeneratedClass))
        {
            const FString ParametersString = FString::Join(UsedParameters.Array(), TEXT(", "));
            TSet<FString> Signatures;
            Signatures.Add(Entry.Name.ToString() + TEXT("(") + ReflectedParametersString + TEXT(")"));
            Signatures.Add(Entry.Name.ToString() + TEXT("_Implementation(") + ParametersString + TEXT(")"));
            for (const FString& Signature : Signatures)
            {
                Declaration.ReplaceInline(*Signature, *(Signature + TEXT(" const")), ESearchCase::CaseSensitive);
            }
        }

        if (OutNativeDesc.FunctionName.IsEmpty() && FNativeEventGeneration::FindImplementation(Entry.OriginalUFunction))
        {
            Declaration.ReplaceInline(TEXT(";\n"), TEXT(" override;\n"));
        }

		Declaration += TEXT("\n");

		if (!Entry.OriginalUFunction)
		{
			PublicContent += Declaration;
		}
		else
		{
			if (Entry.OriginalUFunction->HasAnyFunctionFlags(EFunctionFlags::FUNC_Public) ||
				!Entry.OriginalUFunction->HasAnyFunctionFlags(FUNC_Private | FUNC_Protected))
			{
				PublicContent += Declaration;
			}
			else if (Entry.OriginalUFunction->HasAnyFunctionFlags(EFunctionFlags::FUNC_Protected))
			{
				ProtectedContent += Declaration;
			}
			else
			{
				PrivateContent += Declaration;
			}
		}

	}

	FString Content;

	if (!PublicContent.IsEmpty())
	{
		Content += TEXT("public:\n");
		Content += PublicContent;
	}

	if (!ProtectedContent.IsEmpty())
	{
		Content += TEXT("protected:\n");
		Content += ProtectedContent;
	}

	if (!PrivateContent.IsEmpty())
	{
		Content += TEXT("private:\n");
		Content += PrivateContent;
	}

	return Content;
}




FString UNativizationV2Subsystem::GetFunctionFlagsString(UFunction* Function)
{
	TArray<FString> Flags;
	if (!Function)
	{
		return "";
	}

	// Check common UFUNCTION flags
	if (Function->HasAnyFunctionFlags(FUNC_BlueprintCallable))
	{
		Flags.Add(TEXT("BlueprintCallable"));
	}
	if (Function->HasAnyFunctionFlags(FUNC_BlueprintPure))
	{
		Flags.Add(TEXT("BlueprintPure"));
	}
	if (Function->HasAnyFunctionFlags(FUNC_BlueprintAuthorityOnly))
	{
		Flags.Add(TEXT("BlueprintAuthorityOnly"));
	}
	if (Function->HasAnyFunctionFlags(FUNC_BlueprintCosmetic))
	{
		Flags.Add(TEXT("BlueprintCosmetic"));
	}
	if (Function->HasAnyFunctionFlags(FUNC_Exec))
	{
		Flags.Add(TEXT("Exec"));
	}
	if (Function->HasAnyFunctionFlags(FUNC_Net) && Function->HasAnyFunctionFlags(FUNC_NetReliable))
	{
		Flags.Add(TEXT("Reliable"));
	}
    else if (Function->HasAnyFunctionFlags(FUNC_Net))
    {
        Flags.Add(TEXT("Unreliable"));
    }
	if (Function->HasAnyFunctionFlags(FUNC_NetMulticast))
	{
		Flags.Add(TEXT("NetMulticast"));
	}
	if (Function->HasAnyFunctionFlags(FUNC_NetServer))
	{
		Flags.Add(TEXT("Server"));
	}
	if (Function->HasAnyFunctionFlags(FUNC_NetClient))
	{
		Flags.Add(TEXT("Client"));
	}

	FString Category = Function->GetMetaData("Category");
	if (Category.IsEmpty())
	{
		Category = "Default";
	}
	Flags.Add(FString::Format(TEXT("Category=\"{0}\""), { *Category }));

    if (ShouldGenerateBlueprintNativeEvent(Function))
	{
		Flags.Add("BlueprintNativeEvent");
	}

	FString FlagsString;

	if (Flags.Num() > 0)
	{
		FlagsString = Flags[0];
		Flags.RemoveAt(0);

		for (FString Flag : Flags)
		{
			FlagsString = FlagsString + ", " + Flag;
		}
	}

	return FlagsString;
}
bool UNativizationV2Subsystem::ShouldGenerateBlueprintNativeEvent(const UFunction* Function)
{
    return !Function->HasAnyFunctionFlags(FUNC_Private | FUNC_Net | FUNC_Static);
}

FString UNativizationV2Subsystem::GetPropertyFlagsString(FProperty* Property)
{
	FString FlagsString;
	TArray<FString> Flags;

	const bool bIsEdit = Property->HasAnyPropertyFlags(CPF_Edit);
	const bool bIsBlueprintVisible = Property->HasAnyPropertyFlags(CPF_BlueprintVisible);
	const bool bIsBlueprintReadOnly = Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly);


	if (bIsEdit)
	{
        if (Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance))
        {
            Flags.Add(TEXT("EditDefaultsOnly"));
        }
        else if (Property->HasAnyPropertyFlags(CPF_DisableEditOnTemplate))
        {
            Flags.Add(TEXT("EditInstanceOnly"));
        }
        else
        {
            Flags.Add(TEXT("EditAnywhere"));
        }
	}
	else if (bIsBlueprintVisible)
	{
		Flags.Add(TEXT("VisibleAnywhere"));
	}

	if (bIsBlueprintReadOnly)
	{
		Flags.Add(TEXT("BlueprintReadOnly"));
	}
	else if (bIsBlueprintVisible)
	{
		Flags.Add(TEXT("BlueprintReadWrite"));
	}

    if (Property->HasAnyPropertyFlags(CPF_Net))
    {
        if (Property->RepNotifyFunc != NAME_None)
        {
            UFunction* Notification = Property->GetOwnerClass()->FindFunctionByName(Property->RepNotifyFunc);
            Flags.Add(TEXT("ReplicatedUsing=") + UBlueprintNativizationLibrary::GetUniqueFunctionName(Notification, TEXT("")));
        }
        else
        {
            Flags.Add(TEXT("Replicated"));
        }
    }

	if (Property->HasAnyPropertyFlags(CPF_Transient))
    {
		Flags.Add(TEXT("Transient"));
    }
    if (Property->HasAnyPropertyFlags(CPF_DuplicateTransient))
    {
        Flags.Add(TEXT("DuplicateTransient"));
    }
    if (Property->HasAnyPropertyFlags(CPF_NonPIEDuplicateTransient))
    {
        Flags.Add(TEXT("NonPIEDuplicateTransient"));
    }
	if (Property->HasAnyPropertyFlags(CPF_Config))
    {
        const FProperty* ConfigValueProperty = Property;
        bool bHasUnsupportedMapKey = false;
        while (true)
        {
            if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(ConfigValueProperty))
            {
                ConfigValueProperty = ArrayProperty->Inner;
            }
            else if (const FSetProperty* SetProperty = CastField<FSetProperty>(ConfigValueProperty))
            {
                ConfigValueProperty = SetProperty->ElementProp;
            }
            else if (const FMapProperty* MapProperty = CastField<FMapProperty>(ConfigValueProperty))
            {
                const FProperty* KeyProperty = MapProperty->KeyProp;
                if ((KeyProperty->IsA<FObjectPropertyBase>() && !KeyProperty->IsA<FClassProperty>() &&
                     !KeyProperty->IsA<FSoftObjectProperty>()) || KeyProperty->IsA<FInterfaceProperty>())
                {
                    bHasUnsupportedMapKey = true;
                }
                ConfigValueProperty = MapProperty->ValueProp;
            }
            else
            {
                break;
            }
        }
        const bool bIsUnsupportedObject = ConfigValueProperty->IsA<FObjectPropertyBase>() &&
            !ConfigValueProperty->IsA<FClassProperty>() && !ConfigValueProperty->IsA<FSoftObjectProperty>();
        if (!bHasUnsupportedMapKey && !bIsUnsupportedObject && !ConfigValueProperty->IsA<FInterfaceProperty>())
        {
            Flags.Add(TEXT("Config"));
        }
    }
	if (Property->HasAnyPropertyFlags(CPF_SaveGame))
    {
		Flags.Add(TEXT("SaveGame"));
    }

    if (Property->HasAnyPropertyFlags(CPF_ExposeOnSpawn) || Property->GetBoolMetaData(TEXT("ExposeOnSpawn")))
    {
        Flags.Add(TEXT("meta=(ExposeOnSpawn)"));
    }
	if (Property->GetMetaDataMap())
	{
		if (const FString* Category = Property->GetMetaDataMap()->Find(FName(TEXT("Category"))))
		{
			Flags.Add(FString::Format(TEXT("Category=\"{0}\""), { **Category }));
		}
	}

	if (Flags.Num() > 0)
	{
		FlagsString = FString::Join(Flags, TEXT(", "));
	}

	return FlagsString;
}




FString UNativizationV2Subsystem::GenerateDelegateMacroDeclarationBlueprintCode(UBlueprint* Blueprint)
{
    TArray<FString> Declarations;
    const TArray<FString> Suffixes = { TEXT(""), TEXT("_OneParam"), TEXT("_TwoParams"), TEXT("_ThreeParams"),
        TEXT("_FourParams"), TEXT("_FiveParams"), TEXT("_SixParams"), TEXT("_SevenParams"), TEXT("_EightParams"), TEXT("_NineParams") };
    for (TFieldIterator<FMulticastDelegateProperty> Property(Blueprint->GeneratedClass, EFieldIteratorFlags::ExcludeSuper); Property; ++Property)
    {
        const FString DelegateName = UBlueprintNativizationLibrary::GetPropertyType(*Property);
        TArray<FString> Arguments = { DelegateName };
        int32 ParameterCount = 0;
        for (TFieldIterator<FProperty> Parameter(Property->SignatureFunction); Parameter && Parameter->HasAnyPropertyFlags(CPF_Parm); ++Parameter)
        {
            if (!Parameter->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                Arguments.Add(UBlueprintNativizationLibrary::GetPropertyType(*Parameter));
                Arguments.Add(UBlueprintNativizationLibrary::GetUniquePropertyName(*Parameter));
                ++ParameterCount;
            }
        }
        if (!Suffixes.IsValidIndex(ParameterCount))
        {
            UE_LOG(LogTemp, Error, TEXT("A multicast delegate exceeds the supported native signature arity."));
            continue;
        }
        Declarations.Add(TEXT("DECLARE_DYNAMIC_MULTICAST_DELEGATE") + Suffixes[ParameterCount] + TEXT("(") + FString::Join(Arguments, TEXT(", ")) + TEXT(");"));
    }
    return FString::Join(Declarations, TEXT("\n\n"));
}

FString UNativizationV2Subsystem::GenerateVariableDeclarationBlueprintCode(UBlueprint* Blueprint, FString DirectFunctionName)
{
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		return FString();
	}

	TArray<FString> Declarations;

	for (TFieldIterator<FProperty> PropIt(Blueprint->GeneratedClass, EFieldIteratorFlags::ExcludeSuper); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;

		if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
		{
			if (StructProp->Struct && StructProp->Struct->GetFName() == FName(TEXT("PointerToUberGraphFrame")))
			{
				continue;
			}
		}

		if (UBlueprintNativizationLibrary::GetConversionPropertyFlagForProperty(Property, Blueprint) == EConversionPropertyFlag::Ignore)
		{
			continue;
		}

		FString PropertyType = UBlueprintNativizationLibrary::GetPropertyType(Property);
		FString PropertyName = UBlueprintNativizationLibrary::GetUniquePropertyName(Property);

		FString Declaration;
		if (FMulticastDelegateProperty* DelegateProp = CastField<FMulticastDelegateProperty>(Property))
		{

			FString FlagsString;
            if (Property->HasAnyPropertyFlags(CPF_BlueprintAssignable))
            {
                FlagsString = TEXT("BlueprintAssignable");
            }
            if (Property->HasAnyPropertyFlags(CPF_BlueprintCallable))
            {
                if (!FlagsString.IsEmpty())
                {
                    FlagsString += TEXT(", ");
                }
                FlagsString += TEXT("BlueprintCallable");
            }
            if (Property->HasAnyPropertyFlags(CPF_BlueprintAuthorityOnly))
            {
                if (!FlagsString.IsEmpty())
                {
                    FlagsString += TEXT(", ");
                }
                FlagsString += TEXT("BlueprintAuthorityOnly");
            }

			// UPROPERTY and declaration
			Declaration = FString::Format(
				TEXT("UPROPERTY({0})\n{1} {2};"),
				{ *FlagsString,
				  *PropertyType,
				  *PropertyName }
			);
		}
		else
		{
			FString FlagsString = GetPropertyFlagsString(Property);

			Declaration = FString::Format(
				TEXT("UPROPERTY({0})\n{1} {2};"),
				{ *FlagsString,
				  *PropertyType,
				  *PropertyName }
			);
		}

		Declarations.Add(Declaration);
	}

	return Declarations.Num() > 0 ? FString::Join(Declarations, TEXT("\n\n")) + "\n\n" : FString();
}


FString UNativizationV2Subsystem::GenerateCppFileBlueprintCode(UBlueprint* Blueprint, FString DirectFunctionName)
{
	const FString ClassName = GetGeneratedFileName(Blueprint);

	FString Content;

	if (!Blueprint)
	{
		UE_LOG(LogTemp, Warning, TEXT("GenerateHFileBlueprintCode: Invalid Blueprint."));
		return "";
	}

	Content += "\n";

	Content += GenerateConstructorCppCode(Blueprint);
    FString ReplicatedProperties;
    const FString ReplicatedClassName = UBlueprintNativizationLibrary::GetUniqueFieldName(Blueprint->GeneratedClass);
    for (TFieldIterator<FProperty> Property(Blueprint->GeneratedClass, EFieldIteratorFlags::ExcludeSuper); Property; ++Property)
    {
        if (!Property->HasAnyPropertyFlags(CPF_Net) ||
            UBlueprintNativizationLibrary::GetConversionPropertyFlagForProperty(*Property, Blueprint) == EConversionPropertyFlag::Ignore)
        {
            continue;
        }
        ELifetimeCondition Condition = COND_None;
        if (const FBPVariableDescription* Variable = Blueprint->NewVariables.FindByPredicate([&Property](const FBPVariableDescription& Candidate)
        {
            return Candidate.VarName == Property->GetFName();
        }))
        {
            Condition = Variable->ReplicationCondition;
        }
        const UEnum* Conditions = StaticEnum<ELifetimeCondition>();
        ReplicatedProperties += FString::Format(TEXT("DOREPLIFETIME_CONDITION({0}, {1}, {2});\n"),
            { ReplicatedClassName, UBlueprintNativizationLibrary::GetUniquePropertyName(*Property), Conditions->GetNameStringByValue(Condition) });
    }
    if (!ReplicatedProperties.IsEmpty())
    {
        FString ParentReplication = TEXT("Super::GetLifetimeReplicatedProps(OutLifetimeProps);\n");
        FString PrivateRegistrations;
        for (UClass* ReplicationOwner = Blueprint->ParentClass; ReplicationOwner && ReplicationOwner->HasAnyClassFlags(CLASS_Native);
            ReplicationOwner = ReplicationOwner->GetSuperClass())
        {
            const TSharedPtr<FJsonObject> OwnerType = FNativeApiCatalog::FindType(ReplicationOwner);
            if (!OwnerType)
            {
                break;
            }
            TSharedPtr<FJsonObject> Method;
            for (const TSharedPtr<FJsonValue>& CallableValue : OwnerType->GetArrayField(TEXT("Callables")))
            {
                const TSharedPtr<FJsonObject> Candidate = CallableValue->AsObject();
                if (Candidate->GetStringField(TEXT("Name")) == TEXT("GetLifetimeReplicatedProps") &&
                    Candidate->GetArrayField(TEXT("Parameters")).Num() == 1 && Candidate->GetBoolField(TEXT("Const")))
                {
                    Method = Candidate;
                    break;
                }
            }
            if (!Method)
            {
                continue;
            }
            const FString OwnerName = UBlueprintNativizationLibrary::GetUniqueFieldName(ReplicationOwner);
            if (Method->GetIntegerField(TEXT("Access")) != 3)
            {
                if (!PrivateRegistrations.IsEmpty())
                {
                    ParentReplication = OwnerName + TEXT("::GetLifetimeReplicatedProps(OutLifetimeProps);\n");
                }
                break;
            }
            const FString Body = Method->GetStringField(TEXT("Body"));
            FString Remaining = Body;
            FString OwnerRegistrations;
            TArray<TPair<int32, int32>> Statements;
            FRegexMatcher Registrations(FRegexPattern(TEXT("DOREPLIFETIME\\s*\\(\\s*") + OwnerName + TEXT("\\s*,\\s*([A-Za-z_][A-Za-z_0-9]*)\\s*\\)\\s*;")), Body);
            while (Registrations.FindNext())
            {
                const FString PropertyName = Registrations.GetCaptureGroup(1);
                FProperty* Property = FindFProperty<FProperty>(ReplicationOwner, *PropertyName);
                if (!Property || !Property->HasAnyPropertyFlags(CPF_Net))
                {
                    break;
                }
                Statements.Add({Registrations.GetMatchBeginning(), Registrations.GetMatchEnding()});
                OwnerRegistrations += FString::Format(TEXT("RegisterReplicatedLifetimeProperty(GetReplicatedProperty(StaticClass(), {0}::StaticClass(), FName(TEXT(\"{1}\"))), OutLifetimeProps, FixupParams<{2}>(FDoRepLifetimeParams()));\n"),
                    {OwnerName, PropertyName, UBlueprintNativizationLibrary::GetPropertyType(Property)});
            }
            for (int32 StatementIndex = Statements.Num() - 1; StatementIndex >= 0; --StatementIndex)
            {
                Remaining.RemoveAt(Statements[StatementIndex].Key, Statements[StatementIndex].Value - Statements[StatementIndex].Key);
            }
            const FString ParameterName = Method->GetArrayField(TEXT("Parameters"))[0]->AsObject()->GetStringField(TEXT("Name"));
            FRegexMatcher ParentCall(FRegexPattern(TEXT("Super\\s*::\\s*GetLifetimeReplicatedProps\\s*\\(\\s*") + ParameterName + TEXT("\\s*\\)\\s*;")), Remaining);
            if (ParentCall.FindNext())
            {
                Remaining.RemoveAt(ParentCall.GetMatchBeginning(), ParentCall.GetMatchEnding() - ParentCall.GetMatchBeginning());
            }
            Remaining.ReplaceInline(TEXT("{"), TEXT(""));
            Remaining.ReplaceInline(TEXT("}"), TEXT(""));
            if (!Method->GetBoolField(TEXT("AccessVerified")) || Body.IsEmpty() || !Remaining.TrimStartAndEnd().IsEmpty())
            {
                FNativeApiCatalog::AddError(TEXT("Cannot preserve the private native replication implementation: ") + ReplicationOwner->GetPathName());
                break;
            }
            PrivateRegistrations = OwnerRegistrations + PrivateRegistrations;
        }
        ParentReplication += PrivateRegistrations;
        FNativeGenerationContext::AddImplementationInclude(TEXT("#include \"Net/UnrealNetwork.h\""));
        Content += FString::Format(TEXT("\nvoid {0}::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const\n{{\n{1}{2}}}\n"),
            { ReplicatedClassName, ParentReplication, ReplicatedProperties });
    }
    FString ActorBindings;
    for (const FGenerateFunctionStruct& Entry : EntryNodes)
    {
        if (const UK2Node_ActorBoundEvent* Event = Cast<UK2Node_ActorBoundEvent>(Entry.Node))
        {
            if (Event->EventOwner)
            {
                ActorBindings += FString::Format(TEXT(
                    "for (AActor* EventOwner : GetLevel()->Actors)\n{\n"
                    "if (IsValid(EventOwner) && EventOwner->GetFName() == FName(TEXT(\"{0}\")))\n{\n"
                    "if (FMulticastDelegateProperty* EventProperty = FindFProperty<FMulticastDelegateProperty>(EventOwner->GetClass(), TEXT(\"{1}\")))\n{\n"
                    "FScriptDelegate EventCallback;\nEventCallback.BindUFunction(this, TEXT(\"{2}\"));\n"
                    "EventProperty->AddDelegate(EventCallback, EventOwner);\n}\nbreak;\n}\n}\n"),
                    { Event->EventOwner->GetName().ReplaceCharWithEscapedChar(),
                        UBlueprintNativizationLibrary::GetUniquePropertyName(Event->GetTargetDelegateProperty()), Entry.Name.ToString() });
            }
        }
    }
    if (!ActorBindings.IsEmpty())
    {
        Content += FString::Format(TEXT("void {0}::PostInitializeComponents()\n{\nSuper::PostInitializeComponents();\n{1}}\n"),
            { UBlueprintNativizationLibrary::GetUniqueFieldName(Blueprint->GeneratedClass), ActorBindings });
    }
	Content += "\n\n";
	if (Blueprint->GeneratedClass->IsChildOf(APawn::StaticClass()) ||
        (Blueprint->GeneratedClass->IsChildOf(AActor::StaticClass()) && GetAllContextNodes().ContainsByPredicate([](UK2Node* Node)
        {
            return UInputEventTranslatorObject::HasInputBinding(Node);
        })))
	{
		Content += GenerateSetupPlayerInputComponentCppCode(Blueprint);
		Content += "\n";
	}
    else if (Blueprint->GeneratedClass->IsChildOf(UActorComponent::StaticClass()) &&
        GetAllContextNodes().ContainsByPredicate([](UK2Node* Node)
        {
            return UInputEventTranslatorObject::HasInputBinding(Node);
        }))
    {
        Content += GenerateSetupPlayerInputComponentCppCode(Blueprint);
    }

	for (FGenerateFunctionStruct EntryNode : EntryNodes)
	{
		if (!EntryNode.IsTransientGenerateFunction)
		{
			Content += ProcessGraphsForCodeGeneration(EntryNode);
		}
	}

	Content = UBlueprintNativizationDataLibrary::FormatNamespaceCodeStyle(Content);
	return Content;
}

TArray<FGenerateFunctionStruct> UNativizationV2Subsystem::GenerateAllGenerateFunctionStruct(UBlueprint* Blueprint, FString DirectFunctionName, TArray<UEdGraph*> FilterGraphs)
{
	TArray<UEdGraph*> Graphs;
	if (FilterGraphs.IsEmpty())
	{
		FGenerationGraphContext::GetGraphs(Blueprint, Graphs);
	}
	else
	{
		Graphs = FilterGraphs;
	}


    PersistentEntryParameters.Reset();
    TimelineInitializationNodes.Reset();
    if (DirectFunctionName.IsEmpty() || DirectFunctionName == TEXT("None"))
    {
        for (int32 GraphIndex = 0; GraphIndex < Graphs.Num(); ++GraphIndex)
        {
            for (UEdGraphNode* GraphNode : Graphs[GraphIndex]->Nodes)
            {
                if (UK2Node_Timeline* Timeline = Cast<UK2Node_Timeline>(GraphNode))
                {
                    TimelineInitializationNodes.AddUnique(Timeline);
                }
                else if (UK2Node* Node = Cast<UK2Node>(GraphNode))
                {
                    if (UEdGraph* NestedGraph = FGenerationGraphContext::GetMacroGraph(Node))
                    {
                        Graphs.AddUnique(NestedGraph);
                    }
                }
            }
        }
    }
	EntryNodes.Empty();
	if (!(DirectFunctionName.IsEmpty() || DirectFunctionName == "None"))
	{
		TArray<UK2Node*> DefaultEntryNodes;
		DefaultEntryNodes.Add(Cast<UK2Node>(UBlueprintNativizationLibrary::FindEntryNodeByFunctionName(Blueprint, FName(DirectFunctionName))));
		TArray<FGenerateFunctionStruct> NewNodes = GenerateFunctionsEntryNamesFromEntryNodes(DefaultEntryNodes);
		for (FGenerateFunctionStruct Node : NewNodes)
		{
			if (!EntryNodes.Contains(Node))
			{
				EntryNodes.Add(Node);
			}
		}
	}
	else
	{
		TArray<UK2Node*> DefaultEntryNodes;
		TArray<UEdGraph*> EdGraphs;

		FGenerationGraphContext::GetGraphs(Blueprint, EdGraphs);
		for (UEdGraph* EdGraph : EdGraphs)
		{
			if (!Blueprint->MacroGraphs.Contains(EdGraph) && !Blueprint->DelegateSignatureGraphs.Contains(EdGraph))
			{
				DefaultEntryNodes.Append(UBlueprintNativizationLibrary::GetEntryNodesPerGraph(EdGraph, false));
			}
		}

		TArray<FGenerateFunctionStruct> NewNodes = GenerateFunctionsEntryNamesFromEntryNodes(DefaultEntryNodes);
		for (FGenerateFunctionStruct Node : NewNodes)
		{
			if (!EntryNodes.Contains(Node))
			{
				EntryNodes.Add(Node);
			}
		}
	}
	return EntryNodes;
}

UTranslatorBPToCppObject* UNativizationV2Subsystem::FindTranslatorForNode(UK2Node* Node)
{
	for (UTranslatorBPToCppObject* TranslatorObject : TranslatorObjects)
	{
		if (TranslatorObject->CanApply(Node))
		{
			return TranslatorObject;
		}
	}
	return nullptr;
}

FString UNativizationV2Subsystem::ProcessGraphsForCodeGeneration(const FGenerateFunctionStruct& GenerateFunctionStruct)
{
    TGuardValue<UK2Node*> LocalVariableScope(LocalVariableEntryNode, GenerateFunctionStruct.Node);
	FString Content;

	UK2Node* Node = GenerateFunctionStruct.Node;
    UBlueprint* OwningBlueprint = FGenerationGraphContext::GetBlueprint();
    if (!OwningBlueprint)
    {
        OwningBlueprint = Node->GetBlueprint();
    }



	FString FunctionName = *GenerateFunctionStruct.Name.ToString();
	FString OriginalFunctionName;
	bool PrivateFunction = false;
	if (GenerateFunctionStruct.OriginalUFunction)
	{
		OriginalFunctionName = GenerateFunctionStruct.OriginalUFunction->GetName();
		PrivateFunction = GenerateFunctionStruct.OriginalUFunction->HasAnyFunctionFlags(FUNC_Private);
	}

	FFunctionDescriptor OutNativeDesc;
	if (UBlueprintNativizationSettingsLibrary::FindNativeByClassAndName(OwningBlueprint->GeneratedClass, OriginalFunctionName, OutNativeDesc))
	{
		TSet<FString> DeclarationUniqueParameters;
		TSet<FString> CallSuperUniqueParameters;
        TArray<FProperty*> ReflectedParameters;
        if (GenerateFunctionStruct.OriginalUFunction)
        {
            for (TFieldIterator<FProperty> Parameter(GenerateFunctionStruct.OriginalUFunction); Parameter && Parameter->HasAnyPropertyFlags(CPF_Parm); ++Parameter)
            {
                if (!Parameter->HasAnyPropertyFlags(CPF_ReturnParm))
                {
                    ReflectedParameters.Add(*Parameter);
                }
            }
        }
        int32 ParameterIndex = 0;
		for (FString BaseInputName : OutNativeDesc.ParameterSignature)
		{
            TArray<FString> SignatureWords;
            BaseInputName.ParseIntoArray(SignatureWords, TEXT(" "), true);
            FProperty* ParameterProperty = nullptr;
            if (GenerateFunctionStruct.OriginalUFunction && !SignatureWords.IsEmpty())
            {
                ParameterProperty = GenerateFunctionStruct.OriginalUFunction->FindPropertyByName(FName(*SignatureWords.Last()));
                if (ParameterProperty && !ParameterProperty->HasAnyPropertyFlags(CPF_Parm))
                {
                    ParameterProperty = nullptr;
                }
            }
            if (!ParameterProperty && ReflectedParameters.Num() == OutNativeDesc.ParameterSignature.Num())
            {
                ParameterProperty = ReflectedParameters[ParameterIndex];
            }
            ++ParameterIndex;
            if (FProperty* Property = ParameterProperty)
			{
				const FString ParameterType = BaseInputName.LeftChop(SignatureWords.Last().Len()).TrimEnd();
                DeclarationUniqueParameters.Add(ParameterType + TEXT(" ") + UBlueprintNativizationLibrary::GetUniquePropertyName(Property));
				CallSuperUniqueParameters.Add(UBlueprintNativizationLibrary::GetUniquePropertyName(Property));
			}
			else
			{
				DeclarationUniqueParameters.Add(BaseInputName);

				TArray<FString> Words;
				BaseInputName.ParseIntoArray(Words, TEXT(" "), true);
				FString LastWord = Words.Num() > 0 ? Words.Last() : BaseInputName;
				CallSuperUniqueParameters.Add(LastWord);
			}
		}
		Content = FString::Format(
			TEXT("void {0}::{1}({2})\n{\n"),
			{
				UBlueprintNativizationLibrary::GetUniqueFieldName(OwningBlueprint->GeneratedClass),
				OutNativeDesc.FunctionName,
				*FString::Join(DeclarationUniqueParameters.Array(), TEXT(", "))
			}
		);
        const TArray<UK2Node*> ExecutionNodes = UBlueprintNativizationLibrary::GetAllExecuteNodesInFunction(
            Node, EntryNodes, GenerateFunctionStruct.EntryPinName);
        const bool bCallsParentExplicitly = ExecutionNodes.ContainsByPredicate([&OriginalFunctionName](UK2Node* ExecutionNode)
        {
            UK2Node_CallParentFunction* ParentCall = Cast<UK2Node_CallParentFunction>(ExecutionNode);
            return ParentCall && ParentCall->GetTargetFunction() &&
                ParentCall->GetTargetFunction()->GetName() == OriginalFunctionName;
        });
        if (!bCallsParentExplicitly)
        {
            Content += FString::Format(TEXT("Super::{0}({1}); \n"), { OutNativeDesc.FunctionName, *FString::Join(CallSuperUniqueParameters.Array(), TEXT(", "))});
        }
	}
    else if (PrivateFunction || (GenerateFunctionStruct.OriginalUFunction && GenerateFunctionStruct.OriginalUFunction->HasAnyFunctionFlags(FUNC_Static)))
	{
		TSet<FString> Parameters;

		if (GenerateFunctionStruct.CustomParametersString.Num() > 0)
		{
			for (FString ParamString : GenerateFunctionStruct.CustomParametersString)
			{
				Parameters.Add(ParamString);
			}
		}
		else
		{
			Parameters = GetInputParametersForEntryNode(Node);
		}

		FProperty* ReturnProperty = nullptr;

		if (GenerateFunctionStruct.OriginalUFunction)
		{
			ReturnProperty = GenerateFunctionStruct.OriginalUFunction->GetReturnProperty();
		}
		Content = FString::Format(
			TEXT("{0} {1}::{2}({3})\n{\n"),
			{
				ReturnProperty ? *UBlueprintNativizationLibrary::GetPropertyType(ReturnProperty) : TEXT("void"),
				*UBlueprintNativizationLibrary::GetUniqueFieldName(OwningBlueprint->GeneratedClass),
				*FunctionName,
				*FString::Join(Parameters.Array(), TEXT(", "))
			}
		);
	}
	else
	{
		TSet<FString> Parameters;

		if (GenerateFunctionStruct.CustomParametersString.Num() > 0)
		{
			for (FString ParamString : GenerateFunctionStruct.CustomParametersString)
			{
				Parameters.Add(ParamString);
			}
		}
		else
		{
			Parameters = GetInputParametersForEntryNode(Node);
		}

		FProperty* ReturnProperty = nullptr;

		if (GenerateFunctionStruct.OriginalUFunction)
		{
            const FString NetworkEventBody = UBlueprintNativizationLibrary::GetNetworkEventBodyName(GenerateFunctionStruct.OriginalUFunction);
            if (!NetworkEventBody.IsEmpty())
            {
                FunctionName = NetworkEventBody;
            }
			FunctionName = FunctionName + "_Implementation";
			ReturnProperty = GenerateFunctionStruct.OriginalUFunction->GetReturnProperty();
		}
		Content = FString::Format(
			TEXT("{0} {1}::{2}({3})\n{\n"),
			{
				ReturnProperty ? *UBlueprintNativizationLibrary::GetPropertyType(ReturnProperty) : TEXT("void"),
				*UBlueprintNativizationLibrary::GetUniqueFieldName(OwningBlueprint->GeneratedClass),
				*FunctionName,
				*FString::Join(Parameters.Array(), TEXT(", "))
			}
		);

	}
    if (UFunction* OriginalFunction = GenerateFunctionStruct.OriginalUFunction)
    {
        if (!OriginalFunction->HasAnyFunctionFlags(FUNC_Static) && OutNativeDesc.FunctionName.IsEmpty() &&
            FNativeEventGeneration::IsImplementationConst(OriginalFunction, OwningBlueprint->GeneratedClass))
        {
            const int32 BodyPosition = Content.Find(TEXT("\n{\n"));
            if (BodyPosition != INDEX_NONE)
            {
                Content.InsertAt(BodyPosition, TEXT(" const"));
            }
        }
        if (!OriginalFunction->IsNative() && !OutNativeDesc.ParameterSignature.Num())
        {
            for (TFieldIterator<FProperty> Parameter(OriginalFunction); Parameter && Parameter->HasAnyPropertyFlags(CPF_Parm); ++Parameter)
            {
                FProperty* Property = *Parameter;
                if (Property->HasAnyPropertyFlags(CPF_ReturnParm) || !Property->HasAnyPropertyFlags(CPF_ConstParm) ||
                    (!Property->IsA<FArrayProperty>() && !Property->IsA<FSetProperty>() && !Property->IsA<FMapProperty>() &&
                     !Property->IsA<FStructProperty>()))
                {
                    continue;
                }
                if (Property->IsA<FStructProperty>())
                {
                    bool bWritesInput = false;
                    const TArray<UK2Node*> ExecutionNodes = UBlueprintNativizationLibrary::GetAllExecuteNodesInFunction(
                        Node, EntryNodes, GenerateFunctionStruct.EntryPinName);
                    for (UK2Node* Consumer : ExecutionNodes)
                    {
                        for (UEdGraphPin* Input : Consumer->Pins)
                        {
                            if (Input->Direction != EGPD_Input || Input->LinkedTo.IsEmpty() ||
                                !((Consumer->IsA<UK2Node_SetFieldsInStruct>() && Input->PinName == TEXT("StructRef")) ||
                                  (Input->PinType.bIsReference && !Input->PinType.bIsConst)))
                            {
                                continue;
                            }
                            TArray<UK2Node*> MacroStack;
                            UEdGraphPin* Source = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Input, 0, true, MacroStack, this);
                            if (!Source)
                            {
                                continue;
                            }
                            while (Source->ParentPin)
                            {
                                Source = Source->ParentPin;
                            }
                            UK2Node_VariableGet* Variable = Cast<UK2Node_VariableGet>(Source->GetOwningNode());
                            bWritesInput |= (Source->GetOwningNode() == Node && Source->PinName == Property->GetFName()) ||
                                (Variable && Variable->GetPropertyForVariable() == Property);
                        }
                    }
                    if (!bWritesInput)
                    {
                        continue;
                    }
                }
                const FString Name = UBlueprintNativizationLibrary::GetUniquePropertyName(Property);
                const FString InputName = GetNodeStateName(Node, TEXT("Input") + Name);
                Content.ReplaceInline(*(TEXT(" ") + Name + TEXT(",")), *(TEXT(" ") + InputName + TEXT(",")), ESearchCase::CaseSensitive);
                Content.ReplaceInline(*(TEXT(" ") + Name + TEXT(")")), *(TEXT(" ") + InputName + TEXT(")")), ESearchCase::CaseSensitive);
                Content += TEXT("auto ") + Name + TEXT(" = ") + InputName + TEXT(";\n");
            }
        }
    }
    Content += GenerateFunctionStruct.EntryInitializationCode;
    FString ParameterAssignments;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (const FString* PersistentParameter = PersistentEntryParameters.Find(Pin))
        {
            ParameterAssignments += *PersistentParameter + TEXT(" = ") +
                UBlueprintNativizationLibrary::GetUniquePinName(Pin, EntryNodes) + TEXT(";\n");
        }
    }
    if (!ParameterAssignments.IsEmpty())
    {
        const int32 SuperPosition = Content.Find(TEXT("Super::"));
        if (SuperPosition != INDEX_NONE)
        {
            Content.InsertAt(SuperPosition, ParameterAssignments);
        }
        else
        {
            Content += ParameterAssignments;
        }
    }
	FString LocalVariables = GenerateLocalVariablesCodeFromEntryNode(Node, GenerateFunctionStruct.MacroStack);
    FString BodyCode = GenerateControlFlowFunctions(GenerateFunctionStruct);
    LocalVariables = FGeneratedCodeCleanup::RemoveUnusedScalarDeclarations(LocalVariables, BodyCode);
	if (!LocalVariables.IsEmpty())
	{
		Content += LocalVariables;
		Content += "\n";
	}

	BodyCode = FGeneratedCodeCleanup::RemoveTrailingVoidReturn(BodyCode);
    Content += BodyCode;
	while(Content.EndsWith("\n"))
	{
		Content = Content.Left(Content.Len()-1);
	}
	Content += "\n}\n\n";
    const FString NetworkEventBody = UBlueprintNativizationLibrary::GetNetworkEventBodyName(GenerateFunctionStruct.OriginalUFunction);
    if (!NetworkEventBody.IsEmpty() && UBlueprintNativizationLibrary::GetRootFunctionDeclaration(GenerateFunctionStruct.OriginalUFunction) == GenerateFunctionStruct.OriginalUFunction)
    {
        TArray<FString> Arguments;
        for (TFieldIterator<FProperty> Parameter(GenerateFunctionStruct.OriginalUFunction); Parameter && Parameter->HasAnyPropertyFlags(CPF_Parm); ++Parameter)
        {
            if (!Parameter->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                Arguments.Add(UBlueprintNativizationLibrary::GetUniquePropertyName(*Parameter));
            }
        }
        Content = FString::Format(TEXT("void {0}::{1}_Implementation({2})\n{\n{3}({4});\n}\n\n"),
            {UBlueprintNativizationLibrary::GetUniqueFieldName(OwningBlueprint->GeneratedClass), GenerateFunctionStruct.Name.ToString(),
             FString::Join(GetInputParametersForEntryNode(Node).Array(), TEXT(", ")), NetworkEventBody, FString::Join(Arguments, TEXT(", "))}) + Content;
    }

	return Content;
}

FString UNativizationV2Subsystem::GenerateSetupPlayerInputComponentCppCode(UBlueprint* Blueprint)
{
	FString Content;
    const FString ClassName = UBlueprintNativizationLibrary::GetUniqueFieldName(Blueprint->GeneratedClass);
    const FString ModuleName = UBlueprintNativizationSettingsLibrary::GetModuleName();
    FNativeGenerationContext::AddImplementationInclude(TEXT("#include \"") + ModuleName + TEXT(".h\""));
    if (Blueprint->GeneratedClass->IsChildOf(APawn::StaticClass()))
    {
        Content = FString::Format(TEXT("void {0}::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)\n{\nSuper::SetupPlayerInputComponent(PlayerInputComponent);\n"), { ClassName });
    }
    else if (Blueprint->GeneratedClass->IsChildOf(APlayerController::StaticClass()))
    {
        Content = FString::Format(TEXT("void {0}::SetupInputComponent()\n{\nSuper::SetupInputComponent();\nUInputComponent* PlayerInputComponent = InputComponent;\n"), { ClassName });
    }
    else if (Blueprint->GeneratedClass->IsChildOf(UActorComponent::StaticClass()))
    {
        Content = FString::Format(TEXT("void {0}::OnRegister()\n{\nSuper::OnRegister();\nF{1}ComponentInputRuntime::Register(this, {0}::StaticClass(), [this](UInputComponent* PlayerInputComponent)\n{\n{0}::BindGeneratedComponentInput(PlayerInputComponent);\n});\n}\n\nvoid {0}::BindGeneratedComponentInput(UInputComponent* PlayerInputComponent)\n{\n"), { ClassName, ModuleName });
    }
    else
    {
        FNativeGenerationContext::AddImplementationInclude(TEXT("#include \"GameFramework/InputSettings.h\""));
        Content = FString::Format(TEXT("void {0}::EnableInput(APlayerController* PlayerController)\n{\nif (PlayerController && !InputComponent)\n{\nCreateInputComponent(UInputSettings::GetDefaultInputComponentClass());\n}\nSuper::EnableInput(PlayerController);\n}\n\nvoid {0}::CreateInputComponent(TSubclassOf<UInputComponent> InputComponentToCreate)\n{\nconst bool bAlreadyCreated = InputComponent != nullptr;\nSuper::CreateInputComponent(InputComponentToCreate);\nif (bAlreadyCreated || !InputComponent)\n{\nreturn;\n}\nUInputComponent* PlayerInputComponent = InputComponent;\n"), { ClassName });
    }
    TSet<UK2Node*> BoundNodes;
	for (FGenerateFunctionStruct GenerateFunctionStruct : EntryNodes)
	{
		TArray<UK2Node*> Nodes = UBlueprintNativizationLibrary::GetAllExecuteNodesInFunction(GenerateFunctionStruct.Node);
		Nodes.Append(UBlueprintNativizationLibrary::GetAllInputNodes(Nodes, false));
		for (UK2Node* Node : Nodes)
		{
            if (BoundNodes.Contains(Node))
            {
                continue;
            }
            BoundNodes.Add(Node);
			if (Node->IsNodePure() && UInputEventTranslatorObject::HasInputBinding(Node))
			{
				Content += GetMutableDefault<UInputEventTranslatorObject>()->GenerateSetupPlayerInputComponentFunction(Node, TEXT(""), this);
				continue;
			}
			if (UTranslatorBPToCppObject* TranslatorObject = FindTranslatorForNode(Node))
			{
				Content += TranslatorObject->GenerateSetupPlayerInputComponentFunction(Node, "", this);
			}
		}
	}
    if (Blueprint->GeneratedClass->IsChildOf(AActor::StaticClass()))
    {
        Content += FString::Format(TEXT("F{0}ComponentInputRuntime::BindForActor(this);\n"), { ModuleName });
    }
	Content += "};\n";

	return Content;
}


FString UNativizationV2Subsystem::GenerateGlobalFunctionVariables(UBlueprint* Blueprint)
{
    FString Content;
    for (const TPair<UEdGraphPin*, FString>& Parameter : PersistentEntryParameters)
    {
        Content += FString::Format(TEXT("UPROPERTY(Transient)\n{0} {1}{};\n"),
            { UBlueprintNativizationLibrary::GetPinType(Parameter.Key->PinType, false), Parameter.Value });
    }
    TSet<FString> ProcessedNodes;
    for (const FGenerateFunctionStruct& Entry : EntryNodes)
    {
        for (const FGenerationNodeInstance& Instance : GetNodeInstances(Entry.Node, Entry.MacroStack, true))
        {
            TGuardValue<TArray<UK2Node*>> MacroScope(CurrentMacroStack, Instance.MacroStack);
            const FString Key = GetEvaluationKey(Instance.Node);
            if (ProcessedNodes.Contains(Key))
            {
                continue;
            }
            ProcessedNodes.Add(Key);
            if (UTranslatorBPToCppObject* Translator = FindTranslatorForNode(Instance.Node))
            {
                Content += Translator->GenerateGlobalVariables(Instance.Node, TEXT(""), this);
            }
        }
    }
	return Content;
}


TArray<FGenerateFunctionStruct> UNativizationV2Subsystem::GenerateFunctionsEntryNamesFromEntryNodes(TArray<UK2Node*> BaseEntryNodes)
{
	TArray<FGenerateFunctionStruct> Result;

	for (UK2Node* K2Entry : BaseEntryNodes)
	{
		UFunction* Function = UBlueprintNativizationLibrary::GetFunctionByNode(K2Entry);
		if (Function)
		{
			if (UBlueprintNativizationLibrary::GetConversionFunctionFlagForFunction(K2Entry) == EConversionFunctionFlag::Ignore)
			{
				continue;
			}
		}
		FGenerateFunctionStruct FuncStruct;
		Result.Add(FuncStruct);
		FGenerateFunctionStruct* GenerateFunctionStruct = Result.FindByKey(FuncStruct);
		GenerateFunctionStruct->Node = K2Entry;
        GenerateFunctionStruct->bRequiresReflection = K2Entry->IsA<UK2Node_ActorBoundEvent>();
		if (Function)
		{
			GenerateFunctionStruct->OriginalUFunction = Function;
			GenerateFunctionStruct->FlagsString = GetFunctionFlagsString(Function);
		}
		GenerateFunctionStruct->Name = *UBlueprintNativizationLibrary::GetUniqueEntryNodeName(K2Entry, Result, "");
        if (!Function && K2Entry->IsA<UK2Node_Event>())
        {
            GenerateFunctionStruct->bRequiresReflection = true;
            for (UEdGraphPin* Pin : K2Entry->Pins)
            {
                if (Pin->Direction == EGPD_Output && !Pin->ParentPin && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec &&
                    Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Delegate)
                {
                    FString Type = UBlueprintNativizationLibrary::GetPinType(Pin->PinType, false);
                    if (Pin->PinType.bIsReference)
                    {
                        Type += TEXT("&");
                        if (Pin->PinType.bIsConst)
                        {
                            Type = TEXT("const ") + Type;
                        }
                    }
                    if (UEnum* Enumeration = Cast<UEnum>(Pin->PinType.PinSubCategoryObject.Get()))
                    {
                        Type = UBlueprintNativizationLibrary::GetUniqueFieldName(Enumeration);
                    }
                    GenerateFunctionStruct->CustomParametersString.Add(Type + TEXT(" ") + UBlueprintNativizationLibrary::GetUniquePinName(Pin, Result));
                }
            }
        }
	}

    TArray<UK2Node*> InitialNodes = BaseEntryNodes;
    InitialNodes.Append(TimelineInitializationNodes);
    for (UK2Node* Initial : InitialNodes)
    {
        for (const FGenerationNodeInstance& Instance : GetNodeInstances(Initial, {}, false))
        {
            TGuardValue<TArray<UK2Node*>> MacroScope(CurrentMacroStack, Instance.MacroStack);
            if (UTranslatorBPToCppObject* Translator = FindTranslatorForNode(Instance.Node))
            {
                Translator->GenerateNewFunction(Instance.Node, Instance.MacroStack, Result, this);
            }
        }
    }

	return Result;
}


/*
FString UNativizationV2Subsystem::GetDefaultValueAsLiteral(UEdGraphPin* Pin)
{
	if (!Pin)
	{
		return TEXT("");
	}

	FString PinDefaultValue = UBlueprintNativizationLibrary::GetPinDefaultValue(Pin);
	return FString::Format(TEXT("{0}"), { PinDefaultValue });


	FString PinType = UBlueprintNativizationLibrary::GetPinType(Pin, true);
	FString PinDefaultValue = UBlueprintNativizationLibrary::GetPinDefaultValue(Pin);

	if (PinType.IsEmpty())
	{
		return FString::Format(TEXT("{0}"), {PinDefaultValue});
	}
	else
	{
		return FString::Format(TEXT("{0}({1})"), { PinType , PinDefaultValue });
	}
}
*/


FGenerateResultStruct UNativizationV2Subsystem::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack)
{
    if (!Pin)
    {
        FNativeApiCatalog::AddError(TEXT("Cannot translate a missing input pin on ") + Node->GetPathName());
        return {};
    }
    if (FGenerationGraphContext::GetGenerator() != this)
    {
        return GenerateUncachedInputParameter(Node, Pin, PinIndex, MacroStack);
    }
    TGuardValue<TArray<UK2Node*>> MacroScope(CurrentMacroStack, MacroStack);
    FString Key = TEXT("Input:") + GetEvaluationKey(Node) + FString::Format(TEXT(":{0}:{1}"), {Pin->PinId.ToString(), PinIndex});
    if (bRequiresInputStorage || (Pin->PinType.bIsReference && !Pin->PinType.bIsConst))
    {
        Key += TEXT(":Storage");
    }
    if (const FGenerateResultStruct* Cached = InputValues.Find(Key))
    {
        return *Cached;
    }
    if (ActiveInputValues.Contains(Key))
    {
        FNativeApiCatalog::AddError(TEXT("Pure data dependency cycle: ") + Key);
        return {};
    }
    ActiveInputValues.Add(Key);
    const FGenerateResultStruct Result = GenerateUncachedInputParameter(Node, Pin, PinIndex, MacroStack);
    ActiveInputValues.Remove(Key);
    InputValues.Add(Key, Result);
    return Result;
}

FGenerateResultStruct UNativizationV2Subsystem::GenerateUncachedInputParameter(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack)
{
    TGuardValue<TArray<UK2Node*>> MacroScope(CurrentMacroStack, MacroStack);
	if (!Pin)
	{
		return FGenerateResultStruct(TEXT("/* null pin */"));
	}

	TGuardValue<bool> StorageScope(bRequiresInputStorage, bRequiresInputStorage || (Pin->PinType.bIsReference && !Pin->PinType.bIsConst));
    UK2Node_CallFunction* FunctionCall = Cast<UK2Node_CallFunction>(Node);
    if (FunctionCall && Pin->PinName == UEdGraphSchema_K2::PN_Self &&
        Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Interface && Pin->LinkedTo.IsEmpty() &&
        !Pin->DefaultObject && Pin->DefaultValue.IsEmpty() && !FunctionCall->IsA<UK2Node_Message>())
    {
        UClass* InterfaceClass = Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get());
        if (InterfaceClass && FunctionCall->GetBlueprint()->GeneratedClass->ImplementsInterface(InterfaceClass))
        {
            return FGenerateResultStruct(TEXT("this"));
        }
    }
    if (FunctionCall && Pin->PinName == TEXT("FunctionName") && Pin->LinkedTo.IsEmpty())
    {
        UFunction* Function = FunctionCall->GetTargetFunction();
        UEdGraphPin* ObjectPin = FunctionCall->FindPin(TEXT("Object"), EGPD_Input);
        if (Function && Function->GetOwnerClass() == UKismetSystemLibrary::StaticClass() && ObjectPin)
        {
            UClass* ReceiverClass = nullptr;
            UEdGraphPin* SourcePin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(ObjectPin, 0, true, MacroStack, this);
            if (SourcePin)
            {
                ReceiverClass = Cast<UClass>(SourcePin->PinType.PinSubCategoryObject.Get());
                if (SourcePin->PinType.PinSubCategory == UEdGraphSchema_K2::PSC_Self)
                {
                    ReceiverClass = FunctionCall->GetBlueprint()->GeneratedClass;
                }
            }
            else if (ObjectPin->DefaultObject)
            {
                ReceiverClass = ObjectPin->DefaultObject->GetClass();
            }
            else if (Function->GetMetaData(TEXT("DefaultToSelf")) == ObjectPin->PinName.ToString())
            {
                ReceiverClass = FunctionCall->GetBlueprint()->GeneratedClass;
            }
            if (ReceiverClass)
            {
                UFunction* Callback = ReceiverClass->FindFunctionByName(FName(*Pin->DefaultValue));
                if (Callback && !Callback->GetOwnerClass()->HasAnyClassFlags(CLASS_Native) &&
                    !FRetainedBlueprintGeneration::IsRetained(Callback->GetOwnerClass()))
                {
                    return FGenerateResultStruct(FString::Format(TEXT("TEXT(\"{0}\")"),
                        {UBlueprintNativizationLibrary::GetUniqueFunctionName(Callback, TEXT("")).ReplaceCharWithEscapedChar()}));
                }
            }
        }
    }
    const bool bClassFilterInput = FunctionCall && !Pin->PinType.IsContainer() && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class &&
        FNativeFunctionExpressions::IsClassFilterParameter(FunctionCall->GetTargetFunction(), Pin->PinName);
    TGuardValue<bool> ClassFilterScope(FNativeGenerationContext::bGeneratingClassFilter, bClassFilterInput);

	auto TrimUnderscores = [](FString S) -> FString
		{
			if (S.IsEmpty())
				return S;

			int32 Start = 0;
			int32 End = S.Len() - 1;

			while (Start <= End && S[Start] == TCHAR('_')) ++Start;
			while (End >= Start && S[End] == TCHAR('_')) --End;

			return (Start == 0 && End == S.Len() - 1) ? S : S.Mid(Start, End - Start + 1);
		};


	if (Pin->SubPins.Num() > 0)
	{
		FString PinCategory = UBlueprintNativizationLibrary::GetPinType(Pin->PinType, true);
		TArray<FString> Args;
        FGenerationPreparations InputPreparations;

		if (Pin->PinType.PinCategory == "struct")
		{
            UScriptStruct* Structure = Cast<UScriptStruct>(Pin->PinType.PinSubCategoryObject.Get());
            if (UBlueprintNativizationSettingsLibrary::FindStructTranslator(Structure))
            {
                FStructTranslationRequest Request;
                Request.Structure = Structure;
                Request.Operation = EStructTranslationOperation::Construction;
                for (UEdGraphPin* Member : Pin->SubPins)
                {
                    const FGenerateResultStruct Value = GenerateInputParameterCodeForNode(Node, Member, 0, MacroStack);
                    FString MemberName = Member->PinName.ToString();
                    MemberName.RemoveFromStart(Pin->PinName.ToString() + TEXT("_"));
                    Request.Arguments.Add({FName(*MemberName), Value.Code, UBlueprintNativizationLibrary::GetPinType(Member->PinType, true)});
                    InputPreparations.Append(Value.Preparations);
                }
                const FString Expression = UBlueprintNativizationSettingsLibrary::TranslateStructure(Request, &InputPreparations);
                return FGenerateResultStruct(Expression, InputPreparations);
            }
			FConstructorDescriptor OutNativeDesc;
			if (UBlueprintNativizationSettingsLibrary::FindConstructorDescriptorByStruct(Cast<UScriptStruct>(Pin->PinType.PinSubCategoryObject), OutNativeDesc))
			{
				for (FConstructorPropertyDescriptor ConstructorPropertyDescriptor : OutNativeDesc.ConstructorProperties)
				{
					UEdGraphPin* OptimalPin = UBlueprintNativizationLibrary::FindClosestPinByName(Pin->SubPins, ConstructorPropertyDescriptor.OriginalPropertyName);
					const FGenerateResultStruct Value = GenerateInputParameterCodeForNode(Node, OptimalPin, 0, MacroStack);
                    Args.Add(Value.Code);
                    InputPreparations.Append(Value.Preparations);
				}

				return FGenerateResultStruct(FString::Format(TEXT("{0}({1})"), { PinCategory, FString::Join(Args, TEXT(", ")) }), InputPreparations);
			}
            const FString ValueName = GetNodeStateName(Node, TEXT("Split") + Pin->PinName.ToString(), true);
            FString Initialization = PinCategory + TEXT(" ") + ValueName + TEXT("{};\n");
            for (UEdGraphPin* Member : Pin->SubPins)
            {
                FString MemberName = Member->PinName.ToString();
                MemberName.RemoveFromStart(Pin->PinName.ToString() + TEXT("_"));
                FProperty* Property = Structure->FindPropertyByName(FName(*MemberName));
                if (!Property)
                {
                    FNativeApiCatalog::AddError(TEXT("Cannot resolve a split structure input: ") + Structure->GetPathName() + TEXT(".") + MemberName);
                    return {};
                }
                const FGenerateResultStruct Value = GenerateInputParameterCodeForNode(Node, Member, 0, MacroStack);
                InputPreparations.Append(Value.Preparations);
                FGetterAndSetterPropertyDescriptor Descriptor;
                if (UBlueprintNativizationSettingsLibrary::FindGetterAndSetterDescriptorDescriptorByProperty(Property, Descriptor) &&
                    !Descriptor.SetterPropertyFunctionName.IsEmpty())
                {
                    Initialization += ValueName + TEXT(".") + Descriptor.SetterPropertyFunctionName + TEXT("(") + Value.Code + TEXT(");\n");
                }
                else if (Property->GetOwnerStruct()->IsNative() && Property->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPrivate | CPF_NativeAccessSpecifierProtected))
                {
                    FNativeApiCatalog::AddError(TEXT("No accessible setter for a split structure input: ") + Property->GetPathName());
                    return {};
                }
                else
                {
                    Initialization += ValueName + TEXT(".") + UBlueprintNativizationLibrary::GetUniquePropertyName(Property) + TEXT(" = ") + Value.Code + TEXT(";\n");
                }
            }
            InputPreparations.Add(Initialization);
            return FGenerateResultStruct(ValueName, InputPreparations);
		}

		for (UEdGraphPin* LocalPin : Pin->SubPins)
		{
			const FGenerateResultStruct Value = GenerateInputParameterCodeForNode(Node, LocalPin, 0, MacroStack);
            Args.Add(Value.Code);
            InputPreparations.Append(Value.Preparations);
		}

		return FGenerateResultStruct(FString::Format(TEXT("{0}({1})"), { PinCategory, FString::Join(Args, TEXT(", ")) }), InputPreparations);
	}

	TArray<UK2Node*> LocalMacroStack = MacroStack;
	UEdGraphPin* ConnectedPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Pin, PinIndex, false, LocalMacroStack, this);
    if (ConnectedPin && ConnectedPin->Direction == EGPD_Input)
    {
        Pin = ConnectedPin;
    }

	if (ConnectedPin && ConnectedPin->Direction == EEdGraphPinDirection::EGPD_Output)
	{
		UK2Node* ConnectedNode = Cast<UK2Node>(ConnectedPin->GetOwningNode());
		FGenerateResultStruct OutputCode;

		UTranslatorBPToCppObject* TranslatorObject = FindTranslatorForNode(ConnectedNode);

        if (const FString* PersistentParameter = PersistentEntryParameters.Find(UBlueprintNativizationLibrary::GetParentPin(ConnectedPin)))
        {
            OutputCode = *PersistentParameter;
        }
		else if (TranslatorObject)
		{
            TGuardValue<TArray<UK2Node*>> ProducerMacroScope(CurrentMacroStack, LocalMacroStack);
            UEdGraphPin* ProducerPin = UBlueprintNativizationLibrary::GetParentPin(ConnectedPin);
            FString ValueKey = TEXT("Value:") + GetEvaluationKey(ConnectedNode) + TEXT(":") + ProducerPin->PinId.ToString();
            if (bRequiresInputStorage)
            {
                ValueKey += TEXT(":Storage");
            }
            const bool bPureValue = ConnectedNode->IsNodePure();
            const FGenerateResultStruct* CachedValue = InputValues.Find(ValueKey);
            if (bPureValue && CachedValue)
            {
                OutputCode = *CachedValue;
            }
            else
            {
                if (ActiveInputValues.Contains(ValueKey))
                {
                    FNativeApiCatalog::AddError(TEXT("Pure data dependency cycle: ") + ValueKey);
                    return FGenerateResultStruct();
                }
                ActiveInputValues.Add(ValueKey);
                OutputCode = TranslatorObject->GenerateInputParameterCodeForNode(ConnectedNode, ProducerPin, PinIndex, LocalMacroStack, this);
                ActiveInputValues.Remove(ValueKey);
                if (bPureValue)
                {
                    InputValues.Add(ValueKey, OutputCode);
                }
            }
			if (!TranslatorObject->CanContinueGenerateInputParameterCodeForNode(ConnectedNode, ConnectedPin, PinIndex, LocalMacroStack, this))
			{
				return OutputCode;
			}
		}
		else
		{
			if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(ConnectedNode))
			{
				if (UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(EventNode))
				{	
					UEdGraphPin* DelegatePin = CustomEvent->GetDelegatePin();
					if (CustomEvent->GetDelegatePin() == ConnectedPin)
					{
                        const FGenerateFunctionStruct* Entry = EntryNodes.FindByPredicate([&](const FGenerateFunctionStruct& Candidate)
                        {
                            return Candidate.Node == CustomEvent;
                        });
                        FString CallbackName = GetNodeStateName(CustomEvent, TEXT("Callback"), true);
                        if (DataConsumer)
                        {
                            CallbackName = GetNodeStateName(DataConsumer, InputEvaluationPurpose + CallbackName, true);
                        }
                        FGenerateResultStruct Callback(CallbackName);
                        Callback.Preparations.Add(FString::Format(TEXT("FScriptDelegate {0};\n{0}.BindUFunction(this, TEXT(\"{1}\"));\n"), {CallbackName, Entry->Name.ToString()}));
                        return Callback;
					}
				}
                FProperty* Parameter = UBlueprintNativizationLibrary::GetPropertyFromEntryNode(ConnectedNode, *UBlueprintNativizationLibrary::GetParentPin(ConnectedPin)->GetName());
                if (Parameter)
                {
                    OutputCode = UBlueprintNativizationLibrary::GetUniquePropertyName(Parameter);
                }
                else
                {
                    OutputCode = UBlueprintNativizationLibrary::GetUniquePinName(UBlueprintNativizationLibrary::GetParentPin(ConnectedPin), EntryNodes);
                }
			}
            else if (ConnectedNode->IsA<UK2Node_FunctionEntry>())
            {
                OutputCode = UBlueprintNativizationLibrary::GetUniquePinName(UBlueprintNativizationLibrary::GetParentPin(ConnectedPin), EntryNodes);
            }
            else
            {
                FNativeApiCatalog::AddError(TEXT("No direct input translator for ") + ConnectedNode->GetClass()->GetName() + TEXT(": ") + ConnectedNode->GetPathName());
                return {};
            }

		}

		TArray<UEdGraphPin*> PathPins = UBlueprintNativizationLibrary::GetParentPathPins(ConnectedPin);
		Algo::Reverse(PathPins);

		for(int Count = 1; Count < PathPins.Num(); Count++)
		{
			UEdGraphPin* ChildPin = nullptr;
			UEdGraphPin* ParentPin = nullptr;
			if (PathPins.IsValidIndex(Count))
			{
				ChildPin = PathPins[Count];
			}
			if (PathPins.IsValidIndex(Count-1))
			{
				ParentPin = PathPins[Count - 1];
			}
			FString ChildName = ChildPin->GetName();
			FString ParentName = ParentPin->GetName();
			FString Value = ChildName;
			Value.RemoveFromStart(ParentName);
			Value = Value.TrimStartAndEnd();
			Value = TrimUnderscores(Value);
            UScriptStruct* ParentStructure = Cast<UScriptStruct>(ParentPin->PinType.PinSubCategoryObject.Get());
            if (UBlueprintNativizationSettingsLibrary::FindStructTranslator(ParentStructure))
            {
                FStructTranslationRequest Request;
                Request.Structure = ParentStructure;
                Request.Operation = EStructTranslationOperation::ReadMember;
                Request.MemberName = FName(*Value);
                Request.SourceExpression = TEXT("(") + OutputCode.Code + TEXT(")");
                Request.DestinationType = UBlueprintNativizationLibrary::GetPinType(ChildPin->PinType, false);
                OutputCode.Code = UBlueprintNativizationSettingsLibrary::TranslateStructure(Request, &OutputCode.Preparations);
            }
            else
            {
                FGenerateResultStruct NativeMember;
                if (ParentStructure && FStructInitialization::TryReadNativeMember(ParentStructure, ParentPin,
                    FName(*Value), OutputCode.Code, this, NativeMember))
                {
                    OutputCode.Preparations.Append(NativeMember.Preparations);
                    OutputCode.Code = NativeMember.Code;
                    continue;
                }
                if (ParentStructure)
                {
                    if (FProperty* MemberProperty = ParentStructure->FindPropertyByName(FName(*Value)))
                    {
                        Value = UBlueprintNativizationLibrary::GetUniquePropertyName(MemberProperty);
                    }
                }
                OutputCode.Code += "." + Value;
            }
		}
        if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte && !Pin->PinType.PinSubCategoryObject.IsValid())
        {
            const UEnum* Enumeration = Cast<UEnum>(ConnectedPin->PinType.PinSubCategoryObject.Get());
            if (Enumeration && (Enumeration->IsA<UUserDefinedEnum>() || Enumeration->GetCppForm() == UEnum::ECppForm::EnumClass))
            {
                OutputCode.Code = TEXT("static_cast<uint8>(") + OutputCode.Code + TEXT(")");
            }
        }

        if (bClassFilterInput)
        {
            OutputCode.Code = FRetainedBlueprintGeneration::ResolveClassFilterValue(OutputCode.Code,
                Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get()));
        }
		return OutputCode;
	}
	else
	{
        if (Pin->PinType.IsContainer() && Pin->DefaultValue.IsEmpty())
        {
            FEdGraphPinType ValueType = Pin->PinType;
            ValueType.bIsReference = false;
            ValueType.bIsConst = false;
            return UBlueprintNativizationLibrary::GetPinType(ValueType, false) + TEXT("{}");
        }
        if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object && Pin->DefaultObject && Pin->DefaultObject->IsAsset() && !Pin->DefaultObject->IsA<UEnum>())
        {
            return FRetainedBlueprintGeneration::GetAssetValue(Pin->DefaultObject,
                Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get()));
        }
        if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct && !Pin->PinType.IsContainer() && Pin->DefaultValue.IsEmpty())
        {
            return UBlueprintNativizationLibrary::GetPinType(Pin->PinType, false) + TEXT("{}");
        }
        if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct && !Pin->PinType.IsContainer())
        {
            FConstructorDescriptor Constructor;
            UScriptStruct* Structure = CastChecked<UScriptStruct>(Pin->PinType.PinSubCategoryObject.Get());
            if (!UBlueprintNativizationSettingsLibrary::FindStructTranslator(Structure) &&
                UBlueprintNativizationSettingsLibrary::FindConstructorDescriptorByStruct(Structure, Constructor))
            {
                return UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructor(Pin->GetName(), Pin->PinType,
                    Pin->DefaultValue, Pin->DefaultObject);
            }
            FStructTranslationRequest Request;
            Request.Structure = Structure;
            Request.Operation = EStructTranslationOperation::SerializedValue;
            Request.SerializedValue = Pin->DefaultValue;
            Request.OwnerClass = Node->GetBlueprint()->GeneratedClass;
            FGenerateResultStruct Result;
            Result.Code = UBlueprintNativizationSettingsLibrary::TranslateStructure(Request, &Result.Preparations);
            return Result;
        }
		if (UBlueprintNativizationLibrary::IsAvailibleOneLineDefaultConstructor(Pin->PinType))
		{
			if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Text)
			{
				return UBlueprintNativizationLibrary::GenerateTextLiteral(Pin->DefaultTextValue,
                    Node->GetBlueprint()->GetPathName(), Node->GetGraph()->GetName() + TEXT(".") +
                    Node->NodeGuid.ToString(EGuidFormats::Digits) + TEXT(".") + Pin->GetName());
			}
            if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate)
            {
                return FString(TEXT("FScriptDelegate{}"));
            }
			return UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructor(Pin->GetName(), Pin->PinType, Pin->DefaultValue, Pin->DefaultObject);
		}
		else
		{
			return UBlueprintNativizationLibrary::GetUniquePinName(Pin, EntryNodes);
		}

	}
}

TSet<FString> UNativizationV2Subsystem::GetAllUsedLocalGenerateFunctionParameters(UK2Node* EntryNode, FString EntryPinName)
{
	TSet<FString> Result;

	if (!EntryNode)
	{
		return Result;
	}

	TArray<UK2Node*> ExecNodes = UBlueprintNativizationLibrary::GetAllExecuteNodesInFunction(EntryNode, EntryNodes, EntryPinName);
	TArray<UK2Node*> PureNodes = UBlueprintNativizationLibrary::GetAllInputNodes(ExecNodes, true);

	TArray<UEdGraphPin*> DefaultInputPins = UBlueprintNativizationLibrary::GetFilteredPins(EntryNode, EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::ExecPin, EPinIncludeOnlyFilter::None);

	if (UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(EntryNode))
	{
		DefaultInputPins.Remove(CustomEvent->GetDelegatePin());
	}
	for (UEdGraphPin* EdGraphPin : DefaultInputPins)
	{
		Result.Add(EdGraphPin->GetName());
	}

	for (UK2Node* PureNode : PureNodes)
	{
		if (!PureNode)
		{
			continue;
		}

		if (UK2Node_VariableGet* VarGet = Cast<UK2Node_VariableGet>(PureNode))
		{
			const FName VarName = VarGet->VariableReference.GetMemberName();
			const FString VarNameStr = VarName.ToString();

			if (VarGet->VariableReference.IsLocalScope())
			{
				Result.Add(VarNameStr);
			}
		}
		else
		{
			TArray<UEdGraphPin*> Pins = UBlueprintNativizationLibrary::GetFilteredPins(PureNode, EPinOutputOrInputFilter::Input, EPinExcludeFilter::ExecPin | EPinExcludeFilter::DelegatePin ,EPinIncludeOnlyFilter::None);
			for (UEdGraphPin* Pin : Pins)
			{
				if (Pin->LinkedTo.Num()<=0)
				{
					continue;
				}

				UK2Node* PinNode = Cast<UK2Node>(Pin->LinkedTo[0]->GetOwningNode());
				if (!PinNode->IsNodePure())
				{
					if (!ExecNodes.Contains(PinNode))
					{
						if (EntryNode)
						{
							Result.Add(UBlueprintNativizationLibrary::GetUniquePinName(Pin->LinkedTo[0], EntryNodes));
						}
					}
				}
			}
		}

	}

	return Result;
}

FString UNativizationV2Subsystem::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, FGenerationPreparations Preparations, TArray<UK2Node*> MacroStack)
{
    if (Node)
    {
        TGuardValue<TArray<UK2Node*>> SourceMacroScope(CurrentMacroStack, MacroStack);
        MacroStack = FGenerationGraphContext::GetMacroStack(Node);
    }
    if (UK2Node_Knot* Knot = Cast<UK2Node_Knot>(Node))
    {
        UEdGraphPin* Target = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Knot->GetInputPin(), 0, false, MacroStack, this);
        if (!Target)
        {
            return {};
        }
        return GenerateCodeFromNode(CastChecked<UK2Node>(Target->GetOwningNode()), Target->GetName(), VisitedNodes, Preparations, MacroStack);
    }
    if (Node && (FGenerationGraphContext::GetMacroGraph(Node) || Node->IsA<UK2Node_Tunnel>()) && !FindTranslatorForNode(Node))
    {
        UEdGraphPin* Input = Node->FindPin(FName(*EntryPinName), EGPD_Input);
        if (!Input)
        {
            FNativeApiCatalog::AddError(TEXT("Cannot resolve a macro execution input: ") + Node->GetPathName() + TEXT(".") + EntryPinName);
            return {};
        }
        UEdGraphPin* Target = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Input, 0, false, MacroStack, this);
        if (!Target)
        {
            return {};
        }
        if (Target->GetOwningNode() == Node)
        {
            FNativeApiCatalog::AddError(TEXT("Cannot resolve a macro execution boundary: ") + Node->GetPathName() + TEXT(".") + EntryPinName);
            return {};
        }
        return GenerateCodeFromNode(CastChecked<UK2Node>(Target->GetOwningNode()), Target->GetName(), VisitedNodes, Preparations, MacroStack);
    }
    if (DeferredExecutionNodes.Contains(Node))
    {
        return FString();
    }

	if (!Node)
	{
		return TEXT("");
	}

    FVisitedNodeStack Execution(Node, EntryPinName);
    Execution.MacroStack = MacroStack;
    const int32 ControlFunctionIndex = ControlFunctionEntries.IndexOfByKey(Execution);
    if (ControlFunctionIndex != INDEX_NONE && !bGeneratingControlFunctionEntry)
    {
        return GenerateControlFunctionCall(ControlFunctionIndex);
    }
    bGeneratingControlFunctionEntry = false;
    if (VisitedNodes.Contains(Execution))
    {
        UE_LOG(LogTemp, Error, TEXT("An execution cycle has no generated continuation function."));
        return FString();
    }

	FString Content;
	Content = "";

	//ControlFlowNodeStart
	const FGenerateFunctionStruct* MatchedEntry = EntryNodes.FindByPredicate([&](const FGenerateFunctionStruct& Entry)
    {
        return Entry.Node == Node && Entry.MacroStack == MacroStack && (!Entry.bIsLatentContinuation || Entry.EntryPinName == EntryPinName);
    });
	if (VisitedNodes.Num() > 0 && MatchedEntry)
	{
		if (MatchedEntry->CustomParametersString.IsEmpty() || !MatchedEntry->CustomArguments.IsEmpty())
		{
			Content += MatchedEntry->Name.ToString() + TEXT("(") + FString::Join(MatchedEntry->CustomArguments, TEXT(", ")) + TEXT(");\n");
			return Content;
		}
	}
	VisitedNodes.Add(Execution);
    for (const TPair<FString, FGenerateResultStruct>& PreviousCall : PureFunctionCalls)
    {
        Preparations = Preparations.Difference(PreviousCall.Value.Preparations);
    }
    TGuardValue<UK2Node*> ConsumerScope(DataConsumer, Node);
    TGuardValue<TMap<FString, FGenerateResultStruct>> PureCallScope(PureFunctionCalls, {});
    TGuardValue<TMap<FString, FGenerateResultStruct>> InputScope(InputValues, {});
    TGuardValue<TSet<FString>> ActiveInputScope(ActiveInputValues, {});
    TGuardValue<FGenerationPreparations> EmittedInputScope(EmittedInputPreparations, {});
    TGuardValue<uint64> EvaluationScope(CurrentEvaluation, ++EvaluationSequence);
    TGuardValue<TArray<UK2Node*>> MacroScope(CurrentMacroStack, MacroStack);

    UTranslatorBPToCppObject* TranslatorObject = FindTranslatorForNode(Node);
    if (!TranslatorObject || TranslatorObject->ShouldPrepareInputs(Node, EntryPinName))
    {
        Content += GeneratePurePreparations(Node, MacroStack, Preparations);
    }

	if (IsValid(TranslatorObject))
	{
		Content += TranslatorObject->GenerateCodeFromNode(Node, EntryPinName, VisitedNodes, MacroStack, Preparations, this);
		if (!TranslatorObject->CanContinueCodeGeneration(Node, EntryPinName))
		{
			return Content;
		}
	}

    if (!TranslatorObject && !Node->IsA<UK2Node_Event>() && !Node->IsA<UK2Node_FunctionEntry>())
    {
        FNativeApiCatalog::AddError(TEXT("No direct execution translator for ") + Node->GetClass()->GetName() + TEXT(": ") + Node->GetPathName());
        return {};
    }
	//ControlFlowNodeEnd

	//SimpleNodeStart

	//BaseContinuePin
	TArray<UEdGraphPin*> OutExecPins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::None, EPinIncludeOnlyFilter::ExecPin); // Blueprint-���������

	for (UEdGraphPin* OutExecPin : OutExecPins)
	{
		TArray<UK2Node*> LocalMacroStack = MacroStack;
		UEdGraphPin* GraphPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(OutExecPin, 0, false, LocalMacroStack, this);
		if (GraphPin)
		{
			UK2Node* NextNode = Cast<UK2Node>(GraphPin->GetOwningNode());
			if (Content != "")
			{
				Content += "\n";
			}
			Content += GenerateCodeFromNode(NextNode, GraphPin->GetName(), VisitedNodes, Preparations, LocalMacroStack);
		}
	}
	//SimpleNodeEnd

	return Content;
}


FString UNativizationV2Subsystem::GeneratePurePreparations(UK2Node* Consumer, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations)
{
    FString Content;
    for (UEdGraphPin* Input : Consumer->Pins)
    {
        if (Input->Direction != EGPD_Input || Input->ParentPin ||
            Input->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
        {
            continue;
        }
        const FGenerateResultStruct Value = GenerateInputParameterCodeForNode(Consumer, Input, 0, MacroStack);
        for (const FString& Instruction : Value.Preparations.Difference(Preparations))
        {
            Content += Instruction + TEXT("\n");
        }
        Preparations.Append(Value.Preparations);
    }
    return Content;
}


FString UNativizationV2Subsystem::GenerateLocalVariablesCodeFromEntryNode(UK2Node* EntryNode, TArray<UK2Node*> MacroStack)
{
	//TO DO - IGNORE ARRAYS

	FString Content;

	UBlueprint* Blueprint = EntryNode->GetBlueprint();

	if (FGenerationGraphContext::IsFunctionGraph(EntryNode))
	{
		if (UK2Node_FunctionEntry* FunctionEntry = Cast<UK2Node_FunctionEntry>(EntryNode))
		{
			TArray<FBPVariableDescription> ReversedArray = FunctionEntry->LocalVariables;
			for (FBPVariableDescription BPVariableDescription : ReversedArray)
			{
				UFunction* Function = UBlueprintNativizationLibrary::GetFunctionByNodeAndEntryNodes(FunctionEntry, EntryNodes);
				FProperty* Property = UBlueprintNativizationLibrary::FindFunctionProperty(Function, BPVariableDescription.VarName, false);

                if (BPVariableDescription.VarType.IsMap())
                {
                    FStructOnScope Defaults(Function);
                    void* ValueMemory = Property->ContainerPtrToValuePtr<void>(Defaults.GetStructMemory());
                    if (!BPVariableDescription.DefaultValue.IsEmpty() &&
                        !Property->ImportText_Direct(*BPVariableDescription.DefaultValue, ValueMemory, nullptr, PPF_None))
                    {
                        FNativeApiCatalog::AddError(TEXT("Cannot import local map default: ") + Property->GetPathName());
                        continue;
                    }
                    const FString PropertyName = UBlueprintNativizationLibrary::GetUniquePropertyName(Property);
                    if (UBlueprintNativizationLibrary::IsAvailibleOneLineDefaultConstructorForProperty(Property))
                    {
                        const FString Initializer = UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructorForProperty(Property, ValueMemory);
                        Content += UBlueprintNativizationLibrary::GetPropertyType(Property) + TEXT(" ") + PropertyName + TEXT(" = ") + Initializer + TEXT(";\n");
                    }
                    else
                    {
                        FString ValueName;
                        FString OwnerExpression = TEXT("this");
                        if (Function->HasAnyFunctionFlags(FUNC_Static))
                        {
                            OwnerExpression = TEXT("nullptr");
                        }
                        Content += UBlueprintNativizationLibrary::GenerateManyLinesDefaultConstructorForProperty(Property, ValueMemory,
                            nullptr, EntryNodes, Blueprint->GeneratedClass, true, FString(), ValueName, OwnerExpression);
                        Content += UBlueprintNativizationLibrary::GetPropertyType(Property) + TEXT(" ") + PropertyName + TEXT(" = ") + ValueName + TEXT(";\n");
                    }
                    continue;
                }


				if (!UBlueprintNativizationLibrary::IsAvailibleOneLineDefaultConstructor(BPVariableDescription.VarType))
				{
                    FString OutString;
                    Content += UBlueprintNativizationLibrary::GetPinType(BPVariableDescription.VarType, false) + TEXT(" ") +
                        UBlueprintNativizationLibrary::GetUniquePropertyName(Property) + TEXT("{};\n");
                    Content += UBlueprintNativizationLibrary::GenerateManyLinesDefaultConstructor(UBlueprintNativizationLibrary::GetUniquePropertyName(Property), BPVariableDescription.VarType, BPVariableDescription.DefaultValue, Blueprint->GeneratedClass, false, EntryNode, EntryNodes, false, OutString);
				}
				else
				{
					FString FixedValue = UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructor(BPVariableDescription.VarName.ToString(), BPVariableDescription.VarType, BPVariableDescription.DefaultValue, nullptr);
					FString PropertyName = UBlueprintNativizationLibrary::GetUniquePropertyName(Property);
					FString PropertyType = UBlueprintNativizationLibrary::GetPropertyType(Property);

					Content += FString::Format(TEXT("{0} {1} = {2};\n\n"), { PropertyType, PropertyName, FixedValue });
				}
			}
		}
	}


    TSet<FString> SetContents;
    for (const FGenerationNodeInstance& Instance : GetNodeInstances(EntryNode, MacroStack, true))
    {
        TGuardValue<TArray<UK2Node*>> MacroScope(CurrentMacroStack, Instance.MacroStack);
        if (UTranslatorBPToCppObject* Translator = FindTranslatorForNode(Instance.Node))
        {
            SetContents.Append(Translator->GenerateLocalVariables(Instance.Node, Instance.MacroStack, this));
        }
    }
	Content += FString::Join(SetContents.Array(), TEXT("\n"));

	return Content;
}

void UNativizationV2Subsystem::RegisterPersistentEntryParameters()
{
    PersistentEntryParameters.Reset();
    for (const FGenerateFunctionStruct& Entry : EntryNodes)
    {
        TArray<UK2Node*> ContextNodes = UBlueprintNativizationLibrary::GetAllExecuteNodesInFunction(Entry.Node, EntryNodes, Entry.EntryPinName);
        ContextNodes.Append(UBlueprintNativizationLibrary::GetAllInputNodes(ContextNodes, true));
        for (UK2Node* ContextNode : ContextNodes)
        {
            for (UEdGraphPin* InputPin : ContextNode->Pins)
            {
                if (InputPin->Direction != EGPD_Input || InputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
                {
                    continue;
                }
                TArray<UK2Node*> MacroStack;
                UEdGraphPin* SourcePin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(InputPin, 0, false, MacroStack, this);
                if (!SourcePin)
                {
                    continue;
                }
                SourcePin = UBlueprintNativizationLibrary::GetParentPin(SourcePin);
                UK2Node_Event* SourceEvent = Cast<UK2Node_Event>(SourcePin->GetOwningNode());
                if (!SourceEvent || SourceEvent == Entry.Node || SourcePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate)
                {
                    continue;
                }
                PersistentEntryParameters.FindOrAdd(SourcePin) = GetNodeStateName(SourceEvent,
                    UBlueprintNativizationLibrary::GetUniquePinName(SourcePin, EntryNodes) + TEXT("State"));
            }
        }
    }
}
