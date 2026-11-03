#pragma once
#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_MapForEach.h"
#include "K2Node_SetForEach.h"
#include "ContainerLoopTranslatorObject.generated.h"

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UContainerLoopTranslatorObject : public UTranslatorBPToCppObject
{
    GENERATED_BODY()
public:
    UContainerLoopTranslatorObject()
    {
        ClassNodeForCheck = {UK2Node_MapForEach::StaticClass(), UK2Node_SetForEach::StaticClass()};
    }
    virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* Subsystem) override;
    virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem) override;
    virtual FString GenerateGlobalVariables(UK2Node* Node, FString Prefix, UNativizationV2Subsystem* Subsystem) override;
    virtual TSet<FString> GenerateLocalVariables(UK2Node* Node, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem) override;
    virtual bool CanContinueCodeGeneration(UK2Node* Node, FString EntryPinName) override { return false; }
    virtual bool ShouldPrepareInputs(UK2Node* Node, const FString& EntryPinName) const override { return false; }
    virtual bool CanCreateCircleWithPin(UK2Node* Node, FString EntryPinName) override;
};
