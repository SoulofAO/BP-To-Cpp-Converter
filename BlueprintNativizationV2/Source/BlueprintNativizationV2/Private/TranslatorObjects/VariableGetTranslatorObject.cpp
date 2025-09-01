/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once
#include "TranslatorObjects/VariableGetTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"

/**
 * 
 */

FString UVariableGetTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (UK2Node_VariableGet* VariableGet = Cast<UK2Node_VariableGet>(Node))
	{
		return FString::Format(TEXT("{0}"), { UBlueprintNativizationLibrary::GetUniquePropertyName(VariableGet->GetPropertyForVariable(), NativizationV2Subsystem->EntryNodes)});
	}
	return "";
}
