/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_GetSubsystem.h"
#include "GetSubsystemTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UGetSubsystemTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()
	
	UGetSubsystemTranslatorObject()
	{
		ClassNodeForCheck = { UK2Node_GetSubsystem::StaticClass() };
		NodeNamesForCheck = { };
	}

public:
    virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem) override;
    virtual TSet<FString> GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem) override;
};
