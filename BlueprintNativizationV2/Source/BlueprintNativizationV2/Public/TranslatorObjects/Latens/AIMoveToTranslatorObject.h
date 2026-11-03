/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_AIMoveTo.h"
#include "AIMoveToTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UAIMoveToTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()
public:

    UAIMoveToTranslatorObject()
    {
        ClassNodeForCheck = { UK2Node_AIMoveTo::StaticClass() };
        NodeNamesForCheck = { };
    }


    virtual FString GenerateGlobalVariables(UK2Node* Node, FString Prefix, UNativizationV2Subsystem* Subsystem) override;
    virtual TSet<FString> GenerateLocalVariables(UK2Node* Node, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem) override { return {}; }
    virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem) override;
    virtual void GenerateNewFunction(UK2Node* Node, TArray<UK2Node*> Path, TArray<FGenerateFunctionStruct>& Entries, UNativizationV2Subsystem* Subsystem) override;
    virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* Subsystem) override;
    virtual bool CanContinueCodeGeneration(UK2Node* Node, FString EntryPinName) override { return false; }
    virtual bool ShouldPrepareInputs(UK2Node* Node, const FString& EntryPinName) const override { return false; }
    virtual TSet<FString> GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem) override;
    virtual TSet<FString> GenerateHeaderIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem) override;
    virtual TSet<FString> GenerateCSPrivateDependencyModuleNames(UK2Node* Node, UNativizationV2Subsystem* Subsystem) override
    {
        return {TEXT("\"AIModule\"")};
    }
};
