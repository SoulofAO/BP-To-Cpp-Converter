/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationV2.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/UnrealType.h"
#include "UObject/ObjectMacros.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Event.h"
#include "K2Node_Tunnel.h"
#include "K2Node_CallFunction.h"
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
#include "Components/TimelineComponent.h"
#include "Engine/TimelineTemplate.h"
#include "BlueprintNativizationSettings.h"
#include "Engine/UserDefinedEnum.h"
#include "StructUtils/UserDefinedStruct.h"

#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UObjectIterator.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "ObjectTools.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "BlueprintEditorLibrary.h"
#include "K2Node_EnhancedInputAction.h"
#include "InputAction.h"
#include "FileHelpers.h"


FNativizationModuleResult UNativizationV2Subsystem::RunNativizationForAllObjects(TArray<UObject*> InputTargets,
	bool bSaveToFile,
	FString InputOutputFolder,
	bool bTransformOnlyOneFileCode, bool HotReloadAndReplace, bool MergeWithExistModule,
	bool SaveCache, bool NewLeftAllAssetRefInBlueprint, FString SaveCachePath
)
{
	FNativizationModuleResult NativizationModuleResult;
	const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();

	if (bIsNativizing)
	{
		UE_LOG(LogTemp, Warning, TEXT("RunNativization: Still Other Nativization In Process"));
		return NativizationModuleResult;
	}

	bIsNativizing = true;
	LeftAllAssetRefInBlueprint = NewLeftAllAssetRefInBlueprint;

	TArray<FNativizationCode> NativizationCodesResult;

	LoadTranslatorObjects();

	if (!InputTargets.IsValidIndex(0))
	{
		UE_LOG(LogTemp, Warning, TEXT("RunNativization: Target is null"));
		return NativizationModuleResult;
	}

	if (bTransformOnlyOneFileCode)
	{
		PreloadAllNamesForObjects(InputTargets);

		for (UObject* InputTarget : InputTargets)
		{
			FNativizationCode Code = GenerateCodeForObject(InputTarget, "None");
			NativizationCodesResult.Add(Code);
		}

		NativizationModuleResult.ModuleCode = GenerateUnrealModule("BlueprintNativizationModule", InputTargets);
		NativizationModuleResult.ModuleBuildCsCode = GenerateUnrealBuildCsModule("BlueprintNativizationModule", InputTargets);
	}
	else
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

		PreloadAllNamesForObjects(FilteredDependencies.Array());

		for (UObject* Object : FilteredDependencies)
		{
			FNativizationCode Code = GenerateCodeForObject(Object, "None");
			NativizationCodesResult.Add(Code);
		}

		NativizationModuleResult.ModuleCode = GenerateUnrealModule("BlueprintNativizationModule", { FilteredDependencies.Array() });
		NativizationModuleResult.ModuleBuildCsCode = GenerateUnrealBuildCsModule("BlueprintNativizationModule", { FilteredDependencies.Array() });
	}

	NativizationModuleResult.Sucsess = !NativizationCodesResult.IsEmpty();
	NativizationModuleResult.NativizationCodes = NativizationCodesResult;

	if (bSaveToFile)
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

			const FString TargetModuleName = TEXT("BlueprintNativizationModule");

			auto& HotReloadModule = IHotReloadModule::Get();
			HotReloadModule.RecompileModule(
				"BlueprintNativizationModule",
				*GLog,
				ERecompileModuleFlags::ReloadAfterRecompile
			);

			ReplaceBlueprintTypesAfterHotReload(UBlueprintNativizationLibrary::GetAllRegisterUniqueFieldsInNameSubsistem());
		}

	}

	bIsNativizing = false;
	return NativizationModuleResult;
}


FNativizationCode UNativizationV2Subsystem::RunNativizationForOneFunctionBlueprint(UBlueprint* InputTarget, bool NewLeftAllAssetRefInBlueprint, FString FunctionName)
{
	FNativizationCode OutContent;

	if (bIsNativizing)
	{
		UE_LOG(LogTemp, Warning, TEXT("RunNativization: Still Other Nativization In Process"));
		return  OutContent;
	}

	bIsNativizing = true;
	LeftAllAssetRefInBlueprint = NewLeftAllAssetRefInBlueprint;

	PreloadAllNamesForObjects(TArray<UObject*>({ InputTarget }));

	LoadTranslatorObjects();

	OutContent.Object = InputTarget;
	OutContent.ObjectName = InputTarget->GetName();
	GenerateBlueprintCode(InputTarget, FunctionName, OutContent);

	bIsNativizing = false;
	return OutContent;
}

void UNativizationV2Subsystem::PrepareNewModuleToWork()
{
	const FString ModuleName = "BlueprintNativizationModule";

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
	for (const auto& ModuleValue : ModulesArray)
	{
		TSharedPtr<FJsonObject> ModuleObj = ModuleValue->AsObject();
		if (!ModuleObj.IsValid())
			continue;

		FString Name;
		if (ModuleObj->TryGetStringField(TEXT("Name"), Name) && Name == ModuleName)
		{
			bAlreadyExists = true;
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
	FNativizationCode NativizationCode;
	NativizationCode.Object = InputTarget;
	NativizationCode.ObjectName = InputTarget->GetFName().ToString();

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
void UNativizationV2Subsystem::SaveNativizationResult(FNativizationModuleResult NativizationModuleResult, FString Directory, FString& OutputDirectory, bool MergeWithExistModule)
{
	if (Directory.IsEmpty())
	{
		Directory = FPaths::ProjectDir();
		OutputDirectory = FPaths::Combine(Directory, TEXT("Source"), "BlueprintNativizationModule");
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
			FFileHelper::LoadFileToString(ExistingContent, *BuildCsPath);

			// ===== 1. Извлекаем старые зависимости =====
			TArray<FString> OldDeps;
			int32 OldStart = INDEX_NONE, OldEnd = INDEX_NONE;
			{
				const FRegexPattern Pattern(TEXT("PrivateDependencyModuleNames\\s*=\\s*new\\s*string\\s*\\[\\]\\s*\\{([^}]*)\\}"));
				FRegexMatcher Matcher(Pattern, ExistingContent);
				if (Matcher.FindNext())
				{
					const FString Match = Matcher.GetCaptureGroup(1);
					Match.ParseIntoArray(OldDeps, TEXT(","), true);
					for (FString& Str : OldDeps)
					{
						Str = Str.TrimStartAndEnd().Replace(TEXT("\""), TEXT(""));
					}
					OldStart = Matcher.GetMatchBeginning();
					OldEnd = Matcher.GetMatchEnding();
				}
			}

			// ===== 2. Извлекаем новые зависимости =====
			TArray<FString> NewDeps;
			{
				const FRegexPattern Pattern(TEXT("PrivateDependencyModuleNames\\s*=\\s*new\\s*string\\s*\\[\\]\\s*\\{([^}]*)\\}"));
				FRegexMatcher Matcher(Pattern, NativizationModuleResult.ModuleBuildCsCode);
				if (Matcher.FindNext())
				{
					const FString Match = Matcher.GetCaptureGroup(1);
					Match.ParseIntoArray(NewDeps, TEXT(","), true);
					for (FString& Str : NewDeps)
					{
						Str = Str.TrimStartAndEnd().Replace(TEXT("\""), TEXT(""));
					}
				}
			}

			// ===== 3. Объединяем =====
			TSet<FString> MergedDeps(OldDeps);
			MergedDeps.Append(NewDeps);

			FString NewDepsString = TEXT("PrivateDependencyModuleNames = new string[]\n\t\t{\n");
			for (const FString& Dep : MergedDeps)
			{
				NewDepsString += FString::Printf(TEXT("\t\t\t\"%s\",\n"), *Dep);
			}
			NewDepsString.RemoveFromEnd(TEXT(",\n"));
			NewDepsString += TEXT("\n\t\t};");

			// ===== 4. Подменяем =====
			if (OldStart != INDEX_NONE && OldEnd != INDEX_NONE)
			{
				ExistingContent = ExistingContent.Left(OldStart) + NewDepsString + ExistingContent.Mid(OldEnd);
			}
			else
			{

				ExistingContent += TEXT("\n\n") + NativizationModuleResult.ModuleBuildCsCode;
			}

			CreateFile(BuildCsPath, ExistingContent);
		}
		else
		{
			CreateFile(BuildCsPath, NativizationModuleResult.ModuleBuildCsCode);
		}
	}
	

	if (!NativizationModuleResult.ModuleCode.OutputCppString.IsEmpty())
	{
		const FString ModuleCppPath = FPaths::Combine(OutputDirectory, NativizationModuleResult.ModuleCode.ObjectName + TEXT(".cpp"));
		CreateFile(ModuleCppPath, NativizationModuleResult.ModuleCode.OutputCppString);
	}

	if (!NativizationModuleResult.ModuleCode.OutputHeaderString.IsEmpty())
	{
		const FString ModuleHeaderPath = FPaths::Combine(OutputDirectory, NativizationModuleResult.ModuleCode.ObjectName + TEXT(".h"));
		CreateFile(ModuleHeaderPath, NativizationModuleResult.ModuleCode.OutputHeaderString);
	}

	for (const FNativizationCode& NativizationCode : NativizationModuleResult.NativizationCodes)
	{
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
			CreateFile(HeaderPath, NativizationCode.OutputHeaderString);
		}

		if (!NativizationCode.OutputCppString.IsEmpty())
		{
			const FString CppPath = FPaths::Combine(PathDirectory, NativizationCode.ObjectName + TEXT(".cpp"));
			CreateFile(CppPath, NativizationCode.OutputCppString);
		}
	}


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
		// Инициализация модуля
	}

	void F{0}Module::ShutdownModule()
	{
		// Очистка перед выгрузкой модуля
	}
	)"), { ModuleName });

	NativizationCode.ObjectName = ModuleName;

	return NativizationCode;
};

