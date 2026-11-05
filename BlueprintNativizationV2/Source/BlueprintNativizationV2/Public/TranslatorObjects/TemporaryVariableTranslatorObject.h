/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "BlueprintNativizationData.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_TemporaryVariable.h"
#include "K2Node_AssignmentStatement.h"
#include "K2Node_VariableSetRef.h"
#include "K2Node_Copy.h"
#include "K2Node_PureAssignmentStatement.h"

#include "TemporaryVariableTranslatorObject.generated.h"

/**
 * 
 */

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UTemporaryVariableTranslatorObject : public UTranslatorBPToCppObject
{
    GENERATED_BODY()

public:

    UTemporaryVariableTranslatorObject()
    {
        ClassNodeForCheck = {UK2Node_TemporaryVariable::StaticClass(), UK2Node_AssignmentStatement::StaticClass(), UK2Node_VariableSetRef::StaticClass(), UK2Node_Copy::StaticClass(), UK2Node_PureAssignmentStatement::StaticClass()};
        NodeNamesForCheck = { };
    }

    virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual TSet<FString> GenerateLocalVariables(UK2Node* InputNode, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual FString GenerateGlobalVariables(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* Subsystem) override;
    virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes,
        TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* Subsystem) override;

};
