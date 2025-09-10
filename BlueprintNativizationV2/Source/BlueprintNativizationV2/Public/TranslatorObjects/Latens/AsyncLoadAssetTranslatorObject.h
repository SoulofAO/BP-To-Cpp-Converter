// Fill out your copyright notice in the Description page of Project Settings.

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

    virtual void GenerateNewFunction(UK2Node* InputNode, TArray<UK2Node*> Path, TArray<FGenerateFunctionStruct>& EntryNodes, UNativizationV2Subsystem* NativizationV2Subsystem) override;

	virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, TSet<FString>& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem);

    virtual TSet<FString> GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual bool CanContinueCodeGeneration(UK2Node* InputNode, FString EntryPinName) override
    {
        return false;
    }

};