FString UNativizationV2Subsystem::GenerateUnrealBuildCsModule(const FString& ModuleName, TArray<UObject*> Targets)
{
	TSet<FString> CSPrivateDependencyModuleNames;
	TSet<FString> CSIgnoreDependencyModuleNames = { "\"Core\"", "\"CoreUObject\"", "\"Engine\"", "\"InputCore\"" };

	for (UObject* Target : Targets)
	{
		TArray<UK2Node*> AnswerNodes;
		if (UBlueprint* Blueprint = Cast<UBlueprint>(Target))
		{
			if (!Blueprint->GeneratedClass->IsChildOf<UInterface>())
			{
				EntryNodes = GenerateAllGenerateFunctionStruct(Blueprint, "");
				for (FGenerateFunctionStruct EntryNode : EntryNodes)
				{
					TArray<UK2Node*> ExecNodes = UBlueprintNativizationLibrary::GetAllExecuteNodesInFunctionWithMacroSupported(EntryNode.Node, this);
					TArray<UK2Node*> InputNodes = UBlueprintNativizationLibrary::GetAllInputNodes(ExecNodes, true);
					ExecNodes.Append(InputNodes);

					for (UK2Node* ExecNode : ExecNodes)
					{
						AnswerNodes.AddUnique(ExecNode);
					}
				}
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
	CSPrivateDependencyModuleNames = CSPrivateDependencyModuleNames.Difference(CSIgnoreDependencyModuleNames);

	FString BuildCS = FString::Format(TEXT(R"(
	using UnrealBuildTool;

	public class {0} : ModuleRules
	{
		public {0}(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
			PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });
			PrivateDependencyModuleNames.AddRange(new string[] {{1}});
		}
	}
	)"), { ModuleName, *FString::Join(CSPrivateDependencyModuleNames, TEXT(", "))});

	return BuildCS;
}

void UNativizationV2Subsystem::CreateJSONCache(FString CacheDirectory)
{
	// Если путь не задан, устанавливаем путь по умолчанию
	if (CacheDirectory.IsEmpty())
	{
		CacheDirectory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source/BlueprintNativizationModule"));
	}

	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());

	JsonObject->SetStringField(TEXT("_format"), TEXT("BlueprintNativizationCache"));
	JsonObject->SetNumberField(TEXT("version"), 1);

	TArray<TSharedPtr<FJsonValue>> TranslateObjectJsons;

	TMap<UField*, FName> TranslatedObjects = UBlueprintNativizationLibrary::GetAllRegisterUniqueFieldsInNameSubsistem();

	for (const TPair<UField*, FName>& Pair : TranslatedObjects)
	{
		TSharedPtr<FJsonObject> TranslateObjectJson = MakeShareable(new FJsonObject());
		TranslateObjectJson->SetStringField("OriginalField", UBlueprintNativizationLibrary::GetStableFieldIdentifier(Pair.Key));
		TranslateObjectJson->SetStringField("NewFieldName", Pair.Value.ToString());
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
		UE_LOG(LogTemp, Log, TEXT("JSON cache saved to %s"), *CacheFilePath);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to save JSON cache to %s"), *CacheFilePath);
	}
}

void UNativizationV2Subsystem::ReapplyJSONCache(FString CacheFilePath)
{
	if (CacheFilePath.IsEmpty())
	{
		CacheFilePath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source/BlueprintNativizationModule"));
		CacheFilePath = FPaths::Combine(CacheFilePath, TEXT("BlueprintNativizationCache.bpncache"));
	}

	FString FileContent;
	if (!FFileHelper::LoadFileToString(FileContent, *CacheFilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to load JSON cache from %s"), *CacheFilePath);
		return;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to parse JSON cache file: %s"), *CacheFilePath);
		return;
	}

	const FString Format = JsonObject->GetStringField(TEXT("_format"));
	const int32 Version = JsonObject->GetIntegerField(TEXT("version"));

	if (Format != TEXT("BlueprintNativizationCache") || Version != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("Unsupported cache format or version: %s v%d"), *Format, Version);
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* Items;
	if (!JsonObject->TryGetArrayField(TEXT("items"), Items))
	{
		UE_LOG(LogTemp, Error, TEXT("Missing 'items' array in JSON cache"));
		return;
	}

	TMap<UField*, FName> TranslatedObjects;

	for (const TSharedPtr<FJsonValue>& ItemValue : *Items)
	{
		TSharedPtr<FJsonObject> ItemObject = ItemValue->AsObject();
		if (!ItemObject.IsValid())
		{
			continue;
		}

		const FString OriginalFieldStr = ItemObject->GetStringField(TEXT("OriginalField"));
		const FString NewFieldNameStr = ItemObject->GetStringField(TEXT("NewFieldName"));

		UField* Field = UBlueprintNativizationLibrary::FindFieldByStableIdentifier(OriginalFieldStr);
		if (!Field)
		{
			UE_LOG(LogTemp, Warning, TEXT("Could not find UField for identifier: %s"), *OriginalFieldStr);
			continue;
		}

		TranslatedObjects.Add(Field, *NewFieldNameStr);
	}
	ReplaceBlueprintTypesAfterHotReload(TranslatedObjects);
}

bool UNativizationV2Subsystem::HasNewClassesAfterHotReload()
{
	const FString TargetModuleName = TEXT("BlueprintNativizationModule");

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
	for (TObjectIterator<UField> FieldIt; FieldIt; ++FieldIt)
	{
		UField* Field = *FieldIt;

		if (!Field->IsNative())
			continue;

		FString Name = UBlueprintNativizationLibrary::GetUniqueFieldName(Field);
		if (Name.Contains(TargetName.ToString()))
		{
			return Field;
		}
	}

	return nullptr;
}

void UNativizationV2Subsystem::ReplaceBlueprintTypesAfterHotReload(const TMap<UField*, FName>& TranslatedObjects)
{
	const UBlueprintNativizationV2EditorSettings* Settings = GetDefault<UBlueprintNativizationV2EditorSettings>();
	// ------------------------------------------------------------------------------------
	// Этап 1: Сопоставим старые UField с их новыми копиями из загруженного C++ модуля
	// ------------------------------------------------------------------------------------
	//<Old - New>
	TMap<UField*, UField*> OldToNewFieldMap;

	for (const auto& Pair : TranslatedObjects)
	{
		UField* OldField = Pair.Key;
		const FName& NewName = Pair.Value;

		UField* NewField = FindFieldByNameViaCDO(NewName);

		if (NewField)
		{
			OldToNewFieldMap.Add(OldField, NewField);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Failed to find new field for: %s"), *OldField->GetName());
		}
	}
	// ------------------------------------------------------------------------------------
	// Этап 2: Найдём все Blueprints в проекте
	// ------------------------------------------------------------------------------------
	TArray<UBlueprint*> AllBlueprints;
	for (TObjectIterator<UBlueprint> It; It; ++It)
	{
		AllBlueprints.Add(*It);
	}
	TArray<UBlueprint*> RecompileBlueprints;

	// ------------------------------------------------------------------------------------
	// Этап 3: Обновим все поля в пинах графов, если они используют устаревшие Struct или Enum
	// ------------------------------------------------------------------------------------
	for (TPair<UField*, UField*> Pair : OldToNewFieldMap)
	{
		UBlueprintNativizationLibrary::ReplaceReferencesToType(Pair.Key, Pair.Value, RecompileBlueprints);
	}

	TArray<UObject*> AssetsToDelete;
	TArray<FSoftObjectPath> AssetsToSave;

	UEditorAssetSubsystem* EditorAssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;

	for (TPair<UField*, UField*> Pair : OldToNewFieldMap)
	{
		if (!Pair.Key)
			continue;

		UField* Field = Pair.Key;

		if (UStruct* Struct = Cast<UStruct>(Field))
		{
			if (!Struct->IsA<UClass>())
			{
				AssetsToDelete.Add(Struct);
				continue;
			}
		}
		else if (UEnum* Enum = Cast<UEnum>(Field))
		{
			AssetsToDelete.Add(Enum);
			continue;
		}

		// Обработка UClass, если она часть блюпринта
		TMap<FName, FString> SavedProperties;

		if (UClass* Class = Cast<UClass>(Field))
		{
			if (UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(Class))
			{
				if (Blueprint->GeneratedClass->IsChildOf<UInterface>())
				{
					TSubclassOf<UInterface> InterfaceClass = Blueprint->GeneratedClass.Get();
					AssetsToDelete.Add(InterfaceClass);
					continue;
				}
				else
				{
					UObject* CDO = Class->GetDefaultObject();

					for (TFieldIterator<FProperty> PropIt(Class, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
					{
						FProperty* Property = *PropIt;

						if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_DisableEditOnTemplate))
						{
							continue;
						}

						if (UBlueprintNativizationLibrary::DoesPropertyContainAssetReference(Property, CDO))
						{
							FString Value;
							Property->ExportText_InContainer(0, Value, CDO, CDO, nullptr, PPF_None);
							SavedProperties.Add(Property->GetFName(), Value);
						}
					}

					TArray<FName> Names;
					TArray<UActorComponent*> ActorComponents = UBlueprintNativizationLibrary::GetClassComponentTemplates(Names, Blueprint->GeneratedClass.Get(), UActorComponent::StaticClass(), true);

					for (UActorComponent* ActorComponent : ActorComponents)
					{
						for (TFieldIterator<FProperty> PropIt(ActorComponent->GetClass()); PropIt; ++PropIt)
						{
							FProperty* Property = *PropIt;

							if (UBlueprintNativizationLibrary::DoesPropertyContainAssetReference(Property, ActorComponent))
							{
								FString Value;
								Property->ExportText_InContainer(0, Value, ActorComponent, nullptr, ActorComponent, PPF_None);
								SavedProperties.Add(TPair<FName, FString>(
									UBlueprintNativizationLibrary::GetUniquePropertyComponentGetterName(ActorComponent, EntryNodes) +
									UBlueprintNativizationLibrary::GetUniquePropertyName(Property, EntryNodes),
									Value)
								);

							}
						}
					}
					TArray<UEdGraph*> Graphs;
					Blueprint->GetAllGraphs(Graphs);
					TArray<UK2Node*> Nodes;

					for (UEdGraph* Graph : Graphs)
					{
						TArray<UK2Node_EnhancedInputAction*> NewEnhanceInputNodes;
						Graph->GetNodesOfClass<UK2Node_EnhancedInputAction>(NewEnhanceInputNodes);
						Nodes.Append(NewEnhanceInputNodes);
					}

					for (UK2Node* Node : Nodes)
					{
						if (UK2Node_EnhancedInputAction* EnhancedInputAction = Cast<UK2Node_EnhancedInputAction>(Node))
						{
							FProperty* Property = EnhancedInputAction->GetClass()->FindPropertyByName("InputAction");
							FString Value;
							Property->ExportText_InContainer(0, Value, EnhancedInputAction, nullptr, EnhancedInputAction, PPF_None);
							FString Name = UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByClass(EnhancedInputAction->InputAction->GetName(), Class);
							SavedProperties.Add(*Name, Value);
						}
						else if (UK2Node_FunctionEntry* FunctionEntry = Cast<UK2Node_FunctionEntry>(Node))
						{
							TArray<FBPVariableDescription> ReversedArray = FunctionEntry->LocalVariables;
							for (FBPVariableDescription BPVariableDescription : ReversedArray)
							{
								UFunction* Function = UBlueprintNativizationLibrary::GetFunctionByNodeAndEntryNodes(FunctionEntry, EntryNodes);
								FProperty* Property = Function->FindPropertyByName(BPVariableDescription.VarName);

								if (UBlueprintNativizationLibrary::ContainsAssetInPinType(BPVariableDescription.VarType, BPVariableDescription.DefaultValue, nullptr))
								{
									FString Name = UBlueprintNativizationLibrary::GetUniquePropertyName(Property, EntryNodes);
									SavedProperties.Add(*Name, BPVariableDescription.DefaultValue);
								}
							}
						}
					}
				}

				UBlueprintNativizationLibrary::ClearBlueprint(Blueprint);
				UBlueprintEditorLibrary::ReparentBlueprint(Blueprint, Cast<UClass>(Pair.Value));
				FKismetEditorUtilities::CompileBlueprint(Blueprint);
				
				if (Settings->bAddBPPrefixToReparentBlueprint)
				{
					FString ObjectPath = Blueprint->GetOutermost()->GetPathName();
					FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
					FSoftObjectPath SoftPath(ObjectPath);
					FAssetData Asset = AssetRegistryModule.Get().GetAssetByObjectPath(SoftPath);
					FString OldName = Blueprint->GetName();
					if (!OldName.StartsWith(TEXT("BP_")))
					{
						FString NewName = TEXT("BP_") + OldName;
						FString DestPath = FPaths::GetPath(Asset.GetSoftObjectPath().ToString()) + TEXT("/") + NewName;

						EditorAssetSubsystem->RenameAsset(Asset.GetSoftObjectPath().ToString(), DestPath);
					}

				}
				RecompileBlueprints.Remove(Blueprint);

				{
					UClass* NewClass = Blueprint->GeneratedClass;
					if (NewClass)
					{
						UObject* NewCDO = NewClass->GetDefaultObject();

						for (TFieldIterator<FProperty> PropIt(NewClass, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
						{
							FProperty* Property = *PropIt;
							if (const FString* SavedValue = SavedProperties.Find(Property->GetFName()))
							{
								Property->ImportText_InContainer(**SavedValue, NewCDO, NewCDO, PPF_None);
							}
						}
					}

					TArray<FName> Names;
					TArray<UActorComponent*> ActorComponents = UBlueprintNativizationLibrary::GetClassComponentTemplates(Names, Blueprint->GeneratedClass.Get(), UActorComponent::StaticClass(), true);

					for (UActorComponent* ActorComponent : ActorComponents)
					{
						for (TFieldIterator<FProperty> PropIt(ActorComponent->GetClass()); PropIt; ++PropIt)
						{
							FProperty* Property = *PropIt;
							FString PropertyName = ActorComponent->GetName();
							if (SavedProperties.Contains(*(PropertyName + UBlueprintNativizationLibrary::GetUniquePropertyName(Property, EntryNodes))))
							{
								if (const FString* SavedValue = SavedProperties.Find(*(PropertyName + UBlueprintNativizationLibrary::GetUniquePropertyName(Property, EntryNodes))))
								{
									Property->ImportText_InContainer(**SavedValue, ActorComponent, ActorComponent, PPF_None);
								}
							}
						}
					}
				}

				AssetsToSave.Add(Blueprint);
			}
		}
	}

	for (UBlueprint* Blueprint : RecompileBlueprints)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
	}

	bool bShowConfirmation = false;
	if (!AssetsToDelete.IsEmpty())
	{
		for (UObject* AssetToDelete : AssetsToDelete)
		{
			EditorAssetSubsystem->DeleteAsset(AssetsToDelete[0]->GetPackage()->GetPathName());
		}

		/*
		const int32 NumDeleted = ObjectTools::ForceDeleteObjects(AssetsToDelete, bShowConfirmation);
		if (NumDeleted != AssetsToDelete.Num())
		{
			UE_LOG(LogTemp, Warning, TEXT("Удалено не всё: %d из %d"), NumDeleted, AssetsToDelete.Num());
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("Успешно удалено %d ассетов."), NumDeleted);
		}*/
	}

	if (EditorAssetSubsystem && !AssetsToSave.IsEmpty())
	{
		const bool bOnlyIfDirty = true;
		for (const FSoftObjectPath& AssetPath : AssetsToSave)
		{
			if (!EditorAssetSubsystem->SaveAsset(AssetPath.ToString(), bOnlyIfDirty))
			{
				UE_LOG(LogTemp, Warning, TEXT("Не удалось сохранить ассет: %s"), *AssetPath.ToString());
			}
		}
	}
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	UEditorLoadingAndSavingUtils::SaveDirtyPackages(false, true);

	UE_LOG(LogTemp, Log, TEXT("Blueprint update and replacement finished."));
}

FString UNativizationV2Subsystem::ConvertUEnumToCppEnumDeclaration(UEnum* Enum)
{
	if (!Enum)
	{
		return TEXT("// Invalid enum");
	}

	FString EnumName = UBlueprintNativizationLibrary::GetUniqueFieldName(Enum);
	FString Result;

	Result += TEXT("#pragma once\n\n");
	Result += TEXT("#include \"CoreMinimal.h\"\n");
	Result += FString::Format(TEXT("#include \"{0}.generated.h\"\n\n"), { Enum->GetName() });

	if (Enum->IsA<UUserDefinedEnum>())
	{
		Result += TEXT("// Converted from Blueprint enum\n");
	}

	Result += FString::Printf(TEXT("UENUM(BlueprintType)\n"));
	Result += FString::Printf(TEXT("enum class %s : uint8\n{\n"), *EnumName);

	const int32 NumEnums = Enum->NumEnums();
	for (int32 i = 0; i < NumEnums; ++i)
	{
		if (Enum->HasMetaData(TEXT("Hidden"), i))
		{
			continue;
		}
		
		FString EnumValueName = Enum->GetDisplayNameTextByIndex(i).ToString(); 

		if (Enum->GetMaxEnumValue() == Enum->GetValueByIndex(i))
		{
			continue;
		}

		const FString Tooltip = Enum->GetMetaData(TEXT("ToolTip"), i);
		if (!Tooltip.IsEmpty())
		{
			Result += FString::Printf(TEXT("// %s\n"), *Tooltip);
		}

		Result += FString::Printf(TEXT("%s"), *EnumValueName);
		Result += TEXT(",\n");
	}

	Result += TEXT("};\n");

	Result = UBlueprintNativizationDataLibrary::FormatNamespaceCodeStyle(Result);

	return Result;
}

FString UNativizationV2Subsystem::ConvertUStructToCppStructDeclaration(UStruct* Struct)
{
	if (!Struct)
	{
		return TEXT("// Invalid struct");
	}

	FString StructName = UBlueprintNativizationLibrary::GetUniqueFieldName(Struct);
	FString Result;
	
	Result += TEXT("#pragma once\n\n");
	Result += TEXT("#include \"CoreMinimal.h\"\n");
	Result += FString::Format(TEXT("#include \"{0}.generated.h\"\n\n"), { Struct->GetName() });

	if (Struct->IsA<UUserDefinedStruct>())
	{
		Result += TEXT("// Converted from Blueprint struct\n\n");
	}

	Result += FString::Format(TEXT("USTRUCT(BlueprintType)\nstruct {0}\n{\nGENERATED_BODY()\n\n"), { StructName });

	TArray<FString> PropertyDeclarations;
	TArray<FString> ConstructorParams;
	TArray<FString> InitializerList;

	for (TFieldIterator<FProperty> PropIt(Struct, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;

		if (Property->HasAllPropertyFlags(CPF_Transient))
		{
			continue;
		}

		FString PropertyType = UBlueprintNativizationLibrary::GetPropertyType(Property);
		FString PropertyName = UBlueprintNativizationLibrary::GetUniquePropertyName(Property, TArray<FGenerateFunctionStruct>());

		const FString Tooltip = Property->GetMetaData(TEXT("ToolTip"));
		if (!Tooltip.IsEmpty())
		{
			Result += FString::Format(TEXT("// {0}\n"), { Tooltip });
		}

		FString FlagsString = GetPropertyFlagsString(Property);
		if (UBlueprintNativizationLibrary::IsAvailibleOneLineDefaultConstructorForProperty(Property))
		{
			void* StructMemory = FMemory::Malloc(Struct->GetStructureSize());
			Struct->InitializeStruct(StructMemory);
			FString PropertyValue = UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructorForProperty(Property, LeftAllAssetRefInBlueprint, StructMemory);
			PropertyDeclarations.Add(FString::Format(
				TEXT("UPROPERTY({0})\n{1} {2} = {3};\n"),
				{ *FlagsString,
				  *PropertyType,
				  *PropertyName,
				  *PropertyValue
				}
			));
			Struct->DestroyStruct(StructMemory);
			FMemory::Free(StructMemory);
		}
		else
		{
			PropertyDeclarations.Add(FString::Format(
				TEXT("UPROPERTY({0})\n{1} {2};\n"),
				{ *FlagsString,
				  *PropertyType,
				  *PropertyName,
				}
			));
		}

		ConstructorParams.Add(FString::Format(TEXT("{0} In{1}"), { PropertyType, PropertyName }));
		InitializerList.Add(FString::Format(TEXT("{0}(In{0})"), { PropertyName }));

	}

	for (const FString& Decl : PropertyDeclarations)
	{
		Result += Decl + TEXT("\n");
	}

	Result += FString::Format(TEXT("{0}() = default;\n"), { StructName });

	if (ConstructorParams.Num() > 0)
	{
		Result += FString::Format(
			TEXT("{0}({1})\n: {2}\n{}\n"),
			{ StructName, FString::Join(ConstructorParams, TEXT(", ")), FString::Join(InitializerList, TEXT(",\n")) }
		);
	}
	Result += TEXT("};\n");

	Result = UBlueprintNativizationDataLibrary::FormatNamespaceCodeStyle(Result);

	return Result;
}



void UNativizationV2Subsystem::PreloadAllNamesForObjects(TArray<UObject*> Objects)
{
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
				UBlueprintNativizationLibrary::GetUniquePropertyName(*PropIt, LocalEntryNodes);
			}
		}
	}
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
		TEXT("class {0} {1}\n{\n    GENERATED_BODY()\n\npublic:\n"),
		{ ModuleAPIName,UBlueprintNativizationLibrary::GetUniqueIInterfaceFieldName(InterfaceClass) }
	);

	HFileContent += GenerateFunctionDeclarationsCode(InterfaceClass, true);

	HFileContent += TEXT("};");

	HFileContent = UBlueprintNativizationDataLibrary::FormatNamespaceCodeStyle(HFileContent);

	OutContent.OutputHeaderString = FString::Join(HeaderImports.Array(), TEXT("\n")) + TEXT("\n\n") + HFileContent;
	OutContent.OutputCppString = TEXT("// Interface does not require .cpp file by default.\n");

}



