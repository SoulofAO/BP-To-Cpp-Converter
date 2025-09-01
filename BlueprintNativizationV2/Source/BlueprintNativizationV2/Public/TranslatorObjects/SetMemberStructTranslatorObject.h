/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_SetFieldsInStruct.h"
#include "SetMemberStructTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API USetMemberStructTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()
	
	USetMemberStructTranslatorObject()
	{
		ClassNodeForCheck = {UK2Node_SetFieldsInStruct::StaticClass()};
		NodeNamesForCheck = { };
	}

	virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

};
