/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/Array/GetArrayItemTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"

FString UGetArrayItemTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (UK2Node_GetArrayItem* GetArrayItemNode = Cast<UK2Node_GetArrayItem>(Node))
	{
		return FString::Format(TEXT("{0}[{1}]"),
			{NativizationV2Subsystem->GenerateInputParameterCodeForNode(GetArrayItemNode, GetArrayItemNode->GetTargetArrayPin(), 0, MacroStack),
			NativizationV2Subsystem->GenerateInputParameterCodeForNode(GetArrayItemNode, GetArrayItemNode->GetIndexPin(), 0, MacroStack)});
	}
	return FString();
}
