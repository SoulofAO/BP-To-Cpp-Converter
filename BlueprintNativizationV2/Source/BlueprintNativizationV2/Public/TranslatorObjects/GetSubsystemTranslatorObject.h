/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
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

    virtual bool ShouldExpandNode(UK2Node* Node) const override;
};
