/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once
#include "TranslatorObjects/VariableGetTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"
#include "EdGraphSchema_K2.h"

/**
 * 
 */

bool UVariableGetTranslatorObject::CanApply(UK2Node* Node)
{
    if (UK2Node_VariableGet* Getter = Cast<UK2Node_VariableGet>(Node))
    {
        FProperty* Property = Getter->GetPropertyForVariable();
        return Getter->IsNodePure() && (!Property || !Property->HasMetaData(FBlueprintMetadata::MD_PropertyGetFunction));
    }
    return Node->GetClass() == UK2Node_StructMemberGet::StaticClass();
}

FGenerateResultStruct UVariableGetTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
	if (UK2Node_VariableGet* VariableGet = Cast<UK2Node_VariableGet>(Node))
	{
		return UBlueprintNativizationLibrary::GenerateVariableAccess(VariableGet, MacroStack, NativizationV2Subsystem);
	}
    if (UK2Node_StructMemberGet* Getter = Cast<UK2Node_StructMemberGet>(Node))
    {
        return UBlueprintNativizationLibrary::GetUniquePropertyName(Getter->GetPropertyForVariable()) +
            TEXT(".") + UBlueprintNativizationLibrary::GetUniquePropertyName(Getter->StructType->FindPropertyByName(Pin->PinName));
    }
	return FGenerateResultStruct("");
}