void UNativizationV2Subsystem::GenerateBlueprintCode(UBlueprint* Blueprint, FString FunctionName, FNativizationCode& OutContent)
{
	const FString ClassName = Blueprint->GetName();

	EntryNodes = GenerateAllGenerateFunctionStruct(Blueprint, FunctionName);

	TSet<FString> HeaderImports = GenerateImportsDeclarationsHeaderCode(Blueprint->GeneratedClass);
	TSet<FString> ClassPreDeclarations = GenerateClassPreDeclarationsHeaderCode(Blueprint->GeneratedClass);
	TSet<FString> CppImports = GenerateImportsDeclarationsCppCode(Blueprint);


	CppImports = CppImports.Difference(HeaderImports);

	FString HeaderImportsString = FString::Join(HeaderImports.Array(), TEXT("\n")) +"\n" + "\n" ;
	FString HeaderClassPreDeclarationsString = FString::Join(ClassPreDeclarations.Array(), TEXT("\n")) + "\n";
	FString CppImportsString = FString::Join(CppImports.Array(), TEXT("\n")) + "\n" + "\n";

	OutContent.OutputHeaderString = HeaderImportsString + HeaderClassPreDeclarationsString + GenerateHFileBlueprintCode(Blueprint, FunctionName);
	OutContent.OutputCppString = CppImportsString + GenerateCppFileBlueprintCode(Blueprint, FunctionName);

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

	HFileContent += FString::Format(
		TEXT("UCLASS()\nclass {0} {1} : public {2}\n{\nGENERATED_BODY()\n\n"),
		{ ModuleAPIName, ClassName, AllBaseClasses });

	HFileContent += "public: \n";

	HFileContent += ClassName + "();\n";
	HFileContent += "\n";

	if (Blueprint->GeneratedClass->IsChildOf(APawn::StaticClass()))
	{
		HFileContent += "virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;\n";
	}
	HFileContent += "\n";

	HFileContent += GenerateVariableDeclarationBlueprintCode(Blueprint);
	HFileContent += GenerateFunctionDeclarationsCode(Blueprint->GeneratedClass, false);
	HFileContent += GenerateGlobalFunctionVariables(Blueprint);
	HFileContent += "};";

	HFileContent = UBlueprintNativizationDataLibrary::FormatNamespaceCodeStyle(HFileContent);

	return HFileContent;
}

