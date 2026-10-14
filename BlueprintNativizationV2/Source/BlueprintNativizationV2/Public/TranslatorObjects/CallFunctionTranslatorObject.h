/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */


#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "BlueprintNativizationData.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_CallFunction.h"
#include "CallFunctionTranslatorObject.generated.h"

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UCallFunctionTranslatorObject : public UTranslatorBPToCppObject
{
    GENERATED_BODY()

public:

    virtual bool ShouldExpandNode(UK2Node* Node) const override;

    UCallFunctionTranslatorObject()
    {
        ClassNodeForCheck = {UK2Node_CallFunction::StaticClass()};
        NodeNamesForCheck = { };
    }

    virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual FString GenerateInstanceFunctionCall(UK2Node_CallFunction* Call, const FString& Target, const FString& Arguments) const;

    FString GenerateInstanceFunctionStatement(UK2Node_CallFunction* Call, const FString& Target,
        const FString& Arguments, const FString& ResultName, const TArray<FString>& ArgumentExpressions,
        UNativizationV2Subsystem* Subsystem) const;

    FString AdaptReturnValue(UK2Node_CallFunction* Call, const FString& Expression, FGenerationPreparations& Preparations) const;

    bool CanInlineArithmetic(UK2Node_CallFunction* Call, UK2Node* Consumer) const;

    static bool HasContainerFunctionTranslation(UFunction* Function);

    FString GenerateStaticFunctionCall(UFunction* Function, const FString& Arguments, const TArray<FString>* ArgumentExpressions = nullptr, UK2Node_CallFunction* Call = nullptr);

    virtual void BuildArgs(bool bIsStatic, FString& Code, FGenerationPreparations& Preparation, UK2Node_CallFunction* CallFuncNode, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem, const TMap<FName, FString>& ArgumentOverrides = {}, TArray<FString>* ArgumentExpressions = nullptr);

    virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual TSet<FString> GenerateLocalVariables(UK2Node* InputNode, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual TSet<FString> GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual TSet<FString> GenerateCSPrivateDependencyModuleNames(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual bool CanContinueCodeGeneration(UK2Node* InputNode, FString EntryPinName) override
    {
        return true;
    }

    virtual bool CanCreateCircleWithPin(UK2Node* Node, FString EnterPinName) override
    {
        return true;
    }
};
