/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/Array/GetArrayItemTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"

FGenerateResultStruct UGetArrayItemTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (UK2Node_GetArrayItem* GetArrayItemNode = Cast<UK2Node_GetArrayItem>(Node))
	{
		TSet<FString> LocalPreparations;

		FGenerateResultStruct ArrayPinResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(GetArrayItemNode, GetArrayItemNode->GetTargetArrayPin(), 0, MacroStack);
		FGenerateResultStruct IndexPinResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(GetArrayItemNode, GetArrayItemNode->GetIndexPin(), 0, MacroStack);

		LocalPreparations.Append(ArrayPinResultStruct.Preparations);
		LocalPreparations.Append(IndexPinResultStruct.Preparations);

		return FGenerateResultStruct(FString::Format(TEXT("{0}[{1}]"),
			{ ArrayPinResultStruct.Code,
			IndexPinResultStruct.Code }), LocalPreparations);
	}
	return FString();
}