TSet<FString> UNativizationV2Subsystem::GenerateImportsDeclarationsHeaderCode(TSubclassOf<UObject> Class)
{
	FString ClassName = Class->GetName();
	if (ClassName.EndsWith(TEXT("_C")))
	{
		ClassName = ClassName.LeftChop(2);
	}

	TSet<FString> Includes;

	Includes.Add(TEXT("#pragma once\n\n"));
	Includes.Add(TEXT("#include \"CoreMinimal.h\""));

	if (Class)
	{
		for (TFieldIterator<FProperty> PropIt(Class, EFieldIteratorFlags::ExcludeSuper); PropIt; ++PropIt)
		{
			FProperty* Property = *PropIt;
			if (!Property) continue;

			if (Property->GetFName() == TEXT("UberGraphFrame"))
			{
				continue; 
			}

			if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				FString IncludePath;
				if (GetIncludeFromField(StructProperty->Struct, IncludePath))
				{
					Includes.Add(IncludePath);
				}
			}
			else if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
			{
				FString IncludePath;
				if (GetIncludeFromField(EnumProperty->GetEnum(), IncludePath))
				{
					Includes.Add(IncludePath);
				}
			}
			else if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
			{
				if (ByteProperty->Enum)
				{
					FString IncludePath;
					if (GetIncludeFromField(ByteProperty->Enum, IncludePath))
					{
						Includes.Add(IncludePath);
					}
				}
			}
		}


		// Обход параметров всех UFUNCTION
		for (TFieldIterator<UFunction> FuncIt(Class, EFieldIteratorFlags::ExcludeSuper); FuncIt; ++FuncIt)
		{
			UFunction* Function = *FuncIt;
			if (!Function) continue;

			for (TFieldIterator<FProperty> ParamIt(Function); ParamIt && (ParamIt->PropertyFlags & CPF_Parm); ++ParamIt)
			{
				FProperty* Property = *ParamIt;

				if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
				{
					FString IncludePath;
					if (GetIncludeFromField(StructProperty->Struct, IncludePath))
					{
						Includes.Add(IncludePath);
					}
				}
				else if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
				{
					FString IncludePath;
					if (GetIncludeFromField(EnumProperty->GetEnum(), IncludePath))
					{
						Includes.Add(IncludePath);
					}
				}
				else if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
				{
					if (ByteProperty->Enum)
					{
						FString IncludePath;
						if (GetIncludeFromField(ByteProperty->Enum, IncludePath))
						{
							Includes.Add(IncludePath);
						}
					}
				}
				else if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
				{
					if (ObjectProperty->PropertyClass)
					{
						FString IncludePath;
						if (GetIncludeFromField(ObjectProperty->PropertyClass, IncludePath))
						{
							Includes.Add(IncludePath);
						}
					}
				}
			}
		}

		FString IncludePath;
		for (const FImplementedInterface& ImplInterface : Class->Interfaces)
		{
			UClass* InterfaceClass = ImplInterface.Class;
			if (GetIncludeFromField(InterfaceClass, IncludePath))
			{
				Includes.Add(IncludePath);
			}
		}
		UClass* SuperClass = Class->GetSuperClass();

		if (GetIncludeFromField(SuperClass, IncludePath))
		{
			Includes.Add(IncludePath);
		}
	}

	TArray<UK2Node*> AnswerNodes;
	for (FGenerateFunctionStruct EntryNode : EntryNodes)
	{
		TArray<UK2Node*> ExecNodes = UBlueprintNativizationLibrary::GetAllExecuteNodesInFunctionWithMacroSupported(EntryNode.Node, this);
		TArray<UK2Node*> InputNodes = UBlueprintNativizationLibrary::GetAllInputNodes(ExecNodes, true);
		ExecNodes.Append(InputNodes);

		for (UK2Node* ExecNode : ExecNodes)
		{
			AnswerNodes.AddUnique(ExecNode);
		}
	}

	for (UK2Node* ExecNode : AnswerNodes)
	{
		UTranslatorBPToCppObject* TranslatorObject = FindTranslatorForNode(ExecNode);
		if (TranslatorObject)
		{
			Includes.Append(TranslatorObject->GenerateHeaderIncludeInstructions(ExecNode, this));
		}
	}
	Includes.Add(FString::Format(TEXT("#include \"{0}.generated.h\""), { ClassName }));
	return Includes;
}

