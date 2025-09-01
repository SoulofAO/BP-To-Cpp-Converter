/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/Latens/DelayNextTickTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"


void UDelayNextTickTranslatorObject::GenerateNewFunction(UK2Node* InputNode, TArray<UK2Node*> Path, TArray<FGenerateFunctionStruct>& EntryNodes, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	TArray<UEdGraphPin*> ExecPins = UBlueprintNativizationLibrary::GetFilteredPins(InputNode, EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::None, EPinIncludeOnlyFilter::ExecPin);

	TArray<UK2Node*> MacroNodes;
	UEdGraphPin* FirstNonKnotPin = UBlueprintNativizationLibrary::GetFirstNonKnotPin(ExecPins[0], 0, true, MacroNodes, NativizationV2Subsystem);
	if (FirstNonKnotPin && FirstNonKnotPin->GetOwningNode())
	{
		FGenerateFunctionStruct OutGenerateFunctionStruct;
		if (!UBlueprintNativizationDataLibrary::FindGenerateFunctionStructByNode(EntryNodes, Cast<UK2Node>(FirstNonKnotPin->GetOwningNode()), OutGenerateFunctionStruct))
		{
			FGenerateFunctionStruct GenerateFunctionStruct;
			GenerateFunctionStruct.Node = Cast<UK2Node>(FirstNonKnotPin->GetOwningNode());
			EntryNodes.Add(GenerateFunctionStruct);
			FGenerateFunctionStruct* GenerateFunctionStructRef = EntryNodes.FindByKey(GenerateFunctionStruct);
			GenerateFunctionStructRef->Name = *UBlueprintNativizationLibrary::GetUniqueEntryNodeName(Cast<UK2Node>(FirstNonKnotPin->GetOwningNode()), EntryNodes, "DelayEnd");
		}
	}
}

FString UDelayNextTickTranslatorObject::GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	TArray<UEdGraphPin*> Pins = UBlueprintNativizationLibrary::GetParentPins(UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Ouput, EPinExcludeFilter::None, EPinIncludeOnlyFilter::ExecPin));

	FString TimerHandleName = *UBlueprintNativizationLibrary::GetLambdaUniqueVariableNameByNode("TimerHandle", Node, NativizationV2Subsystem->EntryNodes);

	if (Pins[0]->LinkedTo.Num() > 0)
	{
		FString BindFunctionName = "";

		FGenerateFunctionStruct GenerateFunctionStruct;

		UK2Node* NextDelayFunction = Cast<UK2Node>(UBlueprintNativizationLibrary::GetFirstNonKnotPin(Pins[0], 0, false, MacroStack, NativizationV2Subsystem)->GetOwningNode());

		if (UBlueprintNativizationDataLibrary::FindGenerateFunctionStructByNode(NativizationV2Subsystem->EntryNodes, NextDelayFunction, GenerateFunctionStruct))
		{
			BindFunctionName = GenerateFunctionStruct.Name.ToString();
		}

		return FString::Format(TEXT("GetWorld()->GetTimerManager().SetTimerForNextTick((FTimerDelegate::CreateUObject(this, &{0}::{1}));\n"),
		{*UBlueprintNativizationLibrary::GetUniqueFieldName(Node->GetBlueprint()->GeneratedClass), *BindFunctionName});
	}

	return FString();
}
