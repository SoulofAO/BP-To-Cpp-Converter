/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/Latens/DelayTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"


void UDelayTranslatorObject::GenerateNewFunction(
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

	if (ExecPins.IsEmpty() || ExecPins[0]->LinkedTo.Num() == 0)
	{
		return;
	}

	TArray<UK2Node*> MacroStack;
	UEdGraphPin* TargetPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(ExecPins[0], 0,true, MacroStack, NativizationV2Subsystem);
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
			FoundStruct->Name = *UBlueprintNativizationLibrary::GetUniqueEntryNodeName(TargetNode, EntryNodes, "DelayEnd");
		}
	}
}


TSet<FString> UDelayTranslatorObject::GenerateLocalVariables(UK2Node* InputNode, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	FString TimerHandleName = FString::Format(TEXT("{0}"), { *UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByNode("TimerHandle", InputNode, NativizationV2Subsystem->EntryNodes)});

	return TSet<FString>({FString::Format(TEXT("{0} {1}"), { "FTimerHandle", TimerHandleName }) });
}

FString UDelayTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	TArray<UEdGraphPin*> Pins = UBlueprintNativizationLibrary::GetParentPins(UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::None, EPinIncludeOnlyFilter::ExecPin));

	FString TimerHandleName = *UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByNode("TimerHandle", Node, NativizationV2Subsystem->EntryNodes);

	if (Pins[0]->LinkedTo.Num() > 0)
	{
		FString BindFunctionName = "";

		FGenerateFunctionStruct GenerateFunctionStruct;

		UK2Node* NextDelayFunction = Cast<UK2Node>(UBlueprintNativizationLibrary::GetFirstNonKnotPin(Pins[0], 0, true, MacroStack, NativizationV2Subsystem)->GetOwningNode());

		if (UBlueprintNativizationDataLibrary::FindGenerateFunctionStructByNode(NativizationV2Subsystem->EntryNodes, NextDelayFunction, GenerateFunctionStruct))
		{
			BindFunctionName = GenerateFunctionStruct.Name.ToString();
		}

		return FString::Format(TEXT("GetWorld()->GetTimerManager().SetTimer({0}, this, {1}::{2}, {3}, false);\n"), {
		*TimerHandleName,
		*Node->GetBlueprint()->GetName(),
		*BindFunctionName,
		*NativizationV2Subsystem -> GenerateInputParameterCodeForNode(Node, Node->FindPin(TEXT("Duration")), 0, MacroStack)
			});
	}

	return FString();
}