TSet<FString> UNativizationV2Subsystem::GenerateClassPreDeclarationsHeaderCode(TSubclassOf<UObject> Class)
{
	TSet<FString> PreDeclarations;

	for (TFieldIterator<FProperty> PropertyIt(Class, EFieldIteratorFlags::ExcludeSuper); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;
		if (!Property) continue;

		if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property))
		{
			PreDeclarations.Add(FString::Format(TEXT("class {0};"), { UBlueprintNativizationLibrary::GetUniqueFieldName(ObjProp->PropertyClass)}));
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

	const FString ClassName = Blueprint->GetName();

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

			if (Property->GetOwnerClass() != GeneratedClass)
			{
				FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property);
				if (!ObjectProperty || !ObjectProperty->PropertyClass->IsChildOf(UActorComponent::StaticClass()))
				{
					continue;
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

	TArray<UK2Node*> AnswerNodes;
	for (FGenerateFunctionStruct EntryNode : EntryNodes)
	{
		TArray<UK2Node*> ExecNodes = UBlueprintNativizationLibrary::GetAllExecuteNodesInFunctionWithMacroSupported(EntryNode.Node, this);
		TArray<UK2Node*> InputNodes = UBlueprintNativizationLibrary::GetAllInputNodes(ExecNodes, true);
		ExecNodes.Append(InputNodes);

		for (UK2Node* ExecNode : ExecNodes)
		{
			AnswerNodes.AddUnique(ExecNode);
		}
	}

	for (UK2Node* ExecNode : AnswerNodes)
	{
		UTranslatorBPToCppObject* TranslatorObject = FindTranslatorForNode(ExecNode);
		if (TranslatorObject)
		{
			Includes.Append(TranslatorObject->GenerateCppIncludeInstructions(ExecNode, this));
		}
	}

	return Includes;
}

FString UNativizationV2Subsystem::GenerateConstructorCppCode(UBlueprint* Blueprint)
{
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

			const FString AllowPrivateAccessMeta = Property->GetMetaData(TEXT("AllowPrivateAccess"));
			const bool bAllowPrivateAccess = (AllowPrivateAccessMeta == TEXT("true"));

			if (!bIsEditAnywhere)
			{
				return true;
			}
			if (Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly))
			{
				return true;
			}

			if (bIsEditAnywhere && bAllowPrivateAccess)
			{
				return true;
			}

			if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated) ||
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
				Content += UBlueprintNativizationLibrary::GenerateManyLinesDefaultConstructorForProperty(Property, CDOAdress, SuperAdress, EntryNodes, BpClass, false, LeftAllAssetRefInBlueprint, "", InsideProperty);
			}
			else
			{
				FString FixedValue = UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructorForProperty(Property, LeftAllAssetRefInBlueprint, Property->ContainerPtrToValuePtr<void>(CDO));
				FString CPPName = UBlueprintNativizationLibrary::GetUniquePropertyName(Property, EntryNodes);

				FGetterAndSetterPropertyDescriptor OutGetterAndSetterDescriptorDesc;
				if (UBlueprintNativizationSettingsLibrary::FindGetterAndSetterDescriptorDescriptorByPropertyName(Property, OutGetterAndSetterDescriptorDesc))
				{
					if (!OutGetterAndSetterDescriptorDesc.SetterPropertyFunctionName.IsEmpty())
					{
						Content += *(FString::Format(TEXT("{0}({1});\n"), { OutGetterAndSetterDescriptorDesc.SetterPropertyFunctionName, FixedValue }));
					}
				}
				else
				{
					Content += FString::Format(TEXT("\t{0} = {1};\n"), { *CPPName, FixedValue });
				}
			}
		}
	}

	TArray<FName> Names;
	TArray<UActorComponent*> ActorComponents = UBlueprintNativizationLibrary::GetClassComponentTemplates(Names, Blueprint->GeneratedClass.Get(), UActorComponent::StaticClass(), true);

	for (UActorComponent* ActorComponent : ActorComponents)
	{
		if (!ActorComponent)
			continue;
		FString ComponentVarName = UBlueprintNativizationLibrary::GetUniquePropertyComponentGetterName(ActorComponent, EntryNodes);
		FString ComponentClassName = UBlueprintNativizationLibrary::GetUniqueFieldName(ActorComponent->GetClass());

		UActorComponent* SuperComponent = nullptr;
		if (SuperCDO)
		{
			TArray<UActorComponent*> SuperComponents = UBlueprintNativizationLibrary::GetClassComponentTemplates(Names, SuperCDO->GetClass(), UActorComponent::StaticClass(), false);

			for (UActorComponent* TestComponent : SuperComponents)
			{
				if (TestComponent && TestComponent->GetName() == ActorComponent->GetName() && TestComponent->GetClass() == ActorComponent->GetClass())
				{
					SuperComponent = TestComponent;
					break;
				}
			}
		}
		if (!SuperComponent)
		{
			Content += FString::Format(TEXT("\t{0} = CreateDefaultSubobject<{1}>(TEXT(\"{0}\"));\n"), { ComponentVarName, ComponentClassName });

			if (USceneComponent* SceneComp = Cast<USceneComponent>(ActorComponent))
			{
				USceneComponent* ParentSceneComponent = UBlueprintNativizationLibrary::GetParentComponentTemplate(SceneComp);

				if (ParentSceneComponent)
				{
					FString ParentComponentVarName = UBlueprintNativizationLibrary::GetUniquePropertyComponentGetterName(ParentSceneComponent, EntryNodes);
					Content += FString::Format(TEXT("\t{0}->SetupAttachment({1});\n"), { ComponentVarName, ParentComponentVarName });
				}
				else
				{

					if (UBlueprintNativizationLibrary::IsRootComponentTemplate(SceneComp))
					{
						Content += FString::Format(TEXT("\tRootComponent = {0};\n"), { ComponentVarName });
					}
					else
					{
						Content += FString::Format(TEXT("\t{0}->SetupAttachment(RootComponent);\n"), { ComponentVarName });
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

			FString CurrentValueStr, SuperValueStr, DefaultValueStr;

			Property->ExportText_InContainer(0, CurrentValueStr, ActorComponent, nullptr, ActorComponent, PPF_None);

			if (SuperComponent)
			{
				Property->ExportText_InContainer(0, SuperValueStr, SuperComponent, nullptr, SuperComponent, PPF_None);
			}

			UObject* ClassComponentCDO = ActorComponent->GetClass()->GetDefaultObject();
			if (ClassComponentCDO)
			{
				Property->ExportText_InContainer(0, DefaultValueStr, ClassComponentCDO, nullptr, ClassComponentCDO, PPF_None);
			}

			const bool bDiffersFromSuper = !CurrentValueStr.Equals(SuperValueStr);
			const bool bDiffersFromCDO = !CurrentValueStr.Equals(DefaultValueStr);

			if (CurrentValueStr.IsEmpty() && SuperValueStr.IsEmpty())
			{
				continue;
			}

			if (!CurrentValueStr.Equals(SuperComponent ? SuperValueStr : DefaultValueStr))
			{
				if (!UBlueprintNativizationLibrary::IsAvailibleOneLineDefaultConstructorForProperty(Property))
				{
					void* SuperAdress = nullptr;
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
					Content += UBlueprintNativizationLibrary::GenerateManyLinesDefaultConstructorForProperty(Property, CDOAdress, SuperAdress, EntryNodes, BpClass, false, LeftAllAssetRefInBlueprint, ComponentVarName + "->", InsideProperty);

				}
				else
				{
					FString FixedValue = UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructorForProperty(Property, LeftAllAssetRefInBlueprint, Property->ContainerPtrToValuePtr<void>(ActorComponent));
					FString PropName = UBlueprintNativizationLibrary::GetUniquePropertyName(Property, EntryNodes);

					FGetterAndSetterPropertyDescriptor OutGetterAndSetterDescriptorDesc;
					if (UBlueprintNativizationSettingsLibrary::FindGetterAndSetterDescriptorDescriptorByPropertyName(Property, OutGetterAndSetterDescriptorDesc))
					{
						if (!OutGetterAndSetterDescriptorDesc.SetterPropertyFunctionName.IsEmpty())
						{
							Content += *(FString::Format(TEXT("{0}->{1}({2});\n"), { ComponentVarName, OutGetterAndSetterDescriptorDesc.SetterPropertyFunctionName, FixedValue }));
						}
					}
					else
					{
						Content += FString::Format(TEXT("\t{0}->{1} = {2};\n"), { ComponentVarName, PropName, FixedValue });
					}

					
				}
			}
		}
	}

	return FString::Format(TEXT("{0}::{0}() : Super()\n{\n{1}};"), { ClassName, Content });
};

bool UNativizationV2Subsystem::GetIncludeFromField(UField* OwnerField, FString& IncludePath)
{
	FString HeaderPath;
	
	if (OwnerField->IsA<UUserDefinedStruct>() ||
		OwnerField->IsA<UUserDefinedEnum>() ||
		OwnerField->IsA<UBlueprintGeneratedClass>())
	{
		FString PackagePath = FPackageName::LongPackageNameToFilename(OwnerField->GetOutermost()->GetName());

		const FString ContentToken = TEXT("Content/");
		int32 ContentIndex = PackagePath.Find(ContentToken, ESearchCase::IgnoreCase, ESearchDir::FromStart);
		if (ContentIndex != INDEX_NONE)
		{
			PackagePath = PackagePath.Mid(ContentIndex + 8);
		}

		IncludePath = FString::Format(TEXT("#include \"{0}/{1}.h\""), { "BlueprintNativizationModule", *PackagePath});
		return true;
	}
	else
	{
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

						TArray<FString> DefaultImportNamespaces = { "Public/", "Classes/", "Private/" };

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


TSet<FString> UNativizationV2Subsystem::GetInputParametersForEntryNode(UK2Node* Node)
{
	TSet<FString> Parameters;

	FGenerateFunctionStruct GenerateFunctionStruct;
	UBlueprintNativizationDataLibrary::FindGenerateFunctionStructByNode(EntryNodes, Node, GenerateFunctionStruct);
	if (GenerateFunctionStruct.OriginalUFunction)
	{
		for (TFieldIterator<FProperty> It(GenerateFunctionStruct.OriginalUFunction); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* Property = *It;
			if (Property)
			{
				Parameters.Add(FString::Format(TEXT("{0} {1}"), { UBlueprintNativizationLibrary::GetPropertyType(Property), UBlueprintNativizationLibrary::GetUniquePropertyName(Property, EntryNodes) }));
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

	FString Content;

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
		if (UBlueprintNativizationSettingsLibrary::FindNativeByClassAndName(GeneratedClass, OriginalFunctionName.ToString(), OutNativeDesc))
		{
			Content += "virtual void " + OutNativeDesc.GetDeclarationSignature() + "; \n";
		}
		else
		{
			const FString ParametersString = FString::Join(UsedParameters.Array(), TEXT(", "));


			if (!ParentOriginalFunction)
			{
				Content += FString::Format(
					TEXT(
						"virtual void {1}({2});\n"
					),
					{
						*Entry.FlagsString,
						*Entry.Name.ToString(),
						*ParametersString
					}
				);
			}
			else
			{
				if (ParentOriginalFunction == Entry.OriginalUFunction)
				{
					if (VirtualDeclarationToZeroImplementation)
					{
						Content += FString::Format(
							TEXT(
								"UFUNCTION({0})\n"
								"{1} {2}({3});\n"
								"virtual void {2}_Implementation({3}) = 0;\n"
							),
							{
								*Entry.FlagsString,
								ReturnProperty ? UBlueprintNativizationLibrary::GetPropertyType(ReturnProperty) : "void",
								*Entry.Name.ToString(),
								*ParametersString
							}
						);
					}
					else
					{
						Content += FString::Format(
							TEXT(
								"UFUNCTION({0})\n"
								"{1} {2}({3});\n"
								"virtual void {2}_Implementation({3});\n"
							),
							{
								*Entry.FlagsString,
								ReturnProperty ? UBlueprintNativizationLibrary::GetPropertyType(ReturnProperty) : "void",
								*Entry.Name.ToString(),
								*ParametersString
							}
						);
					}

				}
				else
				{
					Content += FString::Format(
						TEXT(
							"virtual {0} {1}_Implementation({2});\n"
						),
						{
							ReturnProperty ? UBlueprintNativizationLibrary::GetPropertyType(ReturnProperty) : "void",
							*Entry.Name.ToString(),
							*ParametersString
						}
					);
				}
			}
		}

		Content += "\n";
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
		Flags.Add(TEXT("BlueprintCallable"));
	if (Function->HasAnyFunctionFlags(FUNC_BlueprintPure))
		Flags.Add(TEXT("BlueprintPure"));
	if (Function->HasAnyFunctionFlags(FUNC_BlueprintAuthorityOnly))
		Flags.Add(TEXT("BlueprintAuthorityOnly"));
	if (Function->HasAnyFunctionFlags(FUNC_BlueprintCosmetic))
		Flags.Add(TEXT("BlueprintCosmetic"));
	if (Function->HasAnyFunctionFlags(FUNC_Exec))
		Flags.Add(TEXT("Exec"));
	if (Function->HasAnyFunctionFlags(FUNC_Net))
		Flags.Add(TEXT("Net"));
	if (Function->HasAnyFunctionFlags(FUNC_NetReliable))
		Flags.Add(TEXT("NetReliable"));
	if (Function->HasAnyFunctionFlags(FUNC_NetMulticast))
		Flags.Add(TEXT("NetMulticast"));
	if (Function->HasAnyFunctionFlags(FUNC_NetServer))
		Flags.Add(TEXT("NetServer"));
	if (Function->HasAnyFunctionFlags(FUNC_NetClient))
		Flags.Add(TEXT("NetClient"));

	FString Category = Function->GetMetaData("Category");
	if (Category.IsEmpty())
	{
		Category = "Default";
	}
	Flags.Add(FString::Format(TEXT("Category=\"{0}\""), { *Category }));

	if(!Flags.Contains(TEXT("")))
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
FString UNativizationV2Subsystem::GetPropertyFlagsString(FProperty* Property)
{
	FString FlagsString;
	TArray<FString> Flags;

	const bool bIsEdit = Property->HasAnyPropertyFlags(CPF_Edit);
	const bool bIsBlueprintVisible = Property->HasAnyPropertyFlags(CPF_BlueprintVisible);
	const bool bIsBlueprintReadOnly = Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly);


	if (bIsEdit)
	{
		Flags.Add(TEXT("EditAnywhere"));
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

	if (Property->HasAnyPropertyFlags(CPF_Transient))
		Flags.Add(TEXT("Transient"));
	if (Property->HasAnyPropertyFlags(CPF_Config))
		Flags.Add(TEXT("Config"));
	if (Property->HasAnyPropertyFlags(CPF_SaveGame))
		Flags.Add(TEXT("SaveGame"));

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
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		return FString();
	}

	TArray<FString> Declarations;

	// Iterate through all properties in the Blueprint's generated class
	for (TFieldIterator<FProperty> PropIt(Blueprint->GeneratedClass, EFieldIteratorFlags::ExcludeSuper); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;

		// Check if the property is a multicast delegate
		if (FMulticastDelegateProperty* DelegateProp = CastField<FMulticastDelegateProperty>(Property))
		{
			FString DelegateName = UBlueprintNativizationLibrary::GetUniquePropertyName(DelegateProp, EntryNodes);
			UFunction* SignatureFunction = DelegateProp->SignatureFunction;

			if (!SignatureFunction)
			{
				continue;
			}

			// Build parameter list
			TArray<FString> Parameters;
			for (TFieldIterator<FProperty> ParamIt(SignatureFunction); ParamIt; ++ParamIt)
			{
				FProperty* Param = *ParamIt;
				FString ParamType = Param->GetCPPType();
				FString ParamName = Param->GetName();
				FString ParamDecl;

				if (Param->PropertyFlags & CPF_OutParm)
				{
					ParamDecl = FString::Format(TEXT("{0} &{1}"), { *ParamType, *ParamName });
				}
				else
				{
					ParamDecl = FString::Format(TEXT("{0} {1}"), { *ParamType, *ParamName });
				}

				Parameters.Add(ParamDecl);
			}

			// Determine the correct macro based on parameter count
			FString MacroName = Parameters.Num() > 0 ?
				FString::Format(TEXT("DECLARE_DYNAMIC_MULTICAST_DELEGATE_{0}Param"), {*FString::FromInt(Parameters.Num())}) :
				TEXT("DECLARE_DYNAMIC_MULTICAST_DELEGATE");

			// Build full delegate declaration
			FString Declaration = FString::Format(
				TEXT("{0}({1}{2});"),
				{ *MacroName,
				*("F" + DelegateName),
				Parameters.Num() > 0 ? *TEXT(", ") + *FString::Join(Parameters, TEXT(", ")) : TEXT("") }
			);

			Declarations.Add(Declaration);
		}
	}

	// Join all declarations with double newlines
	return Declarations.Num() > 0 ? FString::Join(Declarations, TEXT("\n\n")) : FString();
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

		FString Declaration;
		if (FMulticastDelegateProperty* DelegateProp = CastField<FMulticastDelegateProperty>(Property))
		{
			FString DelegateType = Property->GetCPPType();
			FString DelegateName = UBlueprintNativizationLibrary::GetUniquePropertyName(DelegateProp, EntryNodes);

			FString FlagsString = TEXT("BlueprintAssignable"); 

			// UPROPERTY and declaration
			Declaration = FString::Format(
				TEXT("UPROPERTY({0})\n{1} {2};"),
				{ *FlagsString,
				  *DelegateType,
				  *DelegateName }
			);
		}
		else
		{
			FString PropertyType = UBlueprintNativizationLibrary::GetPropertyType(Property);
			FString PropertyName = UBlueprintNativizationLibrary::GetUniquePropertyName(Property, EntryNodes);

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
	const FString ClassName = Blueprint->GetName();

	FString Content;

	if (!Blueprint)
	{
		UE_LOG(LogTemp, Warning, TEXT("GenerateHFileBlueprintCode: Invalid Blueprint."));
		return "";
	}

	Content += "\n";

	Content += GenerateConstructorCppCode(Blueprint);
	Content += "\n\n";
	if (Blueprint->GeneratedClass->IsChildOf(APawn::StaticClass()))
	{
		Content += GenerateSetupPlayerInputComponentCppCode(Blueprint);
		Content += "\n";
	}

	for (FGenerateFunctionStruct EntryNode : EntryNodes)
	{
		if (!EntryNode.IsTransientGenerateFunction)
		{
			Content += ProcessGraphsForCodeGeneration(EntryNode.Node);
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
		Blueprint->GetAllGraphs(Graphs);
	}
	else
	{
		Graphs = FilterGraphs;
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

		Blueprint->GetAllGraphs(EdGraphs);
		for (UEdGraph* EdGraph : EdGraphs)
		{
			if (!Blueprint->MacroGraphs.Contains(EdGraph))
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

FString UNativizationV2Subsystem::ProcessGraphsForCodeGeneration(UK2Node* Node)
{
	FString Content;

	FGenerateFunctionStruct GenerateFunctionStruct;

	UBlueprintNativizationDataLibrary::FindGenerateFunctionStructByNode(EntryNodes, Node, GenerateFunctionStruct);

	FString FunctionName = *GenerateFunctionStruct.Name.ToString();
	FString OriginalFunctionName;
	if (GenerateFunctionStruct.OriginalUFunction)
	{
		OriginalFunctionName = GenerateFunctionStruct.OriginalUFunction->GetName();
	}
	FFunctionDescriptor OutNativeDesc;
	if (UBlueprintNativizationSettingsLibrary::FindNativeByClassAndName(Node->GetBlueprint()->GeneratedClass, OriginalFunctionName, OutNativeDesc))
	{
		TSet<FString> DeclarationUniqueParameters;
		TSet<FString> CallSuperUniqueParameters;
		for (FString BaseInputName : OutNativeDesc.ParameterSignature)
		{
			TArray<FProperty*> Properties = UBlueprintNativizationLibrary::GetAllPropertiesByFunction(GenerateFunctionStruct.OriginalUFunction);

			if (FProperty* Property = UBlueprintNativizationLibrary::FindClosestPropertyByName(Properties, *BaseInputName))
			{
				DeclarationUniqueParameters.Add(FString::Format(TEXT("{0} {1}"), { UBlueprintNativizationLibrary::GetPropertyType(Property), UBlueprintNativizationLibrary::GetUniquePropertyName(Property, EntryNodes) }));
				CallSuperUniqueParameters.Add(UBlueprintNativizationLibrary::GetUniquePropertyName(Property, EntryNodes));
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
				UBlueprintNativizationLibrary::GetUniqueFieldName(Node->GetBlueprint()->GeneratedClass),
				OutNativeDesc.FunctionName,
				*FString::Join(DeclarationUniqueParameters.Array(), TEXT(", "))
			}
		);
		Content += FString::Format(TEXT("Super::{0}({1}); \n"), { OutNativeDesc.FunctionName, *FString::Join(CallSuperUniqueParameters.Array(), TEXT(", "))});
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
			FunctionName = FunctionName + "_Implementation";
			ReturnProperty = GenerateFunctionStruct.OriginalUFunction->GetReturnProperty();
		}
		Content = FString::Format(
			TEXT("{0} {1}::{2}({3})\n{\n"),
			{
				ReturnProperty ? *UBlueprintNativizationLibrary::GetPropertyType(ReturnProperty) : TEXT("void"),
				*UBlueprintNativizationLibrary::GetUniqueFieldName(Node->GetBlueprint()->GeneratedClass),
				*FunctionName,
				*FString::Join(Parameters.Array(), TEXT(", "))
			}
		);

	}
	FString LocalVariables = GenerateLocalVariablesCodeFromEntryNode(Node, TArray<UK2Node*>());
	if (!LocalVariables.IsEmpty())
	{
		Content += LocalVariables;
		Content += "\n";
	}

	TArray<FVisitedNodeStack> Nodes;
	Content += GenerateCodeFromNode(Node, "None", Nodes, TArray<UK2Node*>());
	while(Content.EndsWith("\n"))
	{
		Content = Content.Left(Content.Len()-1);
	}
	Content += "\n }; \n \n";
	return Content;
}

FString UNativizationV2Subsystem::GenerateSetupPlayerInputComponentCppCode(UBlueprint* Blueprint)
{
	FString Content;

	Content += FString::Format(TEXT("void {0}::SetupPlayerInputComponent(UInputComponent * PlayerInputComponent)\n {\n"), 
		{UBlueprintNativizationLibrary::GetUniqueFieldName(Blueprint->GeneratedClass)});
	for (FGenerateFunctionStruct GenerateFunctionStruct : EntryNodes)
	{
		TArray<UK2Node*> Nodes = UBlueprintNativizationLibrary::GetAllExecuteNodesInFunction(GenerateFunctionStruct.Node);
		for (UK2Node* Node : Nodes)
		{
			if (UTranslatorBPToCppObject* TranslatorObject = FindTranslatorForNode(Node))
			{
				Content += TranslatorObject->GenerateSetupPlayerInputComponentFunction(Node, "", this);
			}
		}
	}
	Content += "};\n";

	return Content;
}


FString UNativizationV2Subsystem::GenerateGlobalFunctionVariables(UBlueprint* Blueprint)
{
	FString Content;
	if (!Blueprint)
	{
		UE_LOG(LogTemp, Warning, TEXT("GenerateHFileBlueprintCode: Invalid Blueprint."));
		return "";
	}

	for (FGenerateFunctionStruct EntryNode : EntryNodes)
	{
		TArray<UK2Node*> Nodes = UBlueprintNativizationLibrary::GetAllExecuteNodesInFunction(EntryNode.Node, EntryNodes, "");
		for (UK2Node* Node : Nodes)
		{
			UTranslatorBPToCppObject* TranslatorBPToCppObject = FindTranslatorForNode(Node);
			if (TranslatorBPToCppObject)
			{
				Content += TranslatorBPToCppObject->GenerateGlobalVariables(Node, "", this);
			}
		}

		if (UK2Node_FunctionEntry* FunctionEntry = Cast<UK2Node_FunctionEntry>(EntryNode.Node))
		{
			TArray<FBPVariableDescription> ReversedArray = FunctionEntry->LocalVariables;
			for (FBPVariableDescription BPVariableDescription : ReversedArray)
			{
				UFunction* Function = UBlueprintNativizationLibrary::GetFunctionByNodeAndEntryNodes(FunctionEntry, EntryNodes);
				FProperty* Property = Function->FindPropertyByName(BPVariableDescription.VarName);

				if (UBlueprintNativizationLibrary::ContainsAssetInPinType(BPVariableDescription.VarType, BPVariableDescription.DefaultValue, nullptr))
				{
					Content += FString::Format(TEXT("UPROPERTY(EditAnywhere, BlueprintReadWrite) \n {0} {1};\n"),
						{ UBlueprintNativizationLibrary::GetPinType(BPVariableDescription.VarType, false),
						UBlueprintNativizationLibrary::GetUniquePropertyName(Property, EntryNodes) });
				}

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
		if (Function)
		{
			GenerateFunctionStruct->OriginalUFunction = Function;
			GenerateFunctionStruct->FlagsString = GetFunctionFlagsString(Function);
		}
		if (UK2Node_EnhancedInputAction* EnhancedInputAction = Cast<UK2Node_EnhancedInputAction>(K2Entry))
		{
			FName FunctionName = *FString::Format(TEXT("InpActEvt_{0}"), { *EnhancedInputAction->InputAction->GetName() });
			GenerateFunctionStruct->Name = *UBlueprintNativizationLibrary::GetUniqueEntryNodeName(K2Entry, Result, FunctionName.ToString());
		}
		else
		{
			GenerateFunctionStruct->Name = *UBlueprintNativizationLibrary::GetUniqueEntryNodeName(K2Entry, Result, "");
		}
	}

	int32 CycleIndex = 0;
	TSet<UK2Node*> StackSet;


	auto RegisterInsideFunction = [&](UK2Node* Head, FString PredictName)
		{

			FGenerateFunctionStruct NewCycleStruct;
			NewCycleStruct.Node = Head;
			NewCycleStruct.OriginalUFunction = UBlueprintNativizationLibrary::GetFunctionByNodeAndEntryNodes(Head, Result);
			Result.Add(NewCycleStruct);
			FGenerateFunctionStruct* GenerateFunctionStruct = Result.FindByKey(NewCycleStruct);
			GenerateFunctionStruct->Name = *UBlueprintNativizationLibrary::GetUniqueEntryNodeName(Head, Result, PredictName);

		};

	TFunction<bool(UK2Node*, FName, TArray<UK2Node*>&)> VisitNode;
	VisitNode = [&](UK2Node* Node, FName EnterPinName, TArray<UK2Node*>& Path) -> bool
		{
			if (StackSet.Contains(Node))
			{
				UTranslatorBPToCppObject* TranslatorBPToCppObject = FindTranslatorForNode(Node);

				if (TranslatorBPToCppObject && !TranslatorBPToCppObject->CanCreateCircleWithPin(Node, EnterPinName.ToString()))
				{
					return false;
				}

				int32 CycleStartIndex = Path.Find(Node);
				if (CycleStartIndex != INDEX_NONE)
				{
					RegisterInsideFunction(Node, "Cycle");
					return true; 
				}
				return false;
			}

			
			UTranslatorBPToCppObject* TranslatorBPToCppObject = FindTranslatorForNode(Node);
			if (TranslatorBPToCppObject)
			{
				TranslatorBPToCppObject->GenerateNewFunction(Node, Path, Result, this);
			}


			StackSet.Add(Node);
			Path.Add(Node);

			bool bFoundCycle = false;
			TArray<UEdGraphPin*> Pins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::None, EPinIncludeOnlyFilter::ExecPin);
			for (UEdGraphPin* Pin : Pins)
			{
				if (Pin->LinkedTo.Num() > 0 && Pin->LinkedTo[0])
				{
					UK2Node* LinkedNode = Cast<UK2Node>(Pin->LinkedTo[0]->GetOwningNode());
					if (LinkedNode && VisitNode(LinkedNode, Pin->LinkedTo[0]->GetFName(), Path))
					{
						bFoundCycle = true;
					}
				}
			}

			Path.Pop();
			StackSet.Remove(Node);
			return bFoundCycle;
		};



	TArray<FGenerateFunctionStruct> CopyResult = Result;
	for (const FGenerateFunctionStruct& Entry : CopyResult)
	{
		if (UK2Node* Node = Entry.Node)
		{
			TArray<UEdGraphPin*> Pins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::None, EPinIncludeOnlyFilter::ExecPin);
			for (UEdGraphPin* Pin : Pins)
			{
				TArray<UK2Node*> Path;
				if (Pin->LinkedTo.Num()>0 && Pin->LinkedTo[0])
				{
					VisitNode(Node, Pin->LinkedTo[0]->GetFName(), Path);
				}
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


FString UNativizationV2Subsystem::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack)
{
	if (!Pin)
	{
		return TEXT("/* null pin */");
	}

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

		if (Pin->PinType.PinCategory == "struct")
		{
			FConstructorDescriptor OutNativeDesc;
			if (UBlueprintNativizationSettingsLibrary::FindConstructorDescriptorByStruct(Cast<UScriptStruct>(Pin->PinType.PinSubCategoryObject), OutNativeDesc))
			{
				for (FConstructorPropertyDescriptor ConstructorPropertyDescriptor : OutNativeDesc.ConstructorProperties)
				{
					UEdGraphPin* OptimalPin = UBlueprintNativizationLibrary::FindOptimalPin(Pin->SubPins, ConstructorPropertyDescriptor.OriginalPropertyName);
					Args.Add(GenerateInputParameterCodeForNode(Node, OptimalPin, 0, MacroStack));
				}

				return FString::Format(TEXT("{0}({1})"), { PinCategory, FString::Join(Args, TEXT(", ")) });
			}
		}

		for (UEdGraphPin* LocalPin : Pin->SubPins)
		{
			Args.Add(GenerateInputParameterCodeForNode(Node, LocalPin, 0, MacroStack));
		}

		return FString::Format(TEXT("{0}({1})"), { PinCategory, FString::Join(Args, TEXT(", ")) });
	}

	if (Pin->bHidden && !Pin->ParentPin)
	{
		return TEXT("");
	}

	TArray<UK2Node*> LocalMacroStack = MacroStack;
	UEdGraphPin* ConnectedPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(Pin, PinIndex, false, LocalMacroStack, this);

	if (ConnectedPin && ConnectedPin->Direction == EEdGraphPinDirection::EGPD_Output)
	{
		UK2Node* ConnectedNode = Cast<UK2Node>(ConnectedPin->GetOwningNode());
		FString OutputCode = "";

		UTranslatorBPToCppObject* TranslatorObject = FindTranslatorForNode(ConnectedNode);

		if (TranslatorObject)
		{
			OutputCode = TranslatorObject->GenerateInputParameterCodeForNode(ConnectedNode, ConnectedPin, PinIndex, LocalMacroStack, this);
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
						return FString::Format(TEXT("this, &{0}::{1}"), { "U" + UBlueprintNativizationLibrary::GetUniqueFieldName(Node->GetBlueprint()->GeneratedClass), UBlueprintNativizationLibrary::GetUniqueEntryNodeName(CustomEvent, EntryNodes, "")});
					}
				}
				OutputCode = UBlueprintNativizationLibrary::GetUniquePropertyName(UBlueprintNativizationLibrary::GetPropertyFromEntryNode(ConnectedNode, *UBlueprintNativizationLibrary::GetParentPin(ConnectedPin)->GetName()), EntryNodes);
			}
			else
			{
				OutputCode = UBlueprintNativizationLibrary::GetUniquePinName(UBlueprintNativizationLibrary::GetParentPin(ConnectedPin), EntryNodes);
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
			OutputCode += "." + Value;
		}
		return OutputCode;
	}
	else
	{
		if (UBlueprintNativizationLibrary::IsAvailibleOneLineDefaultConstructor(Pin->PinType))
		{
			return UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructor(Pin->GetName(), Pin->PinType, Pin->DefaultValue, Pin->DefaultObject, LeftAllAssetRefInBlueprint);
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

FString UNativizationV2Subsystem::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack)
{
	if (!Node)
	{
		return TEXT("");
	}

	if (VisitedNodes.Contains(FVisitedNodeStack(Node, EntryPinName)))
	{
		if (MacroStack.Num()>0)
		{
			UE_LOG(LogTemp, Warning, TEXT("CIRCLE CUSTOM MACRO IS NOT FUCKING SUPPORTER. STOP DO IT. READ DOCUMENTATION, IDIOT."));
			return "";
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("SOME CIRCLE PROBLEM"));
			return "";
		}
	}

	FString Content;
	Content = "";

	//ControlFlowNodeStart
	FGenerateFunctionStruct OutEntryNode;
	if (VisitedNodes.Num()>0 && UBlueprintNativizationDataLibrary::FindGenerateFunctionStructByNode(EntryNodes, Node, OutEntryNode))
	{
		if (OutEntryNode.CustomParametersString.IsEmpty())
		{
			Content += OutEntryNode.Name.ToString() + TEXT("();\n");
			return Content;
		}
	}
	VisitedNodes.Add(FVisitedNodeStack(Node, EntryPinName));

	UTranslatorBPToCppObject* TranslatorObject = FindTranslatorForNode(Node);
	if (IsValid(TranslatorObject))
	{
		Content += TranslatorObject->GenerateCodeFromNode(Node, EntryPinName, VisitedNodes, MacroStack, this);
		if (!TranslatorObject->CanContinueCodeGeneration(Node, EntryPinName))
		{
			return Content;
		}
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
			Content += GenerateCodeFromNode(NextNode, GraphPin->GetName(), VisitedNodes, LocalMacroStack);
		}
	}
	//SimpleNodeEnd

	return Content;
}


FString UNativizationV2Subsystem::GenerateLocalVariablesCodeFromEntryNode(UK2Node* EntryNode, TArray<UK2Node*> MacroStack)
{
	//TO DO - IGNORE ARRAYS

	FString Content;
	if (UBlueprintNativizationLibrary::CheckLoopInFunction(EntryNode, EntryNodes, ""))
	{
		return Content;
	}

	UBlueprint* Blueprint = EntryNode->GetBlueprint();

	if (EntryNode->GetBlueprint()->FunctionGraphs.Contains(EntryNode->GetGraph()))
	{
		if (UK2Node_FunctionEntry* FunctionEntry = Cast<UK2Node_FunctionEntry>(EntryNode))
		{
			TArray<FBPVariableDescription> ReversedArray = FunctionEntry->LocalVariables;
			for (FBPVariableDescription BPVariableDescription : ReversedArray)
			{
				UFunction* Function = UBlueprintNativizationLibrary::GetFunctionByNodeAndEntryNodes(FunctionEntry, EntryNodes);
				FProperty* Property = Function->FindPropertyByName(BPVariableDescription.VarName);

				if (UBlueprintNativizationLibrary::ContainsAssetInPinType(BPVariableDescription.VarType, BPVariableDescription.DefaultValue, nullptr))
				{
					continue;
				}
				
				if (!UBlueprintNativizationLibrary::IsAvailibleOneLineDefaultConstructor(BPVariableDescription.VarType))
				{
					FString OutString;
					Content += UBlueprintNativizationLibrary::GenerateManyLinesDefaultConstructor(BPVariableDescription.VarName.ToString(), BPVariableDescription.VarType, BPVariableDescription.DefaultValue, Blueprint->GeneratedClass, false, EntryNode, EntryNodes, false, LeftAllAssetRefInBlueprint, OutString);
				}
				else
				{
					FString FixedValue = UBlueprintNativizationLibrary::GenerateOneLineDefaultConstructor(BPVariableDescription.VarName.ToString(), BPVariableDescription.VarType, BPVariableDescription.DefaultValue, nullptr, LeftAllAssetRefInBlueprint);
					FString PropertyName = UBlueprintNativizationLibrary::GetUniquePropertyName(Property, EntryNodes);
					FString PropertyType = UBlueprintNativizationLibrary::GetPropertyType(Property);

					Content += FString::Format(TEXT("{0} {1} = {2} \n\n"), { PropertyType, PropertyName, FixedValue });
				}
			}
		}
	}

	
	TArray<UK2Node*> Nodes = UBlueprintNativizationLibrary::GetAllExecuteNodesInFunctionWithMacroSupported(EntryNode, this);
	TArray<UK2Node*> InputNodes = UBlueprintNativizationLibrary::GetAllInputNodes(Nodes, false);

	TSet<UK2Node*> UniqueNodes;
	UniqueNodes.Append(InputNodes);
	UniqueNodes.Append(Nodes);

	TSet<FString> SetContents;

	for (UK2Node* Node : UniqueNodes)
	{
		if (UTranslatorBPToCppObject* TranslatorBPToCppObject = FindTranslatorForNode(Node))
		{
			SetContents.Append(TranslatorBPToCppObject->GenerateLocalVariables(Node, MacroStack, this));
		}
	}
	Content += FString::Join(SetContents.Array(), TEXT("\n"));
	
	return Content;
}
