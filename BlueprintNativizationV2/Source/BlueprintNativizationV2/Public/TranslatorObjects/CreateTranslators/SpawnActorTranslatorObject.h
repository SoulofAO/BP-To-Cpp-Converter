/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_SpawnActorFromClass.h"
#include "SpawnActorTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API USpawnActorTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()

public:
	USpawnActorTranslatorObject()
	{
		ClassNodeForCheck = { UK2Node_SpawnActorFromClass::StaticClass() };
		NodeNamesForCheck = { };
	}
	
	virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, TSet<FString>& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem) override;
};
