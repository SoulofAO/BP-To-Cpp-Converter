/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_LoadAsset.h"
#include "AsyncLoadAssetTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UAsyncLoadAssetTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()
public:


    UAsyncLoadAssetTranslatorObject()
    {
        ClassNodeForCheck = { UK2Node_LoadAsset::StaticClass(), UK2Node_LoadAssetClass::StaticClass()};
        NodeNamesForCheck = { };
    }

    virtual TSet<FString> GenerateHeaderIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem) override;
    virtual FString GenerateGlobalVariables(UK2Node* Node, FString Prefix, UNativizationV2Subsystem* Subsystem) override;
    virtual TSet<FString> GenerateLocalVariables(UK2Node* Node, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem) override { return {}; }
    virtual bool ShouldPrepareInputs(UK2Node* Node, const FString& EntryPinName) const override { return false; }
    virtual void GenerateNewFunction(UK2Node* InputNode, TArray<UK2Node*> Path, TArray<FGenerateFunctionStruct>& EntryNodes, UNativizationV2Subsystem* NativizationV2Subsystem) override;

	virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem);

    virtual TSet<FString> GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual bool CanContinueCodeGeneration(UK2Node* InputNode, FString EntryPinName) override
    {
        return false;
    }

};
