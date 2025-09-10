// Fill out your copyright notice in the Description page of Project Settings.


#include "TranslatorObjects/Latens/AsyncLoadAssetTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"



void UAsyncLoadAssetTranslatorObject::GenerateNewFunction(
	UK2Node* InputNode,
	TArray<UK2Node*> Path,
	TArray<FGenerateFunctionStruct>& EntryNodes,
	UNativizationV2Subsystem* NativizationV2Subsystem)
{
	TArray<UEdGraphPin*> ExecPins = UBlueprintNativizationLibrary::GetFilteredPins(
		InputNode,
		EPinOutputOrInputFilter::Ouput,
		EPinExcludeFilter::None,
		EPinIncludeOnlyFilter::ExecPin
	);

	UEdGraphPin* MainExecPin = InputNode->FindPin(TEXT("then"));
	UEdGraphPin* OnCompleteExecPin = InputNode->FindPin(TEXT("Completed"));

	TArray<UK2Node*> MacroStack;
	UEdGraphPin* TargetPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(OnCompleteExecPin, 0, true, MacroStack, NativizationV2Subsystem);
	if (!TargetPin || !TargetPin->GetOwningNode())
	{
		return;
	}

	UK2Node* TargetNode = Cast<UK2Node>(TargetPin->GetOwningNode());
	if (!TargetNode)
	{
		return;
	}

	FGenerateFunctionStruct ExistingStruct;
	if (!UBlueprintNativizationDataLibrary::FindGenerateFunctionStructByNode(EntryNodes, TargetNode, ExistingStruct))
	{
		FGenerateFunctionStruct NewStruct;
		NewStruct.Node = TargetNode;
		EntryNodes.Add(NewStruct);

		FGenerateFunctionStruct* FoundStruct = EntryNodes.FindByKey(NewStruct);
		if (FoundStruct)
		{
			FoundStruct->Name = *UBlueprintNativizationLibrary::GetUniqueEntryNodeName(TargetNode, EntryNodes, "OnCompletedLoad");
		}
	}
}

FString UAsyncLoadAssetTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, TSet<FString>& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (UK2Node_LoadAsset* LoadAssetNode = Cast<UK2Node_LoadAsset>(Node))
	{
		FString Content;
		 
		UEdGraphPin* AssetPin = nullptr;

		if (Cast<UK2Node_LoadAssetClass>(Node))
		{
			AssetPin = LoadAssetNode->FindPin(TEXT("AssetClass"));
		}
		else
		{
			AssetPin = LoadAssetNode->FindPin(TEXT("Asset"));
		}

		UEdGraphPin* MainExecPin = LoadAssetNode->FindPin(TEXT("then"));
		UEdGraphPin* OnCompleteExecPin = LoadAssetNode->FindPin(TEXT("Completed"));

		FGenerateResultStruct AssetPinResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, AssetPin, 0, MacroStack);
		Content += GenerateNewPreparations(Preparations, AssetPinResultStruct.Preparations);
		TSet<FString> LocalPreparations = Preparations;
		LocalPreparations.Append(AssetPinResultStruct.Preparations);

		if (UEdGraphPin* TargetPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(OnCompleteExecPin, 0, true, MacroStack, NativizationV2Subsystem))
		{
			Content += FString::Format(TEXT("if({0})\n{\n"), { AssetPinResultStruct.Code });
			Content += FString::Format(TEXT("UAssetManager::GetStreamableManager().RequestAsyncLoad({0}.ToSoftObjectPath(),FStreamableDelegate::CreateUObject(this, &{1}::{2}));\n"),
				{ AssetPinResultStruct.Code, 
				UBlueprintNativizationLibrary::GetUniqueEntryNodeName(Cast<UK2Node>(TargetPin->GetOwningNode()), 
				NativizationV2Subsystem->EntryNodes, "OnCompletedLoad")
				});
			Content += TEXT("}\n");
		}
		else
		{
			Content += FString::Format(TEXT("if({0})\n{\n"), { AssetPinResultStruct.Code });
			Content += FString::Format(TEXT("UAssetManager::GetStreamableManager().RequestAsyncLoad({0}.ToSoftObjectPath()));\n"), { AssetPinResultStruct.Code});
			Content += TEXT("}\n");
		}

		TArray<UK2Node*> LocalMacroStack = MacroStack;
		UEdGraphPin* NextPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(MainExecPin, 0, false, LocalMacroStack, NativizationV2Subsystem);

		if (NextPin)
		{
			Content += NativizationV2Subsystem->GenerateCodeFromNode(Cast<UK2Node>(NextPin->GetOwningNode()), NextPin->GetName(), VisitedNodes, Preparations, LocalMacroStack);
		}
		return Content;
	}
	return FString();
}

FGenerateResultStruct UAsyncLoadAssetTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (UK2Node_LoadAsset* LoadAssetNode = Cast<UK2Node_LoadAsset>(Node))
	{
		UEdGraphPin* AssetPin = nullptr;

		if (Cast<UK2Node_LoadAssetClass>(Node))
		{
			AssetPin = LoadAssetNode->FindPin(TEXT("AssetClass"));
		}
		else
		{
			AssetPin = LoadAssetNode->FindPin(TEXT("Asset"));
		}

		FGenerateResultStruct GenerateResultAssetPin = NativizationV2Subsystem->GenerateInputParameterCodeForNode(LoadAssetNode, AssetPin, 0, MacroStack);
		GenerateResultAssetPin.Code += ".Get()";
		return GenerateResultAssetPin;
	}
	return FGenerateResultStruct();
}

TSet<FString> UAsyncLoadAssetTranslatorObject::GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	return 
	{ 
	TEXT("#include \"Engine/StreamableManager.h\""),
	TEXT("#include \"Engine/AssetManager.h\"")
	};
}

